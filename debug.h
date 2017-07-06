#ifndef __GOGO_DEBUG_H
#define __GOGO_DEBUG_H

#include "msg.h"
#include "debug_usb.h"

#ifndef WHO
#define WHO ""
#endif

/*
 * Debugging
 */
#define DBG           	                0x0f
#define DBG1           	                0x0e
#define DBG2           	                0x0d
#define INFO           	                0x10
#define NOTICE        	                0x20
#define WRN        	                0x30
#define ERR        	                0x40
#define SPEC                            0xFE
#define ASSERT        	                0xFF

void fn_log(const char *who, const unsigned int current_dbg_lvl, const unsigned int lvl, const char *function, const unsigned int line, const char *fmt, ...);

void fn_log_msg(const char *who,
                const unsigned int current_dbg_lvl,
                const unsigned int lvl,
                const msg_t *msg,
                const char *function,
                const unsigned int line,
                const char *fmt, ...);

void fn_log_buf(const char *who,
                const unsigned int current_dbg_lvl,
                const unsigned int lvl,
                const char *buf,
                const size_t sz,
                const char *function,
                const unsigned int line,
                const char *fmt, ...);

void fn_assert(const char *who,
               const char *function,
               const unsigned int line,
               int success);

#define _trace(who) printk( KERN_DEBUG "[CALL] %6s %15s\n", who, __FUNCTION__)

#define _log(who,_current,lvl,fmt, ...) fn_log(who,_current,lvl,__FUNCTION__,__LINE__,fmt, ##__VA_ARGS__)

#define _log_msg(who,_current,lvl,_buf,fmt, ...) fn_log_msg(who,_current,lvl,_buf,__FUNCTION__,__LINE__,fmt, ##__VA_ARGS__)

#define _log_buf(who,_current,lvl,_buf,_sz,fmt, ...) fn_log_buf(who,_current,lvl,_buf,_sz,__FUNCTION__,__LINE__,fmt, ##__VA_ARGS__)

#define _assert(who,func) fn_assert(who,__FUNCTION__,__LINE__,func)


#endif
