#include <linux/device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/version.h>

#include "debug.h"
#include "util.h"
#include "msg.h"
#include "com.h"
#include "com_udp.h"
#include "gadget.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
#include "epautoconf.c"
#endif

#ifndef CONFIG_GADGET_DEBUG_LEVEL
#define CONFIG_GADGET_DEBUG_LEVEL DBG
#endif

#ifdef CONFIG_GADGET_TRACE
#define trace _trace("GADGET")
#else
#define trace {}
#endif

#define CONFIG_GADGET_DEBUG

#ifdef CONFIG_GADGET_DEBUG
#define log(lvl,fmt, ...) _log("GADGET",CONFIG_GADGET_DEBUG_LEVEL,lvl,fmt, ##__VA_ARGS__)
#define log_msg(lvl,buf,fmt, ...) _log_msg("GADGET",CONFIG_GADGET_DEBUG_LEVEL,lvl,buf,fmt, ##__VA_ARGS__)
#else
#define log(lvl,fmt, ...) {}
#define log_msg(lvl,buf,fmt, ...) {}
#endif

#define CONFIG_GADGET_ASSERT

#ifdef CONFIG_GADGET_ASSERT
#define assert(func) _assert("GADGET",func)
#else
#define assert(func) {}
#endif

#include "common.h"


/*-------------------------------------------------------------------------*/
int
endpoint_queue(gadget_request_t *req)
{
   int err;
   epid_t *epid = &req->ep->epid;
   struct usb_request *r = req->req;

   log_msg(DBG,req->msg,"USB SEND ++ [%s] req:%s ep:%s %p",dump_endpoint_id(epid),dump_usb_request(r),dump_usb_ep(req->ep->usb_ep));
   err = usb_ep_queue(req->ep->usb_ep, r, GFP_ATOMIC);
   if (err < 0) {
      log(ERR, "Unable to queue request err:[%d] req:[%s] ep:[%s]",err,dump_usb_request(r),dump_endpoint_id(epid));
      return err;
   }
   log(DBG,"usb_ep_queue succeed");

   return 0;
}

/*-------------------------------------------------------------------------
 *
 * Gadget endpoint management
 *
 -------------------------------------------------------------------------*/
gadget_endpoint_t*
add_gadget_ep0_endpoint(epdir_t epdir)
{
   int err;
   gadget_endpoint_t *ep;

   ep = kmalloc(sizeof *ep, GFP_KERNEL);
   if (!ep) {
      goto fail1;
   }

   err = create_ep0_endpoint((ep_t *)ep, epdir, &gadget_cb_conf);
   if (err < 0) {
      goto fail2;
   }

   ep->usb_ep = gadget_state.gadget->ep0;
   ep->usb_ep->driver_data = ep;

   list_add(&ep->list,&gadget_state.eplist);

   log(INFO,"Add gadget endpoint epid:[%s] ep:[%s]",dump_endpoint_id(&ep->epid),ep->epid,dump_usb_ep(ep->usb_ep));

   return ep;

 fail2:
   kfree(ep);
 fail1:
   log(ERR,"Unable to allocate memory");
   return NULL;
}

int
ep_match_name(const char *name, const char *gname)
{
   if (!strcmp(name,gname)) {
      return 1;
   } else {
      if (!strncmp(name,gname,strlen(gname))) {
         return 1;
      }
   }
   return 0;
}

/* Similar to what is done with usb_ep_autoconfig, but do not change address */
static struct usb_ep *
usb_ep_config(struct usb_endpoint_descriptor *desc)
{
   struct usb_ep *ep = NULL;

   char myname[16];

   snprintf(myname, 16, "ep%d%s",
            desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK,
            ((desc->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
             == USB_DIR_IN) ? "in": "out");

   log(DBG, "autoconf: Looking for endpoint %s\n", myname);

   list_for_each_entry (ep, &gadget_state.gadget->ep_list, ep_list) {
      if (ep_match_name(myname,ep->name)) {
         log(DBG,"Find valid ep:[%s]",dump_usb_ep(ep));
         if (ep->maxpacket_limit < (0x7ff & usb_endpoint_maxp(desc))) {
            desc->wMaxPacketSize = cpu_to_le16(ep->maxpacket_limit);
         }
         return ep;
      } else {
         log(SPEC,"not good %s %s",myname,ep->name);
      }
   }

   log(ERR,"Unable to find endpoint %s epdesc:[%s]",myname,dump_usb_endpoint_descriptor(desc));

   return NULL;
}


gadget_endpoint_t*
add_gadget_endpoint(struct usb_endpoint_descriptor *epdesc)
{
   int err;
   gadget_endpoint_t *ep;
   struct usb_ep *usb_ep;

   usb_ep = usb_ep_config(epdesc);
   if(!usb_ep) {
      log(ERR,"Unable to autoconfig endpoint desc:[%s]",dump_usb_endpoint_descriptor(epdesc));
      return NULL;
   }

   ep = kmalloc(sizeof *ep, GFP_KERNEL);
   if (!ep) {
      goto fail1;
   }

   err = create_endpoint((ep_t *)ep, epdesc, &gadget_cb_conf);
   if (err < 0) {
      goto fail2;
   }

   ep->usb_ep = usb_ep;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0)
   usb_ep->desc = ep->desc;
#endif
   usb_ep->driver_data = gadget_state.gadget;

   list_add(&ep->list,&gadget_state.eplist);

   err = usb_ep_disable(usb_ep);
   if (err<0) {
      log(WRN,"Unable to disable [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
   }
   usb_ep->desc = ep->desc;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0)
   err = usb_ep_enable(usb_ep,ep->desc);
#else
   err = usb_ep_enable(usb_ep);
#endif
   if (err < 0) {
      log(ERR,"Unable to enable endpoint err:%d [desc:%s] maxpacket:%hu maxpacket_limit:%hu addr:%02x",err,dump_usb_endpoint_descriptor(usb_ep->desc),usb_ep->maxpacket,usb_ep->maxpacket_limit,usb_ep->address);
      goto fail3;
   }

   log(INFO,"Add gadget endpoint epid:[%s] ep:[%s]",dump_endpoint_id(&ep->epid),dump_usb_ep(ep->usb_ep));

   return ep;

 fail3:
   list_del_init(&ep->list);
   free_endpoint((ep_t *)ep);
 fail2:
   kfree(ep);
 fail1:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
   usb_ep_autoconfig_release(usb_ep);
#endif
   return NULL;
}


