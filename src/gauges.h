/*
 *          STATSD-C
 *          C port of Etsy's node.js-based statsd server
 *
 *          http://github.com/jbuchbinder/statsd-c
 *
 */

#include <semaphore.h>

#include "uthash/uthash.h"

#ifndef __GAUGES_H__
#define __GAUGES_H__ 1

typedef struct {
  char key[100];
  long double value;
  UT_hash_handle hh; /* makes this structure hashable */
} statsd_gauge_t;

extern statsd_gauge_t *gauges;
extern sem_t gauges_lock;

#define wait_for_gauges_lock() sem_wait(&gauges_lock)
#define remove_gauges_lock() sem_post(&gauges_lock)

#endif /* __GAUGES_H__ */

