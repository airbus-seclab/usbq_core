#ifndef __MSG_H

#define __MSG_H

#include "types.h"

#define MSG_FROM_BUF(b) ((msg_t *)((b)-(sizeof(size_t)+sizeof(size_t)+sizeof(epid_t))))

typedef enum msg_type_t {
   DATA,
   ACK,
   MANAGEMENT
} msg_type_t;

#define IS_MANAGEMENT_MSG(m) (((m)->type) == MANAGEMENT)
#define IS_USB_MSG(m) (((m)->type) == DATA || ((m)->type) == ACK)
#define IS_USB_DATA(m) (((m)->type) == DATA)
#define IS_USB_ACK(m) (((m)->type) == ACK)

typedef enum msg_management_type_t {
   RESET,
   NEW_DEVICE,
   RELOAD,
} msg_management_type_t;

#define IS_RESET_MNG_MSG(m) (IS_MANAGEMENT_MSG(m) && ((m)->management_type) == RESET)
#define IS_NEW_DEVICE_MNG_MSG(m) (IS_MANAGEMENT_MSG(m) && ((m)->management_type) == NEW_DEVICE)
#define IS_RELOAD_MNG_MSG(m) (IS_MANAGEMENT_MSG(m) && ((m)->management_type) == RELOAD)

typedef struct msg_t {
   size_t allocated_size;
   size_t size;
   msg_type_t type;
   union {
      // MANAGEMENT
      struct {
         msg_management_type_t management_type;
         char management_data[0];
      } __attribute__((packed));
      // DATA/ACK
      struct {
         epid_t epid;
         union {
            // ACK
            struct {
               int status;
               char ack_data[0];
            } __attribute__((packed));
            // DATA
            struct {
               char data[0];
            };
         } __attribute__((packed));
      } __attribute__((packed));
   } __attribute__((packed));
} __attribute__((packed)) msg_t;

size_t msg_get_data_size(const msg_t *msg);
void msg_set_data_size(msg_t *msg, size_t size);
char *dump_msg(const msg_t*);
msg_t* msgcpy(msg_t*,void*,size_t);
void msg_set_epid(msg_t *,epid_t *);
void msg_set_id(msg_t *msg, const epnum_t num, const eptype_t type, const epdir_t dir);

// Allocate an ACK message for an endpoint
msg_t* alloc_msg_ack(epid_t *, int, char *, size_t);
msg_t* alloc_msg_management(size_t);
msg_t* alloc_msg_data(size_t);
msg_t* alloc_msg(size_t,int);
void free_msg(msg_t *m);
char *msg_get_data(const msg_t* msg);

int check_msg(msg_t *msg);

#endif
