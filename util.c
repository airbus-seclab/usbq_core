#include "util.h"
#include <linux/slab.h>

#define SIZE 1024
static char string[SIZE];

char* dump_endpoint_id(const epid_t *ep)
{
   snprintf(string,SIZE,"ep:%u type:%s dir:%s",ep->num,EP_TYPE_STR(ep->type),EP_DIR_STR(ep->dir));
   return string;
}

/* int alloc_buf(buf_t *buf, size_t size) */
/* { */
/*    buf->size = 0; */
/*    buf->buf = kmalloc(size,GFP_KERNEL); */
/*    if(!buf->buf) */
/*       return -ENOMEM; */
/*    buf->allocated_size = size; */
/*    return 0; */
/* } */

/* int realloc_buf(buf_t *buf, size_t size) */
/* { */
/*    buf->buf = krealloc(buf->buf,size,GFP_KERNEL); */
/*    if(!buf->buf) */
/*       return -ENOMEM; */
/*    if(buf->size > size) { */
/*       buf->size = size; */
/*    } */
/*    buf->allocated_size = size; */
/*    return 0; */
/* } */

/* int bufcpy(buf_t *buf, void *data, size_t size, size_t shift) */
/* { */
/*    memcpy(buf->buf+shift,data,size); */
/*    buf->size = size+shift; */
/*    return 0; */
/* } */

/* void free_buf(buf_t *buf) */
/* { */
/*    buf->size = 0; */
/*    buf->allocated_size = 0; */
/*    kfree(buf->buf); */
/*    buf->buf = NULL; */
/* } */
