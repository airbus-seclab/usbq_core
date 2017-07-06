#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>			/* USB stuff */
#include <linux/usb/ch9.h>			/* USB stuff */
#include <linux/usb/hcd.h>
#include <linux/workqueue.h>
#include <linux/inet.h> /* in4_pton */

#include "util.h"
#include "msg.h"
#include "driver.h"
#include "com.h"
#include "com_udp.h"
#include "debug.h"
#include "debug_usb.h"
#include "common.h"


#ifndef CONFIG_DRIVER_DEBUG_LEVEL
#define CONFIG_DRIVER_DEBUG_LEVEL DBG
#endif

#ifdef CONFIG_DRIVER_TRACE
#define trace _trace("DRIVER")
#else
#define trace {}
#endif

#define CONFIG_DRIVER_DEBUG

#ifdef CONFIG_DRIVER_DEBUG
#define log(lvl,fmt, ...) _log("DRIVER",CONFIG_DRIVER_DEBUG_LEVEL,lvl,fmt, ##__VA_ARGS__)
#define log_msg(lvl,pbuf,fmt, ...) _log_msg("DRIVER",CONFIG_DRIVER_DEBUG_LEVEL,lvl,pbuf,fmt, ##__VA_ARGS__)
#else
#define log(lvl,fmt, ...) {}
#define log_msg(lvl,buf,fmt, ...) {}
#endif

#define CONFIG_DRIVER_ASSERT

#ifdef CONFIG_DRIVER_ASSERT
#define assert(func) _assert("DRIVER",func)
#else
#define assert(func) {}
#endif

#define SERVER_IP               "192.168.64.1"
#define SERVER_PORT             64240

#define NB_ISOC_PKTS 1
#define ISOC_PKTS(wMaxPacketSize) ((le16_to_cpu((wMaxPacketSize))>>11)+1)
#define MAX_ISOC_PKT(wMaxPacketSize) (le16_to_cpu((wMaxPacketSize))&0x7ff)
#define MAX_ISOC_FRAME(wMaxPacketSize) (ISOC_PKTS((wMaxPacketSize))*MAX_ISOC_PKT((wMaxPacketSize)))


/*-------------------------------------------------------------------------*/

typedef ep_t driver_endpoint_t;

typedef struct driver_request_t {
   struct work_struct work;
   msg_t *msg;
   struct urb *urb;
   driver_endpoint_t *ep;
   struct list_head list;
} driver_request_t;

static struct driver_state_t {
   int init;
   struct usb_device                *dev;
   com_t *com;
   struct list_head eplist;
} driver_state;


static void driver_disconnect(struct usb_interface *interface);

static void free_driver_request(driver_request_t *req);

static void
clean_endpoints(void);
/*-------------------------------------------------------------------------*/

int ep_driver_send_usb(driver_endpoint_t *ep, msg_t *msg);

int ep_driver_recv_usb(driver_endpoint_t *ep, driver_request_t *req);
int ep_driver_recv_usb_ctrl(driver_endpoint_t *ep, driver_request_t *req);
int ep_driver_recv_usb_isoc(driver_endpoint_t *ep, driver_request_t *req);

int ep_driver_send_userland(driver_endpoint_t *ep, msg_t *msg);

int ep_driver_recv_userland_ctrl(driver_endpoint_t *ep, msg_t *msg);
int ep_driver_recv_userland(driver_endpoint_t *ep, msg_t *msg);

driver_request_t* ep_driver_fill_ctrl_request(driver_endpoint_t *ep, msg_t *msg);
driver_request_t* ep_driver_fill_isoc_request(driver_endpoint_t *ep, msg_t *msg);
driver_request_t* ep_driver_fill_int_request(driver_endpoint_t *ep, msg_t *msg);
driver_request_t* ep_driver_fill_bulk_request(driver_endpoint_t *ep, msg_t *msg);

void ep_free_driver_request(driver_endpoint_t *ep, driver_request_t *req);
int driver_recv_userland_management(msg_t *msg);

void ubq_disable_device(void);
int ubq_enable_device(void);

/*-------------------------------------------------------------------------*/

static cb_conf_t driver_cb_conf = {
   {  // CTRL CALLBACKS
      (send_usb_ft)ep_driver_send_usb,
      (recv_usb_ft)ep_driver_recv_usb_ctrl,
      (send_userland_ft)ep_driver_send_userland,
      (recv_userland_ft)ep_driver_recv_userland_ctrl,
      (fill_request_ft)ep_driver_fill_ctrl_request,
      (free_request_ft)ep_free_driver_request
   },
   {  // ISO CALLBACKS
      (send_usb_ft)ep_driver_send_usb,
      (recv_usb_ft)ep_driver_recv_usb_isoc,
      (send_userland_ft)ep_driver_send_userland,
      (recv_userland_ft)ep_driver_recv_userland,
      (fill_request_ft)ep_driver_fill_isoc_request,
      (free_request_ft)ep_free_driver_request
   },
   {  // BULK CALLBACKS
      (send_usb_ft)ep_driver_send_usb,
      (recv_usb_ft)ep_driver_recv_usb,
      (send_userland_ft)ep_driver_send_userland,
      (recv_userland_ft)ep_driver_recv_userland,
      (fill_request_ft)ep_driver_fill_bulk_request,
      (free_request_ft)ep_free_driver_request
   },
   {  // INTERRUPT CALLBACKS
      (send_usb_ft)ep_driver_send_usb,
      (recv_usb_ft)ep_driver_recv_usb,
      (send_userland_ft)ep_driver_send_userland,
      (recv_userland_ft)ep_driver_recv_userland,
      (fill_request_ft)ep_driver_fill_int_request,
      (free_request_ft)ep_free_driver_request
   },
};

