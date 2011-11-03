/*
 *          STATSD-C
 *          C port of Etsy's node.js-based statsd server
 *
 *          http://github.com/jbuchbinder/statsd-c
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "json-c/json.h"
#include "uthash/utarray.h"

#include "counters.h"
#include "stats.h"
#include "timers.h"

extern UT_icd timers_icd;

int statsd_deserialize( char *filename ) {
  FILE *fp;
  int filesize;

  fp = fopen(filename, "r");
  if (!fp) {
    return 0;
  }

  fseek(fp, 0, SEEK_END);
  filesize = ftell(fp);
  rewind(fp);

  if (filesize < 10) {
    syslog(LOG_INFO, "No data found, skipping deserialization (length %d).\n", filesize);
    if (fp) fclose(fp);
    return 0;
  }

  char *data = (char*) calloc(sizeof(char), filesize + 20);

  fread(data, 1, filesize, fp);
  if(ferror(fp)) {
    if (fp) fclose(fp);
    if (data) free(data);
    return 0;
  }
  if (fp) fclose(fp);

  json_object *obj = json_tokener_parse(data);

  json_object *obj_stats = json_object_object_get(obj, "stats");
  {
    json_object_object_foreach(obj_stats, key, val) {
      statsd_stat_t *s = malloc(sizeof(statsd_stat_t));
      memset(s, 0, sizeof(statsd_stat_t));

      syslog(LOG_DEBUG, "Found key %s in file\n", key);

      char *period = strchr(key, '.');
      if (!period) {
        strcpy(s->name.key_name, key);
      } else {
        int period_pos = period - key;
        strncpy(s->name.group_name, key, period_pos);
        strncpy(s->name.key_name, key + period_pos + 1, strlen(key) - period_pos + 1);
      }

      s->value = json_object_get_int(val);
      s->locked = 0;

      wait_for_stats_lock();
      HASH_ADD( hh, stats, name, sizeof(statsd_stat_name_t), s );
      remove_stats_lock();
    }
  }

  json_object *obj_timers = json_object_object_get(obj, "timers");
  {
    json_object_object_foreach(obj_timers, key, val) {
      statsd_timer_t *t = malloc(sizeof(statsd_timer_t));

      strcpy(t->key, key);
      t->count = json_object_array_length(val);
      utarray_new(t->values, &timers_icd);
      int i;
      for (i = 0; i < t->count; i++) {
        double d = json_object_get_double(json_object_array_get_idx(val, i));
        utarray_push_back(t->values, &d);
      }

      wait_for_timers_lock();
      HASH_ADD_STR( timers, key, t );
      remove_timers_lock();
    }
  }

  json_object *obj_counters = json_object_object_get(obj, "counters");
  {
    json_object_object_foreach(obj_counters, key, val) {
      statsd_counter_t *s = malloc(sizeof(statsd_counter_t));

      strcpy(s->key, key);
      s->value = json_object_get_double(val);

      wait_for_counters_lock();
      HASH_ADD_STR( counters, key, s );
      remove_counters_lock();
    }
  }

  if (data) free(data);
  return 1;
}

int statsd_serialize( char *filename ) {
  FILE *fp;

  json_object *obj = json_object_new_object();
  
  json_object *obj_stats = json_object_new_object();
  {
    wait_for_stats_lock();
    statsd_stat_t *s, *tmp;
    HASH_ITER(hh, stats, s, tmp) {
      char *tmpkey = malloc(sizeof(s->name.group_name ? strlen(s->name.group_name) + strlen(s->name.key_name) + 1 : strlen(s->name.key_name)));
      if (s->name.group_name) {
        sprintf(tmpkey, "%s.%s", s->name.group_name, s->name.key_name);
      } else {
        tmpkey = strdup(s->name.key_name);
      }
      syslog(LOG_DEBUG, "Serializing with key '%s'\n", tmpkey);
      json_object_object_add(obj_stats, tmpkey, json_object_new_int(s->value));
    }
    remove_stats_lock();
  }
  json_object *obj_timers = json_object_new_object();
  {
    statsd_timer_t *s, *tmp;
    wait_for_timers_lock();
    HASH_ITER(hh, timers, s, tmp) {
      double *iter = NULL;
      json_object *array = json_object_new_array();
      while ( (iter = (double *) utarray_next(s->values, iter))) {
        json_object_array_add(array, json_object_new_double(*iter));
      }
      json_object_object_add(obj_timers, s->key, array);
    }
    remove_timers_lock();
  }
  json_object *obj_counters = json_object_new_object();
  {
    statsd_counter_t *s, *tmp;
    wait_for_counters_lock();
    HASH_ITER(hh, counters, s, tmp) {
      json_object_object_add(obj_counters, s->key, json_object_new_double(s->value));
    }
    remove_counters_lock();
  }

  json_object_object_add(obj, "stats", obj_stats);
  json_object_object_add(obj, "timers", obj_timers);
  json_object_object_add(obj, "counters", obj_counters);

  fp = fopen(filename, "w");
  if (!fp) {
    return 0;
  }
  fputs(json_object_to_json_string(obj), fp);
  fclose(fp);

  return 1;
}

