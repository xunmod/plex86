/*
 * $Id$
 */

/*
 * This driver was derived from the sample "spull" driver
 * from the book "Linux Device Drivers" by Alessandro Rubini and
 * Jonathan Corbet, published by O'Reilly & Associates.
 */


#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/hdreg.h>

#include <asm/system.h>


#define MAJOR_NR haldisk_major /* force definitions on in blk.h */
int haldisk_major; /* must be declared before including blk.h */

#define HALDISK_SHIFT 4                         /* max 16 partitions  */
#define HALDISK_MAXNRDEV 4                      /* max 4 device units */
#define DEVICE_NR(device) (MINOR(device)>>HALDISK_SHIFT)
#define DEVICE_NAME "pd"                      /* name for messaging */
#define DEVICE_INTR haldisk_intrptr         /* pointer to the bottom half */
#define DEVICE_NO_RANDOM                  /* no entropy to contribute */
#define DEVICE_REQUEST haldisk_request
#define DEVICE_OFF(d) /* do-nothing */

#include <linux/blk.h>
#include <linux/ioctl.h>

#ifndef LINUX_VERSION_CODE
#  include <linux/version.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
#  error "Your guest kernel is not 2.4 or greater."
#endif

#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define   PUT_USER   put_user
#define __PUT_USER __put_user


/*
 * Macros to help debugging
 */

#undef PDEBUG             /* undef it, just in case */
#ifdef HALDISK_DEBUG
#  define PDEBUG(fmt, args...) printk( KERN_DEBUG "haldisk: " fmt, ## args)
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

#undef PDEBUGG
#define PDEBUGG(fmt, args...) /* nothing: it's a placeholder */


#define HALDISK_MAJOR 0       /* dynamic major by default */
#define HALDISK_DEVS 2        /* two disks */
#define HALDISK_RAHEAD 2      /* two sectors */
#define HALDISK_SIZE 2048     /* two megs each */
#define HALDISK_BLKSIZE 1024  /* 1k blocks */
#define HALDISK_HARDSECT 512  /* 512-byte hardware sectors */

/*
 * Fixme: The spull device is removable: if it is left closed for more than
 * half a minute, it is removed. Thus use a usage count and a
 * kernel timer
 */

typedef struct Haldisk_Dev {
   int size;
   int usage;
   struct timer_list timer;
   spinlock_t lock;
   u8 *data;
} Haldisk_Dev;



#include <linux/blkpg.h>

/*
 * Non-prefixed symbols are static. They are meant to be assigned at
 * load time. Prefixed symbols are not static, so they can be used in
 * debugging. They are hidden anyways by register_symtab() unless
 * HALDISK_DEBUG is defined.
 */
static int major    = HALDISK_MAJOR;
static int devs     = HALDISK_DEVS;
static int rahead   = HALDISK_RAHEAD;
static int size     = HALDISK_SIZE;
static int irq      = 0;

static int blksize  = HALDISK_BLKSIZE;

MODULE_AUTHOR("Kevin P. Lawton");
MODULE_DESCRIPTION("Plex86 guest disk driver (HAL)");
MODULE_LICENSE("GPL");

MODULE_PARM(major, "i");
MODULE_PARM(devs, "i");
MODULE_PARM(rahead, "i");
MODULE_PARM(size, "i");
MODULE_PARM(blksize, "i");
MODULE_PARM(irq, "i");

MODULE_PARM_DESC(major, "Fixme:");
MODULE_PARM_DESC(devs, "Fixme:");
MODULE_PARM_DESC(rahead, "Fixme:");
MODULE_PARM_DESC(size, "Fixme:");
MODULE_PARM_DESC(blksize, "Fixme:");
MODULE_PARM_DESC(irq, "Fixme:");

int haldisk_devs, haldisk_rahead, haldisk_size;
int haldisk_blksize, haldisk_irq;

/* The following items are obtained through kmalloc() in haldisk_init() */

Haldisk_Dev *haldisk_devices = NULL;
int *haldisk_sizes = NULL;

int haldisk_revalidate(kdev_t i_rdev);
struct block_device_operations haldisk_bdops;

struct gendisk haldisk_gendisk = {
  major:              0,              /* Major number assigned later */
  major_name:         "pd",           /* Name of the major device */
  minor_shift:        HALDISK_SHIFT,    /* Shift to get device number */
  max_p:              1 << HALDISK_SHIFT, /* Number of partitions */
  fops:               &haldisk_bdops,   /* Block dev operations */
  // everything else is dynamic.
  };

