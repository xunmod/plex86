/*
 * $Id$
 */

/*
 * This driver was derived from the sample "spull" driver
 * from the book "Linux Device Drivers" by Alessandro Rubini and
 * Jonathan Corbet, published by O'Reilly & Associates.
 */

// Fixme: make all possible symbols static?
// Fixme: blocks vs sectors.  I assume blks=1024,sector=512 throughout.
// Fixme: do we need dev->usage any more.  Was for removable drives.

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


#define MAJOR_NR halDisk_major /* force definitions on in blk.h */
int halDisk_major; /* must be declared before including blk.h */

#define HALDISK_SHIFT 4                   /* max 16 partitions  */
#define DEVICE_NR(device) (MINOR(device)>>HALDISK_SHIFT)
#define DEVICE_NAME "hald"
#define DEVICE_INTR halDisk_intrptr       /* pointer to the bottom half */
#define DEVICE_NO_RANDOM                  /* no entropy to contribute */
#define DEVICE_REQUEST halDisk_request
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

#undef PDEBUGG
#ifdef HALDISK_DEBUG
#  define PDEBUG(fmt, args...) printk( KERN_DEBUG "halDisk: " fmt, ## args)
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif


#define HALDISK_MAJOR 0       /* dynamic major by default */
#define HALDISK_RAHEAD 2      /* two sectors */
#define HALDISK_BLKSIZE 1024  /* 1k blocks */
#define HALDISK_HARDSECT 512  /* 512-byte hardware sectors */

/*
 * Fixme: The spull device is removable: if it is left closed for more than
 * half a minute, it is removed. Thus use a usage count and a
 * kernel timer
 */

typedef struct halDiskDev_t {
  int usage;
  spinlock_t lock;
  } halDiskDev_t;



#include <linux/blkpg.h>

#include "config.h"
#include "hal.h"

/*
 * Non-prefixed symbols are static. They are meant to be assigned at
 * load time. Prefixed symbols are not static, so they can be used in
 * debugging. They are hidden anyways by register_symtab() unless
 * HALDISK_DEBUG is defined.
 */
static int major    = HALDISK_MAJOR;
static int rahead   = HALDISK_RAHEAD;
static int irq      = 0;


MODULE_AUTHOR("Kevin P. Lawton");
MODULE_DESCRIPTION("Plex86 guest disk driver (HAL)");
MODULE_LICENSE("GPL");

MODULE_PARM(major, "i");
MODULE_PARM(rahead, "i");
MODULE_PARM(irq, "i");

MODULE_PARM_DESC(major, "Fixme:");
MODULE_PARM_DESC(rahead, "Fixme:");
MODULE_PARM_DESC(irq, "Fixme:");

int halDisk_rahead;
int halDisk_irq;

static halDiskDev_t halDisk_devices[HalDiskMaxDisks];

static int halDiskHardSectSize[HalDiskMaxDisks << HALDISK_SHIFT];
static int halDiskBlockSize[HalDiskMaxDisks << HALDISK_SHIFT];
static int halDiskPartitionSizes[HalDiskMaxDisks << HALDISK_SHIFT];
static struct hd_struct halDisk_partitions[HalDiskMaxDisks << HALDISK_SHIFT];

static int halDisk_open(struct inode *inode, struct file *filp);
static int halDisk_release(struct inode *inode, struct file *filp);
static int halDisk_ioctl(struct inode *inode, struct file *filp,
                         unsigned int cmd, unsigned long arg);
static int halDisk_revalidate(kdev_t i_rdev);
static halDiskDev_t *halDisk_locate_device(const struct request *req);
static int halDisk_transfer(halDiskDev_t *device, const struct request *req);
static void halDisk_request(request_queue_t *q);

static void halDisk_irqdriven_request(request_queue_t *q);
static void halDisk_interrupt(unsigned long unused);

// External scope:
int halDisk_init(void);
void halDisk_cleanup(void);

static struct block_device_operations halDisk_bdops = {
  owner:      THIS_MODULE,
  open:       halDisk_open,
  release:    halDisk_release,
  ioctl:      halDisk_ioctl,
  revalidate: halDisk_revalidate,
  };


