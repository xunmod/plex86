/*
 * $Id$
 */

/*
 * This driver was derived from the sample "snull" driver
 * from the book "Linux Device Drivers" by Alessandro Rubini and
 * Jonathan Corbet, published by O'Reilly & Associates.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/sched.h>
#include <linux/slab.h> /* Used to be malloc.h */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/interrupt.h>

#include <linux/in.h>
#include <linux/netdevice.h>   /* struct device, and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/ip.h>          /* struct iphdr */
#include <linux/tcp.h>         /* struct tcphdr */
#include <linux/skbuff.h>


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


#define HAL0_IRQ 3

/*
 * Macros to help debugging
 */

#undef PDEBUG             /* undef it, just in case */
#ifdef SNULL_DEBUG
#  define PDEBUG(fmt, args...) printk( KERN_DEBUG "snull: " fmt, ## args)
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

#undef PDEBUGG
#define PDEBUGG(fmt, args...) /* nothing: it's a placeholder */


/* Default timeout period */
#define SNULL_TIMEOUT 50   /* In jiffies */

static int snull_init(struct net_device *dev);

/*
 * The devices
 */
struct net_device snull_devs[2] = {
  { init: snull_init, },  /* init, nothing more */
  { init: snull_init, }
  };



#include <linux/in6.h>
#include <asm/checksum.h>

#include "config.h"
#include "hal.h"

/* One HAL Rx area for each device. */
static halNetGuestRxArea_t *halNetGuestRxArea[2] = { 0, 0 } ;

/* This is a load-time options */
static int eth = 0; /* Call yourself "ethX". Default is "sn0"/"sn1" */

MODULE_AUTHOR("Kevin P. Lawton");
MODULE_DESCRIPTION("Plex86 guest network driver (HAL)");
MODULE_LICENSE("GPL");

MODULE_PARM(eth, "i");
MODULE_PARM_DESC(eth, "Fixme:");

#if 0
/*
 * Transmitter lockup simulation, normally disabled.
 */
static int lockup = 0;
MODULE_PARM(lockup, "i");
#endif

#ifdef HAVE_TX_TIMEOUT
static int timeout = SNULL_TIMEOUT;
MODULE_PARM(timeout, "i");
#endif

int snull_eth;

static void snull_interrupt(int irq, void *dev_id, struct pt_regs *regs);

/*
 * This structure is private to each device. It is used to pass
 * packets in and out, so there is place for a packet
 */

struct snull_priv {
  struct net_device_stats stats;

  struct sk_buff *tx_skb;

  spinlock_t lock;
  };

void snull_tx_timeout (struct net_device *dev);
      
static unsigned halNetGuestTx(unsigned device, unsigned packetPhyAddr,
                              unsigned len);
static unsigned halNetGuestRegDev(unsigned device, unsigned rxAreaPAddr,
                                  unsigned rxAreaLen);

// Fixme: deal with checksumming

/*
 * Open and close
 */

int snull_open(struct net_device *dev)
{
  MOD_INC_USE_COUNT;
  
  /* request_region(), request_irq(), ....  (like fops->open) */

  if ( request_irq(HAL0_IRQ, snull_interrupt, 0, "hal-net", dev) ) {
    printk("snull_open: request_irq(%u) failed.\n", HAL0_IRQ);
    return 1; // Fixme:
    }

  printk("snull_open: dev_addr=%x:%x:%x:%x:%x:%x.\n",
         dev->dev_addr[0],
         dev->dev_addr[1],
         dev->dev_addr[2],
         dev->dev_addr[3],
         dev->dev_addr[4],
         dev->dev_addr[5]);

  netif_start_queue(dev);
  return 0;
}

int snull_release(struct net_device *dev)
{
  /* release ports, irq and such -- like fops->close */
  free_irq(HAL0_IRQ, dev);

  netif_stop_queue(dev); /* can't transmit any more */
  MOD_DEC_USE_COUNT;
  /* if irq2dev_map was used (2.0 kernel), zero the entry here */
  return 0;
}

