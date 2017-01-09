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

#ifndef nitems
#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

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

int		 dwarf_dump(char *, size_t, uint8_t);
int		 dump_cu(struct dwcu *);
void		 dump_dav(struct dwaval *, size_t, size_t);

/* elf.c */
int		 iself(const char *, size_t);
int		 elf_getshstab(const char *, size_t, const char **, size_t *);
ssize_t		 elf_getsymtab(const char *, const char *, size_t,
		     const Elf_Sym **, size_t *);
ssize_t		 elf_getsection(char *, const char *, const char *,
		     size_t, const char **, size_t *);

uint64_t	 dav2val(struct dwaval *, size_t);
const char	*dav2str(struct dwaval *);
const char	*enc2name(unsigned short);
const char	*lang2name(unsigned short);
const char	*inline2name(unsigned short);

__dead void
usage(void)
{
	fprintf(stderr, "usage: %s [-ai] [file ...]\n",
	    getprogname());
	exit(1);
}

int
main(int argc, char *argv[])
{
	const char *filename;
	uint8_t flags = 0;
	int ch, error = 0;

	setlocale(LC_ALL, "");

	while ((ch = getopt(argc, argv, "ai")) != -1) {
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

	if (argc <= 0)
		usage();

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
	if ((uintmax_t)st.st_size > SIZE_MAX) {
		warnx("file too big to fit memory");
		return 1;
	}

	p = mmap(NULL, st.st_size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
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
dwarf_dump(char *p, size_t filesize, uint8_t flags)
{
	Elf_Ehdr		*eh = (Elf_Ehdr *)p;
	Elf_Shdr		*sh;
	const Elf_Sym		*symtab;
	const char		*shstab, *infobuf, *abbuf;
	size_t			 nsymb, infolen, ablen;
	size_t			 i, shstabsz;

	/* Find section header string table location and size. */
	if (elf_getshstab(p, filesize, &shstab, &shstabsz))
		return 1;

	/* Find abbreviation location and size. */
	if (elf_getsection(p, DEBUG_ABBREV, shstab, shstabsz, &abbuf,
	    &ablen) == -1) {
		warnx("%s section not found", DEBUG_ABBREV);
		return 1;
	}

	if (elf_getsection(p, DEBUG_INFO, shstab, shstabsz, &infobuf,
	    &infolen) == -1) {
		warnx("%s section not found", DEBUG_INFO);
		return 1;
	}

	/* Find string table location and size. */
	if (elf_getsection(p, DEBUG_STR, shstab, shstabsz, &dstrbuf,
	    &dstrlen) == -1)
		warnx("%s section not found", DEBUG_STR);


	if (flags & DUMP_ABBREV) {
		struct dwbuf	 abbrev = { .buf = abbuf, .len = ablen };
		struct dwabbrev_queue dabq = SIMPLEQ_HEAD_INITIALIZER(dabq);

		printf("Contents of the %s section:\n\n", DEBUG_ABBREV);
		while (dw_ab_parse(&abbrev, &dabq) == 0) {
			struct dwabbrev *dab;

 			printf("  Number TAG\n");
			SIMPLEQ_FOREACH(dab, &dabq, dab_next) {
				struct dwattr *dat;

				printf("   %llu      %s    [%s children]\n",
				    dab->dab_code, dw_tag2name(dab->dab_tag),
				    (dab->dab_children) ? "has" : "no");

				SIMPLEQ_FOREACH(dat, &dab->dab_attrs, dat_next){
					printf("    %-18s %s\n",
					    dw_at2name(dat->dat_attr),
					    dw_form2name(dat->dat_form));
				}
			}

			dw_dabq_purge(&dabq);
		}

	}

	if (flags & DUMP_INFO) {
		struct dwbuf	 info = { .buf = infobuf, .len = infolen };
		struct dwbuf	 abbrev = { .buf = abbuf, .len = ablen };
		struct dwcu	*dcu = NULL;

		printf("The section %s contains:\n\n", DEBUG_INFO);
		while (dw_cu_parse(&info, &abbrev, infolen, &dcu) == 0) {
			dump_cu(dcu);
			dw_dcu_free(dcu);
		}
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

		SIMPLEQ_FOREACH(dav, &die->die_avals, dav_next)
			dump_dav(dav, dcu->dcu_psize, dcu->dcu_offset);
	}

	return 0;
}

void
dump_dav(struct dwaval *dav, size_t psz, size_t offset)
{
	uint64_t attr = dav->dav_dat->dat_attr;
	uint64_t form = dav->dav_dat->dat_form;
	uint64_t val = 0;
	const char *str = NULL;

	printf("     %-18s: ", dw_at2name(attr));

	val = dav2val(dav, psz);
	str = dav2str(dav);
	if (val == (uint64_t)-1 && str == NULL) {
		printf("%s: %llu\n", dw_form2name(form), form);
		return;
	}

	switch (attr) {
	case DW_AT_producer:
	case DW_AT_name:
	case DW_AT_comp_dir:
		switch (form) {
		case DW_FORM_string:
			printf("%s", str);
			break;
		case DW_FORM_strp:
			printf("(indirect string, offset:"
			    " 0x%llx): %s", val, str);
			break;
		default:
			printf(" %s", dw_form2name(form));
			break;
		}
		break;
	case DW_AT_byte_size:
	case DW_AT_decl_file:
	case DW_AT_decl_line:
	case DW_AT_upper_bound:
	case DW_AT_prototyped:
	case DW_AT_external:
	case DW_AT_declaration:
	case DW_AT_call_file:
	case DW_AT_call_line:
		printf("%llu", val);
		break;
	case DW_AT_inline:
		printf("%llu\t(%s)", val, inline2name(val));
		break;
	case DW_AT_stmt_list:
	case DW_AT_low_pc:
	case DW_AT_high_pc:
	case DW_AT_ranges:
		printf("0x%llx", val);
		break;
	case DW_AT_language:
		printf("%llu\t(%s)", val, lang2name(val));
		break;
	case DW_AT_encoding:
		printf("%llu\t(%s)", val, enc2name(val));
		break;
	case DW_AT_location:
	case DW_AT_frame_base:
	case DW_AT_data_member_location:
		switch (form) {
		case DW_FORM_block1:
		case DW_FORM_block2:
		case DW_FORM_block4:
		case DW_FORM_block:
			printf("%llu byte block", val);
			break;
		case DW_FORM_data1:
		case DW_FORM_data2:
		case DW_FORM_data4:
		case DW_FORM_data8:
			printf("0x%llx\t(location list)", val);
			break;
		default:
			printf("%s", dw_form2name(form));
			break;
		}
		break;
	case DW_AT_type:
	case DW_AT_sibling:
	case DW_AT_abstract_origin:
		printf("<%llx>", val + offset);
		break;
	default:
		printf("unimplemented");
		break;
	}
	printf("\n");
}

uint64_t
dav2val(struct dwaval *dav, size_t psz)
{
	uint64_t val = (uint64_t)-1;

	switch (dav->dav_dat->dat_form) {
	case DW_FORM_addr:
	case DW_FORM_ref_addr:
		if (psz == sizeof(uint32_t))
			val = dav->dav_u32;
		else
			val = dav->dav_u64;
		break;
	case DW_FORM_block1:
	case DW_FORM_block2:
	case DW_FORM_block4:
	case DW_FORM_block:
		val = dav->dav_buf.len;
		break;
	case DW_FORM_flag:
	case DW_FORM_data1:
	case DW_FORM_ref1:
		val = dav->dav_u8;
		break;
	case DW_FORM_data2:
	case DW_FORM_ref2:
		val = dav->dav_u16;
		break;
	case DW_FORM_data4:
	case DW_FORM_ref4:
		val = dav->dav_u32;
		break;
	case DW_FORM_data8:
	case DW_FORM_ref8:
		val = dav->dav_u64;
		break;
	case DW_FORM_strp:
		val = dav->dav_u32;
		break;
	case DW_FORM_flag_present:
		val = 1;
		break;
	default:
		break;
	}

	return val;
}

const char *
dav2str(struct dwaval *dav)
{
	const char *str = NULL;

	switch (dav->dav_dat->dat_form) {
	case DW_FORM_string:
		str = dav->dav_str;
		break;
	case DW_FORM_strp:
		str = dstrbuf + dav->dav_u32;
		break;
	default:
		break;
	}

	return str;
}

const char *
enc2name(unsigned short enc)
{
	static const char *enc_name[] = { "address", "boolean", "complex float",
	    "float", "signed", "signed char", "unsigned", "unsigned char",
	    "imaginary float", "packed decimal", "numeric string", "edited",
	    "signed fixed", "unsigned fixed", "decimal float" };

	if (enc > 0 && enc <= nitems(enc_name))
		return enc_name[enc - 1];

	return "invalid";
}

const char *
lang2name(unsigned short lang)
{
	static const char *lang_name[] = { "ANSI C", "C", "Ada83", "C++",
	    "Cobol74", "Cobol85", "Fortran77", "Fortran90", "Pascal83",
	    "Modula2", "Java", "C99", "Ada95", "Fortran95", "PLI", "ObjC",
	    "ObjC++", "UPC", "D" };

	if (lang > 0 && lang <= nitems(lang_name))
		return lang_name[lang - 1];

	return "invalid";
}

const char *
inline2name(unsigned short inl)
{
	switch (inl) {
	case DW_INL_not_inlined:
		return "not inlined";
	case DW_INL_inlined:
		return "inlined";
	case DW_INL_declared_not_inlined:
		return "declared as inlined and not inlined";
	case DW_INL_declared_inlined:
		return "declared as inline and inlined";
	default:
		return "invalid";
	}
}