void
dump_active_driver_endpoints(void)
{
   ep_t *ep;
   int i = 0;

   log(SPEC,"Active driver endpoints");
   list_for_each_entry(ep,&driver_state.eplist,list) {
      log(SPEC,"%u : %s",i,dump_usb_endpoint_descriptor(ep->desc));
      i++;
   }
}


/*-------------------------------------------------------------------------*/
int
urb_submit(driver_request_t *req)
{
   int err;
   epid_t *epid = &req->ep->epid;

   log(DBG,"Submit URB epid:[%s] desc:[%s] urb:[%s]",dump_endpoint_id(epid),dump_usb_endpoint_descriptor(req->ep->desc),dump_urb(req->urb));
   err = usb_submit_urb(req->urb,GFP_KERNEL);
   if(err<0) {
      log(ERR,"Unable to submit URB [%d] epid:[%s] urb:[%s] epdesc:[%s]",err,dump_endpoint_id(epid),dump_urb(req->urb),dump_usb_endpoint_descriptor(req->ep->desc));
      return err;
   }
   log(DBG,"URB successfully submitted epid:[%s]",dump_endpoint_id(epid));

   return 0;
}


/*-------------------------------------------------------------------------
 *
 * Driver endpoint management
 *
 -------------------------------------------------------------------------*/
driver_endpoint_t*
add_driver_ep0_endpoint(epdir_t epdir)
{
   int err;
   driver_endpoint_t *ep;

   ep = kmalloc(sizeof *ep, GFP_KERNEL);
   if (!ep) {
      goto fail1;
   }

   err = create_ep0_endpoint((ep_t *)ep, epdir, &driver_cb_conf);
   if (err < 0) {
      goto fail2;
   }

   list_add(&ep->list,&driver_state.eplist);

   return ep;

 fail2:
   kfree(ep);
 fail1:
   log(ERR,"Unable to allocate memory");
   return NULL;
}

driver_endpoint_t*
add_driver_endpoint(struct usb_endpoint_descriptor *epdesc)
{
   int err;
   driver_endpoint_t *ep;

   log(INFO,"Add driver endpoint epdesc:[%s]",dump_usb_endpoint_descriptor(epdesc));

   ep = kmalloc(sizeof *ep, GFP_KERNEL);
   if (!ep) {
      goto fail1;
   }

   err = create_endpoint((ep_t *)ep, epdesc, &driver_cb_conf);
   if (err < 0) {
      goto fail2;
   }

   list_add(&ep->list,&driver_state.eplist);

   return ep;

 fail2:
   kfree(ep);
 fail1:
   log(ERR,"Unable to allocate memory");
   return NULL;
}


/*
 * Cancel a request, will be freed insite completion
 */
void
cancel_driver_request(driver_request_t *req)
{
   log(DBG,"Cancel driver request epid:[%s]",dump_endpoint_id(&req->ep->epid));
   usb_kill_urb(req->urb);
}


void
free_driver_endpoint(driver_endpoint_t *ep)
{
   driver_request_t *req, *tmp;
   log(SPEC,"Free driver endpoint ep:[%s]",dump_endpoint_id(&ep->epid));

   // Remove from list
   list_del(&ep->list);

   // Cancel all requests, will be freed inside completion handler
   list_for_each_entry_safe(req, tmp, &ep->reqlist, list) {
      cancel_driver_request(req);
   }

   //usb_reset_endpoint(driver_state.dev,ep->desc->bEndpointAddress);
   free_endpoint((ep_t *)ep);
   kfree(ep);
}

driver_endpoint_t*
find_driver_endpoint(const epid_t *id)
{
   return (driver_endpoint_t *)find_endpoint(id, &driver_state.eplist);
}

/*-------------------------------------------------------------------------
 *
 * Driver request allocation management
 *
 -------------------------------------------------------------------------*/
static driver_request_t*
alloc_driver_request(driver_endpoint_t *ep, const size_t sz)
{
   driver_request_t *req;
   uint nb_packets = 0;
   size_t size = sz;

   log(DBG,"Allocate driver request ep:[%s] sz:%u",dump_endpoint_id(&ep->epid),sz);

   if (IS_ISOCHRONOUS(ep)) {
      nb_packets = ISOC_PKTS(sz);
      size = nb_packets * MAX_ISOC_PKT(sz);
      nb_packets = NB_ISOC_PKTS;
      size = sz;
   }

   req = kmalloc(sizeof *req, GFP_KERNEL);
   if (!req) {
      log(ERR,"Unable to allocate memory");
      goto fail1;
   }

   req->urb = usb_alloc_urb(nb_packets, GFP_ATOMIC);
   if (!req->urb) {
      log(ERR,"Unable to allocate URB");
      goto fail2;
   }

   req->msg = alloc_msg(size,DATA);
   if (!req->msg) {
      log(ERR,"Unable to allocate msg");
      goto fail3;
   }

   list_add(&req->list,&ep->reqlist);

   msg_set_epid(req->msg, &ep->epid);
   req->ep = ep;

   return req;
 fail3:
   usb_free_urb(req->urb);
 fail2:
   kfree(req);
 fail1:
   return NULL;
}

