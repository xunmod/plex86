/*
 * plex86: run multiple x86 operating systems concurrently
 * 
 * Copyright (C) 2000 Frank van der Linden <fvdl@wasabisystems.com>
 * Copyright (C) 2000 Alexander Langer <alex@big.endian.de>
 * Copyright (C) 2003 Eric Anholt <anholt@FreeBSD.org>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#define DIAGNOSTIC 1
#define CDEV_MAJOR	20
#define timer_t __bsd_timer_t
#define write_eflags __freebsd_write_eflags
#define read_eflags __freebsd_read_eflags

/* XXX recheck, which includes are needed */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>

#include <sys/proc.h>
#include <sys/conf.h>
#include <sys/exec.h>
#include <sys/malloc.h>
#include <sys/syslog.h>
#include <sys/queue.h>
#include <sys/signalvar.h>
#include <sys/mman.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/sysproto.h>

#include <sys/module.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_kern.h>
#include <vm/vm_param.h>
#include <vm/vm_extern.h>

#include <machine/md_var.h>

#include <machine/cpu.h>

#undef timer_t
#undef write_eflags
#undef read_eflags

#include "plex86.h"
#define IN_HOST_SPACE
#include "monitor.h"

static MALLOC_DEFINE(M_PLEX86, "plex86", "Plex86 mem");

static d_open_t plex86_open;
static d_close_t plex86_close;
static d_ioctl_t plex86_ioctl;

static vm_t *find_vm(struct thread *td);
static void register_vm(vm_t *vm, struct thread *td);
static void unregister_vm(struct thread *td);
int plex86_modevent(module_t mod, int type, void *data);

static struct cdevsw plex86_cdevsw = {
	.d_open =  plex86_open,
	.d_close = plex86_close,
	.d_ioctl = plex86_ioctl,
	.d_name =  "plex86",
};


static dev_t plex86_dev = 0;	/* For make_dev/destroy_dev */
static int idlechan;		/* Address for tsleeping when idle */

static struct plex86_softc {
	int	sc_open;
} plex86sc;

TAILQ_HEAD(vmlist, plex86_vmentry) plex86_vmlist;

struct plex86_vmentry {
	pid_t		vm_pid;
	vm_t		*vm_vm;
	TAILQ_ENTRY(plex86_vmentry) link;
};

static int
plex86_open(dev_t dev, int flags, int fmt, struct thread *td)
{
	vm_t *vm;

	if (suser(td) != 0)
		return (EPERM);

	/* Allow one VM per process, since we don't have per-open private data */
	vm = find_vm(td);
	if (vm == NULL) {
		vm = malloc(sizeof(vm_t), M_PLEX86, M_WAITOK);
		if (vm == NULL)
			return (ENOMEM);
		bzero(vm, sizeof(vm_t));
		register_vm(vm, td);
		plex86sc.sc_open++;
	} else
		return (EBUSY);

	/* Kernel independent device open code. */
	hostDeviceOpen(vm);

#ifdef FREEBSD_PLEX86_DEBUG
	printf("plex86: pid %u opened device, vm %p\n", td->td_proc->p_pid);
#endif

	return (0);
}

int
plex86_close(dev_t dev, int flags, int fmt, struct thread *td)
{
	vm_t *vm;
	
	vm = find_vm(td);
	hostUnallocVmPages(vm);
	unregister_vm(td);
	plex86sc.sc_open--;

#ifdef FREEBSD_PLEX86_DEBUG
	printf("plex86: pid %u closed device\n", td->td_proc->p_pid);
#endif

	return (0);
}

int
plex86_ioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct thread *td)
{
	int ret;
	vm_t *vm;

	vm = find_vm(td);
	if (vm == NULL)
		return (EINVAL);
#ifdef FREEBSD_PLEX86_DEBUG
	printf("cmd:0x%lx data:%p\n", cmd, data);
#endif
	ret = hostIoctlGeneric(vm, NULL, NULL, cmd, (unsigned long)data);
	if (ret < 0)
		ret = hostOSConvertPlex86Errno(ret);
#ifdef FREEBSD_PLEX86_DEBUG
	printf("ret=%d\n", ret);
#endif
	return (ret);
}

