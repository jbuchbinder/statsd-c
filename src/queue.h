/*
 *          STATSD-C
 *          C port of Etsy's node.js-based statsd server
 *
 *          http://github.com/jbuchbinder/statsd-c
 *
 */

#include "counters.h"
#include "stats.h"
#include "timers.h"

#ifndef __QUEUE_H
#define __QUEUE_H 1

#define MAX_QUEUE_SIZE ( 1024 * 1024 )

void queue_init( );
int queue_store( char *ptr );
char *queue_pop_first( );

#endif /* __QUEUE_H */

