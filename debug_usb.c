#include <linux/version.h>

#include "debug_usb.h"
#include "common.h"


#define SIZE 1024
static char usb_ctrl_string[SIZE];
static char desc_string[SIZE];
static char ep_string[SIZE];
static char usb_req_string[SIZE];
static char urb_string[SIZE];

char * dump_usb_ctrlrequest(const struct usb_ctrlrequest *req)
{
   snprintf(usb_ctrl_string,SIZE,"CTRLREQUEST => bRequestType:0x%x bRequest:%u wValue:%u wIndex:%u wLength:%u",
            req->bRequestType,
            req->bRequest,
            le16_to_cpu(req->wValue),
            le16_to_cpu(req->wIndex),
            le16_to_cpu(req->wLength));
   return usb_ctrl_string;
}


char * dump_usb_ep(const struct usb_ep *ep)
{
   snprintf(ep_string,SIZE,"name:%s driver_packet:%p maxpacket:%hu maxpacket_limit:%hu addr:%02x max_streams:%u mult:%u", ep->name, ep->driver_data,ep->maxpacket,ep->maxpacket_limit,ep->address,ep->max_streams,ep->mult);
   return ep_string;
}

char * dump_usb_request(const struct usb_request *req)
{
   snprintf(usb_req_string,SIZE,"addr:%p buf:%p length:%u zero:%u complete:%p context:%p status:%d actual:%u",req,
            req->buf,
            req->length,
            req->zero,
            req->complete,
            req->context,
            req->status,
            req->actual);
   return usb_req_string;
}


char * dump_usb_endpoint_descriptor(const struct usb_endpoint_descriptor *d)
{
   if(d == NULL) {
      snprintf(desc_string,SIZE,"NULL");
   } else {
      snprintf(desc_string,SIZE,"number:%u type:%s dir:%s bEndpointAddress:0x%02x interval:%u wMaxPacketSize:%u",d->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK,
            EP_TYPE_STR(EP_TYPE_FROM_KERNEL(d->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)),
            EP_DIR_STR(EP_DIR_FROM_KERNEL(d->bEndpointAddress & USB_ENDPOINT_DIR_MASK)),
            d->bEndpointAddress,d->bInterval,le16_to_cpu(d->wMaxPacketSize));
   }
   return desc_string;
}


char * dump_urb(const struct urb *urb)
{
   snprintf(urb_string,SIZE,"urb:%p status:%d transfer_buffer:%p transfer_buffer_length:%u actual_length:%u setup_packet:%p dev:%p pipe:%u transfert_flags:%u stream_id:%u",
            urb, urb->status, urb->transfer_buffer, urb->transfer_buffer_length, urb->actual_length, urb->setup_packet, urb->dev, urb->pipe,urb->transfer_flags,urb->stream_id);
   return urb_string;
}