static void
register_vm(vm_t *vm, struct thread *td)
{
	struct plex86_vmentry *vp;

	vp = malloc(sizeof(struct plex86_vmentry), M_PLEX86, M_WAITOK);
	vp->vm_pid = td->td_proc->p_pid;
	vp->vm_vm = vm;

	TAILQ_INSERT_HEAD(&plex86_vmlist, vp, link);
}

static void
unregister_vm(struct thread *td)
{
	struct plex86_vmentry *vp;

	TAILQ_FOREACH(vp, &plex86_vmlist, link) {
		if (vp->vm_pid == td->td_proc->p_pid) {
			TAILQ_REMOVE(&plex86_vmlist, vp, link);
			free(vp->vm_vm, M_PLEX86);
			free(vp, M_PLEX86);
			return;
		}
	}
}

static vm_t *
find_vm(struct thread *td)
{
	struct plex86_vmentry *vp;

	TAILQ_FOREACH(vp, &plex86_vmlist, link) {
		if (vp->vm_pid == td->td_proc->p_pid)
			return (vp->vm_vm);
	}
	return (NULL);
}

static int
retrieveKernelModulePages(void)
{
	linker_file_t lf;
	vm_offset_t addr;
	int i;
	
	lf = linker_find_file_by_name("plex86");
	if (lf == NULL) {
		printf("plex86: can't find linker_file 'plex86'\n");
		return (ENXIO);
	}

	kernelModulePages.startOffset = (Bit32u)lf->address;
	kernelModulePages.startOffsetPageAligned =
	    (Bit32u)trunc_page((vm_offset_t)lf->address);
	kernelModulePages.nPages = atop(round_page((vm_offset_t)lf->address + 
	    lf->size) - kernelModulePages.startOffsetPageAligned);

	if (kernelModulePages.nPages > Plex86MaxKernelModulePages)
		return (ENXIO);

	addr = kernelModulePages.startOffset;
	for (i = 0; i < kernelModulePages.nPages; i++) {
		kernelModulePages.ppi[i] = atop(vtophys((void *)addr));
		addr += PAGE_SIZE;
	}

	return (0);
}

int
plex86_modevent(module_t mod, int type, void *data)
{
	switch (type) {
	case MOD_LOAD:
		bzero(&kernelModulePages, sizeof(kernelModulePages));

		TAILQ_INIT(&plex86_vmlist);

		if (retrieveKernelModulePages() != 0) {
			printf("plex86: retrieveKernelModulePages returned error\n");
			return (ENXIO);
		}
	
		if (hostModuleInit() == 0) {
			printf("plex86: hostModuleInit error\n");
			return (ENXIO);
		}
	
		plex86_dev = make_dev(&plex86_cdevsw, 0 /* minor */ , UID_ROOT,
		    GID_WHEEL, 0600, "plex86");
	
		printf("plex86: Module loaded.\n");
		break;
	case MOD_UNLOAD:
		if (plex86sc.sc_open != 0)
			return (EBUSY);

		if (plex86_dev != 0)
			destroy_dev(plex86_dev);

		printf("plex86: Module unloaded.\n");
		break;
	}
	return (0);
}

static moduledata_t plex86_mod = {
	"plex86",
	plex86_modevent,
	0
};

DECLARE_MODULE(plex86, plex86_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);

#ifndef PLEX86_NO_LINUX

#include <sys/sysproto.h>
#include <machine/../linux/linux.h>
#include <machine/../linux/linux_proto.h>

MODULE_DEPEND(DRIVER_NAME, linux, 1, 1, 1);

