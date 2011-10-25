/*
 *          STATSD-C
 *          C port of Etsy's node.js-based statsd server
 *
 *          http://github.com/jbuchbinder/statsd-c
 *
 */

#include <stdbool.h>
#include <semaphore.h>

#include "uthash/uthash.h"

#ifndef __STATS_H__
#define __STATS_H__ 1

typedef struct {
  char group_name[100];
  char key_name[100];
} statsd_stat_name_t;

typedef struct {
  statsd_stat_name_t name;
  long value;
  bool locked;
  UT_hash_handle hh; /* makes this structure hashable */
} statsd_stat_t;

extern statsd_stat_t *stats;
extern sem_t stats_lock;

#define wait_for_stats_lock() sem_wait(&stats_lock)
#define remove_stats_lock() sem_post(&stats_lock)

#endif /* __STATS_H__ */

