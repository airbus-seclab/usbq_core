#include <linux/module.h>
#include <linux/init.h>
#include <linux/in.h>
#include <net/sock.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/inet.h>
#include <linux/uio.h>
#include <linux/version.h>

#include "msg.h"
#include "com.h"
#include "com_udp.h"
#include "debug.h"



#if LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0)
static void
udp_data_ready(struct sock *socket, int bytes)
#else
static void
udp_data_ready(struct sock *socket)
#endif
{
   com_t *com = (com_t *)socket->sk_user_data;
   udp_state_t *state = (udp_state_t *)com->state;
   slog(state,INFO,"UDP MSG Ready %p %p",state,state->cb);
   state->cb(socket->sk_user_data);
}

static int
udp_init(udp_state_t *state, const udp_opt_t *opt)
{
   int servererror;
   struct sockaddr_in *sockservaddr;

   if(sock_create(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &state->udpsocket) < 0) {
      slog(state,ERR,"Unable to create udpsocket");
      return -EIO;
   }

   state->udpsocket->sk->sk_data_ready = udp_data_ready;
   state->udpsocket->sk->sk_user_data = state->com;

   if(opt->connect == 0) {
      sockservaddr = &state->sockservaddr;
      sockservaddr->sin_family = AF_INET;
      sockservaddr->sin_addr.s_addr = INADDR_ANY;
      sockservaddr->sin_port = htons(opt->port);
      servererror = state->udpsocket->ops->bind(state->udpsocket, (struct sockaddr *) sockservaddr, sizeof(*sockservaddr));
      if (servererror) {
         sock_release(state->udpsocket);
         return -EIO;
      }
   } else {
      struct sockaddr_in *c = &state->clientaddr;
      c->sin_port = htons(opt->port);
      c->sin_addr.s_addr = opt->addr;
      c->sin_family = AF_INET;
   }

   return 0;
}

static void
udp_close(udp_state_t *state)
{
   if(state->udpsocket != NULL) {
      sock_release(state->udpsocket);
      state->udpsocket = NULL;
   }
}

void*
udp_com_init(com_t *com, void *opt, void (cb_recv)(com_t*))
{
   int err;
   udp_state_t *state;

   state = kmalloc(sizeof *state, GFP_KERNEL);
   if (!state) {
      com_log("UDP",ERR,"Unable to allocate memory");
      return NULL;
   }

   state->cb = cb_recv;
   state->com = com;

   err = udp_init(state,(udp_opt_t*)opt);
   if (err < 0) {
      kfree(state);
      return NULL;
   }

   return (void *)state;
}


void
udp_com_close(void *state)
{
   udp_state_t *s = (udp_state_t *)state;
   udp_close(s);
   kfree(state);
}


static ssize_t
raw_send(udp_state_t *state, unsigned char *buf, size_t len)
{
   struct msghdr msg;
   struct iovec iov;
   mm_segment_t oldfs;
   ssize_t size = 0;
   struct socket *sock = state->udpsocket;
   struct sockaddr_in *addr = &state->clientaddr;

   if (sock->sk == NULL) {
      slog(state,ERR,"sk NULL");
      return 0;
   }

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)
   iov.iov_base = buf;
   iov.iov_len = len;
   msg.msg_iov = &iov;
   msg.msg_iovlen = 1;
#else
   iov.iov_base = buf;
   iov.iov_len = len;
   iov_iter_init(&msg.msg_iter, WRITE, &iov, 1, len);
#endif

   msg.msg_flags = 0;
   msg.msg_name = addr;
   msg.msg_namelen  = sizeof(struct sockaddr_in);
   msg.msg_control = NULL;
   msg.msg_controllen = 0;
   msg.msg_control = NULL;

   oldfs = get_fs();
   set_fs(KERNEL_DS);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,1,0)
   size = sock_sendmsg(sock,&msg,len);
#else
   size = sock_sendmsg(sock,&msg);
#endif
   set_fs(oldfs);

   return size;
}


static int
udp_send(udp_state_t *state, msg_t *msg)
{
   ssize_t sz;
   size_t len = msg->size;
   char *buf = (char *)&msg->size;

   for(sz=0; sz!=msg->size;) {
      ssize_t s;

      slog(state,DBG,"UDP sending buf:%p sz_sent:%u still:%u",buf+sz,sz,len);
      s = raw_send(state, buf+sz, len);
      if (s < 0) {
         slog(state,ERR,"Error during UDP send : %d", s);
         return s;
      } else if (s == 0) {
         slog(state,WRN,"UDP send prematured end of packet");
         break;
      } else {
         sz += s;
         len -= s;
         slog(state,DBG,"UDP sent %u still:%u",s,len);
      }
   }
   return sz;
}


