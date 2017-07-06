#include <linux/module.h>
#include <stdarg.h>
#include "debug.h"

void fn_log(const char *who,
            const unsigned int current_dbg_lvl,
            const unsigned int lvl,
            const char *function,
            const unsigned int line,
            const char *fmt, ...)
{
   if(lvl>=current_dbg_lvl) {
      va_list argp;
      printk(KERN_DEBUG "[%3u] %6s %15s(%04u): ", lvl, who, function, line);
      va_start(argp, fmt);
      vprintk(fmt, argp);
      va_end(argp);
   }
}


void fn_log_msg(const char *who,
                const unsigned int current_dbg_lvl,
                const unsigned int lvl,
                const msg_t *msg,
                const char *function,
                const unsigned int line,
                const char *fmt, ...)
{
   const int len = 16;

   if(lvl>=current_dbg_lvl) {
      char s[256];
      unsigned int i;
      va_list argp;

      printk( KERN_DEBUG "[%3u] %6s %15s(%04u): ", lvl, who, function, line);

      va_start(argp,fmt);
      vprintk(fmt, argp);
      va_end(argp);

      printk( KERN_DEBUG "[%3u] %6s %15s(%04u): %s\n", lvl, who, function, line, dump_msg(msg));

      if (!msg) {
         return;
      }

      if(msg_get_data_size(msg) == 0) {
         printk(KERN_DEBUG "[%3u] %6s %15s(%04u): Empty Message\n",lvl, who, function, line);
      } else {
         for(i=0;i<(msg_get_data_size(msg));i++) {
            snprintf(s+(3*(i%len)),4,"%02x ",msg_get_data(msg)[i]);
            if(i%len == len-1) {
               printk(KERN_DEBUG "[%3u] %6s %15s(%04u): %s\n",lvl, who, function, line, s);
            }
         }
         if(msg_get_data_size(msg)%len != 0) {
            printk(KERN_DEBUG "[%3u] %6s %15s(%04u): %s\n",lvl, who, function, line, s);
         }
      }
   }
}


void fn_log_buf(const char *who,
                const unsigned int current_dbg_lvl,
                const unsigned int lvl,
                const char *buf,
                const size_t sz,
                const char *function,
                const unsigned int line,
                const char *fmt, ...)
{
   const int len = 16;

   if(lvl>=current_dbg_lvl) {
      char s[256];
      unsigned int i;
      va_list argp;

      printk( KERN_DEBUG "[%3u] %6s %15s(%04u): ", lvl, who, function, line);

      va_start(argp,fmt);
      vprintk(fmt, argp);
      va_end(argp);

      for(i=0;i<sz;i++) {
         snprintf(s+(3*(i%len)),4,"%02x ",buf[i]);
         if(i%len == len-1) {
            printk(KERN_DEBUG "[%3u] %6s %15s(%04u): %s\n",lvl, who, function, line, s);
         }
      }
      if(sz%len != 0) {
         printk(KERN_DEBUG "[%3u] %6s %15s(%04u): %s\n",lvl, who, function, line, s);
      }
   }
}


void fn_assert(const char *who,
               const char *function,
               const unsigned int line,
               int success)
{
   if (!success) {
      printk(KERN_DEBUG "[%3u] %6s %15s(%04u): ASSERTION FAILED !",ASSERT, who, function, line);
   }
}
