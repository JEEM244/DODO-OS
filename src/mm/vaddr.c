#include <stddef.h>
#include <stdint.h>
#include "dodo/mm/vaddr.h"
#define DODO_USER_LIMIT UINT64_C(0x0000800000000000)
static int aligned(uint64_t value) { return (value & (DODO_VADDR_PAGE_SIZE - 1u)) == 0u; }
static int valid_span(uint64_t base, uint64_t length, uint64_t *end) {
 if (length == 0u || !aligned(base) || !aligned(length) || base > UINT64_MAX-length) return 0;
 *end=base+length; return 1;
}
static int valid_space(const struct dodo_vaddr_space *space) {
 return space != NULL && aligned(space->start) && aligned(space->end) &&
        space->start < space->end && space->end <= DODO_USER_LIMIT &&
        space->count <= DODO_VADDR_MAX_RANGES;
}
enum dodo_vaddr_status dodo_vaddr_init(struct dodo_vaddr_space *space,uint64_t start,uint64_t end) {
 if(space==NULL) return DODO_VADDR_INVALID_ARGUMENT;
 if(!aligned(start)||!aligned(end)||start>=end||end>DODO_USER_LIMIT) return DODO_VADDR_INVALID_RANGE;
 space->start=start; space->end=end; space->count=0; return DODO_VADDR_OK;
}
enum dodo_vaddr_status dodo_vaddr_reserve(struct dodo_vaddr_space *space,uint64_t base,uint64_t length) {
 uint64_t end;
 if(space==NULL) return DODO_VADDR_INVALID_ARGUMENT;
 if(!valid_space(space)) return DODO_VADDR_INVALID_ARGUMENT;
 if(!valid_span(base,length,&end)||base<space->start||end>space->end) return DODO_VADDR_INVALID_RANGE;
 uint64_t position=0;
 while(position<space->count&&space->allocated[position].base<base) ++position;
 if(position>0) { const struct dodo_vaddr_range *p=&space->allocated[position-1u]; if(p->base+p->length>base) return DODO_VADDR_OVERLAP; }
 if(position<space->count&&end>space->allocated[position].base) return DODO_VADDR_OVERLAP;
 if(space->count>=DODO_VADDR_MAX_RANGES) return DODO_VADDR_CAPACITY;
 for(uint64_t i=space->count;i>position;--i) space->allocated[i]=space->allocated[i-1u];
 space->allocated[position].base=base; space->allocated[position].length=length; ++space->count; return DODO_VADDR_OK;
}
enum dodo_vaddr_status dodo_vaddr_allocate(struct dodo_vaddr_space *space,uint64_t length,uint64_t alignment,uint64_t *base) {
 if(space==NULL||base==NULL) return DODO_VADDR_INVALID_ARGUMENT;
 if(!valid_space(space)) return DODO_VADDR_INVALID_ARGUMENT;
 if(length==0u||!aligned(length)||alignment<DODO_VADDR_PAGE_SIZE||(alignment&(alignment-1u))!=0u||!aligned(alignment)) return DODO_VADDR_INVALID_RANGE;
 uint64_t candidate=space->start;
 if(candidate>UINT64_MAX-(alignment-1u)) return DODO_VADDR_NO_SPACE;
 candidate=(candidate+alignment-1u)&~(alignment-1u);
 for(uint64_t i=0;i<=space->count;++i) {
  uint64_t gap_end=i==space->count?space->end:space->allocated[i].base;
  if(candidate<=gap_end&&length<=gap_end-candidate) {
   enum dodo_vaddr_status status=dodo_vaddr_reserve(space,candidate,length);
   if(status==DODO_VADDR_OK) *base=candidate;
   return status;
  }
  if(i<space->count) {
   uint64_t allocated_end=space->allocated[i].base+space->allocated[i].length;
   if(allocated_end>UINT64_MAX-(alignment-1u)) return DODO_VADDR_NO_SPACE;
   candidate=(allocated_end+alignment-1u)&~(alignment-1u);
  }
 }
 return DODO_VADDR_NO_SPACE;
}
enum dodo_vaddr_status dodo_vaddr_release(struct dodo_vaddr_space *space,uint64_t base,uint64_t length) {
 if(space==NULL) return DODO_VADDR_INVALID_ARGUMENT;
 if(!valid_space(space)) return DODO_VADDR_INVALID_ARGUMENT;
 uint64_t end;
 if(!valid_span(base,length,&end)) return DODO_VADDR_INVALID_RANGE;
 for(uint64_t i=0;i<space->count;++i) if(space->allocated[i].base==base&&space->allocated[i].length==length) {
  for(uint64_t j=i+1u;j<space->count;++j) space->allocated[j-1u]=space->allocated[j];
  --space->count; return DODO_VADDR_OK;
 }
 return DODO_VADDR_NOT_FOUND;
}
