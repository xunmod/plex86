SRCS=		freebsd.c host-monitor.c fault.c monitor.c paging.c panic.c \
		print.c x86-emu.c util-nexus.c io.c
OBJS=		nexus.o
OCS=		${SRCS:.c=.o}

KMOD=		plex86
KCFLAGS=	-D_KERNEL -DKLD_MODULE -nostdinc -I- -I../include \
		-I../../include -I. -I@ -I@/dev -I@/../include \
		-I/usr/include 
KLDFLAGS=	-Bshareable  -d -warn-common
KLDLDFLAGS=	-d -warn-common -r
KCLEANFILES=	export_syms ${KMOD}.ko ${KMOD}.kld machine sym.tmp tmp.o
CLEANFILES=	host-monitor.c fault.c monitor.c paging.c panic.c print.c \
		x86-emu.c util-nexus.c nexus.S io.c @ machine

.SUFFIXES:	.c.o

.c.o:
	${CC} ${CFLAGS} ${KCFLAGS} -c $<

all:	links ${KMOD}.kld
	awk -f /sys/conf/kmod_syms.awk ${KMOD}.kld  /u/fn/plex86/host-kernel/freebsd/export_syms |  xargs -J% objcopy % plex86.kld
	${LD} ${KLDFLAGS} -o ${KMOD}.ko ${KMOD}.kld

${KMOD}.kld:	${OCS} ${OBJS}
	${LD} ${KLDLDFLAGS} -o $@ ${OCS} ${OBJS}
	touch ${CURDIR}/export_syms

nexus.o:	nexus.S
	${CC} -c -x assembler-with-cpp ${CFLAGS} ${KCFLAGS} $<

host-monitor.c:
	ln -s ${CURDIR}/../vmm/host-space/monitor.c host-monitor.c

fault.c monitor.c paging.c panic.c print.c x86-emu.c io.c:
	ln -s ${CURDIR}/../vmm/monitor-space/$@ $@

nexus.S util-nexus.c:
	ln -s ${CURDIR}/../vmm/nexus-space/$@ $@

links:
	ln -sf /usr/src/sys @
	ln -sf /usr/src/sys/i386/include machine

clean:
	rm -f ${KCLEANFILES} ${CLEANFILES} ${OCS} ${OBJS}
