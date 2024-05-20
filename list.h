#include <inttypes.h>
#include <netinet/in.h>
struct freelist
{
    struct freelist *free_fp;
    struct freelist *free_bp;
    struct in_addr ip;
    struct in_addr netmask;
};

extern struct freelist fhead; // the head of the freelist

extern struct freelist *free_search();
extern void insert_free_tail(struct freelist *p);
extern void remove_from_free(struct freelist *p);