struct hd_struct *haldisk_partitions = NULL;

/*
 * Flag used in "irq driven" mode to mark when we have operations
 * outstanding.
 */
volatile int haldisk_busy = 0;


/*
 * Open and close
 */

int haldisk_open (struct inode *inode, struct file *filp)
{
    Haldisk_Dev *dev; /* device information */
    int num = DEVICE_NR(inode->i_rdev);

    if (num >= haldisk_devs) return -ENODEV;
    dev = haldisk_devices + num;

    /* kill the timer associated to the device: it might be active */
    del_timer(&dev->timer);

    spin_lock(&dev->lock);

    /*
     * If no data area is there, allocate it. Clear its head as
     * well to prevent memory corruption due to bad partition info.
     */
    if (!dev->data) {
        dev->data = vmalloc(dev->size);
        memset(dev->data,0,2048);
    }
    if (!dev->data)
    {
        spin_unlock(&dev->lock);
        return -ENOMEM;
    }
    
    dev->usage++;
    MOD_INC_USE_COUNT;
    spin_unlock(&dev->lock);

    return 0;          /* success */
}



int haldisk_release (struct inode *inode, struct file *filp)
{
    Haldisk_Dev *dev = haldisk_devices + DEVICE_NR(inode->i_rdev);

    spin_lock(&dev->lock);
    dev->usage--;

    /*
     * If the device is closed for the last time, start a timer
     * to release RAM in half a minute. The function and argument
     * for the timer have been setup in haldisk_init()
     */
    if (!dev->usage) {
        dev->timer.expires = jiffies + 60 * HZ;
        add_timer(&dev->timer);
        /* but flush it right now */
        fsync_dev(inode->i_rdev);
        invalidate_buffers(inode->i_rdev);
    }

    MOD_DEC_USE_COUNT;
    spin_unlock(&dev->lock);
    return 0;
}



/*
 * The timer function. As argument it receives the device
 */
void haldisk_expires(unsigned long data)
{
    Haldisk_Dev *dev = (Haldisk_Dev *)data;

    spin_lock(&dev->lock);
    if (dev->usage || !dev->data) {
        spin_unlock(&dev->lock);
        printk(KERN_WARNING "haldisk: timer mismatch for device %i\n",
               dev - haldisk_devices);
        return;
    }
    PDEBUG("freeing device %i\n",dev - haldisk_devices);
    vfree(dev->data);
    dev->data=0;
    spin_unlock(&dev->lock);
    return;
}    

/*
 * The ioctl() implementation
 */

int haldisk_ioctl (struct inode *inode, struct file *filp,
                 unsigned int cmd, unsigned long arg)
{
    int err, size;
    struct hd_geometry geo;

    PDEBUG("ioctl 0x%x 0x%lx\n", cmd, arg);
    switch(cmd) {

      case BLKGETSIZE:
        /* Return the device size, expressed in sectors */
        err = ! access_ok (VERIFY_WRITE, arg, sizeof(long));
        if (err) return -EFAULT;
        size = haldisk_gendisk.part[MINOR(inode->i_rdev)].nr_sects;
        if (copy_to_user((long *) arg, &size, sizeof (long)))
          return -EFAULT;
        return 0;

      case BLKFLSBUF: /* flush */
        if (! capable(CAP_SYS_RAWIO)) return -EACCES; /* only root */
        fsync_dev(inode->i_rdev);
        invalidate_buffers(inode->i_rdev);
        return 0;

      case BLKRAGET: /* return the readahead value */
        err = ! access_ok(VERIFY_WRITE, arg, sizeof(long));
        if (err) return -EFAULT;
        PUT_USER(read_ahead[MAJOR(inode->i_rdev)],(long *) arg);
        return 0;

      case BLKRASET: /* set the readahead value */
        if (!capable(CAP_SYS_RAWIO)) return -EACCES;
        if (arg > 0xff) return -EINVAL; /* limit it */
        read_ahead[MAJOR(inode->i_rdev)] = arg;
        return 0;

      case BLKRRPART: /* re-read partition table */
        return haldisk_revalidate(inode->i_rdev);

      case HDIO_GETGEO:
        /*
         * get geometry: we have to fake one...  trim the size to a
         * multiple of 64 (32k): tell we have 16 sectors, 4 heads,
         * whatever cylinders. Tell also that data starts at sector. 4.
         */
        err = ! access_ok(VERIFY_WRITE, arg, sizeof(geo));
        if (err) return -EFAULT;
        size = haldisk_size * blksize / HALDISK_HARDSECT;
        geo.cylinders = (size & ~0x3f) >> 6;
        geo.heads = 4;
        geo.sectors = 16;
        geo.start = 4;
        if (copy_to_user((void *) arg, &geo, sizeof(geo)))
            return -EFAULT;
        return 0;

      default:
        /*
         * For ioctls we don't understand, let the block layer handle them.
         */
        return blk_ioctl(inode->i_rdev, cmd, arg);
    }

    return -ENOTTY; /* unknown command */
}

