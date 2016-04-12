/*
 * Copyright (c) 2016 Martin Pieuchot
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/exec_elf.h>
#include <sys/mman.h>
#include <sys/queue.h>

#include <err.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dwarf.h"

#include "dw.h"

#define DEBUG_ABBREV	".debug_abbrev"
#define DEBUG_INFO	".debug_info"
#define DEBUG_LINE	".debug_line"
#define DEBUG_STR	".debug_str"

#define DUMP_ABBREV	(1 << 0)
#define DUMP_INFO	(1 << 1)
#define DUMP_LINE	(1 << 2)
#define DUMP_STR	(1 << 3)

int		 dump(const char *, uint8_t);
__dead void	 usage(void);

int		 dwarf_dump(const char *, size_t, uint8_t);
int		 dump_cu(struct dwcu *);

/* elf.c */
int		 iself(const char *, size_t);
int		 elf_getshstrtab(const char *, size_t, const char **, size_t *);
int		 elf_getsymtab(const char *, const char *, size_t,
		     const Elf_Sym **, size_t *);
int		 elf_getsection(const char *, const char *, const char *,
		     size_t, const char **, size_t *);

__dead void
usage(void)
{
	extern char		*__progname;

	fprintf(stderr, "usage: %s [-ails] [file ...]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	const char *filename;
	uint8_t flags = 0;
	int ch, error = 0;

	setlocale(LC_ALL, "");

	while ((ch = getopt(argc, argv, "ails")) != -1) {
		switch (ch) {
		case 'a':
			flags |= DUMP_ABBREV;
			break;
		case 'i':
			flags |= DUMP_INFO;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	/* Dump everything by default */
	if (flags == 0)
		flags = 0xff;

	while ((filename = *argv++) != NULL)
		error |= dump(filename, flags);

	return error;
}

int
dump(const char *path, uint8_t flags)
{
	struct stat		 st;
	int			 fd, error = 1;
	char			*p;

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		warn("open");
		return 1;
	}
	if (fstat(fd, &st) == -1) {
		warn("fstat");
		return 1;
	}
	if (st.st_size < (off_t)sizeof(Elf_Ehdr)) {
		warnx("file too small to be ELF");
		return 1;
	}
	if ((uintmax_t)st.st_size > SIZE_MAX) {
		warnx("file too big to fit memory");
		return 1;
	}

	p = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (p == MAP_FAILED)
		err(1, "mmap");

	if (iself(p, st.st_size))
		error = dwarf_dump(p, st.st_size, flags);

	munmap(p, st.st_size);
	close(fd);

	return error;
}

const char		*dstrbuf;
size_t			 dstrlen;

int
dwarf_dump(const char *p, size_t filesize, uint8_t flags)
{
	Elf_Ehdr		*eh = (Elf_Ehdr *)p;
	Elf_Shdr		*sh;
	const char		*shstrtab;
	const char		*infobuf, *abbrevbuf;
	size_t			 infolen, abbrevlen;
	size_t			 i, shstrtabsize;

	/* Find section header string table location and size. */
	if (elf_getshstrtab(p, filesize, &shstrtab, &shstrtabsize))
		return 1;

	/* Find abbreviation location and size. */
	if (elf_getsection(p, DEBUG_ABBREV, shstrtab, shstrtabsize, &abbrevbuf,
	    &abbrevlen)) {
		warnx("%s section not found", DEBUG_ABBREV);
		return 1;
	}

	if (elf_getsection(p, DEBUG_INFO, shstrtab, shstrtabsize, &infobuf,
	    &infolen)) {
		warnx("%s section not found", DEBUG_INFO);
		return 1;
	}

	/* Find string table location and size. */
	if (elf_getsection(p, DEBUG_STR, shstrtab, shstrtabsize, &dstrbuf,
	    &dstrlen))
		warnx("%s section not found", DEBUG_STR);



	if (flags & DUMP_ABBREV) {
		struct dwabbrev_queue dabq;
		struct dwabbrev *dab;

		SIMPLEQ_INIT(&dabq);
		if (dw_abbrev_parse(abbrevbuf, abbrevlen, &dabq))
			return 1;

		SIMPLEQ_FOREACH(dab, &dabq, dab_next) {
			struct dwattr *dat;

			printf("[%llu] %s\t\t[%s children]\n", dab->dab_code,
			    dw_tag2name(dab->dab_tag),
			    (dab->dab_children) ? "has" : "no");

			SIMPLEQ_FOREACH(dat, &dab->dab_attrs, dat_next) {
				printf("      %s\t%s\n",
				    dw_at2name(dat->dat_attr),
				    dw_form2name(dat->dat_form));
			}
		}

		dw_dabq_purge(&dabq);

	}

	if (flags & DUMP_INFO) {
		struct dwcu_queue	 dcuq;
		struct dwcu		*dcu;

		SIMPLEQ_INIT(&dcuq);
		if (dw_info_parse(infobuf, infolen, abbrevbuf, abbrevlen,
		    &dcuq))
			return 1;

		printf("The section %s contains:\n\n", DEBUG_INFO);
		SIMPLEQ_FOREACH(dcu, &dcuq, dcu_next) {
			dump_cu(dcu);
		}


		dw_dcuq_purge(&dcuq);
	}

	return 0;
}

int
dump_cu(struct dwcu *dcu)
{
	struct dwdie *die;
	struct dwaval *dav;

	printf("  Compilation Unit @ offset 0x%zx:\n", dcu->dcu_offset);
	printf("   Length:        %llu\n", dcu->dcu_length);
	printf("   Version:       %u\n", dcu->dcu_version);
	printf("   Abbrev Offset: %llu\n", dcu->dcu_abbroff);
	printf("   Pointer Size:  %u\n", dcu->dcu_psize);

	SIMPLEQ_FOREACH(die, &dcu->dcu_dies, die_next) {
		printf(" <%u><%lx>: Abbrev Number: %lld (%s)\n", die->die_lvl,
		    die->die_offset, die->die_dab->dab_code,
		    dw_tag2name(die->die_dab->dab_tag));

		SIMPLEQ_FOREACH(dav, &die->die_avals, dav_next) {
			uint64_t form = dav->dav_dat->dat_form;

			printf("     %-18s: ",
			    dw_at2name(dav->dav_dat->dat_attr));

			switch (form) {
			case DW_FORM_addr:
			case DW_FORM_ref_addr:
				if (dcu->dcu_psize == sizeof(uint32_t))
					printf("0x%x\n", dav->dav_u32);
				else
					printf("0x%llx\n", dav->dav_u64);
				break;
			case DW_FORM_block1:
			case DW_FORM_block2:
			case DW_FORM_block4:
			case DW_FORM_block:
				printf("%zu byte block\n", dav->dav_buf.len);
				break;
			case DW_FORM_flag:
			case DW_FORM_data1:
				printf("%u\n", dav->dav_u8);
				break;
			case DW_FORM_data2:
				printf("%u\n", dav->dav_u16);
				break;
			case DW_FORM_data4:
				printf("%u\n", dav->dav_u32);
				break;
			case DW_FORM_data8:
				printf("%llu\n", dav->dav_u64);
				break;
			case DW_FORM_ref1:
				printf("<%x>\n", dav->dav_u8);
				break;
			case DW_FORM_ref2:
				printf("<%x>\n", dav->dav_u16);
				break;
			case DW_FORM_ref4:
				printf("<%x>\n", dav->dav_u32);
				break;
			case DW_FORM_ref8:
				printf("<%llx>\n", dav->dav_u64);
				break;
			case DW_FORM_string:
				printf("%s\n", dav->dav_str);
				break;
			case DW_FORM_strp:
				printf("(indirect string, offset: 0x%x): %s\n",
				    dav->dav_u32, dstrbuf + dav->dav_u32);
				break;
			case DW_FORM_flag_present:
				printf("1\n");
			default:
				printf("%s\n", dw_form2name(form));
				break;
			}
		}
	}

	return 0;
}

#if 0
void
dump_type(struct dwcu_queue *dcuq)
{
	struct dwcu *dcu;
	struct dwdie *die;
	struct dwaval *dav;

	SIMPLEQ_FOREACH(dcu, dcuq, dcu_next) {
		int i = 0;

		SIMPLEQ_FOREACH(die, &dcu->dcu_dies, die_next) {
			const char *name = NULL;
			uint8_t bits = 0, encoding = 0;
			size_t off = 0, ref = 0;

			if (die->die_dab->dab_tag != DW_TAG_base_type &&
			    die->die_dab->dab_tag != DW_TAG_typedef)
			    	continue;

			SIMPLEQ_FOREACH(dav, &die->die_avals, dav_next) {
				switch(dav->dav_dat->dat_attr) {
				case DW_AT_byte_size:
					bits = dav->dav_u8;
					break;
				case DW_AT_encoding:
					encoding = dav->dav_u8;
					break;
				case DW_AT_type:
					ref = dav->dav_u32;
					break;
				case DW_AT_name:
					off = dav->dav_u32;
					name = dstr.buf + dav->dav_u32;
					break;
				default:
					break;
				}
			}

			if (die->die_dab->dab_tag == DW_TAG_base_type) {
				printf("  <%d> %s encoding=0x%x offset=%zu "
				    "bits=%u\n", ++i, name, encoding,
				    off, bits);
			} else {
				printf("  <%d> typedef %s refers to %zu\n",
				    ++i, name, ref);
			}
		}
	}
}
#endif
