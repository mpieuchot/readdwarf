/*
 * Copyright (c) 2016 Martin Pieuchot <mpi@openbsd.org>
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
#include <sys/exec_elf.h>

#include <err.h>
#include <string.h>

int
iself(const char *p, size_t filesize)
{
	Elf_Ehdr		*eh = (Elf_Ehdr *)p;

	if (filesize < (off_t)sizeof(Elf_Ehdr)) {
		warnx("file too small to be ELF");
		return 0;
	}

	if (eh->e_ehsize < sizeof(Elf_Ehdr) || !IS_ELF(*eh))
		return 0;

	if (eh->e_ident[EI_CLASS] != ELFCLASS) {
		warnx("unexpected word size %u", eh->e_ident[EI_CLASS]);
		return 0;
	}
	if (eh->e_ident[EI_VERSION] != ELF_TARG_VER) {
		warnx("unexpected version %u", eh->e_ident[EI_VERSION]);
		return 0;
	}
	if (eh->e_ident[EI_DATA] >= ELFDATANUM) {
		warnx("unexpected data format %u", eh->e_ident[EI_DATA]);
		return 0;
	}
	if (eh->e_shoff > filesize) {
		warnx("bogus section table offset 0x%llx", (off_t)eh->e_shoff);
		return 0;
	}
	if (eh->e_shentsize < sizeof(Elf_Shdr)) {
		warnx("bogus section header size %u", eh->e_shentsize);
		return 0;
	}
	if (eh->e_shnum > (filesize - eh->e_shoff) / eh->e_shentsize) {
		warnx("bogus section header count %u", eh->e_shnum);
		return 0;
	}
	if (eh->e_shstrndx >= eh->e_shnum) {
		warnx("bogus string table index %u", eh->e_shstrndx);
		return 0;
	}

	return 1;
}

int
elf_getshstrtab(const char *p, size_t filesize, const char **shstrtab,
    size_t *shstrtabsize)
{
	Elf_Ehdr		*eh = (Elf_Ehdr *)p;
	Elf_Shdr		*sh;

	sh = (Elf_Shdr *)(p + eh->e_shoff + eh->e_shstrndx * eh->e_shentsize);
	if (sh->sh_type != SHT_STRTAB) {
		warnx("unexpected string table type");
		return 1;
	}
	if (sh->sh_offset > filesize) {
		warnx("bogus string table offset");
		return 1;
	}
	if (sh->sh_size > filesize - sh->sh_offset) {
		warnx("bogus string table size");
		return 1;
	}
	if (shstrtab != NULL)
		*shstrtab = p + sh->sh_offset;
	if (shstrtabsize != NULL)
		*shstrtabsize = sh->sh_size;

	return 0;
}

int
elf_getsymtab(const char *p, const char *shstrtab, size_t shstrtabsize,
    const Elf_Sym **symtab, size_t *nsymb)
{
	Elf_Ehdr	*eh = (Elf_Ehdr *)p;
	Elf_Shdr	*sh;
	size_t		 i;

	for (i = 0; i < eh->e_shnum; i++) {
		sh = (Elf_Shdr *)(p + eh->e_shoff + i * eh->e_shentsize);

		if (sh->sh_type != SHT_SYMTAB)
			continue;

		if ((sh->sh_link >= eh->e_shnum) ||
		    (sh->sh_name >= shstrtabsize))
			continue;

		if (strncmp(shstrtab + sh->sh_name, ELF_SYMTAB,
		    strlen(ELF_SYMTAB)) == 0) {
			if (symtab != NULL)
				*symtab = (Elf_Sym *)(p + sh->sh_offset);
			if (nsymb != NULL)
				*nsymb = (sh->sh_size / sh->sh_entsize);

			return 0;
		}
	}

	return 1;
}

int
elf_getsection(const char *p, const char *sname, const char *shstrtab,
    size_t shstrtabsize, const char **sdata, size_t *ssize)
{
	Elf_Ehdr	*eh = (Elf_Ehdr *)p;
	Elf_Shdr	*sh;
	size_t		 i;

	for (i = 0; i < eh->e_shnum; i++) {
		sh = (Elf_Shdr *)(p + eh->e_shoff + i * eh->e_shentsize);

		if ((sh->sh_link >= eh->e_shnum) ||
		    (sh->sh_name >= shstrtabsize))
			continue;

		if (strncmp(shstrtab + sh->sh_name, sname,
		    strlen(sname)) == 0) {
			if (sdata != NULL)
				*sdata = p + sh->sh_offset;
			if (ssize != NULL)
				*ssize = sh->sh_size;

			return 0;
		}
	}

	return 1;
}
