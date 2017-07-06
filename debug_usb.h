#ifndef __DEBUG_USB_H
#define __DEBUG_USB_H

#include <linux/device.h>
#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/kernel.h>
#include <linux/usb/gadget.h>

char * dump_usb_ctrlrequest(const struct usb_ctrlrequest *);
char * dump_usb_request(const struct usb_request *);
char * dump_usb_ep(const struct usb_ep *);
char * dump_usb_endpoint_descriptor(const struct usb_endpoint_descriptor *d);
char * dump_urb(const struct urb *urb);

#endif
