/*
 *          STATSD-C
 *          C port of Etsy's node.js-based statsd server
 *
 *          http://github.com/jbuchbinder/statsd-c
 *
 */

#include <semaphore.h>

#include "uthash/uthash.h"

#ifndef __TIMER_H__
#define __TIMER_H__ 1

typedef struct {
  UT_hash_handle hh; /* makes this structure hashable */
  char key[100];
  int count;
  double values[];
} statsd_timer_t;

extern statsd_timer_t *timers;
extern sem_t timers_lock;

#define wait_for_timers_lock() sem_wait(&timers_lock)
#define remove_timers_lock() sem_post(&timers_lock)

#endif /* __TIMER_H__ */