void
dump_active_gadget_endpoints(void)
{
   ep_t *ep;
   int i = 0;

   log(SPEC,"Active gadget endpoints");
   list_for_each_entry(ep,&gadget_state.eplist,list) {
      log(SPEC,"%u : %s",i,dump_usb_endpoint_descriptor(ep->desc));
      i++;
   }
}

void
free_gadget_endpoint(gadget_endpoint_t *ep)
{
   gadget_request_t *req, *tmp;
   int err;

   log(DBG,"Free gadget endpoint [%s]",dump_endpoint_id(&ep->epid));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)
   usb_ep_autoconfig_release(ep->usb_ep);
#endif

   // Remove from list
   list_del_init(&ep->list);

   // Safe has to be used, because usb_ep_dequeue will call completion handler
   // That will delete request from list
   list_for_each_entry_safe(req,tmp,&ep->reqlist,list) {
      // Using dequeue will raise completion routine with a status to -ECONNRESET
      // Then request will be freed in completion routine
      err = usb_ep_dequeue(ep->usb_ep,req->req);
      if (err<0) {
         log(WRN,"Unable to dequeue request [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
      }
   }

   usb_ep_fifo_flush(ep->usb_ep);

   err = usb_ep_disable(ep->usb_ep);
   if (err<0) {
      log(WRN,"Unable to disable [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
   }

   free_endpoint((ep_t *)ep);
   kfree(ep);
}

gadget_endpoint_t*
find_gadget_endpoint(const epid_t *id)
{
   return (gadget_endpoint_t *)find_endpoint(id, &gadget_state.eplist);
}

int
disable_active_interface(void)
{
   int i;

   log(DBG,"Disable active interfaces");
   for (i=0; i<gadget_state.identity.nb_int; i++) {
      interface_desc_t *iface = &gadget_state.identity.interfaces[i];
      if (iface->active) {
         int err;
         err = disable_interface(iface);
         if (err<0) {
            log(ERR,"Unable to disable interface [%d]",err);
            return err;
         }
      }
   }

   return 0;
}

/*
  Activate all num 0 interfaces, with all their endpoints
 */
int
enable_default_interface(void)
{
   int i;

   log(DBG,"Enable default interfaces");
   // Enable all interface with altsetting 0
   for (i=0; i<gadget_state.identity.nb_int; i++) {
      interface_desc_t *iface = &gadget_state.identity.interfaces[i];
      if (iface->desc.bAlternateSetting == 0) {
         int err;
         err = enable_interface(iface);
         if (err<0) {
            log(ERR,"Unable to enable interface [%d]",err);
            return err;
         }
      }
   }
   return 0;
}

int
disable_interface(interface_desc_t *interface)
{
   int ep;

   interface->active = 0;

   for (ep=0; ep<interface->desc.bNumEndpoints; ep++) {
      struct usb_endpoint_descriptor *desc = &interface->endpoints[ep];
      epid_t epid = {desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK,
                     EP_TYPE_FROM_KERNEL(desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK),
                     EP_DIR_FROM_KERNEL(desc->bEndpointAddress & USB_ENDPOINT_DIR_MASK)};
      gadget_endpoint_t *ep;

      ep = find_gadget_endpoint(&epid);
      if (ep) {
         log(DBG,"Disabling endpoint %s",dump_endpoint_id(&epid));
         free_gadget_endpoint(ep);
      }
   }
   return 0;
}

int
enable_interface(interface_desc_t *interface)
{
   int ep;
   int err;

   log(DBG,"Enable interface %u %u",interface->desc.bInterfaceNumber,interface->desc.bAlternateSetting);

   interface->active = 1;

   for (ep=0; ep<interface->desc.bNumEndpoints; ep++) {
      struct usb_endpoint_descriptor *epdesc = &interface->endpoints[ep];
      gadget_endpoint_t *epnew;

      epnew = add_gadget_endpoint(epdesc);
      if(!epnew) {
         log(ERR,"Unable to add endpoint %u type:%s dir:%s",
             epdesc->bEndpointAddress,
             EP_TYPE_STR(EP_TYPE_FROM_KERNEL(epdesc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)),
             EP_DIR_STR(EP_DIR_FROM_KERNEL(epdesc->bEndpointAddress & USB_ENDPOINT_DIR_MASK)));
         return -ENOMEM;
      }

      // Wait for host communication if OUT
      if (IS_OUT(epnew) && !IS_CTRL(epnew)) {
         err = epnew->ops->send_usb(epnew, NULL);
         if (err<0) {
            log(ERR,"Unable to ask for OUT [%d] epid:[%s]",err,dump_endpoint_id(&epnew->epid));
            free_gadget_endpoint(epnew);
            return err;
         }
      }
   }

   return 0;
}

int
set_interface(ushort ifnumber, ushort alternative)
{
   int i;
   int ret;
   interface_desc_t *active = NULL;

   log(DBG,"Set interface %u %u", ifnumber, alternative);

   for (i=0; i<gadget_state.identity.nb_int; i++) {
      interface_desc_t *interface = &gadget_state.identity.interfaces[i];

      if (interface->desc.bInterfaceNumber == ifnumber) {
         if (interface->desc.bAlternateSetting == alternative) {
            active = interface;
         } else {
            ret = disable_interface(interface);
            if (ret != 0) {
               log(WRN,"Unable to disable interface [%d] (%u,%u)",ret,interface->desc.bInterfaceNumber,interface->desc.bAlternateSetting);
               return ret;
            }
         }
      }
   }

   if (active) {
      ret = enable_interface(active);
      if (ret != 0) {
         log(ERR,"Problem during enable interface [%d] (%u,%u)",ret,ifnumber,alternative);
         return ret;
      } else {
         return 0;
      }
   }

   log(ERR,"Unable to set interface (%hu,%hu) : not found",ifnumber, alternative);

   return -EINVAL;
}

/*-------------------------------------------------------------------------
 *
 * Gadget request allocation management
 *
 -------------------------------------------------------------------------*/
gadget_request_t*
alloc_gadget_request(gadget_endpoint_t *ep, const size_t sz, int type)
{
   gadget_request_t *req;

   req = kmalloc(sizeof *req, GFP_KERNEL);
   if (!req) {
      goto fail1;
   }

   req->req = usb_ep_alloc_request(ep->usb_ep, GFP_KERNEL);
   if (!req->req) {
      goto fail2;
   }

   req->msg = alloc_msg(sz,type);
   if (!req->msg) {
      goto fail3;
   }

   list_add(&req->list,&ep->reqlist);

   msg_set_epid(req->msg, &ep->epid);
   req->req->buf = msg_get_data(req->msg);
   req->ep = ep;

   return req;
 fail3:
   usb_ep_free_request(ep->usb_ep, req->req);
 fail2:
   kfree(req);
 fail1:
   log(ERR,"Unable to allocate memory");
   return NULL;
}

static void
free_gadget_request(gadget_request_t *req)
{
   log(DBG,"Free gadget request epid:[%s]",dump_endpoint_id(&req->ep->epid));
   list_del(&req->list);
   free_msg(req->msg);
   usb_ep_free_request(req->ep->usb_ep,req->req);
   kfree(req);
}

/* -------------------------------------------------------------------------
 *
 * Generic Callback
 *
 * -------------------------------------------------------------------------*/

/*
  Function executed by workqueue
*/
static void
recv(struct work_struct *data)
{
   gadget_request_t *dreq = (gadget_request_t *)data;
   gadget_endpoint_t *ep = dreq->ep;
   int err;

   err = ep->ops->recv_usb(ep, dreq);
   if (err<0) {
      log(ERR,"Unable to recv usb [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
   }
   ep->ops->free_request(ep, dreq);
}

static void
gadget_recv_usb(struct usb_ep *endpoint, struct usb_request *req)
{
   int err;
   gadget_request_t *dreq = (gadget_request_t *)req->context;
   gadget_endpoint_t *ep = dreq->ep;

   // Cannot be done in Work Queue, because this function will be finished
   // before usb_ep_dequeue returns, but not necessarily the workqueue function
   // So a race can occcur, and ep could be freed
   if(req->status < 0) {
      log(ERR,"USB problem during reception [%d] epid:[%s]",req->status,dump_endpoint_id(&ep->epid));
      ep->ops->free_request(ep,dreq);
      return;
   }

   INIT_WORK(&dreq->work, &recv);

   err = queue_work(dreq->ep->wq, &dreq->work);
   if(err < 0) {
      log(WRN,"Unable to queue work");
   }
}

int
gadget_recv_userland_usb(msg_t *msg)
{
   int err;

   log_msg(DBG,msg,"UDP -- RECV");

   if(!gadget_state.registered) {

   } else {
      gadget_endpoint_t *ep;

      ep = find_gadget_endpoint(&msg->epid);
      if (!ep) {
         log(ERR,"Unable to find endpoint epid:[%s]",dump_endpoint_id(&msg->epid));
         return -EINVAL;
      }

      err = ep->ops->recv_userland(ep, msg);
      if (err<0) {
         log(ERR,"Unable to recv userland [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
         return err;
      }
   }
   return 0;
}


int
check_descriptor_header(char *buffer, int size)
{
   struct usb_descriptor_header *header = (struct usb_descriptor_header *)buffer;

   log(DBG,"HEADER: %02x",header->bDescriptorType);

   if (size < sizeof *header) {
      log(WRN,"Not enough bytes for reading descriptor, skipping");
      return 0;
   }

   header = (struct usb_descriptor_header *)buffer;
   if ((header->bLength > size) || (header->bLength < 2)) {
      log(WRN,"Descriptor size pb %u (remaining %u)",header->bLength,size);
      return 0;
   }

   return 1;
}


/*
 * Parse firt packet containing identity device
 * Counter part of build_init_pkt
 */
int
parse_init_pkt(identity_t *identity, msg_t *msg)
{
   int nintf, nintf_orig;
   int cfgno = 0;
   int cfgidx = 0;
   int i_interface = 0;
   int ret = 0;
   struct usb_device_descriptor *ddesc;
   struct usb_config_descriptor *cdesc;
   char *buffer;
   size_t size;

   buffer = msg_get_data(msg);
   size = msg_get_data_size(msg);

   // Speed Parsing
   if (size < sizeof(enum usb_device_speed)) {
      log(WRN,"Buffer to small for parsing speed");
      ret = -EINVAL;
      goto end;
   }
   memcpy(&identity->speed, buffer, sizeof(enum usb_device_speed));

   buffer += sizeof(enum usb_device_speed);
   size -= sizeof(enum usb_device_speed);

   // DeviceDescriptor Parsing
   if (size < USB_DT_DEVICE_SIZE) {
      log(WRN,"Buffer to small for parsing device");
      ret = -EINVAL;
      goto end;
   }
   memcpy(&identity->device, buffer, USB_DT_DEVICE_SIZE);

   ddesc = &identity->device;
   if (ddesc->bDescriptorType != USB_DT_DEVICE ||
       ddesc->bLength < USB_DT_DEVICE_SIZE ||
       ddesc->bLength > size) {
      log(WRN,"invalid descriptor for device"
              "type = 0x%X, length = %d\n", cfgidx,
              ddesc->bDescriptorType, identity->conf.bLength);
      ret = -EINVAL;
      goto end;
   }

   buffer += ddesc->bLength;
   size -= ddesc->bLength;

   // ConfigurationDescriptor Parsing
   if (size < USB_DT_CONFIG_SIZE) {
      log(WRN,"Buffer to small for parsing configuration");
      ret = -EINVAL;
      goto end;
   }
   memcpy(&identity->conf, buffer, USB_DT_CONFIG_SIZE);

   cdesc = &identity->conf;
   if (cdesc->bDescriptorType != USB_DT_CONFIG ||
       cdesc->bLength < USB_DT_CONFIG_SIZE ||
       cdesc->bLength > size) {
      log(WRN,"invalid descriptor for config index %d: "
              "type = 0x%X, length = %d\n", cfgidx,
          cdesc->bDescriptorType, cdesc->bLength);
      ret = -EINVAL;
      goto end;
   }

   buffer += identity->conf.bLength;
   size -= identity->conf.bLength;


   // InterfaceDescriptor Parsing
   nintf = nintf_orig = cdesc->bNumInterfaces;
   if (nintf > MAX_INTERFACE_CONFIGURATION) {
      log(WRN, "config %d has too many interfaces: %d, "
               "using maximum allowed: %d\n",
               cfgno, nintf, MAX_INTERFACE_CONFIGURATION);
      nintf = MAX_INTERFACE_CONFIGURATION;
   }

   log(DBG,"*** NB INTERFACES:%u",nintf);

   while (size > 0) {
      struct usb_descriptor_header *header;

      if (!check_descriptor_header(buffer,size)) {
         goto end;
      }

      header = (struct usb_descriptor_header *)buffer;
      size -= header->bLength;
      buffer += header->bLength;

      if (header->bDescriptorType == USB_DT_INTERFACE) {
         struct usb_interface_descriptor *d;
         int nb_ep;
         int j_desc;
         int i_ep = 0;

         d = (struct usb_interface_descriptor *) header;
         if (d->bLength < USB_DT_INTERFACE_SIZE) {
            log(WRN,"config %d has an invalid "
                     "interface descriptor of length %d, "
                     "skipping\n", cfgno, d->bLength);

            continue;
         }

         nb_ep = d->bNumEndpoints;
         log(DBG,"*** NEW INTERFACE with %u endpoints",nb_ep);
         for (j_desc=0; i_ep<nb_ep; j_desc++) {
            if (!check_descriptor_header(buffer,size)) {
               goto end;
            }

            header = (struct usb_descriptor_header *)buffer;
            size -= header->bLength;
            buffer += header->bLength;

            log(DBG,"*** NEW descriptor %02X",header->bDescriptorType);
            if (header->bDescriptorType == USB_DT_ENDPOINT) {
               struct usb_endpoint_descriptor *e;

               e = (struct usb_endpoint_descriptor *) header;


               if (d->bLength < USB_DT_ENDPOINT_SIZE) {
                  log(WRN,"config %d has an invalid "
                      "endpoint descriptor of length %d, "
                      "skipping\n", cfgno, e->bLength);

                  continue;
               }
               memcpy(&identity->interfaces[i_interface].endpoints[i_ep],e,sizeof *e);
               log(DBG,"New EP:%s",dump_usb_endpoint_descriptor(e));
               i_ep++;
            }
         }

         memcpy(&identity->interfaces[i_interface].desc,d,sizeof *d);
         identity->interfaces[i_interface].active = 0;
         i_interface++;
      }
   }
 end:
   identity->nb_int = i_interface;
   return ret;
}

struct usb_gadget_driver ubq_gadget = {
   .function  = "ubq_gadget",
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)
   .speed = USB_SPEED_HIGH,
#else
   .max_speed = USB_SPEED_HIGH,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)
   .bind = ubq_bind,
#endif
   .unbind = ubq_unbind,
   .setup = ubq_setup,
   .disconnect = ubq_disconnect,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)
   .reset = ubq_disconnect,
#endif
   .driver = {
      .owner = THIS_MODULE,
      .name = "ubq_gadget"
   },
};


