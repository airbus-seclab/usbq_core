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

#define MAX_SIZE_MSG 16000


typedef struct internal_com_t {
   com_init_fn init;
   com_close_fn close;
   com_send_fn send;
   com_recv_fn recv;
} internal_com_t;

/* Default operations */
static internal_com_t conf_com = {
   (com_init_fn)udp_com_init,
   (com_close_fn)udp_com_close,
   (com_send_fn)udp_com_send,
   (com_recv_fn)udp_com_recv
};

typedef struct {
   struct work_struct work;
   void *com;
} recv_wq_t;


/*
  Function executed by workqueue
*/
static void
com_recv(struct work_struct *data)
{
   recv_wq_t *work = (recv_wq_t *)data;
   com_t *com = (com_t*)work->com;
   ssize_t sz;

   sz = conf_com.recv(com->state,com->msg);
   if (sz < 0) {
      com_log(com->id,ERR,"Unable to receive data from userland (err:%d)",sz);
      return;
   } else if (!check_msg(com->msg)) {
      com_log(com->id,ERR,"Invalid structure of message");
      return;
   } else if (sz == 0) {
      return;
   } else {
      com->cb_recv(com->msg);
   }
}


/*
  Called when a message arrives
*/
static void
wq_recv(com_t *com)
{
   recv_wq_t *work;

   work = kmalloc(sizeof *work, GFP_KERNEL);
   if(!work) {
      com_log("COM",ERR,"Unable to allocate memory");
      return;
   }
   work->com = com;
   INIT_WORK(&work->work, &com_recv);

   queue_work(com->wq, (struct work_struct *)work);
}


int
com_send(com_t *com,msg_t *msg)
{
   if (!check_msg(msg)) {
      com_log(com->id,ERR,"Invalid structure of message, not sending");
      return -EINVAL;
   }
   return conf_com.send(com->state,msg);
}



/*
  Init communication
 */
com_t*
com_init(void *opt, int (cb_recv)(msg_t*), const char *name)
{
   com_t *com;

   com = kmalloc(sizeof *com, GFP_KERNEL);
   if (!com) {
      goto fail1;
   }

   com->msg = alloc_msg(MAX_SIZE_MSG,DATA);
   if (!com->msg) {
      goto fail2;
   }

   com->cb_recv = cb_recv;

   com->state = conf_com.init(com,opt,wq_recv);
   if (com->state < 0) {
      goto fail3;
   }

   strncpy(com->id,name,MAX_SIZE_ID);
   com->send = com_send;

   com->wq = create_workqueue("recv");

   return com;

 fail3:
   free_msg(com->msg);
 fail2:
   kfree(com);
 fail1:
   com_log(com->id,ERR,"Unable to allocate memory");
   return NULL;
}


void
com_close(com_t *com)
{
   flush_workqueue(com->wq);
   destroy_workqueue(com->wq);
   conf_com.close(com->state);
   free_msg(com->msg);
   kfree(com);
}