static struct gendisk halDisk_gendisk = {
  major:              0,              /* Major number assigned later */
  major_name:         DEVICE_NAME,    /* Name of the major device */
  minor_shift:        HALDISK_SHIFT,    /* Shift to get device number */
  max_p:              1 << HALDISK_SHIFT, /* Number of partitions */
  fops:               &halDisk_bdops,   /* Block dev operations */
  // everything else is dynamic.
  };

/*
 * Flag used in "irq driven" mode to mark when we have operations
 * outstanding.
 */
static volatile int halDisk_busy = 0;

/* The fake interrupt-driven request */
static struct timer_list halDisk_timer; /* the engine for async invocation */

//
// HAL interface stuff.
//
static halDiskInfo_t *halDiskInfo = 0;
static halDiskGuestRwArea_t *halDiskRwArea[HalDiskMaxDisks];

static void halDiskGetDiskInfo(unsigned phyAddr);
static unsigned halDiskWrite(unsigned unit, unsigned sector, unsigned phyAddr);
static unsigned halDiskRead(unsigned unit, unsigned sector, unsigned phyAddr);
static unsigned halDiskCleanup(void);


/*
 * Open and close
 */

int halDisk_open (struct inode *inode, struct file *filp)
{
  halDiskDev_t *dev; /* device information */
  int unit = DEVICE_NR(inode->i_rdev);

  if (unit >= HalDiskMaxDisks)
    return -ENODEV;
  if (halDiskInfo[unit].exists==0)
    return -ENODEV;
  dev = halDisk_devices + unit;

  spin_lock(&dev->lock);

  dev->usage++;
  MOD_INC_USE_COUNT;
  spin_unlock(&dev->lock);

  return 0;      /* success */
}



int halDisk_release (struct inode *inode, struct file *filp)
{
  halDiskDev_t *dev = halDisk_devices + DEVICE_NR(inode->i_rdev);

  spin_lock(&dev->lock);
  dev->usage--;

  /*
   * If the device is closed for the last time.
   */
  if (!dev->usage) {
    /* but flush it right now */
    fsync_dev(inode->i_rdev);
    invalidate_buffers(inode->i_rdev);
  }

  MOD_DEC_USE_COUNT;
  spin_unlock(&dev->lock);
  return 0;
}


/*
 * The ioctl() implementation
 */

int halDisk_ioctl(struct inode *inode, struct file *filp,
          unsigned int cmd, unsigned long arg)
{
  int err, size;
  struct hd_geometry geo;

  switch(cmd) {

    case BLKGETSIZE:
    /* Return the device size, expressed in sectors */
    err = ! access_ok (VERIFY_WRITE, arg, sizeof(long));
    if (err) return -EFAULT;
    size = halDisk_gendisk.part[MINOR(inode->i_rdev)].nr_sects;
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
    return halDisk_revalidate(inode->i_rdev);

    case HDIO_GETGEO:
    {
    /*
     * get geometry: we have to fake one...  trim the size to a
     * multiple of 64 (32k): tell we have 16 sectors, 4 heads,
     * whatever cylinders. Tell also that data starts at sector. 4.
     */
    int unit = DEVICE_NR(inode->i_rdev);
    if ( unit >= HalDiskMaxDisks) {
      return -EFAULT; // Fixme: use correct error here.
      }
    err = ! access_ok(VERIFY_WRITE, arg, sizeof(geo));
    if (err)
      return -EFAULT;
    geo.cylinders = halDiskInfo[unit].geom.cylinders;
    geo.heads     = halDiskInfo[unit].geom.heads;
    geo.sectors   = halDiskInfo[unit].geom.spt;
    geo.start     = halDisk_gendisk.part[MINOR(inode->i_rdev)].start_sect;

    if (copy_to_user((void *) arg, &geo, sizeof(geo)))
      return -EFAULT;
    return 0;
    }

    default:
    /*
     * For ioctls we don't understand, let the block layer handle them.
     */
    return blk_ioctl(inode->i_rdev, cmd, arg);
  }

  return -ENOTTY; /* unknown command */
}

