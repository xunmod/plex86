/*
 *  $Id$
 *
 *  plex86: run multiple x86 operating systems concurrently
 *  Copyright (C) 1999-2003 Kevin P. Lawton
 *
 *  paging-mon.c:  Virtualized (monitor) paging functionality.
 *
 */


#include "plex86.h"
#define IN_MONITOR_SPACE
#include "monitor.h"


static inline void loadCR3(Bit32u newCR3)
{
  __asm__ volatile (
    "movl %0, %%cr3"
      : /* No outputs. */
      : "r" (newCR3)
    );
}

static unsigned getFreePageTable(vm_t *, unsigned pdi);
static Bit32u getHostOSPinnedPage(vm_t *vm, Bit32u ppi);

/*
static void sanity_check_pdir(vm_t *vm, unsigned id, Bit32u guest_laddr);
 */

#if 0
======= Old notes =====================================================
   +++ fix retrieve mon pages function and .base issue
       also open_guest_phy_page expects shifted page val
   +++ write_physical() has hack to ignore when perm!=RW, fix!
   +++ add async handling in emulation.c, like in preGuest()

   Cases which would generate a mon #PF
   ====================================
   lazy map
   r/w to current code page
   guest #PF (access checks of cpl,rw)
   w to RO construct
   r/w to NA construct

   inhibits


  IDT,GDT,LDT: limit = 64K; TR is a dont care
  What to do with PDir, PTbl?
  What to do about coherence probs with page tables and TLB?
  When are A,D bits copied between monitor and host?
  Need check for mapping of space used by monitor

  Code cache probably should not have laddr in it

  guest.PG==0, how are phy pages unmarked when constructs move?
  guest.PG transition: dump everything (flush)

  remapping descriptor tables after page flush
  make sure to validate phy_attr everywhere before using it.

  checks for the phy_attr of page that PDir goes in
  page fault because of monP?E.RW==0, but guestP?E==1

  +++ what about virtualized linear structs like GDT, IDT, ...
#endif


// Fixme: Have to be careful unpinning a page which is open
// Fixme: via open_guest_phy_page().  Multiple pages could be
// Fixme: open in the page walk at one time until D/A bits are set.




  /* For a given guest Physical Page Index, make sure the corresponding
   * physical page of guest memory allocated by the host, is pinned
   * in memory by the host.  Since we are mapping this page into the
   * monitor address space (and thus the guest's), we need it pinned
   * so that the host does not swap out that page without our knowlege!
   */

  Bit32u
getHostOSPinnedPage(vm_t *vm, Bit32u guestPPI)
{
  MonAssert(vm, guestPPI < vm->pages.guest_n_pages);

  /* If physical page was already pinned by a previous request, nothing to do.
   * Return the host physical address of the page.
   */
  if ( vm->pageInfo[guestPPI].attr.fields.pinned )
    return( vm->pageInfo[guestPPI].hostPPI );

  /* Page is not already pinned by the host OS.  We need to request
   * from the host OS, that this page is pinned and find the
   * physical address.
   */
  toHostPinUserPage(vm, guestPPI);
  MonAssert(vm, vm->pageInfo[guestPPI].attr.fields.pinned );

  return( vm->pageInfo[guestPPI].hostPPI );
}


  /* Allocate one page for use as a page table.  We maintain a heap
   * of pages for such use, which involve the following data:
   *
   *   ptbl_laddr_map_i:                {0..MON_PAGE_TABLES-1}
   *     This is the index in the free list of the next available
   *     page for use as a page table.
   *
   *   page_tbl[0..MON_PAGE_TABLES-1]:
   *     This is the list of pages to be used as page tables.  This list
   *     is also mapped into the monitor virtual address space, so page
   *     table entries can be accessed efficiently.
   *
   *   page_tbl_laddr_map[pdi=0..1023]: {-1, index}
   *     A map, one entry for each possible PDI in the page directory,
   *     of page table list indeces for any page directory entry which
   *     is present.  This is an easy way to track down which page in the
   *     page list is used for a given PDI.  -1 is stored if there is
   *     no page associated with this PDI.
   */

  unsigned
