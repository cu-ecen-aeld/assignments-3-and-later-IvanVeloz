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
#include <linux/printk.h>
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
    size_t acc_char_offset = 0; //Accumulated char offset

    if(!buffer->full && buffer->in_offs == buffer->out_offs) {
        // The buffer is empty
        return NULL;
    }

    // Iterate through the circular buffer until we get to in_offs, which
    // is as far as we have written to the buffer. 
    // j handles "full buffer" edge case and is a handy counter.
    for(
        uint8_t j = 0, i = buffer->out_offs; 
        j==0 || i != buffer->in_offs; 
        ++j, aesd_circular_increment(&i,AESDCHAR_MAX_INDEX)
    ) 
    {
        r = &(buffer->entry[i]);

        PDEBUG("Entering i=%u, acc_char_offset=%lu, char_offset=%lu, r->size=%lu\n",
            i, acc_char_offset,char_offset,r->size);

        if(acc_char_offset + r->size - 1 >= char_offset) {
            size_t local_offset = char_offset - acc_char_offset;
            *entry_offset_byte_rtn = local_offset;
            PDEBUG("Found the desired entry for char_offset %lu at index %i, local_offset %lu\n", 
                char_offset, i, local_offset);
            goto success;
        }

        acc_char_offset = acc_char_offset + r->size;
        PDEBUG("Entering i=%u, acc_char_offset=%lu char_offset=%lu\n",
            i, acc_char_offset,char_offset);
    }
    r = NULL;
    success:
    return r;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
* @returns NULL if not full or a pointer to a *buffptr member of aesd_buffer_entry that needs to be freed.
*/
const char * aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    const char * r = NULL;
    PDEBUG("Entering aesd_buffer_add_entry. buffer->in_offs = %u, buffer->out_offs = %u, buffer->full = %u\n", 
        buffer->in_offs, buffer->out_offs, buffer->full);
    r = buffer->entry[buffer->in_offs].buffptr;
    buffer->entry[buffer->in_offs] = *add_entry;
    PDEBUG("Added to index %u the entry: %s \n",buffer->in_offs, buffer->entry[buffer->in_offs].buffptr);
    aesd_circular_increment(&buffer->in_offs, AESDCHAR_MAX_INDEX);

    if(buffer->full) {
        aesd_circular_increment(&buffer->out_offs,AESDCHAR_MAX_INDEX);
    }
    else if(buffer->in_offs == buffer->out_offs) {
        buffer->full = true;    
    }
    PDEBUG("Exiting  aesd_buffer_add_entry. buffer->in_offs = %u, buffer->out_offs = %u, buffer->full = %u, r = %p\n", 
        buffer->in_offs, buffer->out_offs, buffer->full, r);
    return r;
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
 * @param max_index and overflowing if necessary. For example, if the index
 * is 8 and max_index is 9, the index will be incremented to 9. In a different
 * case, if the index is 9 and max_index is 9, then the index will be 
 * incremented to 0 and an overflow is considered to have ocurred.
 * In cases where the index is larger than max_index, index is set to 0 and an
 * overflow is considered to have ocurred.
 * @return true if an overflow ocurred, else returns false.
 */
bool aesd_circular_increment(uint8_t *index, uint8_t max_index) {
    
    if(*index >= max_index) {
        *index = 0;
        return true;
    }
    else {
        *index = *index+1;
        return false;
    }
}
