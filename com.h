#ifndef __COMM_H
#define __COMM_H

#include <linux/kernel.h>
#include <linux/workqueue.h>

#include "msg.h"

#define MAX_SIZE_ID 64 // Because 64 is good

#ifndef CONFIG_COM_DEBUG_LEVEL
#define CONFIG_COM_DEBUG_LEVEL DBG
#endif

#define CONFIG_COM_DEBUG

typedef struct com_t {
   int (*send)(struct com_t *,msg_t *);
   int (*cb_recv)(msg_t *);
   msg_t *msg;
   struct task_struct *thread;
   char id[MAX_SIZE_ID];
   struct workqueue_struct *wq;
   void *state; // Specific data for choosen communication
} com_t;

#ifdef CONFIG_COM_DEBUG
#define com_log(id,lvl,fmt,args...) _log(id,CONFIG_COM_DEBUG_LEVEL,lvl,fmt, ## args)
#define com_log_msg(id,lvl,buf,fmt,args...) _log_msg(id,CONFIG_COM_DEBUG_LEVEL,lvl,buf,fmt, ## args)
#define slog(thestate,lvl,fmt,args...) _log((thestate)->com->id,CONFIG_COM_DEBUG_LEVEL,lvl,fmt, ## args)
#define slog_msg(thestate,lvl,buf,fmt,args...) _log_msg((thestate)->com->id,CONFIG_COM_DEBUG_LEVEL,lvl,buf,fmt, ## args)
#else
#define com_log(state,lvl,fmt,args...) {}
#define com_log_msg(state,lvl,buf,fmt,args...) {}
#define slog(thestate,lvl,fmt,args...) {}
#define slog(thestate,lvl,buf,fmt,args...) {}
#endif

typedef void* (*com_init_fn)(com_t *com, void *opt, void (cb_recv)(com_t*));
typedef void (*com_close_fn)(void *state);
typedef int (*com_send_fn)(void *,msg_t *);
typedef int (*com_recv_fn)(void *,msg_t *);


com_t* com_init(void *, int (cb_recv)(msg_t*), const char *name);
void com_close(com_t *);

#endif
