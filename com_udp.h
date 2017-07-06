#ifndef __COMM_UDP_H
#define __COMM_UDP_H

#include <linux/in.h>
#include <net/sock.h>
#include "com.h"

typedef struct udp_opt_t {
   unsigned short port;
   __be32 addr;
   int connect;
} udp_opt_t;


typedef struct udp_state_t {
   struct sockaddr_in sockservaddr;
   struct sockaddr_in clientaddr;
   struct socket *udpsocket;
   void (*cb)(com_t *); // Called when a new message is coming
   com_t *com;
} udp_state_t;


/* API */
void* udp_com_init(com_t *com, void *opt, void (cb_recv)(com_t*));
void udp_com_close(void *state);
int udp_com_send(void *state, msg_t *msg);
int udp_com_recv(void *state, msg_t *msg);

#endif
