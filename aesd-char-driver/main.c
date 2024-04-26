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
    .we.finished_entry = NULL
};
void aesd_cleanup_module(void);

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    
    PDEBUG("open");
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev; /* for other methods */

    /* 
     * Allocate the circular buffer
     * Fill all the entries with NULL. You could use the FOREACH macro.
     */
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    /*Free all the entries on th circular buffer. You could use the FOREACH 
     * macro.
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;

    PDEBUG("the current process is \"%s\" (pid %i)\n", 
        current->comm, current->pid);
    PDEBUG("the index is %lu", dev->we.index);
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev = filp->private_data;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
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
    PDEBUG("count = %lu, we.size = %lu", count, dev->we.size);

    if (mutex_lock_interruptible(&dev->we_mutex))
		return -ERESTARTSYS;
    if(
        (count > dev->we.size) || 
        (count + dev->we.index + 1 > dev->we.size ) 
    ) {
        PDEBUG("Write count exceeds working entry size");
        retval = -ENOMEM;
        goto out;
    }
    if(copy_from_user(dev->we.buffptr, buf, count)) {
        retval = -EFAULT;
        goto out;
    }
    retval = count;

    out:
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
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    PDEBUG("KMALLOC_MAX_SIZE is %lu",KMALLOC_MAX_SIZE);
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }

    //memset(&aesd_device,0,sizeof(struct aesd_dev));

    mutex_init(&aesd_device.we_mutex);

    aesd_device.we.buffptr = kzalloc(KMALLOC_MAX_SIZE,GFP_KERNEL);
    if(!aesd_device.we.buffptr) {
        result = -ENOMEM;
        goto fail;
    }
    printk("Got we.buffptr %p", aesd_device.we.buffptr);

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

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    kfree(aesd_device.we.buffptr);
    if(aesd_device.we.finished_entry) {
        kfree(aesd_device.we.finished_entry->buffptr);
        kfree(aesd_device.we.finished_entry);
    }

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);