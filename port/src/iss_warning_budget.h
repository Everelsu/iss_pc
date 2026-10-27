/* Bounded diagnostic counting by stable shader identity, not volatile values. */
#pragma once
#include <stdint.h>
typedef struct {uint64_t key,count;unsigned occupied;} IssWarningCounter;
typedef struct {IssWarningCounter slots[128];uint64_t overflow;} IssWarningBudget;
static inline uint64_t iss_warning_note(IssWarningBudget *budget,uint64_t key){
 unsigned start=(unsigned)(key^(key>>32))%128;
 for(unsigned i=0;i<128;i++){
  IssWarningCounter *slot=&budget->slots[(start+i)%128];
  if(!slot->occupied){slot->occupied=1;slot->key=key;slot->count=1;return 1;}
  if(slot->key==key){if(slot->count!=UINT64_MAX)slot->count++;return slot->count;}
 }
 if(budget->overflow!=UINT64_MAX)budget->overflow++;
 return budget->overflow;
}
static inline int iss_warning_report(uint64_t count){return count&&!(count&(count-1));}
