/*
 *          STATSD-C
 *          C port of Etsy's node.js-based statsd server
 *
 *          http://github.com/jbuchbinder/statsd-c
 *
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "queue.h"

int queue_store_pos = 0;
int queue_retrieve_pos = 0;
char *queue[MAX_QUEUE_SIZE];
pthread_mutex_t queue_mutex;

void queue_init( ) {
  syslog(LOG_DEBUG, "queue_init");
  queue_store_pos = 0;
  queue_retrieve_pos = 0;
  pthread_mutex_init(&queue_mutex, NULL);
}

int queue_store( char *ptr ) {
  syslog(LOG_DEBUG, "queue_store ('%s')", ptr);
  if (queue_store_pos == MAX_QUEUE_SIZE) {
    syslog(LOG_ERR, "Queue has reached maximum size of %d", MAX_QUEUE_SIZE);
    return 0;
  }
  pthread_mutex_lock(&queue_mutex);
  queue[ queue_store_pos ] = ptr;
  queue_store_pos ++;
  pthread_mutex_unlock(&queue_mutex);
  return 1;
}

char *queue_pop_first( ) {
  if (queue_store_pos <= queue_retrieve_pos) return NULL;
  pthread_mutex_lock(&queue_mutex);
  /* Pull item off beginning of queue */
  char *tmpptr = queue[ 0 ];
  /* Slide everything backwards */
  int p;
  for (p = 1; p < queue_store_pos - 1; p++) {
    if (queue[p] != NULL) queue[p - 1] = queue[p];
  }
  /* Last item would be a dupe, remove for good measure */
  queue[queue_store_pos - 1] = NULL;
  /* Push store "pointer" back one position */
  queue_store_pos --;
  pthread_mutex_unlock(&queue_mutex);
  return tmpptr;
}