getFreePageTable(vm_t *vm, unsigned pdi)
{
  unsigned availIndex;

  /* Allocate one of the (preallocated) pages for */
  /* the monitor to use for a page table at the PDI given. */

  availIndex = vm->ptbl_laddr_map_i;

  if (availIndex >= MON_PAGE_TABLES) {
    monpanic(vm, "getFreePageTable: out of page tables\n");
    }
#if ANAL_CHECKS
  if (vm->guest.addr.page_tbl_laddr_map[pdi] != -1) {
    monpanic(vm, "getFreePageTable: check failed, "
             "  pdi=0x%x, laddr_map=0x%x\n",
             pdi, vm->guest.addr.page_tbl_laddr_map[pdi]);
    }
#endif
  /* Record the new index map entry for this PDI. */
  vm->guest.addr.page_tbl_laddr_map[pdi] = vm->ptbl_laddr_map_i++;
  /* Zero out the page table. */
  /* Fixme: use a page align optimized zero function? */
  nexusMemZero(&vm->guest.addr.page_tbl[availIndex], 4096);

  return(availIndex);
}


  /* For a given PDI, return the page-list index of the page used
   * for a page table.  Fixme: really this doesn't need to be a function,
   * since it's one line of code, but it is a good place to insert
   * sanity checks for now.
   */

  unsigned
getMonPTi(vm_t *vm, unsigned pdi, unsigned source)
{
  unsigned map_i;

  map_i = vm->guest.addr.page_tbl_laddr_map[pdi];
#if ANAL_CHECKS
  if (map_i == -1) {
    monpanic(vm, "getMonPTi: check failed, "
             "pdi=0x%x, map_i=0x%x, source=%u\n",
             pdi, map_i, source);
    }
  if (map_i >= MON_PAGE_TABLES)
    monpanic(vm, "getMonPTi: map_i OOB\n");
#endif
  return(map_i);
}



  phyPageInfo_t *
getPageUsage(vm_t *vm, Bit32u ppi)
{
  phyPageInfo_t *pusage;

  MonAssert(vm, ppi < vm->pages.guest_n_pages);
  pusage = &vm->pageInfo[ppi];

  return(pusage);
}


  void *
open_guest_phy_page(vm_t *vm, Bit32u ppi, Bit8u *mon_offset)
{
  page_t *pageTable;
  Bit32u  pti, mon_range_offset;

  MonAssert(vm, ppi < vm->pages.guest_n_pages);
  /* Since we rewind our CS/DS.base so that the beginning of our */
  /* monitor pages land on the beginning of a new 4Meg boundary */
  /* (separate PDE), find out what mon_offset is in terms of */
  /* an offset from the beginning of the PDE boundary. */
  mon_range_offset = ( ((Bit32u) mon_offset) -
                       kernelModulePages.startOffsetPageAligned );
  pti = (mon_range_offset >> 12) & 0x3ff;
  pageTable = vm->guest.addr.nexus_page_tbl;

  /* Remap the base field.  All the rest of the fields are */
  /* set previously, and can remain the same. */
  pageTable->pte[pti].fields.base = getHostOSPinnedPage(vm, ppi);
  invlpg_mon_offset( (Bit32u) mon_offset );
  return(mon_offset);
}

  void
close_guest_phy_page(vm_t *vm, Bit32u ppi)
{
  /* ppi is >> 12 already */
  /* +++ */
}

  void