/*
 * Called when a new device packet is received
 */
int
callback_new_device(msg_t *msg)
{
   int ret;
   identity_t *ident = &gadget_state.identity;

   log_msg(INFO,msg,"New device detected");

   if (gadget_state.registered) {
      log(WRN,"Device was already registered, force deregister");
      ubq_unregister();
      clean_endpoints();
   }

   ret = parse_init_pkt(ident,msg);
   if (ret<0) {
      log(WRN,"Unable to parser init pkt [%d]",ret);
      return ret;
   }

   ret = ubq_register();
   if (ret<0) {
      log(ERR,"Unable to register driver [%d]",ret);
      return ret;
   }
   gadget_state.registered = 1;

   return 0;
}

/*
 * Called when a reset is received
 */
int
callback_reset(msg_t *msg)
{
   log(INFO,"DEVICE DISCONNECT !!");
   gadget_state.registered = 0;
   ubq_unregister();
   clean_endpoints();

   return 0;
}

int
gadget_recv_userland_management(msg_t *msg)
{
   int err;
   if (IS_RESET_MNG_MSG(msg)) {
      err = callback_reset(msg);
      if (err<0) {
         log(ERR,"Unable to reset [%d]",err);
         return err;
      }
   } else if (IS_NEW_DEVICE_MNG_MSG(msg)) {
      err = callback_new_device(msg);
      if (err<0) {
         log(ERR,"Unable to callback new device [%d]",err);
         return err;
      }
   } else {
      log(WRN,"Unknown userland mangement message received : %u %s",msg->management_type,dump_msg(msg));
      return -EINVAL;
   }
   return 0;
}

