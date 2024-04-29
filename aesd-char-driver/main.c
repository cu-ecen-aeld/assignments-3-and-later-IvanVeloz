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
#include "aesd_ioctl.h"

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
    .cb_size = 0
};
void aesd_cleanup_module(void);

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    
    PDEBUG("open\n");
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev; /* for other methods */
    
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;

    PDEBUG("release\n");
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev; /* for other methods */

    return 0;
}

loff_t aesd_llseek(struct file *filp, loff_t off, int whence) 
{
    loff_t retval = 0;
    struct aesd_dev *dev = filp->private_data;

    fixed_size_llseek(filp, off, whence, dev->cb_size);
    if (retval < 0) return -EINVAL;

    return retval;
}

static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd,
    unsigned int write_cmd_offset)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    loff_t fp = 0;

    if (mutex_lock_interruptible(&dev->cb_mutex))
        return -ERESTARTSYS;

    if(write_cmd_offset+1 > dev->cb.entry[write_cmd].size) return -EINVAL;
    if(write_cmd+1 > AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) return -EINVAL;

    for(uint8_t i = 0;
        i < write_cmd; 
        ++i, aesd_circular_increment(&dev->cb.out_offs,AESDCHAR_MAX_INDEX)
    ) {
        fp += (loff_t)dev->cb.entry[dev->cb.out_offs].size;
    }
    mutex_unlock(&dev->cb_mutex);
    fp += write_cmd_offset;
    filp->f_pos = fp;
    PDEBUG("f_pos = %llu", fp);

    return retval;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int retval = 0;
    struct aesd_seekto st;

    PDEBUG("ioctl\n");

	if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) return -ENOTTY;

    switch(cmd) {
        case AESDCHAR_IOCSEEKTO:
            if(copy_from_user(&st, (const void __user *)arg, sizeof(st))) {
                retval = -EFAULT;
            }
            else {
                PDEBUG("write_cmd = %u, write_cmd_offset = %u\n", 
                    st.write_cmd, st.write_cmd_offset);
                retval = aesd_adjust_file_offset(filp, st.write_cmd, 
                                          st.write_cmd_offset);
            }
            break;
        default:
            return -ENOTTY;
    }
    return retval;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry * starting_entry = NULL;
    size_t starting_entry_offset = 0;

    PDEBUG("the current process is \"%s\" (pid %i)\n", 
        current->comm, current->pid);
    PDEBUG("read %zu bytes with offset %lld\n",count,*f_pos);
    PDEBUG("*f_pos = %llu, filp->f_pos = %llu", *f_pos, filp->f_pos);
    
    if (mutex_lock_interruptible(&dev->cb_mutex))
        return -ERESTARTSYS;

    // Get the buffer we will start reading from
    starting_entry = aesd_circular_buffer_find_entry_offset_for_fpos(
        &dev->cb,
        (size_t)(*f_pos),
        &starting_entry_offset);
    if(starting_entry == NULL) {
        goto out;
    }

    // Iterate through the circular buffer until we get to in_offs, which
    // is as far as we have written to the buffer. 
    // j handles "full buffer" edge case (lets it run the first time)
    // and is a handy counter.
    for(
        uint8_t j = 0, i = dev->cb.out_offs; 
        j==0 || i != dev->cb.in_offs; 
        ++j, aesd_circular_increment(&i,AESDCHAR_MAX_INDEX)
    ) {
        // Ignores everything before the starting_entry that we found earlier.
        if( &(dev->cb.entry[i]) == starting_entry ) {

            size_t n;
            const char * p;
            if(count > dev->cb.entry[i].size - starting_entry_offset) {
                n = dev->cb.entry[i].size - starting_entry_offset;
                p = dev->cb.entry[i].buffptr + starting_entry_offset;
            }
            else {
                n = count;
                p = dev->cb.entry[i].buffptr + starting_entry_offset;
            }

            PDEBUG("copy_to_user(buf = %p, p = %p, n = %lu)", buf, p, n);
            PDEBUG("Entry reads %s\n", p);
            if (copy_to_user(buf, p, n)) {
                retval = -EFAULT;
                goto out;
            }
            *f_pos += n;
            retval = n;
            PDEBUG("sent %lu bytes",n);
            goto out;
        }
    }
    out:
    mutex_unlock(&dev->cb_mutex);
    
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
    PDEBUG("*f_pos = %llu, filp->f_pos = %llu", *f_pos, filp->f_pos);

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
        *f_pos += count - s;
        retval = -EFAULT;
        goto out;
    }
    
    s = dev->we.index;
    dev->we.index += count;
    *f_pos += count;
    
    for(; s < dev->we.index; s++) {
        if(dev->we.buffptr[s] == '\n') {
            const char *oldbuffptr;
            size_t oldsize;
            char * b;
            PDEBUG("Found \\n at index %lu; adding entry now\n",s);
            dev->we.complete = true;

            // +2 because we count form 0 and want one extra for Nul termination
            b = kzalloc(s+2, GFP_KERNEL);
            if(!b) {
                retval = -ENOMEM;
                goto out;
            }
            for(size_t i=0; i<s+1; i++) {
                b[i] = dev->we.buffptr[i];
            }
            b[s+1] = '\0';
            // Null is hidden from normal operation, for debugging with printk()

            finished_entry.buffptr = b;
            finished_entry.size = s+1;

            if (mutex_lock_interruptible(&dev->cb_mutex)) {
		        retval = -ERESTARTSYS;
                goto out;
            }

            oldsize = dev->cb.entry[dev->cb.in_offs].size;  // must go first
            oldbuffptr = aesd_circular_buffer_add_entry(    // must go second
                    &(dev->cb), 
                    &(finished_entry) 
                );
            
            PDEBUG("returned size was %lu, finished_entry.size is %lu", oldsize,
            finished_entry.size);
            dev->cb_size += finished_entry.size - oldsize;

            PDEBUG("Entered buffptr %p to circular buffer\n", 
                finished_entry.buffptr);
            PDEBUG("String reads back %s\n", finished_entry.buffptr);
            PDEBUG("Size of the circular buffer now is %lu\n", dev->cb_size);
            PDEBUG("Freeing oldbuffptr %p\n", oldbuffptr);
            
            kfree(oldbuffptr);
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
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_ioctl
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

    memset(&(aesd_device.cb),0,sizeof(struct aesd_circular_buffer));

    mutex_init(&aesd_device.we_mutex);
    mutex_init(&aesd_device.cb_mutex);

    aesd_device.we.buffptr = kzalloc(KMALLOC_MAX_SIZE,GFP_KERNEL);
    if(!aesd_device.we.buffptr) {
        result = -ENOMEM;
        goto fail;
    }
    printk("Got we.buffptr %p", aesd_device.we.buffptr);

    mutex_init(&aesd_device.we_mutex);
    aesd_circular_buffer_init(&aesd_device.cb);

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

    //while (mutex_lock_interruptible(&aesd_device.we_mutex)) {
    //    printk( KERN_CRIT "aesdchar: " "Failed to get lock for aesd_device.cb_mutex; can't clean up module!!!\n");
    //}
    //while (mutex_lock_interruptible(&aesd_device.cb_mutex)) {
    //    printk( KERN_CRIT "aesdchar: " "Failed to get lock for aesd_device.cb_mutex; can't clean up module!!!\n"); 
    //}
    AESD_CIRCULAR_BUFFER_FOREACH(e,&(aesd_device.cb),i) {
        kfree(e->buffptr);
    }
    kfree(aesd_device.we.buffptr);
    //mutex_unlock(&aesd_device.we_mutex);
    //mutex_unlock(&aesd_device.cb_mutex);
    
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
