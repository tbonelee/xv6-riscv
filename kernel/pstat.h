#ifndef _PSTAT_H_
#define _PSTAT_H_

#include "param.h"
#include "types.h"

struct pstat {
  _Bool inuse[NPROC];   // whether this slot of the process table is in use (1 or 0)
  uint32 tickets[NPROC]; // the number of tickets this process has
  int pid[NPROC];     // the PID of each process 
  uint64 ticks[NPROC];   // the number of ticks each process has accumulated 
};

#endif // _PSTAT_H_