/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>       // file_operations
#include <linux/sched.h>    // current process
#include <linux/slab.h>     // memory allocation constants
#include <linux/uaccess.h>	// copy_*_user
#include "aesdchar.h"
#include "aesd-circular-buffer.h"

#define KMALLOC_MAX_SIZE      (1UL << KMALLOC_SHIFT_MAX) /* = 4194304 on my PC*/

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Ivan Veloz");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device = {
    .we.buffptr = NULL,
    .we.size = KMALLOC_MAX_SIZE,
    .we.index = 0,
    .we.complete = false,
};
void aesd_cleanup_module(void);

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    uint8_t i;
    struct aesd_buffer_entry *e;
    
    PDEBUG("open");
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev; /* for other methods */

    if (mutex_lock_interruptible(&dev->cb_mutex))
        return -ERESTARTSYS;  
    /* Just in case we open with an existing buffer created.
     * This kfree should have no effect if we just initialized 
     * recently.
     */
    AESD_CIRCULAR_BUFFER_FOREACH(e,&(dev->cb),i) {
        kfree(e->buffptr);
        e->buffptr = NULL;
    }
    aesd_circular_buffer_init(&dev->cb);
    mutex_unlock(&dev->cb_mutex);

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    uint8_t i;
    struct aesd_buffer_entry *e;

    PDEBUG("release");
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev; /* for other methods */

    if (mutex_lock_interruptible(&dev->cb_mutex))
        return -ERESTARTSYS;
    
    AESD_CIRCULAR_BUFFER_FOREACH(e,&(dev->cb),i) {
        kfree(e->buffptr);
        e->buffptr = NULL;
    }
    mutex_unlock(&dev->cb_mutex);
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;

    PDEBUG("the current process is \"%s\" (pid %i)\n", 
        current->comm, current->pid);
    PDEBUG("the index is %lu\n", dev->we.index);
    PDEBUG("read %zu bytes with offset %lld\n",count,*f_pos);
    /**
     * TODO: handle read
     */
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    size_t s;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry finished_entry = {
        .buffptr = NULL,
        .size = 0
    };

    PDEBUG("write %zu bytes with offset %lld\n",count,*f_pos);
    /**
     * TODO: handle write
     */
    /* 1. Write to the working entry, and don't mark it as complete until this
     * functions sees a /n. 
     * 2. When the working entry is complete, pass it to 
     * aesd_circular_buffer_add_entry.
     * 3. Free the memory returned by aesd_circular_buffer_add_entry.
     * 4. Allocate new memory for 
     */
    /* 
     * Write to the circular buffer.
     * If buffer becomes full, free the memory from the entry you are going to 
     * overwrite _before_ you overwrite that entry. Return a pointer to the
     * entry you just freed. ("Assignment 8 Overview" minute 10:01). This would 
     * mean modifying the add_entry funuction for aesd_circular_buffer.
     */
    PDEBUG("count = %lu, we.size = %lu\n", count, dev->we.size);
    PDEBUG("index = %lu\n", dev->we.index);
    if (mutex_lock_interruptible(&dev->we_mutex))
		return -ERESTARTSYS;
    if(
        (count > dev->we.size) || 
        (count + dev->we.index > dev->we.size ) 
    ) {
        PDEBUG("Write count exceeds working entry size\n");
        retval = -ENOMEM;
        goto out;
    }
    if((s = copy_from_user((dev->we.buffptr + dev->we.index), buf, count))) {
        dev->we.index += count - s;
        retval = -EFAULT;
        goto out;
    }
    
    s = dev->we.index;
    dev->we.index += count;
    for(; s < dev->we.index; s++) {
        if(dev->we.buffptr[s] == '\n') {
            const char *retbuffptr;
            PDEBUG("Found \\n at index %lu; adding entry now\n",s);
            dev->we.complete = true;
            finished_entry.buffptr = 
                kzalloc(s+2, GFP_KERNEL);   // +2 to fit null terminator
                
            if (mutex_lock_interruptible(&dev->cb_mutex))
		        return -ERESTARTSYS;
            retbuffptr = aesd_circular_buffer_add_entry(
                    &(dev->cb), 
                    &(finished_entry) );
            PDEBUG("Entered buffptr %p to circular buffer\n", 
                finished_entry.buffptr);
            PDEBUG("Freeing retbuffptr %p\n", retbuffptr);
            kfree(retbuffptr);
            mutex_unlock(&dev->cb_mutex);

            dev->we.index = 0;
            dev->we.complete = false;
            break;
        }
    }

    retval = count;

    out:
    PDEBUG("Index is at %lu\n", dev->we.index);
    mutex_unlock(&dev->we_mutex);
    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev\n", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    PDEBUG("KMALLOC_MAX_SIZE is %lu\n",KMALLOC_MAX_SIZE);
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }

    //memset(&aesd_device,0,sizeof(struct aesd_dev));

    mutex_init(&aesd_device.we_mutex);
    mutex_init(&aesd_device.cb_mutex);

    aesd_device.we.buffptr = kzalloc(KMALLOC_MAX_SIZE,GFP_KERNEL);
    if(!aesd_device.we.buffptr) {
        result = -ENOMEM;
        goto fail;
    }

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

    fail:
        aesd_cleanup_module();
        return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    uint8_t i;
    struct aesd_buffer_entry *e;

    cdev_del(&aesd_device.cdev);

    while (mutex_lock_interruptible(&aesd_device.we_mutex)) {
        printk( KERN_CRIT "aesdchar: " "Failed to get lock for aesd_device.cb_mutex; can't clean up module!!!\n");
    }
    kfree(aesd_device.we.buffptr);
    while (mutex_lock_interruptible(&aesd_device.cb_mutex)) {
        printk( KERN_CRIT "aesdchar: " "Failed to get lock for aesd_device.cb_mutex; can't clean up module!!!\n"); 
    }
    AESD_CIRCULAR_BUFFER_FOREACH(e,&(aesd_device.cb),i) {
        kfree(e->buffptr);
        e->buffptr = NULL;
    }
    mutex_unlock(&aesd_device.cb_mutex);
    mutex_unlock(&aesd_device.we_mutex);


    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
