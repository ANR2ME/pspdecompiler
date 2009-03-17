
#include <stdlib.h>
#include <string.h>

#include "prx.h"
#include "hash.h"
#include "utils.h"

#define ELF_HEADER_SIZE              52
#define ELF_SECTION_HEADER_ENT_SIZE  40
#define ELF_PROGRAM_HEADER_ENT_SIZE  32
#define ELF_PRX_FLAGS                (ELF_FLAGS_MIPS_ARCH2 | ELF_FLAGS_MACH_ALLEGREX | ELF_FLAGS_MACH_ALLEGREX)
#define PRX_MODULE_INFO_SIZE         52


static const uint8 valid_ident[] = {
  0x7F, 'E', 'L', 'F',
  0x01, /* Elf class = ELFCLASS32 */
  0x01, /* Elf data = ELFDATA2LSB */
  0x01, /* Version = EV_CURRENT */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 /* Padding */
};

static
uint32 read_uint32_le (const uint8 *bytes)
{
  uint32 r;
  r  = *bytes++;
  r |= *bytes++ << 8;
  r |= *bytes++ << 16;
  r |= *bytes++ << 24;
  return r;
}

static
uint16 read_uint16_le (const uint8 *bytes)
{
  uint16 r;
  r  = *bytes++;
  r |= *bytes++ << 8;
  return r;
}

static
int check_section_header (struct prx *p, uint32 index)
{
  struct elf_section *section = &p->sections[index];
  if (section->offset >= p->size ||
      (section->type != SHT_NOBITS && (section->size > p->size ||
      (section->offset + section->size) > p->size))) {
    error (__FILE__ ": wrong section offset/size (section %d)", index);
    return 0;
  }

  return 1;
}

static
int check_program_header (struct prx *p, uint32 index)
{
  struct elf_program *program = &p->programs[index];
  if (program->offset >= p->size ||
      program->filesz > p->size ||
      (program->offset + program->filesz) > p->size) {
    error (__FILE__ ": wrong program offset/size (program %d)", index);
    return 0;
  }
  return 1;
}

static
int check_elf_header (struct prx *p)
{
  uint32 table_size;

  if (memcmp (p->ident, valid_ident, sizeof (valid_ident))) {
    error (__FILE__ ": invalid identification for ELF/PRX");
    return 0;
  }

  if (p->type != ELF_PRX_TYPE) {
    error (__FILE__ ": not a PRX file (0x%04X)", p->type);
    return 0;
  }

  if (p->machine != ELF_MACHINE_MIPS) {
    error (__FILE__ ": machine is not MIPS (0x%04X)", p->machine);
    return 0;
  }

  if (p->version != ELF_VERSION_CURRENT) {
    error (__FILE__ ": version is not EV_CURRENT (0x%08X)", p->version);
    return 0;
  }

  if (p->ehsize != ELF_HEADER_SIZE) {
    error (__FILE__ ": wrong ELF header size (%u)", p->ehsize);
    return 0;
  }

  if ((p->flags & ELF_PRX_FLAGS) != ELF_PRX_FLAGS) {
    error (__FILE__ ": wrong ELF flags (0x%08X)", p->flags);
    return 0;
  }

  if (p->phnum && p->phentsize != ELF_PROGRAM_HEADER_ENT_SIZE) {
    error (__FILE__ ": wrong ELF program header entity size (%u)", p->phentsize);
    return 0;
  }

  if (!p->phnum) {
    error (__FILE__ ": PRX has no programs");
    return 0;
  }

  table_size = p->phentsize;
  table_size *= (uint32) p->phnum;
  if (p->phoff >= p->size ||
      table_size > p->size ||
      (p->phoff + table_size) > p->size) {
    error (__FILE__ ": wrong ELF program header table offset/size");
    return 0;
  }

  if (p->shnum && p->shentsize != ELF_SECTION_HEADER_ENT_SIZE) {
    error (__FILE__ ": wrong ELF section header entity size (%u)", p->shentsize);
    return 0;
  }

  table_size = p->shentsize;
  table_size *= (uint32) p->shnum;
  if (p->shoff >= p->size ||
      table_size > p->size ||
      (p->shoff + table_size) > p->size) {
    error (__FILE__ ": wrong ELF section header table offset/size");
    return 0;
  }

  return 1;

}

static
int load_sections (struct prx *p)
{
  struct elf_section *sections;
  uint32 idx;
  uint32 offset;

  p->sections = NULL;
  p->secbyname = hashtable_create (64, &hash_string, &hashtable_string_compare);
  if (p->shnum == 0) return 1;

  sections = xmalloc (p->shnum * sizeof (struct elf_section));
  p->sections = sections;

  offset = p->shoff;
  for (idx = 0; idx < p->shnum; idx++) {

    sections[idx].idxname = read_uint32_le (&p->data[offset]);
    sections[idx].type = read_uint32_le (&p->data[offset+4]);
    sections[idx].flags = read_uint32_le (&p->data[offset+8]);
    sections[idx].addr = read_uint32_le (&p->data[offset+12]);
    sections[idx].offset = read_uint32_le (&p->data[offset+16]);
    sections[idx].size = read_uint32_le (&p->data[offset+20]);
    sections[idx].link = read_uint32_le (&p->data[offset+24]);
    sections[idx].info = read_uint32_le (&p->data[offset+28]);
    sections[idx].addralign = read_uint32_le (&p->data[offset+32]);
    sections[idx].entsize = read_uint32_le (&p->data[offset+36]);

    sections[idx].data = &p->data[sections[idx].offset];

    if (!check_section_header (p, idx))
      return 0;

    offset += p->shentsize;
  }

  if (p->shstrndx > 0) {
    if (sections[p->shstrndx].type == SHT_STRTAB) {
      char *strings = (char *) sections[p->shstrndx].data;
      uint32 max_index = sections[p->shstrndx].size;
      if (max_index > 0) {

        if (strings[max_index - 1] != '\0') {
          error (__FILE__ ": string table section not terminated with null byte");
          return 0;
        }

        for (idx = 0; idx < p->shnum; idx++) {
          if (sections[idx].idxname < max_index) {
            sections[idx].name = &strings[sections[idx].idxname];
          } else {
            error (__FILE__ ": invalid section name");
            return 0;
          }
        }
        for (idx = 0; idx < p->shnum; idx++) {
          hashtable_insert (p->secbyname, (void *) sections[idx].name, &sections[idx]);
        }
      }
    }
  }

  return 1;
}

static
int load_programs (struct prx *p)
{
  struct elf_program *programs;
  uint32 idx;
  uint32 offset;

  programs = xmalloc (p->phnum * sizeof (struct elf_program));
  p->programs = programs;

  offset = p->phoff;
  for (idx = 0; idx < p->phnum; idx++) {
    programs[idx].type = read_uint32_le (&p->data[offset]);
    programs[idx].offset = read_uint32_le (&p->data[offset+4]);
    programs[idx].vaddr = read_uint32_le (&p->data[offset+8]);
    programs[idx].paddr = read_uint32_le (&p->data[offset+12]);
    programs[idx].filesz = read_uint32_le (&p->data[offset+16]);
    programs[idx].memsz = read_uint32_le (&p->data[offset+20]);
    programs[idx].flags = read_uint32_le (&p->data[offset+24]);
    programs[idx].align = read_uint32_le (&p->data[offset+28]);

    programs[idx].data = &p->data[programs[idx].offset];

    if (!check_program_header (p, idx))
      return 0;

    offset += p->phentsize;
  }

  return 1;
}

static
int load_module_info (struct prx *p)
{
  struct elf_section *s;
  struct prx_modinfo *info;
  s = hashtable_search (p->secbyname, PRX_MODULE_INFO, NULL);
  p->modinfo = NULL;
  if (s) {
    uint32 offset = s->offset;

    info = (struct prx_modinfo *) xmalloc (sizeof (struct prx_modinfo));
    p->modinfo = info;
    info->attributes = read_uint16_le (&p->data[offset]);
    info->version = read_uint16_le (&p->data[offset+2]);
    info->name = (const char *) &p->data[offset+4];
    info->gp = read_uint32_le (&p->data[offset+32]);
    info->libent = read_uint32_le (&p->data[offset+36]);
    info->libentbtm = read_uint32_le (&p->data[offset+40]);
    info->libstub = read_uint32_le (&p->data[offset+44]);
    info->libstubbtm = read_uint32_le (&p->data[offset+48]);
    return 1;
  }
  return 0;
}


struct prx *load_prx (const char *path)
{
  struct prx *p;
  uint8 *elf_bytes;
  size_t elf_size;
  elf_bytes = read_file (path, &elf_size);

  if (!elf_bytes) return NULL;

  if (elf_size < ELF_HEADER_SIZE) {
    error (__FILE__ ": elf size too short");
    free ((void *) elf_bytes);
    return NULL;
  }

  p = xmalloc (sizeof (struct prx));
  p->size = elf_size;
  p->data = elf_bytes;

  memcpy (p->ident, p->data, ELF_HEADER_IDENT);
  p->type = read_uint16_le (&p->data[ELF_HEADER_IDENT]);
  p->machine = read_uint16_le (&p->data[ELF_HEADER_IDENT+2]);

  p->version = read_uint32_le (&p->data[ELF_HEADER_IDENT+4]);
  p->entry = read_uint32_le (&p->data[ELF_HEADER_IDENT+8]);
  p->phoff = read_uint32_le (&p->data[ELF_HEADER_IDENT+12]);
  p->shoff = read_uint32_le (&p->data[ELF_HEADER_IDENT+16]);
  p->flags = read_uint32_le (&p->data[ELF_HEADER_IDENT+20]);
  p->ehsize = read_uint16_le (&p->data[ELF_HEADER_IDENT+24]);
  p->phentsize = read_uint16_le (&p->data[ELF_HEADER_IDENT+26]);
  p->phnum = read_uint16_le (&p->data[ELF_HEADER_IDENT+28]);
  p->shentsize = read_uint16_le (&p->data[ELF_HEADER_IDENT+30]);
  p->shnum = read_uint16_le (&p->data[ELF_HEADER_IDENT+32]);
  p->shstrndx = read_uint16_le (&p->data[ELF_HEADER_IDENT+34]);

  if (!check_elf_header (p)) {
    free_prx (p);
    return NULL;
  }

  if (!load_sections (p)) {
    free_prx (p);
    return NULL;
  }

  if (!load_programs (p)) {
    free_prx (p);
    return NULL;
  }

  if (!load_module_info (p)) {
    free_prx (p);
    return NULL;
  }

  return p;
}

static
void free_sections (struct prx *p)
{
  if (p->sections)
    free (p->sections);
  p->sections = NULL;
  if (p->secbyname)
    hashtable_destroy (p->secbyname, NULL);
  p->secbyname = NULL;
}

static
void free_programs (struct prx *p)
{
  if (p->programs)
    free (p->programs);
  p->programs = NULL;
}

static
void free_module_info (struct prx *p)
{
  if (p->modinfo)
    free (p->modinfo);
  p->modinfo = NULL;
}

void free_prx (struct prx *p)
{
  free_sections (p);
  free_programs (p);
  free_module_info (p);
  if (p->data)
    free ((void *) p->data);
  p->data = NULL;
  free (p);
}

static
void print_section (struct prx *p, uint32 idx)
{
  struct elf_section *section = &p->sections[idx];
  const char *type;

  switch (section->type) {
  case SHT_DYNAMIC: type = "DYNAMIC"; break;
  case SHT_NOBITS: type = "NOBITS"; break;
  case SHT_PRXRELOC: type = "PRXRELOC"; break;
  case SHT_STRTAB: type = "STRTAB"; break;
  case SHT_PROGBITS: type = "PROGBITS"; break;
  case SHT_NULL: type = "NULL"; break;
  default: type = "";
  }
  report ("  [%2d] %28s %10s %08X %08X %08X\n",
      idx, section->name, type, section->addr, section->offset, section->size);
}

static
void print_program (struct prx *p, uint32 idx)
{
  struct elf_program *program = &p->programs[idx];
  const char *type;

  switch (program->type) {
  case PT_LOAD: type = "LOAD"; break;
  default: type = "";
  }
  report ("  %10s 0x%08X 0x%08X 0x%08X 0x%08X\n",
      type, program->offset, program->vaddr, program->paddr, program->filesz);
}

void print_prx (struct prx *p)
{
  uint32 idx;
  report ("ELF header:\n");
  report ("  Entry point address:        0x%08X\n", p->entry);
  report ("  Start of program headers:   0x%08X\n", p->phoff);
  report ("  Start of section headers:   0x%08X\n", p->shoff);
  report ("  Number of programs:           %8d\n", p->phnum);
  report ("  Number of sections:           %8d\n", p->shnum);

  report ("\nSection Headers:\n");
  report ("  [Nr]            Name                  Type    Addr      Off      Size   ES Flg Lk Inf Al\n");

  for (idx = 0; idx < p->shnum; idx++) {
    print_section (p, idx);
  }

  report ("\nProgram Headers:\n");
  report ("  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align\n");

  for (idx = 0; idx < p->phnum; idx++) {
    print_program (p, idx);
  }

  report ("\n");
}
