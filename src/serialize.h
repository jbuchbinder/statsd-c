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

int statsd_serialize( char *filename );
int statsd_deserialize( char *filename );

