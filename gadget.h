#ifndef __UBQ_GADGET_H
#define __UBQ_GADGET_H

#define SERVER_PORT 64241

// Carefull ep shall remain the first attribute
typedef struct gadget_endpoint_t {
   ep_t;
   struct usb_ep *usb_ep;
} gadget_endpoint_t;

typedef struct gadget_request_t {
   struct work_struct work;
   msg_t *msg;
   struct usb_request *req;
   gadget_endpoint_t *ep;
   struct list_head list;
} gadget_request_t;

typedef struct setup_request_t {
   struct work_struct work;
   struct usb_ctrlrequest *ctrl;
} setup_request_t;

static struct gadget_state {
   struct usb_gadget  *gadget;
   com_t *com;
   struct usb_device_descriptor descriptor; // Current device descriptor
   int registered;
   struct list_head eplist;
   identity_t identity;
} gadget_state;



static void free_gadget_request(gadget_request_t *req);
static void gadget_recv_usb(struct usb_ep *endpoint, struct usb_request *req);

/*-------------------------------------------------------------------------*/

int ep_gadget_send_usb(gadget_endpoint_t *ep, msg_t *msg);

int ep_gadget_recv_usb_ctrl(gadget_endpoint_t *ep, gadget_request_t *req);
int ep_gadget_recv_usb(gadget_endpoint_t *ep, gadget_request_t *req);

int ep_gadget_send_userland(gadget_endpoint_t *ep, msg_t *msg);

int ep_gadget_recv_userland_ctrl(gadget_endpoint_t *ep, msg_t *msg);
int ep_gadget_recv_userland(gadget_endpoint_t *ep, msg_t *msg);

gadget_request_t* ep_fill_ctrl_request(gadget_endpoint_t *ep, msg_t *msg);
gadget_request_t* ep_fill_request(gadget_endpoint_t *ep, msg_t *msg);

void ep_free_gadget_request(gadget_endpoint_t *ep, gadget_request_t *req);



static void clean_endpoints(void);

// Interface management
int disable_active_interface(void);
int enable_default_interface(void);
int disable_interface(interface_desc_t *);
int enable_interface(interface_desc_t *);

// Gadget specific
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
int ubq_bind(struct usb_gadget *);
#else
int ubq_bind(struct usb_gadget *, struct usb_gadget_driver *);
#endif
static void ubq_unbind(struct usb_gadget *);
static void ubq_disconnect(struct usb_gadget *);
static int ubq_setup(struct usb_gadget *,const struct usb_ctrlrequest *);

// Module specific
int gadget_init(void);
int ubq_register(void);
void ubq_unregister(void);
void gadget_exit(void);


static cb_conf_t gadget_cb_conf = {
   {  // CTRL CALLBACKS
      (send_usb_ft)ep_gadget_send_usb,
      (recv_usb_ft)ep_gadget_recv_usb_ctrl,
      (send_userland_ft)ep_gadget_send_userland,
      (recv_userland_ft)ep_gadget_recv_userland_ctrl,
      (fill_request_ft)ep_fill_ctrl_request,
      (free_request_ft)ep_free_gadget_request
   },
   {  // ISOC CALLBACKS
      (send_usb_ft)ep_gadget_send_usb,
      (recv_usb_ft)ep_gadget_recv_usb,
      (send_userland_ft)ep_gadget_send_userland,
      (recv_userland_ft)ep_gadget_recv_userland,
      (fill_request_ft)ep_fill_ctrl_request,
      (free_request_ft)ep_free_gadget_request
   },
   {  // BULK CALLBACKS
      (send_usb_ft)ep_gadget_send_usb,
      (recv_usb_ft)ep_gadget_recv_usb,
      (send_userland_ft)ep_gadget_send_userland,
      (recv_userland_ft)ep_gadget_recv_userland,
      (fill_request_ft)ep_fill_request,
      (free_request_ft)ep_free_gadget_request
   },
   {  // INTERRUPT CALLBACKS
      (send_usb_ft)ep_gadget_send_usb,
      (recv_usb_ft)ep_gadget_recv_usb,
      (send_userland_ft)ep_gadget_send_userland,
      (recv_userland_ft)ep_gadget_recv_userland,
      (fill_request_ft)ep_fill_request,
      (free_request_ft)ep_free_gadget_request
   }
};

#endif
