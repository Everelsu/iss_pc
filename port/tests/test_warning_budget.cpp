#include "../src/iss_warning_budget.h"
#include <cassert>
#include <cstdio>
int main(){
 IssWarningBudget b={};unsigned logs=0;
 // Varying descriptor sizes must not reset a stable shader's repeat count.
 for(unsigned descriptor=0;descriptor<65536;descriptor++)logs+=iss_warning_report(iss_warning_note(&b,0x1234));
 assert(logs==17);
 // Hash collisions retain independent counts.
 assert(iss_warning_note(&b,0x12b4)==1);assert(iss_warning_note(&b,0x1334)==1);
 assert(iss_warning_note(&b,0x12b4)==2);assert(iss_warning_note(&b,0x1234)==65537);
 // Full storage shares a bounded overflow budget instead of logging every novelty.
 IssWarningBudget full={};for(unsigned i=0;i<128;i++)assert(iss_warning_note(&full,i)==1);
 logs=0;for(unsigned i=0;i<4096;i++)logs+=iss_warning_report(iss_warning_note(&full,1000+i));assert(logs==13);
 assert(!iss_warning_report(0));assert(!iss_warning_report(UINT64_MAX));
 puts("Warning budget: stable counts despite changing values, collision isolation and bounded overflow passed.");
}