static void
free_driver_request(driver_request_t *req)
{
   log(DBG,"Free driver epid:[%s]",dump_endpoint_id(&req->ep->epid));
   list_del(&req->list);

   usb_free_urb(req->urb);

   free_msg(req->msg);
   kfree(req);
}


int
disable_driver_interface(struct usb_host_interface *interface)
{
   int ep;

   log(INFO,"Disable driver interface");

   for (ep=0; ep<interface->desc.bNumEndpoints; ep++) {
      struct usb_endpoint_descriptor *desc = &interface->endpoint[ep].desc;
      epid_t epid = {desc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK,
                     EP_TYPE_FROM_KERNEL(desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK),
                     EP_DIR_FROM_KERNEL(desc->bEndpointAddress & USB_ENDPOINT_DIR_MASK)};
      driver_endpoint_t *ep;

      ep = find_driver_endpoint(&epid);
      if (ep) {
         log(DBG,"Disabling endpoint epid:[%s]",dump_endpoint_id(&epid));
         free_driver_endpoint(ep);
      }
   }
   return 0;
}


int
enable_driver_interface(struct usb_host_interface *interface)
{
   int ep;

   log(INFO,"Enable interface [%u,%u]",interface->desc.bInterfaceNumber,interface->desc.bAlternateSetting);

   for (ep=0; ep<interface->desc.bNumEndpoints; ep++) {
      struct usb_endpoint_descriptor *epdesc = &interface->endpoint[ep].desc;
      driver_endpoint_t *epnew;

      epnew = add_driver_endpoint(epdesc);
      if(!epnew) {
         log(ERR,"Unable to add endpoint %u type:%s dir:%s",
             EP_TYPE_STR(EP_TYPE_FROM_KERNEL(epdesc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)),
             EP_DIR_STR(EP_DIR_FROM_KERNEL(epdesc->bEndpointAddress & USB_ENDPOINT_DIR_MASK)));
         return -ENOMEM;
      }

      // Wait for driver communication if IN
      if (IS_IN(epnew) && !IS_CTRL(epnew)) {
         int err;
         err = epnew->ops->send_usb(epnew, NULL);
         if (err<0) {
            log(ERR,"Unable to send USB for receiving IN [%d] epid:[%s]",err,dump_endpoint_id(&epnew->epid));
            return err;
         }
      }
   }

   return 0;
}

int
disable_active_driver_interface(void)
{
   struct usb_host_config *config = driver_state.dev->actconfig; // XXX TODO not necessary the first one
   int i;

   // In fact, we do not have information on the active interface
   // We disable everything
   for (i=0; i<config->desc.bNumInterfaces; i++) {
      struct usb_interface *iface = config->interface[i];
      int j;

      for (j=0;j<iface->num_altsetting;j++) {
         struct usb_host_interface *intf = &iface->altsetting[j];
         int err;

         err = disable_driver_interface(intf);
         if (err<0) {
            log(ERR,"Unable to enable interface [%d]",err);
            return err;
         }
      }
   }
   return 0;
}

int
enable_default_driver_interface(void)
{
   struct usb_host_config *config = driver_state.dev->actconfig; // XXX TODO not necessary the first one
   int i;

   for (i=0; i<config->desc.bNumInterfaces; i++) {
      struct usb_host_interface *iface = config->interface[i]->cur_altsetting;
      int err;

      err = enable_driver_interface(iface);
      if (err<0) {
         log(ERR,"Unable to enable interface [%d]",err);
         return err;
      }
   }
   return 0;
}

int
set_driver_interface(ushort ifnumber, ushort alternative)
{
   int err = 0;
   struct usb_interface *interface = usb_ifnum_to_if(driver_state.dev,ifnumber);
   struct usb_host_interface *old = interface->cur_altsetting;
   struct usb_host_interface *new = usb_altnum_to_altsetting(interface, alternative);

   log(INFO,"Set interface [%u,%u]",ifnumber,alternative);

   // Do the usb communication and structure modifications
   err = usb_set_interface(driver_state.dev,ifnumber,alternative);
   if (err < 0) {
      log(ERR,"Unable to set interface [%d]",err);
      return err;
   }

   err = disable_driver_interface(old);
   if (err != 0) {
      log(WRN,"Unable to disable interface");
      return err;
   }

   err = enable_driver_interface(new);
   if (err != 0) {
      log(WRN,"Unable to enable interface");
      return err;
   }

   return 0;
}


/* -------------------------------------------------------------------------
 *
 * Generic Callback
 *
 * -------------------------------------------------------------------------*/