/*
 * Configuration changes (passed on by ifconfig)
 */
int snull_config(struct net_device *dev, struct ifmap *map)
{
  if (dev->flags & IFF_UP) /* can't act on a running interface */
    return -EBUSY;

  /* Don't allow changing the I/O address */
  if (map->base_addr != dev->base_addr) {
    printk(KERN_WARNING "snull: Can't change I/O address\n");
    return -EOPNOTSUPP;
    }

  /* Allow changing the IRQ */
  if (map->irq != dev->irq) {
    dev->irq = map->irq;
    /* request_irq() is delayed to open-time */
    }

  /* ignore other fields */
  return 0;
}

/*
 * Receive a packet: retrieve, encapsulate and pass over to upper levels
 */
void snull_rx(struct net_device *dev, int len, unsigned char *buf)
{
  struct sk_buff *skb;
  struct snull_priv *priv = (struct snull_priv *) dev->priv;

  /*
   * The packet has been retrieved from the transmission
   * medium. Build an skb around it, so upper layers can handle it
   */
  skb = dev_alloc_skb(len+2);
  if (!skb) {
    printk("snull rx: low on mem - packet dropped\n");
    priv->stats.rx_dropped++;
    return;
    }
  skb_reserve(skb, 2); /* align IP on 16B boundary */  
  memcpy(skb_put(skb, len), buf, len);

  /* Write metadata, and then pass to the receive level */
  skb->dev = dev;
  skb->protocol = eth_type_trans(skb, dev);
  skb->ip_summed = CHECKSUM_UNNECESSARY; /* don't check it */
  priv->stats.rx_packets++;
  priv->stats.rx_bytes += len;
  netif_rx(skb);
  return;
}
  
      
/*
 * The typical interrupt entry point
 */
void snull_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
  struct snull_priv *priv;
  /*
   * As usual, check the "device" pointer for shared handlers.
   * Then assign "struct device *dev"
   */
  struct net_device *dev = (struct net_device *)dev_id;
  /* ... and check with hw if it's really ours */

  if (!dev /*paranoid*/ ) return;

  /* Lock the device */
  priv = (struct snull_priv *) dev->priv;
  spin_lock(&priv->lock);

  if ( halNetGuestRxArea[0]->rxBufferFull ) {
    // Fixme: check len to be valid against ethernet max.
    snull_rx(dev, halNetGuestRxArea[0]->rxBufferLen,
             halNetGuestRxArea[0]->rxBuffer);
    // Clear buffer full flag, now that the packet has been received.
    halNetGuestRxArea[0]->rxBufferFull = 0;
    }

  /* Unlock the device and we are done */
  spin_unlock(&priv->lock);
  return;
}


/*
 * Transmit a packet (low level interface)
 */