invalidateGuestLinAddr(vm_t *vm, Bit32u guestLinAddr)
{
  // For a given guest linear address (and assuming a flat guest 32-bit
  // segment with base of 0), invalidate the TLB for this linear address
  // and remove the corresponding shadow page table entry (if it exists)
  // since it is no longer valid for this guest address.

  Bit32u       pdi, pti;
  Bit32u       guest_lpage_index;
  page_t      *monPTbl;
  pageEntry_t *monPDE, *monPTE;
  unsigned     pt_index;

  // Sanity check that paging is on.
  if ( vm->guest.addr.guest_cpu->cr0.fields.pg == 0 ) {
    monpanic(vm, "invGuestLinAddr: CR0.pg=0?\n");
    }

  guest_lpage_index = guestLinAddr >> 12;
  pdi = guest_lpage_index >> 10;
  pti = guest_lpage_index & 0x3ff;
  monPDE = &vm->guest.addr.page_dir[pdi];

  /* Check monitor PDE */
  if (monPDE->fields.P == 0) {
    goto done; // No (monitor) shadow PDE.
    }

  /* This laddr should not conflict with monitor space.  This is not
   * critical since it's only a TLB entry flush, but for good measure...
   */
  if ( (guestLinAddr & 0xffc00000) == vm->mon_pde_mask )
    monpanic(vm, "invGuestLinAddr: address(0x%x) conflicts with monitor.\n",
             guestLinAddr);

  pt_index = getMonPTi(vm, pdi, 12);
  monPTbl = &vm->guest.addr.page_tbl[pt_index];

  monPTE = &monPTbl->pte[pti];

  /* Check monitor PTE */
  if (monPTE->fields.P == 0) {
    goto done; // No (monitor) shadow PTE.
    }

  // Clear the PTE.  This will force a re-shadow next time the page is
  // accessed, given the TLB entry is flushed below.
  monPTE->raw = 0;

  // Note, the corresponding page table may be in use by other
  // linear addresses, so don't return it to the free pool!

done:
  // The shadow page table entry is cleared.  Now invalidate the TLB entry.
  invlpg_mon_offset( Guest2Monitor(vm, guestLinAddr) );
}

  void
monInitShadowPaging(vm_t *vm)
{
  // Fixme: synchronize with similar code in host-space.
  pageEntry_t *monPDir;
  Bit32u pdi;
  nexus_t *nexus = vm->guest.addr.nexus;

  /* Reset page table heap */
  vm->ptbl_laddr_map_i = 0;

  /* Clear monitor PD except 4Meg range used by monitor */
  monPDir = vm->guest.addr.page_dir;
  for (pdi=0; pdi<1024; pdi++) {
#if ANAL_CHECKS
    vm->guest.addr.page_tbl_laddr_map[pdi] = -1; /* max unsigned */
#endif
    if (pdi != vm->mon_pdi)
      monPDir[pdi].raw = 0;
    }

  /* Update vpaging timestamp. */
  vm->vpaging_tsc = vm_rdtsc();

  // Reload actual monitor CR3 value to flush old guest mappings.
  loadCR3( nexus->mon_cr3 );
}


  unsigned
