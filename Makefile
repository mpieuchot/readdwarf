
PROG=		readdwarf
SRCS=		readdwarf.c elf.c dw.c

CFLAGS+=	-Wall -Wno-unused -Werror

.include <bsd.prog.mk>