int halDisk_revalidate(kdev_t i_rdev)
{
  unsigned unit = DEVICE_NR(i_rdev);
  /* first partition, # of partitions */
  int part1 = (DEVICE_NR(i_rdev) << HALDISK_SHIFT) + 1;
  int npart = (1 << HALDISK_SHIFT) -1;

  /* first clear old partition information */
  memset(halDisk_gendisk.sizes+part1, 0, npart*sizeof(int));
  memset(halDisk_gendisk.part +part1, 0, npart*sizeof(struct hd_struct));
  halDisk_gendisk.part[DEVICE_NR(i_rdev) << HALDISK_SHIFT].nr_sects =
      halDiskInfo[unit].geom.numSectors;

  grok_partitions(&halDisk_gendisk, unit,  1<<HALDISK_SHIFT,
          halDisk_gendisk.sizes[unit<<HALDISK_SHIFT]);
  return 0;
}




/*
 * Block-driver specific functions
 */

/*
 * Find the device for this request.
 */
halDiskDev_t *halDisk_locate_device(const struct request *req)
{
  int unit;
  halDiskDev_t *device;

  /* Check if the minor number is in range */
  unit = DEVICE_NR(req->rq_dev);
  if ( (unit>=HalDiskMaxDisks) || (halDiskInfo[unit].exists==0) ) {
    static int count = 0;
    if (count++ < 5) /* print the message at most five times */
      printk(KERN_WARNING "halDisk: request for unknown device\n");
    return NULL;
    }
  device = halDisk_devices + unit;
  return device;
}


/*
 * Perform an actual transfer.
 */
int halDisk_transfer(halDiskDev_t *device, const struct request *req)
{
  int minor = MINOR(req->rq_dev);
  unsigned sector, sectorCount, i;
  unsigned unit = DEVICE_NR(req->rq_dev);
  unsigned lAddr, pAddr;
  
  sector = halDisk_partitions[minor].start_sect + req->sector;
  sectorCount = req->current_nr_sectors;

  /*
   * Make sure that the transfer fits within the device.
   */
  if (req->sector + req->current_nr_sectors >
          halDisk_partitions[minor].nr_sects) {
    static int count = 0;
    if (count++ < 5)
      printk(KERN_WARNING "halDisk: request past end of partition\n");
    return 0;
  }
  /*
   * Looks good, do the transfer.
   */
  switch(req->cmd) {
    case READ:
      lAddr = (unsigned) halDiskRwArea[unit]->rwBuffer;
      pAddr = lAddr - PAGE_OFFSET;
      for (i=0; i<sectorCount; i++) {
        // Fixme: act on return value of halDiskRead
        halDiskRead(unit, sector+i, pAddr);
        memcpy(req->buffer, (void*) lAddr, sectorCount*512);
        }
      return 1;

    case WRITE:
      lAddr = (unsigned) halDiskRwArea[unit]->rwBuffer;
      pAddr = lAddr - PAGE_OFFSET;
      for (i=0; i<sectorCount; i++) {
        memcpy((void*) lAddr, req->buffer, sectorCount*512);
        // Fixme: act on return value of halDiskWrite
        halDiskWrite(unit, sector+i, pAddr);
        }
      return 1;

    default:
      /* can't happen */
      return 0;
    }
}

  unsigned
halDiskWrite(unsigned unit, unsigned sector, unsigned phyAddr)
{
  unsigned result;

  __asm__ volatile (
    "int $0xff"
    : "=a" (result)
    : "0" (HalCallDiskWrite),
      "b" (phyAddr),
      "c" (sector),
      "d" (unit)
    );
  return(result);
}

  unsigned
halDiskRead(unsigned unit, unsigned sector, unsigned phyAddr)
{
  unsigned result;

  __asm__ volatile (
    "int $0xff"
    : "=a" (result)
    : "0" (HalCallDiskRead),
      "b" (phyAddr),
      "c" (sector),
      "d" (unit)
    );
  return(result);
}

  unsigned
halDiskCleanup(void)
{
  unsigned result;

  __asm__ volatile (
    "int $0xff"
    : "=a" (result)
    : "0" (HalCallDiskCleanup)
    );
  return(result);
}



void halDisk_request(request_queue_t *q)
{
  halDiskDev_t *device;
  int status;
  long flags;

  while(1) {
    INIT_REQUEST;  /* returns when queue is empty */

    /* Which "device" are we using?  (Is returned locked) */
    device = halDisk_locate_device (CURRENT);
    if (device == NULL) {
      end_request(0);
      continue;
    }
    spin_lock_irqsave(&device->lock, flags);

    /* Perform the transfer and clean up. */
    status = halDisk_transfer(device, CURRENT);
    spin_unlock_irqrestore(&device->lock, flags);
    end_request(status); /* success */
  }
}


