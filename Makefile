
PROG=		readdwarf
SRCS=		readdwarf.c elf.c dw.c

CFLAGS+=	-W -Wall -Wno-unused -Wstrict-prototypes -Wno-unused-parameter

.include <bsd.prog.mk>