int
udp_com_send(void *state, msg_t *msg)
{
   return udp_send((udp_state_t*)state,msg);
}


#if LINUX_VERSION_CODE > KERNEL_VERSION(3,19,0)
#include <linux/pagemap.h>

int
import_single_range(int rw, void __user *buf, size_t len,
                    struct iovec *iov, struct iov_iter *i)
{
   if (len > MAX_RW_COUNT)
      len = MAX_RW_COUNT;

   // Type argument dropped in 5.0
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0)
   if (unlikely(!access_ok(!rw, buf, len)))
#else
   if (unlikely(!access_ok(buf, len)))
#endif
      return -EFAULT;

   iov->iov_base = buf;
   iov->iov_len = len;
   iov_iter_init(i, rw, iov, 1, len);
   return 0;
 }
#endif

static ssize_t
raw_recv(udp_state_t *state, unsigned char *addr, size_t len)
{
   struct msghdr msg;
   struct iovec iov;
   mm_segment_t oldfs;
   ssize_t size = 0;
   struct socket *sock = state->udpsocket;

   if (sock->sk == NULL) {
      slog(state,ERR,"Socket used is null");
      return 0;
   }

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)
   iov.iov_base = addr;
   iov.iov_len = len;
   msg.msg_iov = &iov;
   msg.msg_iovlen = 1;
#else
   {
      int err;
      err = import_single_range(READ,addr,len,&iov,&msg.msg_iter);
      if (unlikely(err)) {
         slog(state,ERR,"Error import_single_range ret %d",err);
         return err;
      }
      len = iov_iter_count(&msg.msg_iter);
   }
#endif

   msg.msg_flags = MSG_DONTWAIT;
   msg.msg_name = &state->clientaddr;
   msg.msg_namelen  = sizeof(struct sockaddr_in);
   msg.msg_control = NULL;
   msg.msg_controllen = 0;
   msg.msg_control = NULL;


   slog(state,DBG,"READING DATA");
   oldfs = get_fs();
   set_fs(KERNEL_DS);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,7,1)
   size = sock_recvmsg(sock, &msg, len, msg.msg_flags);
#else
   size = sock_recvmsg(sock, &msg, msg.msg_flags);
#endif
   set_fs(oldfs);

   state->clientaddr = *(struct sockaddr_in *)msg.msg_name;

   return size;
}


static ssize_t
udp_recv(udp_state_t *state, msg_t *msg)
{
   ssize_t sz;
   size_t len;
   char *buf = (char *)&msg->size;

   slog(state,DBG,"udp_recv");

   sz = raw_recv(state, buf, msg->allocated_size);
   if (sz < 0) {
      slog(state,ERR,"Bad UDP recv %d",sz);
      return -EINVAL;
   } else if (sz < sizeof msg->size) {
      slog(state,ERR,"Unable to read msg size [%u bytes read]",sz);
      return -EINVAL;
   } else if (msg->size < sz) {
      slog(state,ERR,"Wrong size msg should be max %u but read %u bytes",msg->size,sz);
      return -EINVAL;
   } else if (sz == 0) {
      return 0;
   }

   len = msg->size;

   slog(state,DBG,"UDP Read size : %u total:%u buffer_sz:%u",sz,len,msg->allocated_size);

   if (len > msg->allocated_size) {
      slog(state,ERR,"UDP Buffer too small in order to received data (sz_buffer:%u,total:%u)",msg->allocated_size,len);
      return -EINVAL;
   }

   for(; sz!=len;) {
      ssize_t s;

      slog(state,DBG,"UDP reading buf:%p sz_read:%u still:%u", buf+sz,sz,len-sz);
      s = raw_recv(state, buf+sz, len-sz);
      if (s < 0) {
         slog(state,ERR,"Error during UDP received : %d", s);
         return s;
      } else if (s == 0) {
         slog(state,DBG,"No data");
      } else {
         sz += s;
         slog(state,DBG,"UDP read %u still:%u",s,len-sz);
      }
   }

   return sz;
}

int
udp_com_recv(void *state, msg_t *msg)
{
   return udp_recv((udp_state_t*)state,msg);
}