mapGuestLinAddr(vm_t *vm, Bit32u guest_laddr, Bit32u *guest_ppi,
                unsigned req_us, unsigned req_rw, Bit32u attr,
                Bit32u *error)
{
  Bit32u       pdi, pti;
  Bit32u       guest_lpage_index, ptbl_ppi;
  page_t      *monPTbl;
  pageEntry_t *monPDE, *monPTE;
  pageEntry_t *guestPDir, guestPDE, *guestPTbl, guestPTE;
  Bit32u       guest_pdir_page_index;
  unsigned     pt_index, us, rw;
  phyPageInfo_t *pusage;
  unsigned wasRemap = 0;
//monprint(vm, "MGLinAddr: 0x%x, PG=%u\n", guest_laddr,
//         vm->guest.addr.guest_cpu->cr0.fields.pg);

  guest_lpage_index = guest_laddr >> 12;
  pdi = guest_lpage_index >> 10;
  pti = guest_lpage_index & 0x3ff;
  monPDE = &vm->guest.addr.page_dir[pdi];

  if (vm->guest.addr.guest_cpu->cr0.fields.pg) {
    /* Check out the guest's mapping of this address to see if it
     * if would allow for an access.  First, get the guest PDE.
     */
    guest_pdir_page_index = A20Addr(vm, vm->guest.addr.guest_cpu->cr3) >> 12;
    if (guest_pdir_page_index >= vm->pages.guest_n_pages)
      monpanic(vm, "mapGuestLinAddr: PG=1 guest PDE OOB\n");
    /* Open a window into guest physical memory */
    guestPDir = open_guest_phy_page(vm, guest_pdir_page_index,
                                    vm->guest.addr.tmp_phy_page0);
    guestPDE = guestPDir[pdi];

    /* See if present, before fetching PTE. */
    if (guestPDE.fields.P==0) {
      *error = 0x00000000; /* RSVD=0, P=0 */
      goto np_exception;
      }

// Fixme: ignoring PDEUnhandled bits
#if 0
    if (guestPDE.raw & PDEUnhandled)
      monpanic(vm, "mapGuestLinAddr: guestPDE 0x%08x\n", guestPDE.raw);
#endif

    if (vm->guestCPUIDInfo.procSignature.fields.family < 6) {
      /* Earlier processors update the A bit early, before the entire
       * page table walk completes.
       */
      /* Update A bit of PDE memory image if not already. */
      if ( guestPDE.fields.A == 0 ) {
        guestPDE.fields.A = 1;
        guestPDir[pdi] = guestPDE;
        }
      }

    /* Get the guest PTE. */
    ptbl_ppi = A20PageIndex(vm, guestPDE.fields.base);
    if (ptbl_ppi >= vm->pages.guest_n_pages)
      monpanic(vm, "mapGuestLinAddr: PG=1 guest PTE OOB\n");
    guestPTbl = open_guest_phy_page(vm, ptbl_ppi,
                                    vm->guest.addr.tmp_phy_page1);
    guestPTE = guestPTbl[pti];

    if (guestPTE.fields.P==0) {
      *error = 0x00000000; /* RSVD=0, P=0 */
      goto np_exception;
      }

    /* See if requested guest priv is weaker than guest PDE priv */
    if (req_us > guestPDE.fields.US) {
      *error = 0x00000001; /* RSVD=0, P=1 */
      goto access_exception;
      }
    if ( (req_rw > guestPDE.fields.RW) &&
         (vm->guest.addr.guest_cpu->cr0.fields.wp || req_us) ) {
      *error = 0x00000001; /* RSVD=0, P=1 */
      goto access_exception;
      }

// Fixme: ignoring PTEUnhandled bits.
#if 0
    if (guestPTE.raw & PTEUnhandled)
      monpanic(vm, "mapGuestLinAddr: guestPTE 0x%08x\n", guestPTE.raw);
#endif

    if (req_us > guestPTE.fields.US) {
      *error = 0x00000001; /* RSVD=0, P=1 */
      goto access_exception;
      }
    if ( (req_rw > guestPTE.fields.RW) &&
         (vm->guest.addr.guest_cpu->cr0.fields.wp || req_us) ) {
      *error = 0x00000001; /* RSVD=0, P=1 */
      goto access_exception;
      }

    if (vm->guestCPUIDInfo.procSignature.fields.family >= 6) {
      /* Update A bit of PDE memory image if not already */
      if ( guestPDE.fields.A == 0 ) {
        guestPDE.fields.A = 1;
        guestPDir[pdi] = guestPDE;
        }
      }

    /* Update A bit in PTE memory image if not already. */
    if ( (guestPTE.fields.A == 0) ||
         ((req_rw==1) && !guestPTE.fields.D) ) {
      guestPTE.fields.A = 1;
      if (req_rw==1)
        guestPTE.fields.D = 1;
      guestPTbl[pti] = guestPTE;
      }
    
    *guest_ppi = A20PageIndex(vm, guestPTE.fields.base);
    }
  else {
    /* guest paging is off, linear address is physical address */
    guest_pdir_page_index = 0; /* keep compiler quiet */
    *guest_ppi = A20PageIndex(vm, guest_lpage_index);
    }
  if (*guest_ppi >= vm->pages.guest_n_pages)
    return(MapLinPPageOOB);

/* +++ mapping in guest pages, check static phy_attr bits first before */
/* +++ allowing non-protected. */


/* mapIntoMonitor: */

  /* At this point, we know that the guest's paging system
   * (if enabled) would allow for this access.  Now we have to
   * see about mapping it into the monitor linear address space.
   */
  pusage = getPageUsage(vm, *guest_ppi);

  if (wasRemap > 1)
    monpanic(vm, "wasRemap>1\n");

  /*
   * Check monitor PDE
   */
  if (monPDE->fields.P == 0) {
    /* OK, Lazy PT map/allocate */
    if (vm->guest.addr.guest_cpu->cr0.fields.pg) {
      phyPageInfo_t *pde_pusage;

      pde_pusage =
          getPageUsage(vm, A20PageIndex(vm, guestPDE.fields.base));
      if (pde_pusage->attr.raw & PageBadUsage4PTbl) {

// Fixme: PDE->PDir hack.
monpanic(vm, "PDE->PDir hack.\n");
//monpanic(vm, "PDE.base=0x%x CR3=0x%x\n",
//         A20PageIndex(vm, guestPDE.fields.base),
//         A20Addr(vm, vm->guest.addr.guest_cpu->cr3) );
return(MapLinEmulate);
        }

      /* Allocate PT using scheme for paged guest. */
      pt_index = getFreePageTable(vm, pdi);
      monPTbl = &vm->guest.addr.page_tbl[pt_index];

      if (vm->guestCPL==3) {
        /* For user code, we can use the guest US & RW values as-is, */
        /* since they are honored as such with either CR0.WP value. */
        us = guestPDE.fields.US;
        rw = guestPDE.fields.RW;
        }
      else { /* guest supervisor code */
        /* For supervisor code, access rules are different dependent on */
        /* the value of CR0.WP. */
        if (vm->guest.addr.guest_cpu->cr0.fields.wp==0) {
          /* If CR0.WP=0, then supervisor code can write to any page, */
          /* and permissions are effectively ignored. */
          us = 1;
          rw = 1;
          }
        else { /* CR0.WP==1 */
          /* If CR0.WP=0, then supervisor code can read from any page, */
          /* but write permission depends on the RW bit. */
          us = 1;
          rw = guestPDE.fields.RW;
          }
        }


      /* Base/Avail=0/G=0/PS=0/D=d/A=a/PCD=0/PWT=0/US=us/RW=rw/P=1 */
      monPDE->raw =
          (vm->pages.page_tbl[pt_index] << 12) | (guestPDE.raw & 0x60) |
          (us<<2) | (rw<<1) | 1;
      }
    else {
      /* Allocate PT using scheme for non-paged guest. */
      pt_index = getFreePageTable(vm, pdi);
      monPTbl = &vm->guest.addr.page_tbl[pt_index];
      /* Base/Avail=0/G=0/PS=0/D=0/A=0/PCD=0/PWT=0/US=1/RW=1/P=1 */
      monPDE->raw =
          (vm->pages.page_tbl[pt_index] << 12) | 0x7;
      }
    }
  else {
    /* monPDE->P == 1 */

    /* Make sure this laddr does not conflict with monitor space */
    /* This can only happen when monPDE.P==1, since the monitor */
    /* is always mapped in. */
    if ( (guest_laddr & 0xffc00000) == vm->mon_pde_mask )
      return(MapLinMonConflict);

    pt_index = getMonPTi(vm, pdi, 12);
    monPTbl = &vm->guest.addr.page_tbl[pt_index];
    }

  monPTE = &monPTbl->pte[pti];

  /*
   * Check monitor PTE
   */
  if (monPTE->fields.P == 0) {
    if (vm->guest.addr.guest_cpu->cr0.fields.pg) {
      if (vm->guestCPL==3) {
        /* For user code, we can use the guest US & RW values as-is, */
        /* since they are honored as such with either CR0.WP value. */
        us = guestPTE.fields.US;
        rw = guestPTE.fields.RW;
        }
      else { /* guest supervisor code */
        /* For supervisor code, access rules are different dependent on */
        /* the value of CR0.WP. */
        if (vm->guest.addr.guest_cpu->cr0.fields.wp==0) {
          /* If CR0.WP=0, then supervisor code can write to any page, */
          /* and permissions are effectively ignored. */
          us = 1; 
          rw = 1;
          }
        else { /* CR0.WP==1 */
          /* If CR0.WP=0, then supervisor code can read from any page, */
          /* but write permission depends on the RW bit. */
          us = 1;
          rw = guestPTE.fields.RW;
          }
        }
      if (pusage->attr.fields.RO) {
        rw = 0;
        if (req_rw)
          return(MapLinEmulate);
        }
      else if (pusage->attr.fields.memMapIO)
        return(MapLinEmulate);

      /* Base/Avail=0/G=0/PS=0/D=d/A=a/PCD=0/PWT=0/US=1/RW=rw/P=1 */
      monPTE->raw =
          (getHostOSPinnedPage(vm, *guest_ppi) << 12) | (guestPTE.raw & 0x60) |
          0x5 | (rw<<1);
      }
    else { /* CR0.PG==0 */
      rw = 1; /* Paging off is effectively RW */
      if (pusage->attr.fields.RO) {
        rw = 0;
        if (req_rw)
          return(MapLinEmulate);
        }
      else if (pusage->attr.fields.memMapIO)
        return(MapLinEmulate);
      /* Base/Avail=0/G=0/PS=0/D=0/A=0/PCD=0/PWT=0/US=1/RW=rw/P=1 */
      monPTE->raw =
          (getHostOSPinnedPage(vm, *guest_ppi) << 12) | 0x5 | (rw<<1);
      }

    invlpg_mon_offset( Guest2Monitor(vm, guest_laddr) );
    return(MapLinOK);
    }
  else {
    /* PTE.P == 1 */
    return(MapLinAlreadyMapped);
    }

np_exception:
access_exception:
  *error |= (req_us<<2) | (req_rw<<1);
  return(MapLinException);
}


  void