void snull_hw_tx(char *buf, int len, struct net_device *dev)
{
  /*
   * This function deals with hw details. This interface loops
   * back the packet to the other snull interface (if any).
   * In other words, this function implements the snull behaviour,
   * while all other procedures are rather device-independent
   */
  struct iphdr *ih;
// struct net_device *dest;
  struct snull_priv *priv;
  u32 *saddr, *daddr;
  struct ethhdr *ethHdr;

  /* I am paranoid. Ain't I? */
  if (len < sizeof(struct ethhdr) + sizeof(struct iphdr)) {
    printk("snull: Hmm... packet too short (%i octets)\n",
           len);
    return;
    }

  if (0) { /* enable this conditional to look at the data */
    int i;
    PDEBUG("len is %i\n" KERN_DEBUG "data:",len);
    for (i=14 ; i<len; i++)
        printk(" %02x",buf[i]&0xff);
    printk("\n");
    }

  /*
   * Ethhdr is 14 bytes, but the kernel arranges for iphdr
   * to be aligned (i.e., ethhdr is unaligned)
   */
  ethHdr = (struct ethhdr *) buf;
  ih = (struct iphdr *)(buf+sizeof(struct ethhdr));
  saddr = &ih->saddr;
  daddr = &ih->daddr;

#if 0
printk("saddr offset = %u, daddr offset = %u.\n",
       ((unsigned)saddr) - (unsigned) buf,
       ((unsigned)daddr) - (unsigned) buf);

  {
  u8 *srcOctet = (u8 *) saddr;
  u8 *dstOctet = (u8 *) daddr;
  printk("Tx: IP  %u.%u.%u.%u -> %u.%u.%u.%u\n",
      srcOctet[0], srcOctet[1], srcOctet[2], srcOctet[3],
      dstOctet[0], dstOctet[1], dstOctet[2], dstOctet[3]);
  printk("Tx Mac src=%02x:%02x:%02x:%02x:%02x:%02x, "
                "dst=%02x:%02x:%02x:%02x:%02x:%02x.\n",
         ethHdr->h_source[0],
         ethHdr->h_source[1],
         ethHdr->h_source[2],
         ethHdr->h_source[3],
         ethHdr->h_source[4],
         ethHdr->h_source[5],
         ethHdr->h_dest[0],
         ethHdr->h_dest[1],
         ethHdr->h_dest[2],
         ethHdr->h_dest[3],
         ethHdr->h_dest[4],
         ethHdr->h_dest[5]);
  }
#endif

  if (dev == snull_devs)
      PDEBUGG("%08x:%05i --> %08x:%05i\n",
             ntohl(ih->saddr),ntohs(((struct tcphdr *)(ih+1))->source),
             ntohl(ih->daddr),ntohs(((struct tcphdr *)(ih+1))->dest));
  else
      PDEBUGG("%08x:%05i <-- %08x:%05i\n",
             ntohl(ih->daddr),ntohs(((struct tcphdr *)(ih+1))->dest),
             ntohl(ih->saddr),ntohs(((struct tcphdr *)(ih+1))->source));

#if 0
  /*
   * Ok, now the packet is ready for transmission: first simulate a
   * receive interrupt on the twin device, then  a
   * transmission-done on the transmitting device
   */
//dest = snull_devs + (dev==snull_devs ? 1 : 0);
  dest = snull_devs + (0); // loop to same device instead of twin device.
  priv = (struct snull_priv *) dest->priv;
  priv->status |= SNULL_RX_INTR;
  priv->rx_packetlen = len;
  priv->rx_packetdata = buf;
#endif

  // Deliver the Tx packet to the VMM HAL layer.
  // Fixme: To convert the virtual packet buffer address to physical, I
  // Fixme: just subtract PAGE_OFFSET.  Since the Linux networking system
  // Fixme: works with DMA capable addresses, should be OK?
  if ( halNetGuestTx(0, ((unsigned) buf) - PAGE_OFFSET, len) ) {
    /* Transmission is complete: free the skb, and record stats. */
    priv = (struct snull_priv *) dev->priv;
    priv->stats.tx_packets++;
    priv->stats.tx_bytes += len;
    dev_kfree_skb(priv->tx_skb);
    }
  else {
    printk("halNetGuestTx failed.\n");
    }

#if 0
  if ( halNetGuestRxArea[0]->rxBufferFull ) {
    priv = (struct snull_priv *) dev->priv;
    priv->status |= SNULL_RX_INTR;
    priv->rx_packetlen = halNetGuestRxArea[0]->rxBufferLen;
    priv->rx_packetdata = halNetGuestRxArea[0]->rxBuffer;
    snull_interrupt(0, dev, NULL);
    }
#endif

#if 0
  snull_interrupt(0, dest, NULL);

  priv = (struct snull_priv *) dev->priv;
  priv->status |= SNULL_TX_INTR;
  priv->tx_packetlen = len;
  priv->tx_packetdata = buf;
  if (lockup && ((priv->stats.tx_packets + 1) % lockup) == 0) {
      /* Simulate a dropped transmit interrupt */
      netif_stop_queue(dev);
      PDEBUG("Simulate lockup at %ld, txp %ld\n", jiffies,
                      (unsigned long) priv->stats.tx_packets);
      }
  else
      snull_interrupt(0, dev, NULL);
#endif
}