/*
 * The fake interrupt-driven request
 */

void halDisk_irqdriven_request(request_queue_t *q)
{
  halDiskDev_t *device;
  int status;
  long flags;

  /* If we are already processing requests, don't do any more now. */
  if (halDisk_busy)
      return;

  while(1) {
    INIT_REQUEST;  /* returns when queue is empty */

    /* Which "device" are we using? */
    device = halDisk_locate_device (CURRENT);
    if (device == NULL) {
      end_request(0);
      continue;
    }
    spin_lock_irqsave(&device->lock, flags);
    
    /* Perform the transfer and clean up. */
    status = halDisk_transfer(device, CURRENT);
    spin_unlock_irqrestore(&device->lock, flags);
    /* ... and wait for the timer to expire -- no end_request(1) */
    halDisk_timer.expires = jiffies + halDisk_irq;
    add_timer(&halDisk_timer);
    halDisk_busy = 1;
    return;
  }
}


/* this is invoked when the timer expires */
void halDisk_interrupt(unsigned long unused)
{
  unsigned long flags;

  spin_lock_irqsave(&io_request_lock, flags);
  end_request(1);  /* This request is done - we always succeed */

  halDisk_busy = 0;  /* We have io_request_lock, no conflict with request */
  if (! QUEUE_EMPTY) /* more of them? */
    halDisk_irqdriven_request(NULL);  /* Start the next transfer */
  spin_unlock_irqrestore(&io_request_lock, flags);
}


//
// HAL features:
//
  void
halDiskGetDiskInfo( unsigned phyAddr )
{
  __asm__ volatile (
    "int $0xff"
    :
    : "a" (HalCallDiskGetInfo),
      "b" (phyAddr)
    );
}


/*
 * Finally, the module stuff
 */

