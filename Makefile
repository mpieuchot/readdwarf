
PROG=		readdwarf
SRCS=		readdwarf.c elf.c dw.c

CFLAGS+=	-W -Wall -Wstrict-prototypes -Wno-unused -Wunused-variable


.include <bsd.prog.mk>
