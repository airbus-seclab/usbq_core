#include "common.h"
#include "msg.h"
#include <linux/kernel.h>
#include <linux/slab.h>

static char debug_msg[256];


char *dump_msg(const msg_t *m)
{
   if (!m) {
      snprintf(debug_msg,256,"msg:NULL");
   } else if (IS_USB_DATA(m)) {
      snprintf(debug_msg,256,"msg:%p asize:%u data_size:%u %s",m,m->allocated_size,msg_get_data_size(m),dump_endpoint_id(&m->epid));
   } else if (IS_USB_ACK(m)) {
      snprintf(debug_msg,256,"msg:%p asize:%u data_size:%u ACK status:%d %s",m,m->allocated_size,msg_get_data_size(m),m->status,dump_endpoint_id(&m->epid));
   } else {
      snprintf(debug_msg,256,"msg:%p asize:%u data_size:%u type:%u management_type:%u",m,m->allocated_size,msg_get_data_size(m),m->type,m->management_type);
   }
   return debug_msg;
}

size_t _msg_diff_size(int type)
{
   size_t sz = sizeof(msg_type_t) + sizeof(size_t);
   if (type == DATA) {
      sz += sizeof(epid_t);
   } else if (type == ACK) {
      sz += sizeof(epid_t) + sizeof(int);
   } else if (type == MANAGEMENT) {
      sz += sizeof(msg_management_type_t);
   }
   return sz;
}

char *msg_get_data(const msg_t* msg) {
   if (IS_MANAGEMENT_MSG(msg)) {
      return (char *)msg->management_data;
   } else if (IS_USB_ACK(msg)) {
      return (char *)msg->ack_data;
   } else {
      return (char *)msg->data;
   }
}

size_t msg_get_data_size(const msg_t *msg)
{
   return msg->size - _msg_diff_size(msg->type);
}

void msg_set_data_size(msg_t *msg, size_t size)
{
   msg->size = size + _msg_diff_size(msg->type);
}

msg_t* alloc_msg(size_t size, int type)
{
   msg_t *m;

   m = kmalloc(size + _msg_diff_size(type) + sizeof(size_t), GFP_KERNEL);
   if (!m) {
      return NULL;
   }
   // In fact allocated size is greater than size (MSG_REAL_SIZE(size))
   // But we keep size, because this allocated size will be sent to host
   // It is the allocated size for message
   m->allocated_size = size;
   m->type = type;
   msg_set_data_size(m,0);

   return m;
}

msg_t* alloc_msg_data(size_t size)
{
   return alloc_msg(size,DATA);
}

msg_t* alloc_msg_ack(epid_t *epid, int status, char *buf, size_t size)
{
   msg_t *m;

   m = alloc_msg(size,ACK);
   if (!m) {
      return NULL;
   }

   m->status = status;
   msg_set_epid(m,epid);
   if (buf) {
      msgcpy(m,buf,size);
   }
   return m;
}

msg_t* alloc_msg_management(size_t size)
{
   return alloc_msg(size,MANAGEMENT);
}

void free_msg(msg_t *m) {
   kfree(m);
}

msg_t* msgcpy(msg_t *msg, void *buf, size_t sz)
{
   char *mbuf;
   mbuf = msg_get_data(msg) + msg_get_data_size(msg);
   msg_set_data_size(msg,sz+msg_get_data_size(msg));
   memcpy(mbuf,buf,sz);
   return msg;
}


void msg_set_epid(msg_t *msg, epid_t *epid)
{
   epid_t *id = &msg->epid;
   id->num = epid->num;
   id->type = epid->type;
   id->dir = epid->dir;
}

void msg_set_id(msg_t *msg, const epnum_t num, const eptype_t type, const epdir_t dir)
{
   epid_t *id = &msg->epid;
   id->num = num;
   id->type = type;
   id->dir = dir;
}

/*
 * Check message integrity
 */
int
check_msg(msg_t *msg)
{
   uint32_t size = 8; // size + type
   if (msg->size < size) {
      return 0;
   }

   if (msg->type == MANAGEMENT) {
      size += sizeof(msg_management_type_t);
      if (msg->size < size) {
         return 0;
      }
      if (msg->management_type != RESET && msg->management_type != RELOAD && msg->management_type != NEW_DEVICE) {
         return 0;
      }
   } else if (msg->type == DATA || msg->type == ACK) {
      size += sizeof(epid_t);
      if (msg->size < size) {
         return 0;
      }
      if (msg->epid.type != CTRL && msg->epid.type != INTERRUPT && msg->epid.type != ISOC && msg->epid.type != BULK) {
         return 0;
      }
      if (msg->epid.dir != IN && msg->epid.dir != OUT) {
         return 0;
      }
      if (msg->type == ACK) {
         size += 4; // Status
         if (msg->size < size) {
            return 0;
         }
      }
   } else {
      return 0;
   }
   return 1;
}
