#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "dodo/mm/vaddr.h"
#define PAGE UINT64_C(4096)
int main(void) {
 struct dodo_vaddr_space s; uint64_t a=0;
 assert(dodo_vaddr_init(&s,0x10000,0x20000)==DODO_VADDR_OK);
 assert(dodo_vaddr_allocate(&s,2*PAGE,PAGE,&a)==DODO_VADDR_OK&&a==0x10000);
 assert(dodo_vaddr_allocate(&s,PAGE,2*PAGE,&a)==DODO_VADDR_OK&&a==0x12000);
 assert(dodo_vaddr_reserve(&s,0x11000,PAGE)==DODO_VADDR_OVERLAP);
 assert(dodo_vaddr_reserve(&s,0x1f000,2*PAGE)==DODO_VADDR_INVALID_RANGE);
 assert(dodo_vaddr_release(&s,0x10000,PAGE)==DODO_VADDR_NOT_FOUND);
 assert(dodo_vaddr_release(&s,0x12000,PAGE)==DODO_VADDR_OK);
 assert(dodo_vaddr_release(&s,0x10000,2*PAGE)==DODO_VADDR_OK);
 assert(dodo_vaddr_allocate(&s,2*PAGE,PAGE,&a)==DODO_VADDR_OK&&a==0x10000);
 assert(dodo_vaddr_reserve(&s,0x12000,PAGE)==DODO_VADDR_OK);
 assert(dodo_vaddr_reserve(&s,0x11000,2*PAGE)==DODO_VADDR_OVERLAP);
 assert(dodo_vaddr_release(&s,0x12000,PAGE)==DODO_VADDR_OK);
 assert(dodo_vaddr_allocate(&s,PAGE,3*PAGE,&a)==DODO_VADDR_INVALID_RANGE);
 assert(dodo_vaddr_init(&s,0x10001,0x20000)==DODO_VADDR_INVALID_RANGE);
 assert(dodo_vaddr_init(&s,UINT64_C(0x00007ffffffff000),UINT64_C(0x0000800000000000))==DODO_VADDR_OK);
 a=UINT64_C(0xdeadbeef);
 assert(dodo_vaddr_allocate(&s,2*PAGE,PAGE,&a)==DODO_VADDR_NO_SPACE);
 assert(a==UINT64_C(0xdeadbeef));
 assert(dodo_vaddr_init(&s,0x30000,0x30000)==DODO_VADDR_INVALID_RANGE);
 assert(dodo_vaddr_init(NULL,0x10000,0x20000)==DODO_VADDR_INVALID_ARGUMENT);
 assert(dodo_vaddr_reserve(NULL,0x10000,PAGE)==DODO_VADDR_INVALID_ARGUMENT);
 assert(dodo_vaddr_allocate(NULL,PAGE,PAGE,&a)==DODO_VADDR_INVALID_ARGUMENT);
 assert(dodo_vaddr_release(NULL,0x10000,PAGE)==DODO_VADDR_INVALID_ARGUMENT);
 assert(dodo_vaddr_allocate(&s,0,PAGE,&a)==DODO_VADDR_INVALID_RANGE);
 assert(dodo_vaddr_allocate(&s,PAGE,3*PAGE,&a)==DODO_VADDR_INVALID_RANGE);
 assert(dodo_vaddr_init(&s,0x10000,0x10000+((DODO_VADDR_MAX_RANGES*2u+1u)*PAGE))==DODO_VADDR_OK);
 for (uint64_t i=0;i<DODO_VADDR_MAX_RANGES;++i) {
  assert(dodo_vaddr_reserve(&s,0x10000+i*2*PAGE,PAGE)==DODO_VADDR_OK);
 }
 assert(s.count==DODO_VADDR_MAX_RANGES);
 assert(dodo_vaddr_reserve(&s,0x10000+DODO_VADDR_MAX_RANGES*2*PAGE,PAGE)==DODO_VADDR_CAPACITY);
 s.count=DODO_VADDR_MAX_RANGES+1u;
 assert(dodo_vaddr_reserve(&s,0x10000,PAGE)==DODO_VADDR_INVALID_ARGUMENT);
 puts("virtual-address range allocator tests passed"); return 0;
}
