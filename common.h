#ifndef __COMMON_H
#define __COMMON_H

#include <linux/wait.h>
#include <linux/usb/ch9.h>
#include <linux/usb.h>
#include <linux/usb/gadget.h>
#include "com.h"
#include "msg.h"

#define MAX_ENDPOINT 256
#define MAX_SIZE_CTRL_DATA 256
#define MAX_BULK_SIZE 2048

#define IS_TYPE_STANDARD(r) (((r)->bRequestType&USB_TYPE_MASK) == USB_TYPE_STANDARD)
#define IS_GET_DESCRIPTOR(r) (IS_TYPE_STANDARD((r)) && (r)->bRequest == USB_REQ_GET_DESCRIPTOR)


#define IS_SET_CONFIGURATION(r) (IS_TYPE_STANDARD((r)) && (r)->bRequest == USB_REQ_SET_CONFIGURATION)
#define IS_GET_DESC_CONFIGURATION(r) (IS_GET_DESCRIPTOR((r)) && (le16_to_cpu((r)->wValue) >> 8) == USB_DT_CONFIG)

#define IS_SET_INTERFACE(r) (IS_TYPE_STANDARD((r)) && (r)->bRequest == USB_REQ_SET_INTERFACE)

#define EP_TYPE_FROM_KERNEL(t) ((t)==USB_ENDPOINT_XFER_INT?INTERRUPT:((t)==USB_ENDPOINT_XFER_CONTROL?CTRL:((t)==USB_ENDPOINT_XFER_ISOC?ISOC:((t)==USB_ENDPOINT_XFER_BULK?BULK:UNKNOWN))))
#define EP_DIR_FROM_KERNEL(d) ((d)==USB_DIR_IN?IN:OUT)


#define EP_DIR_STR(dir) ((dir==IN)?"IN":"OUT")
#define EP_TYPE_STR(t) ((t)==CTRL?"CTRL":((t)==INTERRUPT?"INTERRUPT":((t)==ISOC?"ISOC":((t)==BULK?"BULK":"UNKOWN"))))

#define IS_OUT(e) ((((ep_t*)(e))->epid).dir == OUT)
#define IS_IN(e) ((((ep_t*)(e))->epid).dir == IN)

#define IS_CTRL(e) ((((ep_t*)(e))->epid).type == CTRL)
#define IS_INTERRUPT(e) ((((ep_t*)(e))->epid).type == INTERRUPT)
#define IS_BULK(e) ((((ep_t*)(e))->epid).type == BULK)
#define IS_ISOCHRONOUS(e) ((((ep_t*)(e))->epid).type == ISOC)


// Endpoint management
int create_ep0_endpoint(ep_t *ep, const epdir_t epdir, cb_conf_t *callbacks);
int create_endpoint(ep_t *ep, const struct usb_endpoint_descriptor *desc, cb_conf_t *callbacks);
void free_endpoint(ep_t *ep);
ep_t* find_endpoint(const epid_t *id, struct list_head *list);
char* dump_endpoint_id(const epid_t *ep);

// Userland communication
int send_userland(com_t *com, msg_t *msg);

#endif