/*
 * Transmit a packet (called by the kernel)
 */
int snull_tx(struct sk_buff *skb, struct net_device *dev)
{
  int len;
  char *data;
  struct snull_priv *priv = (struct snull_priv *) dev->priv;

  len = skb->len < ETH_ZLEN ? ETH_ZLEN : skb->len;
  data = skb->data;
  dev->trans_start = jiffies; /* save the timestamp */

  /* Remember the skb, so we can free it at interrupt time */
  priv->tx_skb = skb;

  /* actual deliver of data is device-specific, and not shown here */
  snull_hw_tx(data, len, dev);

  return 0; /* Our simple device can not fail */
}

/*
 * Deal with a transmit timeout.
 */
void snull_tx_timeout (struct net_device *dev)
{
  struct snull_priv *priv = (struct snull_priv *) dev->priv;

  printk("Transmit timeout at %ld, latency %ld\n", jiffies,
                  jiffies - dev->trans_start);
  dev_kfree_skb(priv->tx_skb);
  priv->stats.tx_errors++;
  netif_wake_queue(dev);
  return;
}



/*
 * Ioctl commands 
 */
int snull_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
 
  PDEBUG("ioctl\n");
  return 0;
}

/*
 * Return statistics to the caller
 */
struct net_device_stats *snull_stats(struct net_device *dev)
{
  struct snull_priv *priv = (struct snull_priv *) dev->priv;
  return &priv->stats;
}

#if 0
/*
 * This function is called to fill up an eth header, since arp is not
 * available on the interface
 */
int snull_rebuild_header(struct sk_buff *skb)
{
  struct ethhdr *eth = (struct ethhdr *) skb->data;
  struct net_device *dev = skb->dev;
  
  memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
  memcpy(eth->h_dest, dev->dev_addr, dev->addr_len);
  eth->h_dest[ETH_ALEN-1]   ^= 0x01;   /* dest is us xor 1 */
  return 0;
}


int snull_header(struct sk_buff *skb, struct net_device *dev,
              unsigned short type, void *daddr, void *saddr,
              unsigned int len)
{
  struct ethhdr *eth = (struct ethhdr *)skb_push(skb,ETH_HLEN);

  eth->h_proto = htons(type);
  memcpy(eth->h_source, saddr ? saddr : dev->dev_addr, dev->addr_len);
  memcpy(eth->h_dest,   daddr ? daddr : dev->dev_addr, dev->addr_len);
  eth->h_dest[ETH_ALEN-1]   ^= 0x01;   /* dest is us xor 1 */
  return (dev->hard_header_len);
}
#endif





/*
 * The "change_mtu" method is usually not needed.
 * If you need it, it must be like this.
 */
int snull_change_mtu(struct net_device *dev, int new_mtu)
{
  unsigned long flags;
  spinlock_t *lock = &((struct snull_priv *) dev->priv)->lock;
  
  /* check ranges */
  if ((new_mtu < 68) || (new_mtu > 1500))
      return -EINVAL;
  /*
   * Do anything you need, and the accept the value
   */
  spin_lock_irqsave(lock, flags);
  dev->mtu = new_mtu;
  spin_unlock_irqrestore(lock, flags);
  return 0; /* success */
}

/*
 * The init function (sometimes called probe).
 * It is invoked by register_netdev()
 */
