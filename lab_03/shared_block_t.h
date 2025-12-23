#ifndef SHARED_BLOCK_T_H
#define SHARED_BLOCK_T_H

#include <semaphore.h>

typedef struct {
  int number;
  char status; // 0 - continue, 1 - stop
  sem_t sem_child;
  sem_t sem_parent;
} shared_block_t;

#endif // SHARED_BLOCK_T_H
