
#include <stdlib.h>
#include "alloc.h"
#include "utils.h"

struct _link {
  struct _link *next;
};

struct _fixedpool {
  struct _link *allocated;
  struct _link *nextfree;
  size_t size;
  size_t grownum;
};


fixedpool fixedpool_create (size_t size, size_t grownum)
{
  fixedpool p = (fixedpool) xmalloc (sizeof (struct _fixedpool));
  if (size < sizeof (struct _link)) size = sizeof (struct _link);
  p->size = size;
  p->grownum = grownum;
  p->allocated = NULL;
  p->nextfree = NULL;
  return p;
}

void fixedpool_destroy (fixedpool p)
{
  struct _link *ptr, *nptr;
  for (ptr = p->allocated; ptr; ptr = nptr) {
    nptr = ptr->next;
    free (ptr);
  }
  p->allocated = NULL;
  p->nextfree = NULL;
  free (p);
}

void fixedpool_grow (fixedpool p, void *ptr, size_t size)
{
  char *cptr;
  struct _link *l;
  size_t count;

  if (size < 2 * p->size) {
    free (ptr);
    return;
  }
  l = ptr;
  l->next = p->allocated;
  p->allocated = l;

  cptr = ptr;
  cptr += p->size;
  count = 2 * p->size;

  while (count <= size) {
    l = (struct _link *) cptr;
    l->next = p->nextfree;
    p->nextfree = l;
    cptr += p->size;
    count += p->size;
  }
}

void *fixedpool_alloc (fixedpool p)
{
  struct _link *l;
  if (!p->nextfree) {
    size_t size;
    void *ptr;

    size = (p->grownum + 1) * p->size;
    ptr = xmalloc (size);
    fixedpool_grow (p, ptr, size);
  }
  l = p->nextfree;
  p->nextfree = l->next;
  return (void *) l;
}

void fixedpool_free (fixedpool p, void *ptr)
{
  struct _link *l = ptr;
  l->next = p->nextfree;
  p->nextfree = l;
}