int halDisk_init(void)
{
  int result;
  unsigned unit, p;

  // Allocate a page of memory for the disk info structure, to be
  // communicated with the host via a HAL call.  Memory is zeroed out for us.
  halDiskInfo = (halDiskInfo_t *) get_zeroed_page(GFP_KERNEL | __GFP_DMA);
  // Fixme: check return status of get_zeroed_page().

  // Get the disk geometry of supported disks from the host, via a HAL call.
  // Pass in the *physical* address of the page containing the info structure.
  // The host application only deals with physical addresses, which is why
  // we need to assign a separate page so the structure is aligned within
  // the page.
  halDiskGetDiskInfo( ((unsigned) halDiskInfo) - PAGE_OFFSET );

  for (unit=0; unit<HalDiskMaxDisks; unit++) {
    if ( halDiskInfo[unit].exists ) {
      // This disk exists.  Create a RW area for it for the host<-->guest
      // communications.
      printk(KERN_INFO "halDisk(%u): Geom: C=%u/H=%u/SPT=%u.\n",
             unit,
             halDiskInfo[unit].geom.cylinders,
             halDiskInfo[unit].geom.heads,
             halDiskInfo[unit].geom.spt
             );
      halDiskRwArea[unit] = (halDiskGuestRwArea_t *)
          get_zeroed_page(GFP_KERNEL | __GFP_DMA);
      // Fixme: check return status of get_zeroed_page().
      }
    else {
      // This disk does not exist.  No RW area allocated.
      halDiskRwArea[unit] = 0;
      }
    }

  /*
   * Copy the (static) cfg variables to public prefixed ones to allow
   * snoozing with a debugger.
   */

  halDisk_major  = major;
  halDisk_rahead   = rahead;

  /*
   * Register your major, and accept a dynamic number
   */
// Fixme: use devfs_register_blkdev() instead?
  result = register_blkdev(halDisk_major, "halDisk", &halDisk_bdops);
  if (result < 0) {
    printk(KERN_WARNING "halDisk: can't get major %d\n",halDisk_major);
    return result;
    }
  if (halDisk_major == 0)
    halDisk_major = result; /* dynamic */
  major = halDisk_major; /* Use `major' later on to save typing */
  halDisk_gendisk.major = major; /* was unknown at load time */

  memset(halDisk_devices, 0, sizeof(halDisk_devices));
  memset(halDiskPartitionSizes, 0, sizeof(halDiskPartitionSizes));
  memset(halDisk_partitions, 0, sizeof(halDisk_partitions));

  result = -ENOMEM; /* for the possible errors */

  // Fill in per-driver entries.
  read_ahead[MAJOR_NR] = halDisk_rahead;

  // Fill in whole-disk entries.
  for (unit=0; unit<HalDiskMaxDisks; unit++) {
    spin_lock_init(&halDisk_devices[unit].lock);
    halDiskPartitionSizes[unit<<HALDISK_SHIFT] =
        (halDiskInfo[unit].geom.numSectors>>1);
    halDisk_partitions[unit << HALDISK_SHIFT].nr_sects =
      halDiskInfo[unit].geom.numSectors;
    }

  halDisk_gendisk.part = halDisk_partitions;
  halDisk_gendisk.nr_real = HalDiskMaxDisks;
  halDisk_gendisk.sizes = halDiskPartitionSizes;

  // Fill in partition-specific entries.
  for (p=0; p<(HalDiskMaxDisks<<HALDISK_SHIFT); p++) {
    halDiskHardSectSize[p] = HALDISK_HARDSECT;
    halDiskBlockSize[p]    = HALDISK_BLKSIZE;
    }
  hardsect_size[MAJOR_NR] = halDiskHardSectSize;
  blksize_size[MAJOR_NR]  = halDiskBlockSize;
  blk_size[MAJOR_NR]      = halDisk_gendisk.sizes;

  // Put our gendisk structure on the list.
  add_gendisk( &halDisk_gendisk );

  /*
   * Allow interrupt-driven operation, if "irq=" has been specified
   */
  halDisk_irq = irq; /* copy the static variable to the visible one */
  if (halDisk_irq) {
    halDisk_timer.function = halDisk_interrupt;
    blk_init_queue(BLK_DEFAULT_QUEUE(major), halDisk_irqdriven_request);
    }
  else
    blk_init_queue(BLK_DEFAULT_QUEUE(major), halDisk_request);

  for (unit = 0; unit<HalDiskMaxDisks; unit++) {
    register_disk(&halDisk_gendisk, MKDEV(major, unit<<HALDISK_SHIFT),
                  1<<HALDISK_SHIFT, &halDisk_bdops,
                  halDiskInfo[unit].geom.numSectors);
    }

#ifndef HALDISK_DEBUG
  EXPORT_NO_SYMBOLS; /* otherwise, leave global symbols visible */
#endif

  printk(KERN_INFO "halDisk: init complete, %d devs, size %d blks %d\n",
         HalDiskMaxDisks, halDiskInfo[0].geom.numSectors>>1, HALDISK_BLKSIZE);
  return 0; /* succeed */
}

void halDisk_cleanup(void)
{
  int unit, p;
/*
 * Before anything else, get rid of the timer functions.  Set the "usage"
 * flag on each device as well, under lock, so that if the timer fires up
 * just before we delete it, it will either complete or abort.  Otherwise
 * we have nasty race conditions to worry about.
 */
  for (unit = 0; unit<HalDiskMaxDisks; unit++) {
    halDiskDev_t *dev = halDisk_devices + unit;
    spin_lock(&dev->lock);
    dev->usage++;
    spin_unlock(&dev->lock);
    }

  /* flush it all and reset all the data structures */

/*
 * Unregister the device now to avoid further operations during cleanup.
 */
  unregister_blkdev(major, "halDisk");

  for (p = 0; p < (HalDiskMaxDisks << HALDISK_SHIFT); p++)
    fsync_dev(MKDEV(halDisk_major, p)); /* flush the devices */
  blk_cleanup_queue(BLK_DEFAULT_QUEUE(major));
  read_ahead[major] = 0;
  hardsect_size[major] = NULL;
  blksize_size[major] = NULL;
  blk_size[major] = NULL;

  // Get our gendisk off the list.
  del_gendisk( &halDisk_gendisk );

  // HAL cleanup.
  halDiskCleanup();
}


module_init(halDisk_init);
module_exit(halDisk_cleanup);