guestPageFault(vm_t *vm, guestStackContext_t *context, Bit32u cr2)
{
  Bit32u   guest_ppi, error, gerror;
  unsigned us, rw;
  unsigned mapResult;

  /* Make sure this laddr does not conflict with monitor space */
  if ( (cr2 & 0xffc00000) == vm->mon_pde_mask )
    monpanic(vm, "PageFault: guest access to monitor space\n");

  error = context->error;
  if (error & 0x8) /* If RSVD bits used in PDir */
    monpanic(vm, "guestPageFault: RSVD\n");

  us = vm->guestCPL == 3;
  rw = (error >> 1) & 1;

/* +++ should base attr (currently 0) on whether this is */
/* code or data???  only if siv==1 */

  mapResult = mapGuestLinAddr(vm, cr2, &guest_ppi, us, rw, 0, &gerror);

  switch ( mapResult ) {
    case MapLinOK:
//monprint(vm, "GLA: 0x%x OK.\n", cr2);
      return;
    case MapLinMonConflict:
      monpanic(vm, "guestPageFault: MapLinMonConflict:\n");
    case MapLinAlreadyMapped:
      monpanic(vm, "guestPageFault: MapLinAlreadyMapped (0x%x):\n", cr2);
      /*emulate_instr(vm, context, 2);*/
      return;
    case MapLinPPageOOB:
      monpanic(vm, "guestPageFault: MapLinPPageOOB (0x%x):\n", cr2);
    case MapLinEmulate:
      monpanic(vm, "guestPageFault: MapLinEmulate:\n");
      /*emulate_instr(vm, context, 3);*/
      return;

    case MapLinException:
      //monprint(vm, "guestPageFault: MLE: cr2=0x%x.\n", cr2);
      vm->guest.addr.guest_cpu->cr2 = cr2;
      doGuestFault(vm, ExceptionPF, error);
      return;

    default:
      monpanic(vm, "guestPageFault: MapLin: default case:\n");
    }
}

#if 0
  void
sanity_check_pdir(vm_t *vm, unsigned id, Bit32u guest_laddr)
{
  pageEntry_t *monPDE;
  Bit32u       pdi;
  unsigned     pt_index;
 
  for (pdi=0; pdi<1024; pdi++) {
    monPDE = &vm->guest.addr.page_dir[pdi];
    if ( (pdi!=vm->mon_pdi) &&
        monPDE->fields.P ) {

      pt_index = vm->guest.addr.page_tbl_laddr_map[pdi];
      if (pt_index == -1)
        monpanic(vm, "sanity_check_pdir: pt_index==-1\n");
      if (pt_index >= vm->pages.guest_n_pages)
        monpanic(vm, "sanity_check_pdir: pt_index OOB\n");
      if ( monPDE->fields.base != vm->pages.page_tbl[pt_index] ) {
        monprint(vm, "gaddr=0x%x\n", guest_laddr);
        monprint(vm, "pt_index=%u\n", pt_index);
        monprint(vm, "map[0x302]=%u\n",
          vm->guest.addr.page_tbl_laddr_map[0x302]);
        monpanic(vm, "sanity_check_pdir: id=%u "
          "pdi=0x%x\n", id, pdi);
        }
      }
    }
}
#endif
