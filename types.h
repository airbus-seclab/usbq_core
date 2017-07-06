#ifndef __USBMITM_TYPES_H

#define __USBMITM_TYPES_H

#include <linux/usb/ch9.h>
#include <linux/workqueue.h>
#include "msg.h"

#define MAX_INTERFACE_CONFIGURATION 64
#define MAX_ENDPOINT_INTERFACE 8

struct msg_t;

typedef enum eptype_t {
   CTRL = USB_ENDPOINT_XFER_CONTROL,
   INTERRUPT = USB_ENDPOINT_XFER_INT,
   ISOC = USB_ENDPOINT_XFER_ISOC,
   BULK = USB_ENDPOINT_XFER_BULK,
   UNKNOWN = USB_ENDPOINT_MAX_ADJUSTABLE
} eptype_t;


typedef enum epdir_t {
   IN = 0,
   OUT = 1
} epdir_t;

typedef unsigned short epnum_t;

typedef struct epid_t {
   epnum_t num;
   eptype_t type;
   epdir_t dir;
} __attribute__((packed)) epid_t;

/*
  Operation done by an endpoint
*/
typedef int (*send_usb_ft)(void*, struct msg_t*);
typedef int (*recv_usb_ft)(void*, void*);
typedef int (*send_userland_ft)(void*, struct msg_t*);
typedef int (*recv_userland_ft)(void*, struct msg_t*);
typedef void* (*fill_request_ft)(void*, struct msg_t*);
typedef void (*free_request_ft)(void*, void *);

typedef struct ep_ops_t {
   send_usb_ft send_usb;
   recv_usb_ft recv_usb;
   send_userland_ft send_userland;
   recv_userland_ft recv_userland;
   fill_request_ft fill_request;
   free_request_ft free_request;
} ep_ops_t;


/*
 * Endpoint representation
 */
typedef struct ep_t {
   epid_t epid;
   ep_ops_t *ops;
   const struct usb_endpoint_descriptor *desc;
   struct list_head list;
   struct list_head reqlist;
   struct workqueue_struct *wq;
   char *name;
} ep_t;


typedef struct interface_desc_t {
   struct usb_interface_descriptor desc;
   struct usb_endpoint_descriptor endpoints[MAX_ENDPOINT_INTERFACE];
   int active;
} interface_desc_t;


/*
 * Represent a device identity
 */
typedef struct identity_t {
   enum usb_device_speed speed;
   struct usb_device_descriptor device;
   struct usb_config_descriptor conf;
   uint nb_int;
   interface_desc_t interfaces[MAX_INTERFACE_CONFIGURATION];
} identity_t;

typedef ep_ops_t cb_conf_t[4];

#endif