static linux_ioctl_function_t plex86_linux_ioctl;
static struct linux_ioctl_handler plex86_linux_handler = {plex86_linux_ioctl, 
    0x6b00, 0x6b0f};

SYSINIT(plex86_linux_register, SI_SUB_KLD, SI_ORDER_MIDDLE, 
    linux_ioctl_register_handler, &plex86_linux_handler);
SYSUNINIT(plex86_linux_unregister, SI_SUB_KLD, SI_ORDER_MIDDLE, 
    linux_ioctl_unregister_handler, &plex86_linux_handler);

static u_int32_t dirbits[4] = { IOC_VOID, IOC_IN, IOC_OUT, IOC_INOUT };

#define	SETDIR(l, b)	(b) = ((l) & ~IOC_DIRMASK) | dirbits[(l) >> 30]

static int
plex86_linux_ioctl(struct thread *td, struct linux_ioctl_args* args)
{
	int error;
	int cmd = args->cmd;

	SETDIR(cmd, args->cmd);
	
	error = ioctl(td, (struct ioctl_args *)args);

	return error;
}

#endif /* !PLEX86_NO_LINUX */

/************************************************************************
 * The requisite host-specific functions.  An implementation of each of
 * these functions needs to be offered for each host-XYZ.c file.
 ************************************************************************/

/* Give CPU time to something else, and return nonzero if there are no signals
 * pending
 */
unsigned int
hostOSIdle(void)
{
	int ret;

	ret = tsleep(&idlechan, PZERO | PCATCH, "vmidle", 1);
	return (ret != EINTR);
}

/* Allocate size bytes of page-aligned, zeroed, wired memory */
void *
hostOSAllocZeroedMem(unsigned long size)
{
	if (size <= (PAGE_SIZE / 2))
		size = PAGE_SIZE;
	return (malloc(size, M_PLEX86, M_ZERO | M_WAITOK));
}

void
hostOSFreeMem(void *ptr)
{
	free(ptr, M_PLEX86);
}

void *
hostOSAllocZeroedPage(void)
{
	return (malloc(PAGE_SIZE, M_PLEX86, M_ZERO | M_WAITOK));
}

void
hostOSFreePage(void *ptr)
{
	free(ptr, M_PLEX86);
}

unsigned int
hostOSGetAllocedMemPhyPages(Bit32u *page, int max_pages, void *ptr,
    unsigned size)
{
	vm_offset_t addr = (vm_offset_t)ptr;
	int i;
	
	if (addr == NULL) {
		printf("plex86: hostOSGetAllocedMemPhyPages: NULL addr!\n");
		return (0);
	}
	if ((addr & PAGE_MASK) != 0) {
		printf("plex86: hostOSGetAllocedMemPhyPages: not page aligned!\n");
		return (0);
	}

	for (i = 0; i < atop(round_page(size)); i++) {
		page[i] = atop(vtophys((void *)addr));
		addr += PAGE_SIZE;
	}

	return (round_page(size));
}

Bit32u
hostOSGetAllocedPagePhyPage(void *addr)
{
	if (addr == NULL)
		return (NULL);
	return (atop(vtophys(addr)));
}

void
hostOSKernelPrint(char *fmt, ...)
{
	va_list args;
	int ret;
	unsigned char buffer[256];

	va_start(args, fmt);
	ret = vsnprintf(buffer, 256, fmt, args);
	if (ret == -1)
		printf("plex86: hostprint: vsnprintf returns error.\n");
	else
		printf("plex86: %s\n", buffer);
}


void
hostOSUserPrint(vm_t *vm, char *fmt, ...)
{
	va_list args;
	int ret;
	unsigned char buffer[256];

	/* We can not print to the user buffer given the current
	 * conditions.  Print using the kernel services instead.
	 */
	va_start(args, fmt);
	ret = vsnprintf(buffer, 256, fmt, args);
	if (ret == -1)
		printf("plex86: hostprint: vsnprintf returns error.\n");
	else
		printf("plex86: %s\n", buffer);
}

