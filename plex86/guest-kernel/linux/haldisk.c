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

#define HALDISK_SHIFT 4                         /* max 16 partitions  */
#define DEVICE_NR(device) (MINOR(device)>>HALDISK_SHIFT)
#define DEVICE_NAME "hald"
#define DEVICE_INTR halDisk_intrptr         /* pointer to the bottom half */
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

#undef PDEBUG             /* undef it, just in case */
#ifdef HALDISK_DEBUG
#  define PDEBUG(fmt, args...) printk( KERN_DEBUG "halDisk: " fmt, ## args)
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

#undef PDEBUGG
#define PDEBUGG(fmt, args...) /* nothing: it's a placeholder */


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

static int blksize  = HALDISK_BLKSIZE;

MODULE_AUTHOR("Kevin P. Lawton");
MODULE_DESCRIPTION("Plex86 guest disk driver (HAL)");
MODULE_LICENSE("GPL");

MODULE_PARM(major, "i");
MODULE_PARM(rahead, "i");
MODULE_PARM(blksize, "i");
MODULE_PARM(irq, "i");

MODULE_PARM_DESC(major, "Fixme:");
MODULE_PARM_DESC(rahead, "Fixme:");
MODULE_PARM_DESC(blksize, "Fixme:");
MODULE_PARM_DESC(irq, "Fixme:");

int halDisk_rahead;
int halDisk_blksize, halDisk_irq;

/* The following items are obtained through kmalloc() in halDisk_init() */

halDiskDev_t *halDisk_devices = NULL;
int *halDisk_sizes = NULL;

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

static struct hd_struct *halDisk_partitions = NULL;

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

  PDEBUG("ioctl 0x%x 0x%lx\n", cmd, arg);
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
    printk(KERN_WARNING "halDisk: GETGEO: unit=%u\n", unit);
    err = ! access_ok(VERIFY_WRITE, arg, sizeof(geo));
    if (err)
      return -EFAULT;
    geo.cylinders = halDiskInfo[unit].geom.cylinders;
    geo.heads     = halDiskInfo[unit].geom.heads;
    geo.sectors   = halDiskInfo[unit].geom.spt;
    geo.start     = halDiskInfo[unit].geom.start;

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

/*
 * Note no locks taken out here.  In a worst case scenario, we could drop
 * a chunk of system memory.  But that should never happen, since validation
 * happens at open or mount time, when locks are held.
 */
