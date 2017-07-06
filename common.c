#include "msg.h"
#include "common.h"
#include "types.h"
#include "debug.h"

#define CONFIG_COMMON_DEBUG

#ifndef CONFIG_COMMON_DEBUG_LEVEL
#define CONFIG_COMMON_DEBUG_LEVEL INFO
#endif

#ifdef CONFIG_COMMON_DEBUG
#define log(id,lvl,fmt, ...) _log(id,CONFIG_COMMON_DEBUG_LEVEL,lvl,fmt, ##__VA_ARGS__)
#define log_msg(id,lvl,buf,fmt, ...) _log_msg(id,CONFIG_COMMON_DEBUG_LEVEL,lvl,buf,fmt, ##__VA_ARGS__)
#else
#define log(id,lvl,fmt, ...) {}
#define log_msg(id,lvl,buf,fmt, ...) {}
#endif


/* -------------------------------------------------------------------------------
 *
 * Endpoint Management
 *
 *--------------------------------------------------------------------------------
 */

#define SIZE_DEBUG_ENDPOINT 256
static char debug_endpoint[SIZE_DEBUG_ENDPOINT];

char* dump_endpoint_id(const epid_t *ep)
{
   snprintf(debug_endpoint,SIZE_DEBUG_ENDPOINT,"ep:%u type:%s dir:%s",ep->num,EP_TYPE_STR(ep->type),EP_DIR_STR(ep->dir));
   return debug_endpoint;
}

int _create_endpoint(ep_t *ep, const epnum_t epnum, const eptype_t eptype, const epdir_t epdir,
                     const struct usb_endpoint_descriptor *desc, cb_conf_t *callbacks)
{
   ep->epid.num = epnum;
   ep->epid.dir = epdir;
   ep->epid.type = eptype;

   if (desc) {
      ep->desc = kmalloc(sizeof *desc,GFP_ATOMIC);
      if (!ep->desc) {
         goto fail1;
      }
      memcpy((void*)ep->desc,desc,sizeof *desc);
   } else {
      ep->desc = NULL;
   }

   ep->name = kmalloc(128,GFP_KERNEL);
   if(!ep->name) {
      goto fail2;
   }
   snprintf(ep->name,128,"%s%d_%s",EP_TYPE_STR(ep->epid.type),ep->epid.num,EP_DIR_STR(ep->epid.dir));

   INIT_LIST_HEAD(&ep->reqlist);

   ep->wq = create_workqueue(ep->name);

   ep->ops = &((*callbacks)[eptype]);

   return 0;

 fail2:
   kfree(ep->name);
 fail1:
   return -ENOMEM;
}

int create_ep0_endpoint(ep_t *ep, const epdir_t epdir, cb_conf_t *callbacks) {
   return _create_endpoint(ep, 0, CTRL, epdir, NULL, callbacks);
}

int create_endpoint(ep_t *ep, const struct usb_endpoint_descriptor *desc, cb_conf_t *callbacks) {
   epnum_t epnum = desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
   eptype_t eptype = EP_TYPE_FROM_KERNEL(desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK);
   epdir_t epdir = EP_DIR_FROM_KERNEL(desc->bEndpointAddress & USB_ENDPOINT_DIR_MASK);

   return _create_endpoint(ep, epnum, eptype, epdir, desc, callbacks);
}

void free_endpoint(ep_t *ep)
{
   if (ep->desc) {
      kfree(ep->desc);
   }
   flush_workqueue(ep->wq);
   destroy_workqueue(ep->wq);
   kfree(ep->name);
}

ep_t* find_endpoint(const epid_t *id, struct list_head *list)
{
   ep_t *ep;

   list_for_each_entry(ep,list,list) {
      if(id->num == ep->epid.num && id->type == ep->epid.type && id->dir == ep->epid.dir) {
         return ep;
      }
   }
   return NULL;
}


/* Send messsage to userland */
int send_userland(com_t *com, msg_t *msg)
{
   ssize_t sz;

   sz = com->send(com,msg);
   if (sz < 0) {
      return -EAGAIN;
   }

   return sz;
}
