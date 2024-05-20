#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "list.h"
#include "mydhcp.h"

// edit freelist
struct freelist *free_search()
{
    struct freelist *p;

    for (p = fhead.free_fp; p != &fhead; p = p->free_fp)
    {
        if (p != NULL)
        {
            return p;
        }
    }
    return NULL;
}

void insert_free_tail(struct freelist *p)
{
    p->free_bp = fhead.free_bp;
    p->free_fp = &fhead;
    fhead.free_bp->free_fp = p;
    fhead.free_bp = p;
}

void remove_from_free(struct freelist *p)
{
    p->free_bp->free_fp = p->free_fp;
    p->free_fp->free_bp = p->free_bp;
    p->free_fp = p->free_bp = NULL;
}
