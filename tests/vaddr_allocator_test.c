#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "dodo/mm/vaddr.h"
#define PAGE UINT64_C(4096)

static uint64_t next_random(uint64_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

static void check_invariants(const struct dodo_vaddr_space *space) {
    assert(dodo_vaddr_validate(space) == DODO_VADDR_OK);
    assert(space->count <= DODO_VADDR_MAX_RANGES);
    for (uint64_t i = 0; i < space->count; ++i) {
        const struct dodo_vaddr_range *range = &space->allocated[i];
        assert((range->base & (PAGE - 1u)) == 0u);
        assert((range->length & (PAGE - 1u)) == 0u);
        assert(range->length > 0u);
        assert(range->base >= space->start);
        assert(range->base + range->length <= space->end);
        if (i > 0u) {
            const struct dodo_vaddr_range *previous =
                &space->allocated[i - 1u];
            assert(previous->base < range->base);
            assert(previous->base + previous->length <= range->base);
        }
    }
}
static void test_release_clears_vacated_slot(void) {
 struct dodo_vaddr_space s;
 assert(dodo_vaddr_init(&s,0x10000,0x20000)==DODO_VADDR_OK);
 assert(dodo_vaddr_reserve(&s,0x12000,PAGE)==DODO_VADDR_OK);
 assert(dodo_vaddr_release(&s,0x12000,PAGE)==DODO_VADDR_OK);
 assert(s.count==0u);
 assert(s.allocated[0].base==0u && s.allocated[0].length==0u);
 assert(dodo_vaddr_validate(&s)==DODO_VADDR_OK);
}

static void test_init_clears_stale_slots(void) {
 struct dodo_vaddr_space s;
 assert(dodo_vaddr_init(&s,0x10000,0x20000)==DODO_VADDR_OK);
 s.allocated[DODO_VADDR_MAX_RANGES-1u].base=0x10000;
 s.allocated[DODO_VADDR_MAX_RANGES-1u].length=PAGE;
 assert(dodo_vaddr_init(&s,0x30000,0x40000)==DODO_VADDR_OK);
 for(uint64_t i=0;i<DODO_VADDR_MAX_RANGES;++i) {
  assert(s.allocated[i].base==0u && s.allocated[i].length==0u);
 }
}

int main(void) {
 test_release_clears_vacated_slot();
 test_init_clears_stale_slots();
 struct dodo_vaddr_space s; uint64_t a=0;
 assert(dodo_vaddr_init(&s,0x10000,0x20000)==DODO_VADDR_OK);
 assert(dodo_vaddr_validate(&s)==DODO_VADDR_OK);
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
 assert(dodo_vaddr_validate(&s)==DODO_VADDR_OK);
 s.count=DODO_VADDR_MAX_RANGES+1u;
 assert(dodo_vaddr_validate(&s)==DODO_VADDR_INVALID_ARGUMENT);
 assert(dodo_vaddr_reserve(&s,0x10000,PAGE)==DODO_VADDR_INVALID_ARGUMENT);

 assert(dodo_vaddr_init(&s,0x10000,0x20000)==DODO_VADDR_OK);
 s.count=1;
 s.allocated[0].base=0x10000;
 s.allocated[0].length=0;
 assert(dodo_vaddr_validate(&s)==DODO_VADDR_INVALID_ARGUMENT);
 assert(dodo_vaddr_reserve(&s,0x12000,PAGE)==DODO_VADDR_INVALID_ARGUMENT);

 /* Public validation rejects overlapping/corrupted reservation metadata. */
 assert(dodo_vaddr_validate(NULL)==DODO_VADDR_INVALID_ARGUMENT);
 assert(dodo_vaddr_init(&s,0x10000,0x20000)==DODO_VADDR_OK);
 assert(dodo_vaddr_reserve(&s,0x12000,PAGE)==DODO_VADDR_OK);
 s.allocated[0].base=0x20000;
 assert(dodo_vaddr_validate(&s)==DODO_VADDR_INVALID_ARGUMENT);
 s.allocated[0].base=0x12000;
 s.allocated[0].length=PAGE+1u;
 assert(dodo_vaddr_validate(&s)==DODO_VADDR_INVALID_ARGUMENT);
 s.allocated[0].length=PAGE;
 assert(dodo_vaddr_validate(&s)==DODO_VADDR_OK);
 assert(dodo_vaddr_init(&s,0x10000,0x20000)==DODO_VADDR_OK);
 s.count=2;
 s.allocated[0].base=0x10000;
 s.allocated[0].length=2*PAGE;
 s.allocated[1].base=0x11000;
 s.allocated[1].length=PAGE;
 assert(dodo_vaddr_validate(&s)==DODO_VADDR_INVALID_ARGUMENT);
 a=UINT64_C(0xdeadbeef);
 assert(dodo_vaddr_allocate(&s,PAGE,PAGE,&a)==DODO_VADDR_INVALID_ARGUMENT);
 assert(a==UINT64_C(0xdeadbeef));

 assert(dodo_vaddr_init(&s,0x10000,0x20000)==DODO_VADDR_OK);
 assert(dodo_vaddr_reserve(&s,0x14000,PAGE)==DODO_VADDR_OK);
 assert(dodo_vaddr_reserve(&s,0x10000,PAGE)==DODO_VADDR_OK);
 assert(dodo_vaddr_reserve(&s,0x12000,PAGE)==DODO_VADDR_OK);
 assert(s.allocated[0].base==0x10000);
 assert(s.allocated[1].base==0x12000);
 assert(s.allocated[2].base==0x14000);
 check_invariants(&s);

 assert(dodo_vaddr_init(&s,0x10000,0x10000+1024*PAGE)==DODO_VADDR_OK);
 uint64_t random_state = UINT64_C(0x6d2b79f5a4c31e87);
 for (uint64_t step = 0; step < 10000u; ++step) {
  const uint64_t random_value = next_random(&random_state);
  if (s.count > 0u && (random_value & 3u) == 0u) {
   const uint64_t index = next_random(&random_state) % s.count;
   const uint64_t base = s.allocated[index].base;
   const uint64_t length = s.allocated[index].length;
   assert(dodo_vaddr_release(&s,base,length)==DODO_VADDR_OK);
  } else {
   const uint64_t length = (1u + (random_value % 4u)) * PAGE;
   const uint64_t alignment = PAGE << ((random_value >> 8) % 3u);
   a = UINT64_C(0xdeadbeef);
   const enum dodo_vaddr_status status =
       dodo_vaddr_allocate(&s,length,alignment,&a);
   if (status == DODO_VADDR_OK) {
    assert((a & (alignment - 1u)) == 0u);
   } else {
    assert(status == DODO_VADDR_NO_SPACE ||
           status == DODO_VADDR_CAPACITY);
    assert(a == UINT64_C(0xdeadbeef));
   }
  }
  check_invariants(&s);
 }
 puts("virtual-address range allocator tests passed"); return 0;
}
