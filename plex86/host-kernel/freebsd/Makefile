# FreeBSD Kernel module makefile

SRCS=	freebsd.c host-monitor.c fault.c monitor.c paging.c panic.c print.c \
	x86-emu.c util-nexus.c
OBJS=	nexus.o

KMOD=	plex86
NOMAN=	yes
CFLAGS+=	-I../include -I../../include
CLEANFILES+=	host-monitor.c fault.c monitor.c paging.c panic.c print.c \
		x86-emu.c util-nexus.c nexus.S

nexus.o: nexus.S
	${CC} -c -x assembler-with-cpp ${CFLAGS} ${.IMPSRC} -o ${.TARGET}

host-monitor.c:
	ln -s ${.CURDIR}/../vmm/host-space/monitor.c host-monitor.c

fault.c monitor.c paging.c panic.c print.c x86-emu.c:
	ln -s ${.CURDIR}/../vmm/monitor-space/${.TARGET} ${.TARGET}

nexus.S util-nexus.c:
	ln -s ${.CURDIR}/../vmm/nexus-space/${.TARGET} ${.TARGET}

.include <bsd.kmod.mk>