int
hostOSConvertPlex86Errno(unsigned ret)
{
	switch (-ret) {
	case 0:			return (0);
	case Plex86ErrnoEBUSY:	return (EBUSY);
	case Plex86ErrnoENOMEM:	return (ENOMEM);
	case Plex86ErrnoEFAULT:	return (EFAULT);
	case Plex86ErrnoEINVAL:	return (EINVAL);
	case Plex86ErrnoEACCES:	return (EACCES);
	case Plex86ErrnoEAGAIN:	return (EAGAIN);
	default:
		printf("unknown error\n");
		return (EINVAL);
	}
}


Bit32u
hostOSKernelOffset(void)
{
	return (0);
}

void
hostOSModuleCountReset(vm_t *vm, void *inode, void *filp)
{
}

unsigned long
hostOSCopyFromUser(void *to, void *from, unsigned long len)
{
	return (copyin(from, to, len));
}

unsigned long
hostOSCopyToUser(void *to, void *from, unsigned long len)
{
	return (copyout(from, to, len));
}

unsigned long
hostOSCopyFromUserIoctl(void *to, void *from, unsigned long len)
{
	memcpy(to, from, len);
	return (0);
}

unsigned long
hostOSCopyToUserIoctl(void *to, void *from, unsigned long len)
{
	memcpy(to, from, len);
	return (0);
}

Bit32u
hostOSGetAndPinUserPage(vm_t *vm, Bit32u userAddr, void **osSpecificPtr,
    Bit32u *ppi, Bit32u *kernelAddr)
{
	vm_page_t page;
	vm_paddr_t pa;
	vm_offset_t kaddr;
	int result;
	struct pmap *pmap = &curproc->p_vmspace->vm_pmap;

	if ((userAddr & PAGE_MASK) != NULL) {
		printf("plex86: bad userAddr\n");
		return 0;
	}

	if (vm_fault_quick((void *)userAddr, VM_PROT_ALL) < 0) {
		printf("plex86: vm_fault_quick failed.\n");
		return 0;
	}

	pa = pmap_extract(pmap, (vm_offset_t)userAddr);

	page = PHYS_TO_VM_PAGE(pa);
	vm_page_lock_queues();
	vm_page_wire(page);
	vm_page_unlock_queues();
	
	if (kernelAddr != NULL) {
		kaddr = vm_map_min(kmem_map);
		result = vm_map_find(kmem_map, NULL, 0, &kaddr, PAGE_SIZE, TRUE,
		    VM_PROT_ALL, VM_PROT_ALL, 0);
		if (result != KERN_SUCCESS) {
			vm_page_lock_queues();
			vm_page_unwire(page, 0);
			vm_page_unlock_queues();
			printf("plex86: hostGetAndPinUserPages: failed.\n");
			return (0);
		}
		pmap_qenter(kaddr, &page, 1);
		*kernelAddr = kaddr;
	}

	*osSpecificPtr = page;
	*ppi = atop(VM_PAGE_TO_PHYS(page));

	return (1); /* OK. */
}

void
hostOSUnpinUserPage(vm_t *vm, Bit32u userAddr, void *osSpecificPtr,
                          Bit32u ppi, Bit32u *kernelAddr, unsigned dirty)
{
	vm_page_t page;
	
	page = osSpecificPtr;

	if (dirty)
		vm_page_dirty(page);
	vm_page_lock_queues();
	vm_page_unwire(page, 0);
	vm_page_unlock_queues();

	if (kernelAddr != NULL) {
		pmap_qremove((vm_offset_t)*kernelAddr, 1);
		(void) vm_map_remove(kmem_map, trunc_page((vm_offset_t)kernelAddr),
		    round_page((vm_offset_t)*kernelAddr + PAGE_SIZE));
	}
}

void
hostOSInstrumentIntRedirCount(unsigned interruptVector)
{
	/*atomic_inc( &interruptRedirCount[interruptVector] );*/
}
