/*
 * aesdchar.h
 *
 *  Created on: Oct 23, 2019
 *      Author: Dan Walkes
 */

#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

#define AESD_DEBUG 1  //Remove comment on this line to enable debug

#undef PDEBUG             /* undef it, just in case */
#ifdef AESD_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "aesdchar: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

#include <linux/mutex.h>
#include "aesd-circular-buffer.h"

struct aesd_working_entry
{
     /* We to store the incomplete command here. Allocate as much memory as 
      * kmalloc() allows, to comply with assignment requirements. 
      */
     char * buffptr;
     /* Stores the size of the buffptr memory block.
      */
     const size_t size;
     /* To support incomplete entries (partial writes), this index stores the 
      * last position that has been written to the entry. 
      */
     size_t index;
     /* To support incomplete entries, this flag stores the completion status.*/
     bool complete;
    /* The buffer entry we copy the finished command into. Allocate the minimum
     * size possible (use index or workingentry.size). 
     */
     struct aesd_buffer_entry finished_entry;
};

struct aesd_dev
{
    /**
     * TODO: Add structure(s) and locks needed to complete assignment requirements
     */
    struct cdev cdev;                           /* Char device structure      */
    struct aesd_working_entry we;               /* Working entry */     
    struct mutex we_mutex;                      /* Locking primitive */
    struct aesd_circular_buffer cb;             /* Circular buffer */
    struct mutex cb_mutex;                      /* Locking primitive */
};

#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */
