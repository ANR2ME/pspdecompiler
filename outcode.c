
#include <stdio.h>
#include <ctype.h>

#include "output.h"
#include "utils.h"

static
void ident_line (FILE *out, int size)
{
  int i;
  for (i = 0; i < size; i++)
    fprintf (out, "  ");
}

static
void print_asm (FILE *out, int ident, struct operation *op)
{
  struct location *loc;
  element el;

  ident_line (out, ident);
  fprintf (out, "__asm__ (\n");
  for (loc = op->begin; ; loc++) {
    ident_line (out, ident);
    fprintf (out, "  \"%s\"\n", allegrex_disassemble (loc->opc, loc->address, FALSE));
    if (loc == op->end) break;
  }
  ident_line (out, ident);
  fprintf (out, "  : ");

  el = list_head (op->results);
  while (el) {
    struct value *val = element_getvalue (el);
    if (el != list_head (op->results))
      fprintf (out, ", ");
    fprintf (out, "\"=r\"(");
    print_value (out, val, FALSE);
    fprintf (out, ")");
    el = element_next (el);
  }
  fprintf (out, "\n");

  ident_line (out, ident);
  fprintf (out, "  : ");
  el = list_head (op->results);
  while (el) {
    struct value *val = element_getvalue (el);
    if (el != list_head (op->results))
      fprintf (out, ", ");
    fprintf (out, "\"r\"(");
    print_value (out, val, FALSE);
    fprintf (out, ")");
    el = element_next (el);
  }

  fprintf (out, "\n");
  ident_line (out, ident);
  fprintf (out, ");\n");
}


static
void print_block (FILE *out, int ident, struct basicblock *block)
{
  element el;

  el = list_head (block->operations);
  while (el) {
    struct operation *op = element_getvalue (el);

    if (op->flushed && op->type != OP_START && op->type != OP_END &&
        op->type != OP_PHI) {
      ident_line (out, ident);
      print_operation (out, op, TRUE);
    }

    el = element_next (el);
  }
}

static
void print_subroutine (FILE *out, struct code *c, struct subroutine *sub)
{
  element el;
  int ident;

  if (sub->import) return;

  fprintf (out, "/**\n * Subroutine at address 0x%08X\n", sub->begin->address);
  fprintf (out, " */\n");
  fprintf (out, "void ");
  print_subroutine_name (out, sub);
  fprintf (out, " (void)\n{\n");

  if (sub->haserror) {
    struct location *loc;
    for (loc = sub->begin; ; loc++) {
      fprintf (out, "%s\n", allegrex_disassemble (loc->opc, loc->address, TRUE));
      if (loc == sub->end) break;
    }
  } else {
    el = list_head (sub->dfsblocks);
    while (el) {
      struct basicblock *block = element_getvalue (el);
      ident = 1;
      print_block (out, ident, block);
      fprintf (out, "\n");
      el = element_next (el);
    }
  }
  fprintf (out, "}\n\n");
}

static
void print_source (FILE *out, struct code *c, char *headerfilename)
{
  uint32 i, j;
  element el;

  fprintf (out, "#include <pspsdk.h>\n");
  fprintf (out, "#include \"%s\"\n\n", headerfilename);

  for (i = 0; i < c->file->modinfo->numimports; i++) {
    struct prx_import *imp = &c->file->modinfo->imports[i];

    fprintf (out, "/*\n * Imports from library: %s\n */\n", imp->name);
    for (j = 0; j < imp->nfuncs; j++) {
      struct prx_function *func = &imp->funcs[j];
      fprintf (out, "extern ");
      if (func->name) {
        fprintf (out, "void %s (void);\n",func->name);
      } else {
        fprintf (out, "void %s_%08X (void);\n", imp->name, func->nid);
      }
    }
    fprintf (out, "\n");
  }

  el = list_head (c->subroutines);
  while (el) {
    struct subroutine *sub;
    sub = element_getvalue (el);

    print_subroutine (out, c, sub);
    el = element_next (el);
  }

}

static
void print_header (FILE *out, struct code *c, char *headerfilename)
{
  uint32 i, j;
  char buffer[256];
  int pos = 0;

  while (pos < sizeof (buffer) - 1) {
    char c = headerfilename[pos];
    if (!c) break;
    if (c == '.') c = '_';
    else c = toupper (c);
    buffer[pos++] =  c;
  }
  buffer[pos] = '\0';

  fprintf (out, "#ifndef __%s\n", buffer);
  fprintf (out, "#define __%s\n\n", buffer);

  for (i = 0; i < c->file->modinfo->numexports; i++) {
    struct prx_export *exp = &c->file->modinfo->exports[i];

    fprintf (out, "/*\n * Exports from library: %s\n */\n", exp->name);
    for (j = 0; j < exp->nfuncs; j++) {
      struct prx_function *func = &exp->funcs[j];
      if (func->name) {
        fprintf (out, "void %s (void);\n",func->name);
      } else {
        fprintf (out, "void %s_%08X (void);\n", exp->name, func->nid);
      }
    }
    fprintf (out, "\n");
  }

  fprintf (out, "#endif /* __%s */\n", buffer);
}


int print_code (struct code *c, char *prxname)
{
  char buffer[64];
  char basename[32];
  FILE *cout, *hout;


  get_base_name (prxname, basename, sizeof (basename));
  sprintf (buffer, "%s.c", basename);

  cout = fopen (buffer, "w");
  if (!cout) {
    xerror (__FILE__ ": can't open file for writing `%s'", buffer);
    return 0;
  }

  sprintf (buffer, "%s.h", basename);
  hout = fopen (buffer, "w");
  if (!hout) {
    xerror (__FILE__ ": can't open file for writing `%s'", buffer);
    return 0;
  }


  print_header (hout, c, buffer);
  print_source (cout, c, buffer);

  fclose (cout);
  fclose (hout);
  return 1;
}