int snull_init(struct net_device *dev)
{
#if 0
  /*
   * Make the usual checks: check_region(), probe irq, ...  -ENODEV
   * should be returned if no device found.  No resource should be
   * grabbed: this is done on open(). 
   */
#endif

  /* 
   * Then, assign other fields in dev, using ether_setup() and some
   * hand assignments
   */
  ether_setup(dev); /* assign some of the fields */

  dev->open            = snull_open;
  dev->stop            = snull_release;
  dev->set_config      = snull_config;
  dev->hard_start_xmit = snull_tx;
  dev->do_ioctl        = snull_ioctl;
  dev->get_stats       = snull_stats;
  dev->change_mtu      = snull_change_mtu;  
  // dev->rebuild_header  = snull_rebuild_header;
  // dev->hard_header     = snull_header;
#ifdef HAVE_TX_TIMEOUT
  dev->tx_timeout     = snull_tx_timeout;
  dev->watchdog_timeo = timeout;
#endif
  // /* keep the default flags, just add NOARP */
  // dev->flags           |= IFF_NOARP;
  // dev->hard_header_cache = NULL;      /* Disable caching */
  SET_MODULE_OWNER(dev);

  /*
   * Then, allocate the priv field. This encloses the statistics
   * and a few private fields.
   */
  dev->priv = kmalloc(sizeof(struct snull_priv), GFP_KERNEL);
  if (dev->priv == NULL)
    return -ENOMEM;
  memset(dev->priv, 0, sizeof(struct snull_priv));
  spin_lock_init(& ((struct snull_priv *) dev->priv)->lock);
  return 0;
}




/*
 * Finally, the module stuff
 */

int snull_init_module(void)
{

  int result, i, device_present = 0;

  snull_eth = eth; /* copy the cfg datum in the non-static place */

  if (!snull_eth) {
    strcpy(snull_devs[0].name, "sn0");
    strcpy(snull_devs[1].name, "sn1");
    }
  else { /* use automatic assignment */
    strcpy(snull_devs[0].name, "eth%d");
    strcpy(snull_devs[1].name, "eth%d");
    }

  for (i=0; i<2;  i++) {
    if ( (result = register_netdev(snull_devs + i)) )
      printk("snull: error %i registering device \"%s\"\n",
             result, snull_devs[i].name);
    else device_present++;
    }

#ifndef SNULL_DEBUG
  EXPORT_NO_SYMBOLS;
#endif

  // Allocate a page for use as the guest packet Rx area.  Then
  halNetGuestRxArea[0] = (halNetGuestRxArea_t *)
      get_zeroed_page(GFP_KERNEL | __GFP_DMA);
  if ( !halNetGuestRxArea[0] ) {
    return -ENODEV;
    }

  // Register this packet Rx area with the HAL.
  halNetGuestRegDev( 0, ((unsigned) halNetGuestRxArea[0]) - PAGE_OFFSET,
                     sizeof(*halNetGuestRxArea[0]) );

  return device_present ? 0 : -ENODEV;
}

void snull_cleanup(void)
{
  int i;
   
  for (i=0; i<2;  i++) {
    kfree(snull_devs[i].priv);
    unregister_netdev(snull_devs + i);
    }

  if ( halNetGuestRxArea[0] )
    free_page( (unsigned long) halNetGuestRxArea[0] );

  return;
}


  unsigned
halNetGuestTx(unsigned device, unsigned packetPhyAddr, unsigned len)
{
  unsigned result;

  __asm__ volatile (
    "int $0xff"
    : "=a" (result)
    : "0" (HalCallNetGuestTx),
      "b" (device),
      "c" (packetPhyAddr),
      "d" (len)
    );
  return result;
}

  unsigned
halNetGuestRegDev(unsigned device, unsigned rxAreaPAddr, unsigned rxAreaLen)
{
  unsigned result;

  printk("halNetGuestRegDev: buffer addr = 0x%x.\n", rxAreaPAddr);
  __asm__ volatile (
    "int $0xff"
    : "=a" (result)
    : "0" (HalCallNetGuestRegDev),
      "b" (device),
      "c" (rxAreaPAddr),
      "d" (rxAreaLen)
    );
  return result;
}

module_init(snull_init_module);
module_exit(snull_cleanup);