uint
get_pipe(driver_endpoint_t *ep) {
   if (IS_CTRL(ep)) {
      return IS_IN(ep) ? usb_rcvctrlpipe(driver_state.dev,ep->epid.num)
                       : usb_sndctrlpipe(driver_state.dev,ep->epid.num);
   } else if (IS_INTERRUPT(ep)) {
      return IS_IN(ep) ? usb_rcvintpipe(driver_state.dev,ep->epid.num)
                       : usb_sndintpipe(driver_state.dev,ep->epid.num);
   } else if (IS_BULK(ep)) {
      return IS_IN(ep) ? usb_rcvbulkpipe(driver_state.dev,ep->epid.num)
                       : usb_sndbulkpipe(driver_state.dev,ep->epid.num);
   } else { // ISOC
      return IS_IN(ep) ? usb_rcvisocpipe(driver_state.dev,ep->epid.num)
                       : usb_rcvisocpipe(driver_state.dev,ep->epid.num);
   }
}

int
ep_clear_halt(driver_endpoint_t *ep)
{
   log(INFO,"Clear halt epid:[%s]",dump_endpoint_id(&ep->epid));
   return usb_clear_halt(driver_state.dev,get_pipe(ep));
}

/*
  Function executed by workqueue
*/
static void
recv(struct work_struct *data)
{
   driver_request_t *req = (driver_request_t *)data;
   driver_endpoint_t *ep = req->ep;
   struct urb *urb = req->urb;
   int err;

   log(DBG,"CALLBACK RECV USB epid:[%s] urb:[%s]",dump_endpoint_id(&ep->epid),dump_urb(urb));

   switch (urb->status) {
   case 0:			/* success */
      err = ep->ops->recv_usb(ep, req);
      if (err<0) {
         log(ERR,"Unable to recv USB [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
      }
      break;
   case -EPIPE:
      if (IS_IN(ep)) {
         if (!IS_CTRL(ep)) {
            err = ep_clear_halt(ep);
            if (err<0) {
               log(ERR,"Unable to clear halt [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
            }
            err = ep->ops->send_usb(ep,NULL);
            if (err<0) {
               log(ERR,"Unable to resubmit USB [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
            }
         } else { // Warn other part that there was an error
            msg_t *m = alloc_msg_ack(&ep->epid, -EPIPE, NULL, 0);
            if(!m) {
               log(ERR,"Unable to allocate memory");
               return;
            }
            err = ep->ops->send_userland(ep,m);
            free_msg(m);
            if (err < 0) {
               log(ERR,"Unable to send on userland [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
               return;
            }
         }
      }
   default:
      // If an ERROR is returned we have to sent a message to other part, otherwise a timeout
      // will happened. Unfortunately error is not forwarded to other part
      log(WRN,"URB Status [%d] epid:[%s] urb:[%s]", urb->status, dump_endpoint_id(&ep->epid),dump_urb(urb));
   }

   ep->ops->free_request(ep, req);
}

static void
driver_recv_usb(struct urb *urb)
{
   int err;
   driver_request_t *req = (driver_request_t *)urb->context;

   INIT_WORK(&req->work, &recv);

   err = queue_work(req->ep->wq, &req->work);
   if(err < 0) {
      log(WRN,"Unable to queue work");
   }
}


int
driver_recv_userland_usb(msg_t *msg)
{
   int err;
   driver_endpoint_t *ep;

   ep = find_driver_endpoint(&msg->epid);
   if (!ep) {
      log(ERR,"Unable to find endpoint epid:[%s]",dump_endpoint_id(&msg->epid));
      return -EINVAL;
   }

   log_msg(DBG,msg,"UDP -- RECV epid:[%s]",dump_endpoint_id(&ep->epid));

   err = ep->ops->recv_userland(ep, msg);
   if (err<0) {
      log(ERR,"Unable to recv from userland [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
      return err;
   }

   return 0;
}

int
driver_recv_userland(msg_t *msg)
{
   int err;
   if (IS_USB_MSG(msg)) {
      err = driver_recv_userland_usb(msg);
      if (err<0) {
         log(ERR,"Unable to recv userland usb [%d]",err);
         return err;
      }
   } else if (IS_MANAGEMENT_MSG(msg)) {
      err = driver_recv_userland_management(msg);
      if (err<0) {
         log(ERR,"Unable to recv userland management [%d]",err);
         return err;
      }
   } else {
      log(WRN,"Unknown userland message received [%d]",msg->type);
      return -EINVAL;
   }
   return 0;
}


/*-------------------------------------------------------------------------
 *
 * Driver endpoints Callback implementations
 *
 -------------------------------------------------------------------------*/
int
ep_driver_send_userland(driver_endpoint_t *ep, msg_t *msg)
{
   int err;
   log_msg(DBG,msg,"UDP -- SEND epid:[%s]",dump_endpoint_id(&ep->epid));
   err = send_userland(driver_state.com, msg);
   if (err<0) {
      log(ERR,"Unable to send userland [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
      return err;
   }
   return 0;
}

int
ep_driver_recv_userland_ctrl(driver_endpoint_t *ep, msg_t *msg)
{
   int ret;
   struct usb_ctrlrequest *ctrl = (struct usb_ctrlrequest *)msg->data;

   if (IS_SET_INTERFACE(ctrl)) {
      msg_t *m;

      log(DBG,"Receive SET interface");

      m = alloc_msg_ack(&ep->epid, 0, msg_get_data(msg), msg_get_data_size(msg));
      if(!m) {
         log(ERR,"Unable to allocate memory");
         return -ENOMEM;
      }

      // Will send the packet, so no send_usb
      ret = set_driver_interface(ctrl->wIndex,ctrl->wValue);
      if (ret < 0) {
         log(ERR,"Unable to set interface [%d]",ret);
         free_msg(m);
         return ret;
      }

      ret = ep->ops->send_userland(ep, m);
      free_msg(m);
      if (ret<0) {
         log(ERR,"Unable to send userland [%d] epid:[%s]",ret,dump_endpoint_id(&ep->epid));
         return ret;
      }

      return 0;
   }

   ret = ep->ops->send_usb(ep, msg);
   if (ret<0) {
      log(ERR,"Unable to send usb [%d] epid:[%s]",ret,dump_endpoint_id(&ep->epid));
      return ret;
   }

   return 0;
}

/*
 * Called when a userland message is received
 * Type: BULK, INTERRUPT, ISOC
 */
int
ep_driver_recv_userland(driver_endpoint_t *ep, msg_t *msg)
{
   int err;

   if(IS_USB_ACK(msg)) {
      // resubmit for IN
      assert(IS_IN(ep));

      err = ep->ops->send_usb(ep, NULL);
      if (err<0) {
         log(ERR,"Unable to send USB to get IN [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
         return err;
      }
   } else {
      // Send OUT data
      assert(IS_OUT(ep));

      err = ep->ops->send_usb(ep, msg);
      if (err<0) {
         log(ERR,"Unable to send USB OUT [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
         return err;
      }
   }
   return 0;
}


/*
 * Called to send an USB message
 * Type : CONTROL, BULK, INTERRUPT, ISOC
 */
int
ep_driver_send_usb(driver_endpoint_t *ep, msg_t *msg)
{
   int err;
   driver_request_t *req;

   req = ep->ops->fill_request(ep, msg);
   if (!req) {
      log(ERR,"Unable to fill request");
      return -EINVAL;
   }

   err = urb_submit(req);
   if (err < 0) {
      log(ERR,"Unable to submit URB : %d",err);
      ep->ops->free_request(ep, req);
      return err;
   }

   return 0;
}

/*
 * Called when an USB CTRL message is received
 * Type : CONTROL
 */
int
ep_driver_recv_usb_ctrl(driver_endpoint_t *ep, driver_request_t *req)
{
   int ret;
   epid_t *epid = &ep->epid;
   struct usb_ctrlrequest *ctrl = (struct usb_ctrlrequest *)req->urb->setup_packet;

   if (IS_IN(ep)) { // IN: We have received data
      msg_set_data_size(req->msg, req->urb->actual_length + msg_get_data_size(req->msg));
   } else { // OUT: We have just sent data...
   }

   log_msg(DBG,req->msg,"USB ++ RECV (%s) (rep to: %s)", dump_endpoint_id(epid), dump_usb_ctrlrequest(ctrl));

   if (epid->dir == IN) {
      ret = ep->ops->send_userland(ep, req->msg);
      if (ret<0) {
         log(ERR,"Unable to send userland [%d] epid:[%s]",ret,dump_endpoint_id(epid));
         return ret;
      }
   } else {
      if (epid->num == 0) {
         if (IS_SET_CONFIGURATION(ctrl)) {
            ret = disable_active_driver_interface();
            if (ret<0) {
               log(ERR,"Unable to disable active interface [%s]",ret);
               return ret;
            }

            ret = enable_default_driver_interface();
            if (ret<0) {
               log(ERR,"Unable to enable default interface [%s]",ret);
               return ret;
            }
         }
      }

      // CTRL OUT with data needs two submits to be received
      // The first one contains the request, and the other one the data
      // If there was data, so the 2nd submit has been done and implies that the request
      // was accepted
      // If there wasn't any data, we have to submit an ACK to the driver in order to accept the request
      /* if (le16_to_cpu(ctrl->wLength) == 0) { // OUT consumed, send ACK to userland */
      /*    msg_t *m = alloc_msg_ack(&ep->epid, req->urb->status, msg_get_data(req->msg), msg_get_data_size(req->msg)); */
      /*    if(!m) { */
      /*       log(ERR,"Unable to allocate memory"); */
      /*       return -ENOMEM; */
      /*    } */
      /*    ret = ep->ops->send_userland(ep, m); */
      /*    free_msg(m); */
      /*    if (ret<0) { */
      /*       log(ERR,"Unable to send ACK userland [%d]",ret); */
      /*       return ret; */
      /*    } */
      /* } else { */
      /*    // ACK not needed, the second submits implies that we have accepted the request */
      /* } */
   }

   return 0;
}


/*
 * Called when a USB message is received
 * Type : Bulk, Interrupt
 */
int
ep_driver_recv_usb(driver_endpoint_t *ep, driver_request_t *req)
{
   int err;

   msg_set_data_size(req->msg, req->urb->actual_length);

   log_msg(DBG,req->msg,"URB ++ RECV (%s) (status:%d) (actual_length:%u)", dump_endpoint_id(&req->ep->epid),req->urb->status, req->urb->actual_length);

   if(ep->epid.dir == IN) {
      err = ep->ops->send_userland(ep, req->msg);
      if (err<0) {
         log(ERR,"Unable to send userland [%d] epid:[%s]",err,dump_endpoint_id(&ep->epid));
         return err;
      }
   }

   return 0;
}


/*
 * Called when a USB message is received
 * Type : Isoc
 */
int
ep_driver_recv_usb_isoc(driver_endpoint_t *ep, driver_request_t *req)
{
   int err;
   struct urb *urb = req->urb;
   uint i=0;

   msg_set_data_size(req->msg, urb->actual_length);

   log_msg(DBG,req->msg,"URB ++ RECV (%s) (status:%d) (actual_length:%u) nb_packets:%u", dump_endpoint_id(&req->ep->epid),urb->status, urb->actual_length,urb->number_of_packets);

   for (i=0;i<urb->number_of_packets;i++) {
      log(DBG,"P%u: off=%d, len=%d, act=%d, status=%d (%08x)\n", i,
          urb->iso_frame_desc[i].offset,
          urb->iso_frame_desc[i].length,
          urb->iso_frame_desc[i].actual_length,
          urb->iso_frame_desc[i].status,
          le32_to_cpu(((int*)urb->transfer_buffer)[urb->iso_frame_desc[i].offset/4]));
   }

   if (ep->epid.dir == IN) {
      if (urb->actual_length > 0 && urb->status == 0) {
         err = ep->ops->send_userland(ep, req->msg);
         if (err < 0) {
            log(WRN,"Unable to send on userland");
            return err;
         }
      } else { // Resubmit
         err = ep->ops->send_usb(ep,NULL);
         if (err < 0) {
            log(WRN,"Unable to resubmit");
            return err;
         }
      }
   }

   return 0;
}


driver_request_t*
ep_driver_fill_ctrl_request(driver_endpoint_t *ep, msg_t *msg)
{
   driver_request_t *req;
   struct usb_ctrlrequest *ctrl = (struct usb_ctrlrequest *)msg->data;
   size_t sz;

   /*
     In case of CTRL IN
       - msg contains request, and we have to allocate memory for response too
       - size of response will be le16_to_cpu(ctrl->wLength) (at max)
     In case of CTRL OUT
       - msg contains request, and maybe data
       - There is no data response on it
       - len of data is le16_to_cpu(ctrl->wLength) (or msg_get_data_size(msg)-sizeof *ctrl)
    */

   if (IS_IN(ep)) {
      sz = le16_to_cpu(ctrl->wLength) + msg_get_data_size(msg);
   } else {
      sz = msg_get_data_size(msg);
   }

   req = alloc_driver_request(ep,sz);
   if (!req) {
      log(ERR,"Unable to allocate request epid:[%s]",dump_endpoint_id(&ep->epid));
      return NULL;
   }

   // Backup data after response
   msgcpy(req->msg, msg->data, msg_get_data_size(msg));

   usb_fill_control_urb(req->urb,
                        driver_state.dev,
                        get_pipe(ep),
                        (unsigned char *)req->msg->data,
                        req->msg->data + sizeof *ctrl,
                        le16_to_cpu(ctrl->wLength),
                        driver_recv_usb,
                        (void*)req);

   return req;
}


driver_request_t*
ep_driver_fill_int_request(driver_endpoint_t *ep, msg_t *msg)
{
   driver_request_t *req;
   size_t sz;

   if (!msg) {
      assert(IS_IN(ep));
      req = alloc_driver_request(ep, le16_to_cpu(ep->desc->wMaxPacketSize));
      if (!req) {
         log(ERR,"Unable to allocate request epid:[%s]",dump_endpoint_id(&ep->epid));
         return NULL;
      }
   } else {
      assert(IS_OUT(ep));
      req = alloc_driver_request(ep, msg_get_data_size(msg));
      if (!req) {
         log(ERR,"Unable to allocate request epid:[%s]",dump_endpoint_id(&ep->epid));
         return NULL;
      }
      // Backup data after response
      msgcpy(req->msg, msg->data, msg_get_data_size(msg));
   }

   if(ep->epid.dir == IN) {
      sz = req->msg->allocated_size;
   } else {
      sz = msg_get_data_size(req->msg);
   }

   usb_fill_int_urb(req->urb,
                    driver_state.dev,
                    get_pipe(ep),
                    req->msg->data,
                    sz,
                    driver_recv_usb,
                    (void*)req,
                    ep->desc->bInterval);

   return req;
}

driver_request_t*
ep_driver_fill_bulk_request(driver_endpoint_t *ep, msg_t *msg)
{
   driver_request_t *req;
   size_t sz;

   if (!msg) {
      //size_t sz = le16_to_cpu(ep->desc->wMaxPacketSize);
      size_t sz = MAX_BULK_SIZE;
      assert(IS_IN(ep));
      req = alloc_driver_request(ep, sz);
      if (!req) {
         log(ERR,"Unable to allocate request epid:[%s]",dump_endpoint_id(&ep->epid));
         return NULL;
      }
   } else {
      assert(IS_OUT(ep));
      req = alloc_driver_request(ep, msg_get_data_size(msg));
      if (!req) {
         log(ERR,"Unable to allocate request epid:[%s]",dump_endpoint_id(&ep->epid));
         return NULL;
      }
      // Backup data after response
      msgcpy(req->msg, msg->data, msg_get_data_size(msg));
   }

   if(ep->epid.dir == IN) {
      sz = req->msg->allocated_size;
   } else {
      sz = msg_get_data_size(req->msg);
   }


   usb_fill_bulk_urb(req->urb,
                     driver_state.dev,
                     get_pipe(ep),
                     req->msg->data,
                     sz,
                     driver_recv_usb,
                     (void*)req);

   return req;
}

driver_request_t*
ep_driver_fill_isoc_request(driver_endpoint_t *ep, msg_t *msg)
{
   driver_request_t *req;
   size_t sz;
   uint pipe;
   struct urb *urb;
   int i;

   if (!msg) {
      assert(IS_IN(ep));
      sz = MAX_ISOC_FRAME(ep->desc->wMaxPacketSize);
      /* if (le16_to_cpu(sz) == 5056) { */
      /*    sz = cpu_to_le16(512); */
      /* } */
      req = alloc_driver_request(ep, sz*NB_ISOC_PKTS);
      if (!req) {
         log(ERR,"Unable to allocate request epid:[%s]",dump_endpoint_id(&ep->epid));
         return NULL;
      }
      pipe = usb_rcvisocpipe(driver_state.dev,ep->epid.num);
   } else {
      assert(IS_OUT(ep));
      req = alloc_driver_request(ep, msg_get_data_size(msg));
      if (!req) {
         log(ERR,"Unable to allocate request epid:[%s]",dump_endpoint_id(&ep->epid));
         return NULL;
      }
      // Backup data after response
      msgcpy(req->msg, msg->data, msg_get_data_size(msg));

      pipe = usb_sndisocpipe(driver_state.dev,ep->epid.num);
      sz = msg_get_data_size(req->msg);
   }

   urb = req->urb;

   urb->dev = driver_state.dev;
   urb->pipe = pipe;
   urb->transfer_flags = URB_ISO_ASAP;
   urb->transfer_buffer = req->msg->data;
   urb->transfer_buffer_length = MAX_ISOC_FRAME(sz)*NB_ISOC_PKTS;
   urb->complete = driver_recv_usb;
   urb->context = (void*)req;
   urb->start_frame = 0;

   switch(driver_state.dev->speed) {
   case USB_SPEED_LOW:
   case USB_SPEED_FULL:
      urb->interval = ep->desc->bInterval;
      break;
   default:
      urb->interval = 1 << (ep->desc->bInterval - 1);
   }
   urb->interval = ep->desc->bInterval;

   //   urb->number_of_packets = ISOC_PKTS(ep->desc->wMaxPacketSize);
   urb->number_of_packets = NB_ISOC_PKTS;
   for (i=0; i<urb->number_of_packets; i++) {
      urb->iso_frame_desc[i].offset = i*sz;
      urb->iso_frame_desc[i].length = sz;
   }

   return req;
}

void
ep_free_driver_request(driver_endpoint_t *ep, driver_request_t *req)
{
   free_driver_request(req);
}


/*
 * Build initialisation packet that will be send to gadget part
 * Include : speed, DeviceDescriptor, ConfigurationDescriptor,
 * InterfaceDescriptor and EndpointDescriptor
 */
static msg_t*
build_init_pkt(struct usb_interface *interface)
{
   size_t sz;
   struct usb_device *dev = interface_to_usbdev(interface);
   struct usb_device_descriptor *device_descriptor = &dev->descriptor;
   struct usb_host_config *config = dev->config;
   struct usb_config_descriptor *config_descriptor = &config->desc;
   msg_t *msg;
   uint i,j;

   // Compute total size of packet
   sz = sizeof(enum usb_device_speed);
   sz += sizeof(struct usb_device_descriptor);
   sz += sizeof(struct usb_config_descriptor);

   // For each function interface
   for (i=0; i<config_descriptor->bNumInterfaces; i++) {
      struct usb_interface *intf = config->interface[i];

      // For each interface setting
      for (j=0; j<intf->num_altsetting; j++) {
         sz += sizeof(struct usb_interface_descriptor);
         sz += intf->altsetting[j].desc.bNumEndpoints * USB_DT_ENDPOINT_SIZE;
      }
   }

   msg = alloc_msg_management(sz);
   if (!msg) {
      log(ERR,"Unable to allocate msg");
      return NULL;
   }
   msg->management_type = NEW_DEVICE;

   // Build Packet
   msgcpy(msg,(unsigned char *)&dev->speed,sizeof dev->speed);
   msgcpy(msg,(unsigned char *)device_descriptor,sizeof *device_descriptor);
   msgcpy(msg,(unsigned char *)config_descriptor,sizeof *config_descriptor);
   for (i=0; i<config_descriptor->bNumInterfaces; i++) {
      struct usb_interface *intf = config->interface[i];

      for (j=0; j<intf->num_altsetting; j++) {
         uint k;
         struct usb_host_interface *interface = &intf->altsetting[j];
         struct usb_interface_descriptor *idesc = &interface->desc;

         msgcpy(msg,(unsigned char *)idesc,idesc->bLength);
         for (k=0; k<idesc->bNumEndpoints; k++) {
            struct usb_endpoint_descriptor *edesc = &interface->endpoint[k].desc;

            msgcpy(msg,(unsigned char *)edesc,edesc->bLength);
         }
      }
   }

   log_msg(DBG,msg,"Init packet");

   return msg;
}


/*-------------------------------------------------------------------------*/

/* table of devices that work with this driver */
static struct usb_device_id driver_table [] = {
   { .driver_info = 64},
   {} /* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, driver_table);

static int
driver_probe(struct usb_interface *interface, const struct usb_device_id *id) {
   int err = -ENODEV;
   struct usb_device *dev = interface_to_usbdev(interface);
   driver_endpoint_t *epin, *epout;
   msg_t *msg;

   driver_state.dev = dev;

   log(SPEC,"SPEED: %u",dev->speed);

   if(driver_state.init) {
      return 0;
   }
   driver_state.init = 1;

   epin = add_driver_ep0_endpoint(IN);
   if(!epin) {
      log(ERR,"Unable to add driver endpoint");
      return -ENOMEM;
   }

   epout = add_driver_ep0_endpoint(OUT);
   if(!epout) {
      log(ERR,"Unable to add driver endpoint");
      /* FIXME: Free epin */
      return -ENOMEM;
   }

   msg = build_init_pkt(interface);
   if (!msg) {
      log(ERR,"Unable to build init msg");
      return -ENOMEM;
   }

   err = epin->ops->send_userland(epin, msg);
   free_msg(msg);
   if (err < 0) {
      log(ERR,"Unable to send to userland init pkt [%d]",err);
      return err;
   }

   return 0;
}

static struct usb_driver ubq_driver = {
   .name = "ubq_driver",
   .id_table = driver_table,
   .probe = driver_probe,
   .disconnect = driver_disconnect,
};

int
driver_recv_userland_management(msg_t *msg)
{
   log(DBG,"Management msg received: %u",msg->management_type);
   if (IS_RESET_MNG_MSG(msg)) {
      ubq_disable_device();
   } else if (IS_RELOAD_MNG_MSG(msg)) {
      int err;
      err = ubq_enable_device();
      if (err<0) {
         log(ERR,"Unable to enable device");
         return err;
      }
   } else {
      log(WRN,"Unknown userland mangement message received [%d]",msg->management_type);
      return -EINVAL;
   }
   return 0;
}

static void
clean_endpoints(void)
{
   ep_t *ep, *tmp;
   // Free endpoints
   list_for_each_entry_safe(ep, tmp, &driver_state.eplist, list) {
      free_driver_endpoint((driver_endpoint_t *)ep);
   }
}

static void
driver_disconnect(struct usb_interface *interface)
{
   msg_t *msg;

   if(driver_state.init == 0) {
      return;
   }

   log(INFO,"DRIVER DISCONNECT");

   // Specify to gadget that device has been disconnected
   msg = alloc_msg_management(0);
   if (!msg) {
      log(ERR,"Unable to allocate msg");
   } else {
      int err;
      msg->management_type = RESET;

      err = send_userland(driver_state.com, msg);
      free_msg(msg);
      if (err<0) {
         log(ERR,"Unable to send to userland [%d]",err);
      }
   }

   clean_endpoints();

   driver_state.init = 0;
}

/*
  Start communication with physical device
*/
int
ubq_enable_device(void)
{
   log(SPEC,"ENABLE DEVICE");
   return usb_register(&ubq_driver);
}

/*
  Stop communication with pysical device
*/
void
ubq_disable_device(void)
{
   log(SPEC,"DISABLE DEVICE");
   clean_endpoints();
   usb_deregister(&ubq_driver);
}


int
ubq_driver_init(void)
{
   int err;
   udp_opt_t options;
   trace;

   options.port = SERVER_PORT;
   in4_pton(SERVER_IP,strlen(SERVER_IP),(u8*)&options.addr,'\0',NULL);
   options.connect = 1;

   INIT_LIST_HEAD(&driver_state.eplist);

   driver_state.init = 0;

   driver_state.com = com_init((void *)&options,driver_recv_userland,"DRIVER");
   if (!driver_state.com) {
      log(ERR,"Unable to initialise communication");
      return -ENOMEM;
   }

   /* register this driver with the USB subsystem */
   err = ubq_enable_device();
   if (err) {
      log(ERR,"usb_register failed. Error number %d", err);
      com_close(driver_state.com);
      return err;
   }
   return 0;
}


int
ubq_driver_exit(void)
{
   ubq_disable_device();
   com_close(driver_state.com);
   log(INFO,"DRIVER_EXIT OK");
   return 0;
}