int halDisk_revalidate(kdev_t i_rdev)
{
  /* first partition, # of partitions */
  int part1 = (DEVICE_NR(i_rdev) << HALDISK_SHIFT) + 1;
  int npart = (1 << HALDISK_SHIFT) -1;
  unsigned unit;

  /* first clear old partition information */
  memset(halDisk_gendisk.sizes+part1, 0, npart*sizeof(int));
  memset(halDisk_gendisk.part +part1, 0, npart*sizeof(struct hd_struct));
  unit = DEVICE_NR(i_rdev);
  halDisk_gendisk.part[DEVICE_NR(i_rdev) << HALDISK_SHIFT].nr_sects =
      halDiskInfo[unit].geom.numSectors;

  /* then fill new info */
  printk(KERN_INFO "Haldisk partition check: (%d) ", DEVICE_NR(i_rdev));
  register_disk(&halDisk_gendisk, i_rdev, HalDiskMaxDisks, &halDisk_bdops,
          halDiskInfo[unit].geom.numSectors);
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
  int result, i;

// Fixme: Need check of HalDiskMaxDisks vs devs

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

  for (i=0; i<HalDiskMaxDisks; i++) {
    if ( halDiskInfo[i].exists ) {
      // This disk exists.  Create a RW area for it for the host<-->guest
      // communications.
      printk(KERN_WARNING "halDisk(%u): Geom: C=%u/H=%u/SPT=%u.\n",
             i,
             halDiskInfo[i].geom.cylinders,
             halDiskInfo[i].geom.heads,
             halDiskInfo[i].geom.spt
             );
      halDiskRwArea[i] = (halDiskGuestRwArea_t *)
          get_zeroed_page(GFP_KERNEL | __GFP_DMA);
      // Fixme: check return status of get_zeroed_page().
      }
    else {
      // This disk does not exist.  No RW area allocated.
      halDiskRwArea[i] = 0;
      }
    }

  /*
   * Copy the (static) cfg variables to public prefixed ones to allow
   * snoozing with a debugger.
   */

  halDisk_major  = major;
  halDisk_rahead   = rahead;
  halDisk_blksize  = blksize;

  /*
   * Register your major, and accept a dynamic number
   */
  result = register_blkdev(halDisk_major, "halDisk", &halDisk_bdops);
  if (result < 0) {
    printk(KERN_WARNING "halDisk: can't get major %d\n",halDisk_major);
    return result;
  }
  if (halDisk_major == 0) halDisk_major = result; /* dynamic */
  major = halDisk_major; /* Use `major' later on to save typing */
  halDisk_gendisk.major = major; /* was unknown at load time */

  /* 
   * allocate the devices -- we can't have them static, as the number
   * can be specified at load time
   */

  halDisk_devices = kmalloc(HalDiskMaxDisks * sizeof (halDiskDev_t),
                            GFP_KERNEL);
  if (!halDisk_devices)
    goto fail_malloc;
  memset(halDisk_devices, 0, HalDiskMaxDisks * sizeof (halDiskDev_t));
  for (i=0; i < HalDiskMaxDisks; i++) {
    /* data and usage remain zeroed */
    spin_lock_init(&halDisk_devices[i].lock);
    }

  /*
   * Fixme: Assign the other needed values: request, rahead, size, blksize,
   * Fixme: hardsect. All the minor devices feature the same value.
   * Fixme: Note that `spull' defines all of them to allow testing non-default
   * Fixme: values. A real device could well avoid setting values in global
   * Fixme: arrays if it uses the default values.
   */

  read_ahead[major] = halDisk_rahead;
  result = -ENOMEM; /* for the possible errors */

  halDisk_sizes = kmalloc( (HalDiskMaxDisks << HALDISK_SHIFT) * sizeof(int),
              GFP_KERNEL);
  if (!halDisk_sizes)
    goto fail_malloc;

  /* Start with zero-sized partitions, and correctly sized units */
  memset(halDisk_sizes, 0, (HalDiskMaxDisks << HALDISK_SHIFT) * sizeof(int));
  for (i=0; i< HalDiskMaxDisks; i++)
    halDisk_sizes[i<<HALDISK_SHIFT] = (halDiskInfo[i].geom.numSectors>>1);
  blk_size[MAJOR_NR] = halDisk_gendisk.sizes = halDisk_sizes;

  /* Allocate the partitions array. */
  halDisk_partitions = kmalloc( (HalDiskMaxDisks << HALDISK_SHIFT) *
                 sizeof(struct hd_struct), GFP_KERNEL);
  if (!halDisk_partitions)
    goto fail_malloc;

  memset(halDisk_partitions, 0, (HalDiskMaxDisks << HALDISK_SHIFT) *
       sizeof(struct hd_struct));
  /* fill in whole-disk entries */
  for (i=0; i < HalDiskMaxDisks; i++) 
    halDisk_partitions[i << HALDISK_SHIFT].nr_sects =
      halDiskInfo[i].geom.numSectors;
  halDisk_gendisk.part = halDisk_partitions;
  halDisk_gendisk.nr_real = HalDiskMaxDisks;

  /*
   * Put our gendisk structure on the list.
   */
  halDisk_gendisk.next = gendisk_head;
  gendisk_head = &halDisk_gendisk; 

  /* dump the partition table to see it */
  for (i=0; i < HalDiskMaxDisks << HALDISK_SHIFT; i++)
    PDEBUGG("part %i: beg %lx, size %lx\n", i,
         halDisk_partitions[i].start_sect,
         halDisk_partitions[i].nr_sects);

  /*
   * Allow interrupt-driven operation, if "irq=" has been specified
   */
  halDisk_irq = irq; /* copy the static variable to the visible one */
  if (halDisk_irq) {
    PDEBUG("setting timer\n");
    halDisk_timer.function = halDisk_interrupt;
    blk_init_queue(BLK_DEFAULT_QUEUE(major), halDisk_irqdriven_request);
  }
  else
    blk_init_queue(BLK_DEFAULT_QUEUE(major), halDisk_request);

#ifdef NOTNOW
  for (i = 0; i < HalDiskMaxDisks; i++)
      register_disk(NULL, MKDEV(major, i), 1, &halDisk_bdops,
              halDiskInfo[i].geom.numSectors);
#endif

#ifndef HALDISK_DEBUG
  EXPORT_NO_SYMBOLS; /* otherwise, leave global symbols visible */
#endif

  printk ("<1>halDisk: init complete, %d devs, size %d blks %d\n",
          HalDiskMaxDisks, halDiskInfo[0].geom.numSectors>>1, halDisk_blksize);
  return 0; /* succeed */

  fail_malloc:
  read_ahead[major] = 0;
  if (halDisk_sizes) kfree(halDisk_sizes);
  if (halDisk_partitions) kfree(halDisk_partitions);
  blk_size[major] = NULL;
  if (halDisk_devices) kfree(halDisk_devices);

  unregister_blkdev(major, "halDisk");
  return result;
}

void halDisk_cleanup(void)
{
  int i;
  struct gendisk **gdp;
/*
 * Before anything else, get rid of the timer functions.  Set the "usage"
 * flag on each device as well, under lock, so that if the timer fires up
 * just before we delete it, it will either complete or abort.  Otherwise
 * we have nasty race conditions to worry about.
 */
  for (i = 0; i < HalDiskMaxDisks; i++) {
    halDiskDev_t *dev = halDisk_devices + i;
    spin_lock(&dev->lock);
    dev->usage++;
    spin_unlock(&dev->lock);
  }

  /* flush it all and reset all the data structures */

/*
 * Unregister the device now to avoid further operations during cleanup.
 */
  unregister_blkdev(major, "halDisk");

  for (i = 0; i < (HalDiskMaxDisks << HALDISK_SHIFT); i++)
    fsync_dev(MKDEV(halDisk_major, i)); /* flush the devices */
  blk_cleanup_queue(BLK_DEFAULT_QUEUE(major));
  read_ahead[major] = 0;
  kfree(blk_size[major]); /* which is gendisk->sizes as well */
  blk_size[major] = NULL;
  kfree(halDisk_gendisk.part);
  kfree(blksize_size[major]);
  blksize_size[major] = NULL;

  /*
   * Get our gendisk structure off the list.
   */
  for (gdp = &gendisk_head; *gdp; gdp = &((*gdp)->next))
    if (*gdp == &halDisk_gendisk) {
      *gdp = (*gdp)->next;
      break;
    }

  /* finally, the usual cleanup */
  halDiskCleanup();

  kfree(halDisk_devices);
}


module_init(halDisk_init);
module_exit(halDisk_cleanup);