/*
 * Support for removable devices
 */

int haldisk_check_change(kdev_t i_rdev)
{
    int minor = DEVICE_NR(i_rdev);
    Haldisk_Dev *dev = haldisk_devices + minor;

    if (minor >= haldisk_devs) /* paranoid */
        return 0;

    PDEBUG("check_change for dev %i\n",minor);

    if (dev->data)
        return 0; /* still valid */
    return 1; /* expired */
}




/*
 * The file operations
 */

struct block_device_operations haldisk_bdops = {
    open:       haldisk_open,
    release:    haldisk_release,
    ioctl:      haldisk_ioctl,
    revalidate: haldisk_revalidate,
    check_media_change: haldisk_check_change,
};


/*
 * Note no locks taken out here.  In a worst case scenario, we could drop
 * a chunk of system memory.  But that should never happen, since validation
 * happens at open or mount time, when locks are held.
 */
int haldisk_revalidate(kdev_t i_rdev)
{
    /* first partition, # of partitions */
    int part1 = (DEVICE_NR(i_rdev) << HALDISK_SHIFT) + 1;
    int npart = (1 << HALDISK_SHIFT) -1;

    /* first clear old partition information */
    memset(haldisk_gendisk.sizes+part1, 0, npart*sizeof(int));
    memset(haldisk_gendisk.part +part1, 0, npart*sizeof(struct hd_struct));
    haldisk_gendisk.part[DEVICE_NR(i_rdev) << HALDISK_SHIFT].nr_sects =
            haldisk_size << 1;

    /* then fill new info */
    printk(KERN_INFO "Haldisk partition check: (%d) ", DEVICE_NR(i_rdev));
    register_disk(&haldisk_gendisk, i_rdev, HALDISK_MAXNRDEV, &haldisk_bdops,
                    haldisk_size << 1);
    return 0;
}




/*
 * Block-driver specific functions
 */

/*
 * Find the device for this request.
 */
static Haldisk_Dev *haldisk_locate_device(const struct request *req)
{
    int devno;
    Haldisk_Dev *device;

    /* Check if the minor number is in range */
    devno = DEVICE_NR(req->rq_dev);
    if (devno >= haldisk_devs) {
        static int count = 0;
        if (count++ < 5) /* print the message at most five times */
            printk(KERN_WARNING "haldisk: request for unknown device\n");
        return NULL;
    }
    device = haldisk_devices + devno;
    return device;
}


/*
 * Perform an actual transfer.
 */
static int haldisk_transfer(Haldisk_Dev *device, const struct request *req)
{
    int size, minor = MINOR(req->rq_dev);
    u8 *ptr;
    
    ptr = device->data +
            (haldisk_partitions[minor].start_sect + req->sector)*HALDISK_HARDSECT;
    size = req->current_nr_sectors*HALDISK_HARDSECT;
    /*
     * Make sure that the transfer fits within the device.
     */
    if (req->sector + req->current_nr_sectors >
                    haldisk_partitions[minor].nr_sects) {
        static int count = 0;
        if (count++ < 5)
            printk(KERN_WARNING "haldisk: request past end of partition\n");
        return 0;
    }
    /*
     * Looks good, do the transfer.
     */
    switch(req->cmd) {
        case READ:
            memcpy(req->buffer, ptr, size); /* from haldisk to buffer */
            return 1;
        case WRITE:
            memcpy(ptr, req->buffer, size); /* from buffer to haldisk */
            return 1;
        default:
            /* can't happen */
            return 0;
        }
}



void haldisk_request(request_queue_t *q)
{
    Haldisk_Dev *device;
    int status;
    long flags;

    while(1) {
        INIT_REQUEST;  /* returns when queue is empty */

        /* Which "device" are we using?  (Is returned locked) */
        device = haldisk_locate_device (CURRENT);
        if (device == NULL) {
            end_request(0);
            continue;
        }
        spin_lock_irqsave(&device->lock, flags);

        /* Perform the transfer and clean up. */
        status = haldisk_transfer(device, CURRENT);
        spin_unlock_irqrestore(&device->lock, flags);
        end_request(status); /* success */
    }
}


