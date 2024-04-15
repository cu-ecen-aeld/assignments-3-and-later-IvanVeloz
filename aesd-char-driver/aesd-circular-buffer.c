/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#include <stdio.h>
#endif

#define SCULL_DEBUG

#undef PDEBUG
#ifdef SCULL_DEBUG
#   ifdef __KERNEL__
    /* This one if debugging is on, and kernel space */
#   define PDEBUG(fmt, args...) printk( KERN_DEBUG "scull: " fmt, ## args)
#   else
    /* This one is for user space */
#   define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#   define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

#undef PDEBUGG
#define PDEBUGG(fmt, args...) /* nothing: it's a placeholder */

#include "aesd-circular-buffer.h"

static bool aesd_circular_increment(uint8_t *index, uint8_t max_index);

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    struct aesd_buffer_entry *r = NULL;
    buffer->full = false;

    if(buffer->in_offs == buffer->out_offs) {
        // It's empty
        return NULL;
    }

    r = &buffer->entry[buffer->out_offs];
    aesd_circular_increment(&buffer->out_offs, AESDCHAR_MAX_INDEX);

    entry_offset_byte_rtn = (char_offset < r->size)? 
        ((size_t *)(r->buffptr + char_offset)) : (NULL);
    
    return r;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    PDEBUG("Entering aesd_buffer_add_entry. buffer->in_offs = %u, buffer->out_offs = %u, buffer->full = %u \n", 
        buffer->in_offs, buffer->out_offs, buffer->full);
    buffer->entry[buffer->in_offs] = *add_entry;
    PDEBUG("Added to index %u the entry: %s \n",buffer->in_offs, buffer->entry[buffer->in_offs].buffptr);
    aesd_circular_increment(&buffer->in_offs, AESDCHAR_MAX_INDEX);

    if(buffer->full) {
        aesd_circular_increment(&buffer->out_offs,AESDCHAR_MAX_INDEX);
    }
    else if(buffer->in_offs == buffer->out_offs) {
        buffer->full = true;    
    }
    PDEBUG("Exiting  aesd_buffer_add_entry. buffer->in_offs = %u, buffer->out_offs = %u, buffer->full = %u \n", 
        buffer->in_offs, buffer->out_offs, buffer->full);
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
    buffer->in_offs = 0;
    buffer->out_offs = 0;
    buffer->full = false;
}

/**
 * Increment an @param index taking consideration the maximum 
 * @param entry_size and overflowing if necessary. For example, if the index
 * is 8 and entry_size is 9, the index will be incremented to 9. In a different
 * case, if the index is 9 and entry_size is 9, then the index will be 
 * incremented to 0 and an overflow is considered to have ocurred.
 * In cases where the index is larger than entry_size, index is set to 0 and an
 * overflow is considered to have ocurred.
 * @return true if an overflow ocurred, else returns false.
 */
static bool aesd_circular_increment(uint8_t *index, uint8_t max_index) {
    
    if(*index >= max_index) {
        *index = 0;
        return true;
    }
    else {
        *index = *index+1;
        return false;
    }
}
