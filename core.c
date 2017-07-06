/* #define VERBOSE_DEBUG */

#include <linux/device.h>
#include <linux/module.h>

int ubq_gadget_init(void);
void ubq_gadget_exit(void);

int ubq_driver_init(void);
void ubq_driver_exit(void);

static int __init ubq_core_init(void)
{
   int retval;
   retval = ubq_gadget_init();
   if(retval < 0) return retval;
   retval = ubq_driver_init();
   if(retval < 0) {
      ubq_gadget_exit();
      return retval;
   }
   return retval;
}

static void __exit ubq_core_exit(void)
{
   ubq_gadget_exit();
   ubq_driver_exit();
}

module_init(ubq_core_init);
module_exit(ubq_core_exit);

MODULE_AUTHOR("Benoit Camredon");
MODULE_LICENSE("GPL");