/*
 * The fake interrupt-driven request
 */
struct timer_list haldisk_timer; /* the engine for async invocation */

void haldisk_irqdriven_request(request_queue_t *q)
{
    Haldisk_Dev *device;
    int status;
    long flags;

    /* If we are already processing requests, don't do any more now. */
    if (haldisk_busy)
            return;

    while(1) {
        INIT_REQUEST;  /* returns when queue is empty */

        /* Which "device" are we using? */
        device = haldisk_locate_device (CURRENT);
        if (device == NULL) {
            end_request(0);
            continue;
        }
        spin_lock_irqsave(&device->lock, flags);
        
        /* Perform the transfer and clean up. */
        status = haldisk_transfer(device, CURRENT);
        spin_unlock_irqrestore(&device->lock, flags);
        /* ... and wait for the timer to expire -- no end_request(1) */
        haldisk_timer.expires = jiffies + haldisk_irq;
        add_timer(&haldisk_timer);
        haldisk_busy = 1;
        return;
    }
}


/* this is invoked when the timer expires */
void haldisk_interrupt(unsigned long unused)
{
    unsigned long flags;

    spin_lock_irqsave(&io_request_lock, flags);
    end_request(1);    /* This request is done - we always succeed */

    haldisk_busy = 0;  /* We have io_request_lock, no conflict with request */
    if (! QUEUE_EMPTY) /* more of them? */
        haldisk_irqdriven_request(NULL);  /* Start the next transfer */
    spin_unlock_irqrestore(&io_request_lock, flags);
}




/*
 * Finally, the module stuff
 */

int haldisk_init(void)
{
    int result, i;

    /*
     * Copy the (static) cfg variables to public prefixed ones to allow
     * snoozing with a debugger.
     */

    haldisk_major    = major;
    haldisk_devs     = devs;
    haldisk_rahead   = rahead;
    haldisk_size     = size;
    haldisk_blksize  = blksize;

    /*
     * Register your major, and accept a dynamic number
     */
    result = register_blkdev(haldisk_major, "haldisk", &haldisk_bdops);
    if (result < 0) {
        printk(KERN_WARNING "haldisk: can't get major %d\n",haldisk_major);
        return result;
    }
    if (haldisk_major == 0) haldisk_major = result; /* dynamic */
    major = haldisk_major; /* Use `major' later on to save typing */
    haldisk_gendisk.major = major; /* was unknown at load time */

    /* 
     * allocate the devices -- we can't have them static, as the number
     * can be specified at load time
     */

    haldisk_devices = kmalloc(haldisk_devs * sizeof (Haldisk_Dev), GFP_KERNEL);
    if (!haldisk_devices)
        goto fail_malloc;
    memset(haldisk_devices, 0, haldisk_devs * sizeof (Haldisk_Dev));
    for (i=0; i < haldisk_devs; i++) {
        /* data and usage remain zeroed */
        haldisk_devices[i].size = blksize * haldisk_size;
        init_timer(&(haldisk_devices[i].timer));
        haldisk_devices[i].timer.data = (unsigned long)(haldisk_devices+i);
        haldisk_devices[i].timer.function = haldisk_expires;
        spin_lock_init(&haldisk_devices[i].lock);
    }

    /*
     * Fixme: Assign the other needed values: request, rahead, size, blksize,
     * Fixme: hardsect. All the minor devices feature the same value.
     * Fixme: Note that `spull' defines all of them to allow testing non-default
     * Fixme: values. A real device could well avoid setting values in global
     * Fixme: arrays if it uses the default values.
     */

    read_ahead[major] = haldisk_rahead;
    result = -ENOMEM; /* for the possible errors */

    haldisk_sizes = kmalloc( (haldisk_devs << HALDISK_SHIFT) * sizeof(int),
                          GFP_KERNEL);
    if (!haldisk_sizes)
        goto fail_malloc;

    /* Start with zero-sized partitions, and correctly sized units */
    memset(haldisk_sizes, 0, (haldisk_devs << HALDISK_SHIFT) * sizeof(int));
    for (i=0; i< haldisk_devs; i++)
        haldisk_sizes[i<<HALDISK_SHIFT] = haldisk_size;
    blk_size[MAJOR_NR] = haldisk_gendisk.sizes = haldisk_sizes;

    /* Allocate the partitions array. */
    haldisk_partitions = kmalloc( (haldisk_devs << HALDISK_SHIFT) *
                               sizeof(struct hd_struct), GFP_KERNEL);
    if (!haldisk_partitions)
        goto fail_malloc;

    memset(haldisk_partitions, 0, (haldisk_devs << HALDISK_SHIFT) *
           sizeof(struct hd_struct));
    /* fill in whole-disk entries */
    for (i=0; i < haldisk_devs; i++) 
        haldisk_partitions[i << HALDISK_SHIFT].nr_sects =
          haldisk_size*(blksize/HALDISK_HARDSECT);
    haldisk_gendisk.part = haldisk_partitions;
    haldisk_gendisk.nr_real = haldisk_devs;

    /*
     * Put our gendisk structure on the list.
     */
    haldisk_gendisk.next = gendisk_head;
    gendisk_head = &haldisk_gendisk; 

    /* dump the partition table to see it */
    for (i=0; i < haldisk_devs << HALDISK_SHIFT; i++)
        PDEBUGG("part %i: beg %lx, size %lx\n", i,
               haldisk_partitions[i].start_sect,
               haldisk_partitions[i].nr_sects);

    /*
     * Allow interrupt-driven operation, if "irq=" has been specified
     */
    haldisk_irq = irq; /* copy the static variable to the visible one */
    if (haldisk_irq) {
        PDEBUG("setting timer\n");
        haldisk_timer.function = haldisk_interrupt;
        blk_init_queue(BLK_DEFAULT_QUEUE(major), haldisk_irqdriven_request);
    }
    else
        blk_init_queue(BLK_DEFAULT_QUEUE(major), haldisk_request);

#ifdef NOTNOW
    for (i = 0; i < haldisk_devs; i++)
            register_disk(NULL, MKDEV(major, i), 1, &haldisk_bdops,
                            haldisk_size << 1);
#endif

#ifndef HALDISK_DEBUG
    EXPORT_NO_SYMBOLS; /* otherwise, leave global symbols visible */
#endif

    printk ("<1>haldisk: init complete, %d devs, size %d blks %d\n",
                    haldisk_devs, haldisk_size, haldisk_blksize);
    return 0; /* succeed */

  fail_malloc:
    read_ahead[major] = 0;
    if (haldisk_sizes) kfree(haldisk_sizes);
    if (haldisk_partitions) kfree(haldisk_partitions);
    blk_size[major] = NULL;
    if (haldisk_devices) kfree(haldisk_devices);

    unregister_blkdev(major, "haldisk");
    return result;
}