int
gadget_recv_userland(msg_t *msg)
{
   int err;

   if (IS_USB_MSG(msg)) {
      err = gadget_recv_userland_usb(msg);
      if (err<0) {
         log(ERR,"Unable to recv userland usb [%d]",err);
         return err;
      }
   } else if (IS_MANAGEMENT_MSG(msg)) {
      err = gadget_recv_userland_management(msg);
      if (err<0) {
         log(ERR,"Unable to recv userland management [%d]",err);
         return err;
      }
   } else {
      log(WRN,"Unknown userland message received");
      return -EINVAL;
   }
   return 0;
}


/*-------------------------------------------------------------------------
 *
 * Endpoint Gadget Callback implementations
 *
 -------------------------------------------------------------------------*/
int
ep_gadget_send_usb(gadget_endpoint_t *ep, msg_t *msg)
{
   int err;
   gadget_request_t *req;

   req = ep->ops->fill_request(ep, msg);
   if (!req) {
      log(ERR,"Unable to fill request");
      return -EINVAL;
   }

   err = endpoint_queue(req);
   if (err < 0) {
      log(ERR,"Unable to endpoint queue [%d]",err);
      ep->ops->free_request(ep, req);
      return err;
   }

   return 0;
}

int
ep_gadget_recv_usb_ctrl(gadget_endpoint_t *ep, gadget_request_t *req)
{
   int err;

   if (IS_IN(ep)) { // It must be a IN ACK because IN request are handle in setup
      if (req->req->status != 0) {
         log(ERR,"Unable to sent request [%d] %s on ep %s",req->req->status,dump_usb_request(req->req), dump_endpoint_id(&ep->epid));
         return req->req->status;
      }
      log_msg(DBG,req->msg,"USB ++ SENT %s %s", dump_usb_request(req->req), dump_endpoint_id(&ep->epid));
   } else { // OUT
      struct usb_ctrlrequest *ctrl = (struct usb_ctrlrequest *)msg_get_data(req->msg);
      msg_set_data_size(req->msg,req->req->actual + sizeof(struct usb_ctrlrequest));
      if (le16_to_cpu(ctrl->wLength) > 0) { // Data remaining from CTRL OUT
         log_msg(DBG,req->msg,"USB ++ RECV %s %s", dump_usb_request(req->req), dump_endpoint_id(&ep->epid));
         err = ep->ops->send_userland(ep, req->msg);
         if (err < 0) {
            log(ERR,"Unable to send on userland [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
            return err;
         }
      } else { // ACK of CTRL OUT
         log_msg(DBG,req->msg,"USB ++ SENT ACK %s %s", dump_usb_request(req->req), dump_endpoint_id(&ep->epid));
      }
   }

   return 0;
}

int
ep_gadget_recv_usb(gadget_endpoint_t *ep, gadget_request_t *req)
{
   int err;

   log(DBG,"RECV USB [%s] sz:%u status:%d",dump_endpoint_id(&ep->epid),req->req->actual,req->req->status);
   msg_set_data_size(req->msg,req->req->actual);
   if (IS_OUT(ep)) {
      log_msg(DBG,req->msg,"USB ++ RECV %s %s", dump_usb_request(req->req), dump_endpoint_id(&ep->epid));
      err = ep->ops->send_userland(ep, req->msg);
      if (err < 0) {
         log(ERR,"Unable to send on userland [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
         return err;
      }

      log(DBG,"Resubmitting OUT endpoint [%s]",dump_endpoint_id(&ep->epid));

      // TODO: Wait for ACK from userland before resubmit
      err = ep->ops->send_usb(ep,NULL);  // Need to receive other OUT message, so resubmit
      if (err<0) {
         log(ERR,"Unable to ask for OUT data [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
         return err;
      }
   } else { // IN message consumed by host, send ACK to user land
      msg_t *m = alloc_msg_ack(&ep->epid, req->req->status, NULL, 0);
      if(!m) {
         log(ERR,"Unable to allocate memory");
         return -ENOMEM;
      }

      log_msg(DBG,req->msg,"USB ++ SENT %s %s", dump_usb_request(req->req), dump_endpoint_id(&ep->epid));

      err = ep->ops->send_userland(ep,m);
      free_msg(m);
      if (err < 0) {
         log(ERR,"Unable to send on userland [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
         return err;
      }
   }

   return 0;
}

int
ep_gadget_send_userland(gadget_endpoint_t *ep, msg_t *msg) {
   int err;
   log_msg(DBG,msg,"UDP -- SEND");

   err = send_userland(gadget_state.com, msg);
   if (err<0) {
      log(ERR,"Unable to send on userland [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
      return err;
   }
   return 0;
}

int
ep_gadget_recv_userland_ctrl(gadget_endpoint_t *ep, msg_t *msg)
{
   int err;
   struct usb_ctrlrequest *ctrl = (struct usb_ctrlrequest *)msg_get_data(msg);

   if (IS_USB_ACK(msg)) {
      log(WRN,"RECV ACK [%d] halting epid:[%s]",msg->status,dump_endpoint_id(&ep->epid));
      if (msg->status == -EPIPE) {
         usb_ep_set_halt(ep->usb_ep);
      }
      return 0;
   }

   log_msg(DBG,msg,"RECV CTRL from USERLAND epid:[%s] ctrl:[%s]",dump_endpoint_id(&ep->epid),dump_usb_ctrlrequest(ctrl));

   if (IS_IN(ep)) {
      // Need to find endpoint headers, to create them
      // FIXME: 9 ?? Why 9 We need to get a complete response
      // FIXME: Overlapp on ep->ctrl ???
      if(IS_GET_DESC_CONFIGURATION(ctrl) && le16_to_cpu(ctrl->wLength) > 9) {
         err = disable_active_interface();
         if (err<0) {
            log(ERR,"Unable to disable active interfaces [%d]",err);
            return err;
         }

         err = enable_default_interface();
         if (err<0) {
            log(ERR,"Unable to enable default interfaces [%d]",err);
            return err;
         }
      }

      err = ep->ops->send_usb(ep, msg);
      if(err < 0) {
         log(ERR,"Unable to send usb [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
         return err;
      }
   } else {  // OUT
      // ACK received from userland, for a CTRL OUT without data
      log(DBG,"Received ACK for CTRL OUT without data");
      err = ep->ops->send_usb(ep, msg);
      if(err < 0) {
         log(ERR,"Unable to send usb [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
         return err;
      }
   }

   return 0;
}

int
ep_gadget_recv_userland(gadget_endpoint_t *ep, msg_t *msg) {
   int err;

   err = ep->ops->send_usb(ep, msg);
   if (err<0) {
      log(ERR,"Unable to send usb [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
      return err;
   }
   return 0;
}

/*
 * Called when create a CTRL request
 */
gadget_request_t*
ep_fill_ctrl_request(gadget_endpoint_t *ep, msg_t *msg)
{
   gadget_request_t *dreq;
   struct usb_request *req;
   size_t sz;
   struct usb_ctrlrequest *ctrl = (struct usb_ctrlrequest *)msg_get_data(msg);

   /*
     msg->buf = [CTRL REQUEST | REQUEST_BUFFER]
    */

   if (IS_IN(ep)) {
      sz = msg_get_data_size(msg);
   } else { // OUT
      sz = msg_get_data_size(msg) + le16_to_cpu(ctrl->wLength);
   }

   dreq = alloc_gadget_request(ep,sz,msg->type);
   if (!dreq) {
      log(ERR,"Unable to allocate gadget request epid:[%s]",dump_endpoint_id(&ep->epid));
      return NULL;
   }

   // Copy data
   msgcpy(dreq->msg, msg_get_data(msg), msg_get_data_size(msg));

   req = dreq->req;
   req->context = dreq;
   req->length = sz - sizeof *ctrl;
   req->zero = msg_get_data_size(dreq->msg) < req->length;
   req->complete = gadget_recv_usb;
   // Buffer to be used is after backup of ctrl
   req->buf = msg_get_data(dreq->msg) + sizeof *ctrl;

   return dreq;
}

gadget_request_t*
ep_fill_request(gadget_endpoint_t *ep, msg_t *msg)
{
   gadget_request_t *dreq;
   struct usb_request *req;

   if (!msg) { // OUT
      assert(IS_OUT(ep));

      //dreq = alloc_gadget_request(ep,MAX_BULK_DATA_SIZE,DATA);
      /* If a size greater than le16_to_cpu(ep->desc->wMaxPacketSize) is used (like MAX_BULK_DATA_SIZE,DATA)
         for MASS_STORAGE device host cannot send data anymore (BULKOUT for modification for instance) */
      dreq = alloc_gadget_request(ep,le16_to_cpu(ep->desc->wMaxPacketSize),DATA);

      if (!dreq) {
         log(ERR,"Unable to allocate gadget request epid:[%s]",dump_endpoint_id(&ep->epid));
         return NULL;
      }
   } else { // IN
      assert(IS_IN(ep));
      dreq = alloc_gadget_request(ep, msg_get_data_size(msg),msg->type);
      if (!dreq) {
         log(ERR,"Unable to allocate gadget request epid:[%s]",dump_endpoint_id(&ep->epid));
         return NULL;
      }
      msgcpy(dreq->msg, msg_get_data(msg), msg_get_data_size(msg));
   }

   req = dreq->req;

   req->context = dreq;
   if(ep->epid.dir == IN) {
      req->length = msg_get_data_size(dreq->msg);
   } else {
      req->length = dreq->msg->allocated_size;
   }
   req->zero = 0;
   req->complete = gadget_recv_usb;
   req->buf = msg_get_data(dreq->msg);

   return dreq;
}

void
ep_free_gadget_request(gadget_endpoint_t *ep, gadget_request_t *req)
{
   free_gadget_request(req);
}

/* ------------------------------------------------------------------------------------ */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
int
ubq_bind(struct usb_gadget *gadget)
#else
int
ubq_bind(struct usb_gadget *gadget, struct usb_gadget_driver *driver)
#endif
{
   int err;

   gadget_state.gadget = gadget;

   usb_ep_autoconfig_reset(gadget);

   if (!add_gadget_ep0_endpoint(IN)) {
      log(ERR,"bind: failure");
      return err;
   }

   if (!add_gadget_ep0_endpoint(OUT)) {
      log(ERR,"bind: failure");
      return err;
   }

   return 0;
}

static void
ubq_unbind(struct usb_gadget *gadget)
{
   gadget->ep0->driver_data = NULL;
}


static void
handle_setup(struct work_struct *data)
{
   setup_request_t *setup = (setup_request_t *)data;
   struct usb_ctrlrequest *ctrl = setup->ctrl;
   gadget_endpoint_t *ep;
   int err;
   epid_t epid = {0,CTRL,EP_DIR_FROM_KERNEL(ctrl->bRequestType & USB_ENDPOINT_DIR_MASK)};
   msg_t *msg;

   trace;

   ep = find_gadget_endpoint(&epid);
   if(!ep) {
      log(ERR,"Unable to find endpoint epid:[%s]",dump_endpoint_id(&epid));
      goto end;
   }

   msg = alloc_msg(sizeof *ctrl,DATA);
   if (!msg) {
      log(ERR,"Unable to allocate memory");
      goto end;
   }
   msg_set_id(msg, 0, CTRL, ep->epid.dir);
   msgcpy(msg, (void *)ctrl, sizeof *ctrl);

   log_msg(DBG,msg,"USB ++ RECV ctrl %s on ep %s", dump_usb_ctrlrequest(ctrl), dump_endpoint_id(&ep->epid));

   // If OUT with data, then data are not yet embeded, we shall ask USB host for that data
   if (IS_OUT(ep) && le16_to_cpu(ctrl->wLength) != 0) {
      log(DBG,"CTRL 0 OUT, need to retrieve data %s",dump_usb_ctrlrequest(ctrl));
      err = ep->ops->send_usb(ep, msg);
      if (err < 0) {
         log(ERR,"Unable to send USB to ask OUT data [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
         goto end;
      }
   } else {
      if ((ctrl->bRequestType&USB_TYPE_MASK) == USB_TYPE_STANDARD) {
         switch (ctrl->bRequest) {
         case USB_REQ_SET_CONFIGURATION:
            log(DBG,"RECEIVE SET CONFIGURATION");
            break;
         case USB_REQ_SET_INTERFACE:
            err = set_interface(le16_to_cpu(ctrl->wIndex),le16_to_cpu(ctrl->wValue));
            if (err<0) {
               log(ERR,"Unable to set interface [%d]",err);
               goto end;
            }
            break;
         }
      }
      err = ep->ops->send_userland(ep, msg);
      if(err < 0) {
         log(ERR,"Unable to send to userland [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
         goto end;
      }
      if (IS_OUT(ep) && le16_to_cpu(ctrl->wLength) == 0) {
         log(DBG,"Send ACK to CTRL OUT without data");
         err = ep->ops->send_usb(ep, msg);
         if (err < 0) {
            log(ERR,"Unable to send USB ACK OUT without data [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
            goto end;
         }
      }
   }

   log(DBG,"Handle setup ok");

 end:
   kfree(setup->ctrl);
   kfree(setup);
}


static int
ubq_setup(struct usb_gadget *gadget, const struct usb_ctrlrequest *ctrl)
{
   int err;
   setup_request_t *setup;
   gadget_endpoint_t *ep;
   epid_t epid = {0,CTRL,EP_DIR_FROM_KERNEL(ctrl->bRequestType & USB_ENDPOINT_DIR_MASK)};

   trace;

   gadget->ep0->driver_data = gadget;

   ep = find_gadget_endpoint(&epid);
   if(!ep) {
      log(ERR,"Unable to find gadget endpoint epid:[%s]",dump_endpoint_id(&epid));
      err = -EINVAL;
      goto fail1;
   }

   setup = kmalloc(sizeof *setup, GFP_KERNEL);
   if(!setup) {
      log(WRN,"Unable to allocate memory");
      err = -ENOMEM;
      goto fail1;
   }

   setup->ctrl = kmalloc(sizeof *setup->ctrl, GFP_KERNEL);
   if(!setup->ctrl) {
      log(WRN,"Unable to allocate memory");
      err = -ENOMEM;
      goto fail2;
   }

   memcpy(setup->ctrl,ctrl,sizeof *ctrl);

   INIT_WORK(&setup->work, &handle_setup);

   err = queue_work(ep->wq, &setup->work);
   if(err < 0) {
      log(WRN,"Unable to queue work handle_setup");
      goto fail2;
   }

   return 0;

 fail2:
   kfree(setup);
 fail1:
   return err;
}


static void
ubq_disconnect(struct usb_gadget *gadget)
{
}


static void
clean_endpoints(void)
{
   ep_t *ep, *tmp;
   // Free endpoints
   list_for_each_entry_safe(ep, tmp, &gadget_state.eplist, list) {
      free_gadget_endpoint((gadget_endpoint_t *)ep);
   }
}

int
ubq_register(void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
   return usb_gadget_probe_driver(&ubq_gadget,ubq_bind);
#else
   return usb_gadget_probe_driver(&ubq_gadget);
#endif
}


void
ubq_unregister(void)
{
   usb_gadget_unregister_driver(&ubq_gadget);
}

int
ubq_gadget_init(void)
{
   struct udp_opt_t options;

   trace;

   gadget_state.registered = 0;
   INIT_LIST_HEAD(&gadget_state.eplist);

   options.port = SERVER_PORT;
   options.connect = 0;

   gadget_state.com = com_init((void *)&options,gadget_recv_userland,"GADGET");
   if (!gadget_state.com) {
      return -ENOMEM;
   }

   log(INFO,"GADGET INIT OK");

   return 0;
}

void
ubq_gadget_exit(void)
{
   if(gadget_state.registered) {
      ubq_unregister();
   }
   clean_endpoints();
   com_close(gadget_state.com);

   log(INFO,"GADGET_EXIT OK");
}