void haldisk_cleanup(void)
{
    int i;
    struct gendisk **gdp;
/*
 * Before anything else, get rid of the timer functions.  Set the "usage"
 * flag on each device as well, under lock, so that if the timer fires up
 * just before we delete it, it will either complete or abort.  Otherwise
 * we have nasty race conditions to worry about.
 */
    for (i = 0; i < haldisk_devs; i++) {
        Haldisk_Dev *dev = haldisk_devices + i;
        del_timer(&dev->timer);
        spin_lock(&dev->lock);
        dev->usage++;
        spin_unlock(&dev->lock);
    }

    /* flush it all and reset all the data structures */

/*
 * Unregister the device now to avoid further operations during cleanup.
 */
    unregister_blkdev(major, "haldisk");

    for (i = 0; i < (haldisk_devs << HALDISK_SHIFT); i++)
        fsync_dev(MKDEV(haldisk_major, i)); /* flush the devices */
    blk_cleanup_queue(BLK_DEFAULT_QUEUE(major));
    read_ahead[major] = 0;
    kfree(blk_size[major]); /* which is gendisk->sizes as well */
    blk_size[major] = NULL;
    kfree(haldisk_gendisk.part);
    kfree(blksize_size[major]);
    blksize_size[major] = NULL;

    /*
     * Get our gendisk structure off the list.
     */
    for (gdp = &gendisk_head; *gdp; gdp = &((*gdp)->next))
        if (*gdp == &haldisk_gendisk) {
            *gdp = (*gdp)->next;
            break;
        }

    /* finally, the usual cleanup */
    for (i=0; i < haldisk_devs; i++) {
        if (haldisk_devices[i].data)
            vfree(haldisk_devices[i].data);
    }
    kfree(haldisk_devices);
}


module_init(haldisk_init);
module_exit(haldisk_cleanup);
