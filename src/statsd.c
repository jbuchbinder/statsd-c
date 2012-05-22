/*
 *          STATSD-C
 *          C port of Etsy's node.js-based statsd server
 *
 *          http://github.com/jbuchbinder/statsd-c
 *
 */

#include "config.h"

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/types.h>
#include <errno.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif
#ifdef HAVE_SEMAPHORE_H
#include <semaphore.h>
#endif
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#include "json-c/json.h"
#include "uthash/utarray.h"
#include "queue.h"
#include "statsd.h"
#include "serialize.h"
#include "stats.h"
#include "timers.h"
#include "counters.h"
#include "strings.h"
#include "embeddedgmetric.h"

#define LOCK_FILE "/tmp/statsd.lock"

/*
 * GLOBAL VARIABLES
 */

statsd_stat_t *stats = NULL;
sem_t stats_lock;
statsd_counter_t *counters = NULL;
sem_t counters_lock;
statsd_timer_t *timers = NULL;
sem_t timers_lock;
UT_icd timers_icd = { sizeof(double), NULL, NULL, NULL };

int stats_udp_socket, stats_mgmt_socket;
pthread_t thread_udp;
pthread_t thread_mgmt;
pthread_t thread_flush;
pthread_t thread_queue;
int port = PORT, mgmt_port = MGMT_PORT, ganglia_port = GANGLIA_PORT, flush_interval = FLUSH_INTERVAL;
int debug = 0, friendly = 0, clear_stats = 0, daemonize = 0, enable_gmetric = 0;
char *serialize_file = NULL, *ganglia_host = NULL, *ganglia_spoof = NULL, *ganglia_metric_prefix = NULL, *lock_file = NULL;

/*
 * FUNCTION PROTOTYPES
 */

void add_timer( char *key, double value );
void update_stat( char *group, char *key, char *value);
void update_counter( char *key, double value, double sample_rate );
void update_timer( char *key, double value );
void process_stats_packet(char buf_in[]);
void process_json_stats_packet(char buf_in[]);
void process_json_stats_object(json_object *sobj);
void dump_stats();
void p_thread_udp(void *ptr);
void p_thread_mgmt(void *ptr);
void p_thread_flush(void *ptr);
void p_thread_queue(void *ptr);

void init_stats() {
  char startup_time[12];
  sprintf(startup_time, "%ld", time(NULL));

  if (serialize_file && !clear_stats) {
    syslog(LOG_DEBUG, "Deserializing stats from file.");

    statsd_deserialize(serialize_file);
  }

  remove_stats_lock();

  update_stat( "graphite", "last_flush", startup_time );
  update_stat( "messages", "last_msg_seen", startup_time );
  update_stat( "messages", "bad_lines_seen", "0" );
}

void cleanup() {
  pthread_cancel(thread_flush);
  pthread_cancel(thread_udp);
  pthread_cancel(thread_mgmt);
  pthread_cancel(thread_queue);

  if (stats_udp_socket) {
    syslog(LOG_INFO, "Closing UDP stats socket.");
    close(stats_udp_socket);
  }

  if (serialize_file) {
    syslog(LOG_INFO, "Serializing state to file.");
    if (statsd_serialize(serialize_file)) {
      syslog(LOG_INFO, "Serialized state successfully.");
    } else {
      syslog(LOG_ERR, "Failed to serialize state.");
    }
  }

  sem_destroy(&stats_lock);
  sem_destroy(&timers_lock);
  sem_destroy(&counters_lock);

  syslog(LOG_INFO, "Removing lockfile %s", lock_file != NULL ? lock_file : LOCK_FILE);
  unlink(lock_file != NULL ? lock_file : LOCK_FILE);
}

void die_with_error(char *s) {
  perror(s);
  cleanup();
  exit(1);
}

void sighup_handler (int signum) {
  syslog(LOG_ERR, "SIGHUP caught");
  cleanup();
  exit(1);
}

void sigint_handler (int signum) {
  syslog(LOG_ERR, "SIGINT caught");
  cleanup();
  exit(1);
}

void sigquit_handler (int signum) {
  syslog(LOG_ERR, "SIGQUIT caught");
  cleanup();
  exit(1);
}

void sigterm_handler (int signum) {
  syslog(LOG_ERR, "SIGTERM caught");
  cleanup();
  exit(1);
}

int double_sort (const void *a, const void *b) {
  double _a = *(double *)a;
  double _b = *(double *)b;
  if (_a == _b) return 0;
  return (_a < _b) ? -1 : 1;
}

void daemonize_server() {
  int pid;
  int lockfp;
  char str[10];

  if (getppid() == 1) {
    return;
  }
  pid = fork();
  if (pid < 0) {
    exit(1);
  }
  if (pid > 0) {
    exit(0);
  }

  /* Try to become root, but ignore if we can't */
  setuid((uid_t) 0);
  errno = 0;

  setsid();
  for (pid = getdtablesize(); pid>=0; --pid) {
    close(pid);
  }
  pid = open("/dev/null", O_RDWR); dup(pid); dup(pid);
  umask((mode_t) 022);
  lockfp = open(lock_file != NULL ? lock_file : LOCK_FILE, O_RDWR | O_CREAT, 0640);
  if (lockfp < 0) {
    syslog(LOG_ERR, "Could not serialize PID to lock file");
    exit(1);
  }
  if (lockf(lockfp, F_TLOCK,0)<0) {
    syslog(LOG_ERR, "Could not create lock, bailing out");
    exit(0);
  }
  sprintf(str, "%d\n", getpid());
  write(lockfp, str, strlen(str));
  close(lockfp);

  /* Signal handling */
  signal(SIGCHLD, SIG_IGN        );
  signal(SIGTSTP, SIG_IGN        );
  signal(SIGTTOU, SIG_IGN        );
  signal(SIGTTIN, SIG_IGN        );
  signal(SIGHUP , sighup_handler );
  signal(SIGTERM, sigterm_handler);
}

#define CHECK_PTHREAD_DETACH() if (rc == EINVAL) syslog(LOG_ERR, "pthread_detach returned EINVAL"); if (rc == ESRCH) syslog(LOG_ERR, "pthread_detach returned ESRCH")

void syntax(char *argv[]) {
  fprintf(stderr, "statsd-c version %s\nhttps://github.com/jbuchbinder/statsd-c\n\n", STATSD_VERSION);
  fprintf(stderr, "Usage: %s [-hDdfFc] [-p port] [-m port] [-s file] [-G host] [-g port] [-S spoofhost] [-P prefix] [-l lockfile]\n", argv[0]);
  fprintf(stderr, "\t-p port           set statsd udp listener port (default 8125)\n");
  fprintf(stderr, "\t-m port           set statsd management port (default 8126)\n");
  fprintf(stderr, "\t-s file           serialize state to and from file (default disabled)\n");
  fprintf(stderr, "\t-G host           ganglia host (default disabled)\n");
  fprintf(stderr, "\t-g port           ganglia port (default 8649)\n");
  fprintf(stderr, "\t-S spoofhost      ganglia spoof host (default statsd:statsd)\n");
  fprintf(stderr, "\t-P prefix         ganglia metric prefix (default is none)\n");
  fprintf(stderr, "\t-l lockfile       lock file (only used when daemonizing)\n");
  fprintf(stderr, "\t-h                this help display\n");
  fprintf(stderr, "\t-d                enable debug\n");
  fprintf(stderr, "\t-D                daemonize\n");
  fprintf(stderr, "\t-f                enable friendly mode (breaks wire compatibility)\n");
  fprintf(stderr, "\t-F seconds        set flush interval in seconds (default 10)\n");
  fprintf(stderr, "\t-c                clear stats on startup\n");
  exit(1);
}

int main(int argc, char *argv[]) {
  int pids[4] = { 1, 2, 3, 4 };
  int opt, rc = 0;
  pthread_attr_t attr;

  signal (SIGINT, sigint_handler);
  signal (SIGQUIT, sigquit_handler);

  sem_init(&stats_lock, 0, 1);
  sem_init(&timers_lock, 0, 1);
  sem_init(&counters_lock, 0, 1);

  queue_init();

  while ((opt = getopt(argc, argv, "dDfhp:m:s:cg:G:F:S:P:l:")) != -1) {
    switch (opt) {
      case 'd':
        printf("Debug enabled.\n");
        debug = 1;
        break;
      case 'D':
        printf("Daemonize enabled.\n");
        daemonize = 1;
        break;
      case 'f':
        printf("Friendly mode enabled (breaks wire compatibility).\n");
        friendly = 1;
        break;
      case 'F':
        flush_interval = atoi(optarg);
        printf("Flush interval set to %d seconds.\n", flush_interval);
        break;
      case 'p':
        port = atoi(optarg);
        printf("Statsd port set to %d\n", port);
        break;
      case 'm':
        mgmt_port = atoi(optarg);
        printf("Management port set to %d\n", mgmt_port);
        break;
      case 's':
        serialize_file = strdup(optarg);
        printf("Serialize to file %s\n", serialize_file);
        break;
      case 'c':
        clear_stats = 1;
        printf("Clearing stats on start.\n");
        break;
      case 'G':
        ganglia_host = strdup(optarg);
        enable_gmetric = 1;
        printf("Ganglia host %s\n", ganglia_host);
        break;
      case 'g':
        ganglia_port = atoi(optarg);
        printf("Ganglia port %d\n", ganglia_port);
        break;
      case 'S':
        ganglia_spoof = strdup(optarg);
        printf("Ganglia spoof host %s\n", ganglia_spoof);
        break;
      case 'P':
        ganglia_metric_prefix = strdup(optarg);
        printf("Ganglia metric prefix %s\n", ganglia_metric_prefix);
        break;
      case 'l':
        lock_file = strdup(optarg);
        printf("Lock file %s\n", lock_file);
        break;
      case 'h':
      default:
        syntax(argv);
        break;
    }
  }

  if (ganglia_spoof == NULL) {
    ganglia_spoof = strdup("statsd:statsd");
  }

  if (debug) {
    setlogmask(LOG_UPTO(LOG_DEBUG));
    openlog("statsd-c",  LOG_CONS | LOG_NDELAY | LOG_PERROR | LOG_PID, LOG_USER);
  } else {
    setlogmask(LOG_UPTO(LOG_INFO));
    openlog("statsd-c", LOG_CONS, LOG_USER);
  }

  /* Initialization of certain stats, here. */
  init_stats();

  if (daemonize) {
    syslog(LOG_DEBUG, "Daemonizing statsd-c");
    daemonize_server();

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN + 1024 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  }

  pthread_create (&thread_udp,   daemonize ? &attr : NULL, (void *) &p_thread_udp,   (void *) &pids[0]);
  pthread_create (&thread_mgmt,  daemonize ? &attr : NULL, (void *) &p_thread_mgmt,  (void *) &pids[1]);
  pthread_create (&thread_flush, daemonize ? &attr : NULL, (void *) &p_thread_flush, (void *) &pids[2]);
  pthread_create (&thread_queue, daemonize ? &attr : NULL, (void *) &p_thread_queue, (void *) &pids[3]);

  if (daemonize) {
    syslog(LOG_DEBUG, "Destroying pthread attributes");
    pthread_attr_destroy(&attr);
    syslog(LOG_DEBUG, "Detaching pthreads");
    rc = pthread_detach(thread_udp);
    CHECK_PTHREAD_DETACH();
    rc = pthread_detach(thread_mgmt);
    CHECK_PTHREAD_DETACH();
    rc = pthread_detach(thread_flush);
    CHECK_PTHREAD_DETACH();
    rc = pthread_detach(thread_queue);
    CHECK_PTHREAD_DETACH();
    for (;;) { }
  } else {
    syslog(LOG_DEBUG, "Waiting for pthread termination");
    pthread_join(thread_udp,   NULL);
    pthread_join(thread_mgmt,  NULL);
    pthread_join(thread_flush, NULL);
    pthread_join(thread_queue, NULL);
    syslog(LOG_DEBUG, "Pthreads terminated");
  }

  return 0;
}

void add_timer( char *key, double value ) {
  statsd_timer_t *t;
  HASH_FIND_STR( timers, key, t );
  if (t) {
    /* Add to old entry */
    wait_for_timers_lock();
    t->count++;
    utarray_push_back(t->values, &value);
    remove_timers_lock();
  } else {
    /* Create new entry */
    t = malloc(sizeof(statsd_timer_t));

    strcpy(t->key, key);
    t->count = 1;
    utarray_new(t->values, &timers_icd);
    utarray_push_back(t->values, &value);

    wait_for_timers_lock();
    HASH_ADD_STR( timers, key, t );
    remove_timers_lock();
  }
}

/**
 * Record or update stat value.
 */
void update_stat( char *group, char *key, char *value ) {
  syslog(LOG_DEBUG, "update_stat ( %s, %s, %s )\n", group, key, value);
  statsd_stat_t *s;
  statsd_stat_name_t l;

  memset(&l, 0, sizeof(statsd_stat_name_t));
  strcpy(l.group_name, group);
  strcpy(l.key_name, key);
  syslog(LOG_DEBUG, "HASH_FIND '%s' '%s'\n", l.group_name, l.key_name);
  HASH_FIND( hh, stats, &l, sizeof(statsd_stat_name_t), s );

  if (s) {
    syslog(LOG_DEBUG, "Updating old stat entry");

    wait_for_stats_lock();
    s->value = atol( value );
    remove_stats_lock();
  } else {
    syslog(LOG_DEBUG, "Adding new stat entry");
    s = malloc(sizeof(statsd_stat_t));
    memset(s, 0, sizeof(statsd_stat_t));

    strcpy(s->name.group_name, group);
    strcpy(s->name.key_name, key);
    s->value = atol(value);
    s->locked = 0;

    wait_for_stats_lock();
    HASH_ADD( hh, stats, name, sizeof(statsd_stat_name_t), s );
    remove_stats_lock();
  }
}

void update_counter( char *key, double value, double sample_rate ) {
  syslog(LOG_DEBUG, "update_counter ( %s, %f, %f )\n", key, value, sample_rate);
  statsd_counter_t *c;
  HASH_FIND_STR( counters, key, c );
  if (c) {
    syslog(LOG_DEBUG, "Updating old counter entry");
    if (sample_rate == 0) {
      wait_for_counters_lock();
      c->value = c->value + value;
      remove_counters_lock();
    } else {
      wait_for_counters_lock();
      c->value = c->value + ( value * ( 1 / sample_rate ) );
      remove_counters_lock();
    }
  } else {
    syslog(LOG_DEBUG, "Adding new counter entry");
    c = malloc(sizeof(statsd_counter_t));

    strcpy(c->key, key);
    c->value = 0;
    if (sample_rate == 0) {
      c->value = value;
    } else {
      c->value = value * ( 1 / sample_rate );
    }

    wait_for_counters_lock();
    HASH_ADD_STR( counters, key, c );
    remove_counters_lock();
  }
}

void update_timer( char *key, double value ) {
  syslog(LOG_DEBUG, "update_timer ( %s, %f )\n", key, value);
  statsd_timer_t *t;
  syslog(LOG_DEBUG, "HASH_FIND_STR '%s'\n", key);
  HASH_FIND_STR( timers, key, t );
  syslog(LOG_DEBUG, "after HASH_FIND_STR '%s'\n", key);
  if (t) {
    syslog(LOG_DEBUG, "Updating old timer entry");
    wait_for_timers_lock();
    utarray_push_back(t->values, &value);
    t->count++;
    remove_timers_lock();
  } else {
    syslog(LOG_DEBUG, "Adding new timer entry");
    t = malloc(sizeof(statsd_timer_t));

    strcpy(t->key, key);
    t->count = 0;
    utarray_new(t->values, &timers_icd);
    utarray_push_back(t->values, &value);
    t->count++;

    wait_for_timers_lock();
    HASH_ADD_STR( timers, key, t );
    remove_timers_lock();
  }
}

void dump_stats() {
  if (debug) {
    {
      syslog(LOG_DEBUG, "Stats dump:");
      statsd_stat_t *s, *tmp;
      HASH_ITER(hh, stats, s, tmp) {
        syslog(LOG_DEBUG, "%s.%s: %ld", s->name.group_name, s->name.key_name, s->value);
      }
      if (s) free(s);
      if (tmp) free(tmp);
    }

    {
      syslog(LOG_DEBUG, "Counters dump:");
      statsd_counter_t *c, *tmp;
      HASH_ITER(hh, counters, c, tmp) {
        syslog(LOG_DEBUG, "%s: %Lf", c->key, c->value);
      }
      if (c) free(c);
      if (tmp) free(tmp);
    }
  }
}

void process_json_stats_packet(char buf_in[]) {
  if (strlen(buf_in) < 2) {
    UPDATE_LAST_MSG_SEEN()
    return;
  }

  json_object *obj = json_tokener_parse(&buf_in[0]);
  if (!obj) {
    syslog(LOG_ERR, "Bad JSON object, skipping");
    return;
  }

  if (json_object_get_type(obj) == json_type_object) {
    syslog(LOG_DEBUG, "Processing single stats object");
    process_json_stats_object(obj);
  } else if (json_object_get_type(obj) == json_type_array) {
    int i;
    for (i=0; i<json_object_array_length(obj); i++) {
      syslog(LOG_DEBUG, "Iterating through objects at pos %d", i);
      process_json_stats_object(json_object_array_get_idx(obj, i));
    }
  } else {
    syslog(LOG_ERR, "Bad JSON data presented");
  }
}

void process_json_stats_object(json_object *sobj) {
  syslog(LOG_INFO, "Processing stat %s", json_object_to_json_string(sobj));

  json_object *timer_obj = json_object_object_get(sobj, "timer");
  json_object *counter_obj = json_object_object_get(sobj, "counter");

  if (timer_obj && counter_obj) {
    syslog(LOG_ERR, "Can't specify both timer and counter in same object");
    return;
  }

  if (timer_obj) {
    json_object *value_obj = json_object_object_get(sobj, "value");

    if (!timer_obj || !value_obj) {
      syslog(LOG_ERR, "Could not process, requires timer && value attributes");
      return;
    }

    char *key_name = (char *) json_object_get_string(timer_obj);
    sanitize_key(key_name);
    double value = json_object_get_double(value_obj);

    update_timer(key_name, value);
  } else if (counter_obj) {
    json_object *value_obj = json_object_object_get(sobj, "value");
    json_object *sample_rate_obj = json_object_object_get(sobj, "sample_rate");

    if (!counter_obj || !value_obj) {
      syslog(LOG_ERR, "Could not process, requires counter && value attributes");
      return;
    }

    char *key_name = (char *) json_object_get_string(counter_obj);
    sanitize_key(key_name);
    double value = json_object_get_double(value_obj);
    double sample_rate = sample_rate_obj ? json_object_get_double(sample_rate_obj) : 0;

    update_counter(key_name, value, sample_rate);
  }
}

void process_stats_packet(char buf_in[]) {
  char *key_name = NULL;

  if (strlen(buf_in) < 2) {
    UPDATE_LAST_MSG_SEEN()
    return;
  }

  char *save, *subsave, *token, *subtoken, *bits, *fields;
  double value = 1.0;

  int i;
  for (i = 1, bits=&buf_in[0]; ; i++, bits=NULL) {
    syslog(LOG_DEBUG, "i = %d\n", i);
    token = strtok_r(bits, ":", &save);
    if (token == NULL) { break; }
    if (i == 1) {
      syslog(LOG_DEBUG, "Found token '%s', key name\n", token);
      key_name = strdup( token );
      sanitize_key(key_name);
      /* break; */
    } else {
      syslog(LOG_DEBUG, "\ttoken [#%d] = %s\n", i, token);
      char *s_sample_rate = NULL, *s_number = NULL;
      double sample_rate = 1.0;
      bool is_timer = 0;

      if (strstr(token, "|") == NULL) {
        syslog(LOG_DEBUG, "No pipes found, basic logic");
        sanitize_value(token);
        syslog(LOG_DEBUG, "\t\tvalue = %s\n", token);
        value = strtod(token, (char **) NULL);
        syslog(LOG_DEBUG, "\t\tvalue = %s => %f\n", token, value);
      } else {
        int j;
        for (j = 1, fields = token; ; j++, fields = NULL) {
          subtoken = strtok_r(fields, "|", &subsave);
          if (subtoken == NULL) { break; }
          syslog(LOG_DEBUG, "\t\tsubtoken = %s\n", subtoken);
  
          switch (j) {
            case 1:
              syslog(LOG_DEBUG, "case 1");
              sanitize_value(subtoken);
              value = strtod(subtoken, (char **) NULL);
              break;
            case 2:
              syslog(LOG_DEBUG, "case 2");
              if (subtoken == NULL) { break ; }
              if (strlen(subtoken) < 2) {
                syslog(LOG_DEBUG, "subtoken length < 2");
                is_timer = 0;
              } else {
                syslog(LOG_DEBUG, "subtoken length >= 2");
                if (*subtoken == 'm' && *(subtoken + 1) == 's') {
                  is_timer = 1;
                }
              }
              break;
            case 3:
              syslog(LOG_DEBUG, "case 3");
              if (subtoken == NULL) { break ; }
              s_sample_rate = strdup(subtoken);
              break;
          }
        }
      }

      syslog(LOG_DEBUG, "Post token processing");

      if (is_timer == 1) {
        /* ms passed, handle timer */
        update_timer( key_name, value );
      } else {
        /* Handle non-timer, as counter */
        if (s_sample_rate && *s_sample_rate == '@') {
          sample_rate = strtod( (s_sample_rate + 1), (char **) NULL );
        }
        update_counter(key_name, value, sample_rate);
        syslog(LOG_DEBUG, "Found key name '%s'\n", key_name);
        syslog(LOG_DEBUG, "Found value '%f'\n", value);
      }
      if (s_sample_rate) free(s_sample_rate);
      if (s_number) free(s_number);
    }
  }
  i--; /* For ease */

  syslog(LOG_DEBUG, "After loop, i = %d, value = %f", i, value);

  if (i <= 1) {
    /* No value, assign "1" and process */
    update_counter(key_name, value, 1);
  }

  syslog(LOG_DEBUG, "freeing key and value");
  if (key_name) free(key_name);
      
  UPDATE_LAST_MSG_SEEN()
}

/*
 *  THREADS
 */

void p_thread_udp(void *ptr) {
  syslog(LOG_INFO, "Thread[Udp]: Starting thread %d\n", (int) *((int *) ptr));
    struct sockaddr_in si_me, si_other;
    fd_set read_flags,write_flags;
    struct timeval waitd;
    int stat;

    /* begin udp listener */

    if ((stats_udp_socket=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1)
      die_with_error("UDP: Could not grab socket.");

    /* Reuse socket, please */
    int on = 1;
    setsockopt(stats_udp_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    /* Use non-blocking sockets */
    int flags = fcntl(stats_udp_socket, F_GETFL, 0);
    fcntl(stats_udp_socket, F_SETFL, flags | O_NONBLOCK);

    memset((char *) &si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(port);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    syslog(LOG_DEBUG, "UDP: Binding to socket.");
    if (bind(stats_udp_socket, (struct sockaddr *)&si_me, sizeof(si_me))==-1)
        die_with_error("UDP: Could not bind");
    syslog(LOG_DEBUG, "UDP: Bound to socket on port %d", port);

    while (1) {
      waitd.tv_sec = 1;
      waitd.tv_usec = 0;
      FD_ZERO(&read_flags);
      FD_ZERO(&write_flags);
      FD_SET(stats_udp_socket, &read_flags);

      stat = select(stats_udp_socket+1, &read_flags, &write_flags, (fd_set*)0, &waitd);
      /* If we can't do anything for some reason, wait a bit */
      if (stat < 0) {
        syslog(LOG_INFO, "Can't do anything, stat == %d", stat);
        sleep(1);
        continue;
      }

      char buf_in[BUFLEN];
      if (FD_ISSET(stats_udp_socket, &read_flags)) {
        FD_CLR(stats_udp_socket, &read_flags);
        memset(&buf_in, 0, sizeof(buf_in));
        if (read(stats_udp_socket, buf_in, sizeof(buf_in)) <= 0) {
          close(stats_udp_socket);
          break;
        }
        /* make sure that the buf_in is NULL terminated */
        buf_in[BUFLEN - 1] = 0;

        syslog(LOG_DEBUG, "UDP: Received packet from %s:%d\nData: %s\n\n", 
            inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port), buf_in);

        char *packet = strdup(buf_in);
        syslog(LOG_DEBUG, "UDP: Storing packet in queue");
        queue_store( packet );
        syslog(LOG_DEBUG, "UDP: Stored packet in queue");
      }
    }

    if (stats_udp_socket) close(stats_udp_socket);

    /* end udp listener */
  syslog(LOG_INFO, "Thread[Udp]: Ending thread %d\n", (int) *((int *) ptr));
  pthread_exit(0);
}

void p_thread_queue(void *ptr) {
  syslog(LOG_INFO, "Thread[Queue]: Starting thread %d\n", (int) *((int *) ptr));

  while (1) {
    char *packet = queue_pop_first();
    while (packet != NULL) {
      char buf_in[BUFLEN];
      memset(&buf_in, 0, sizeof(buf_in));
      strcpy(buf_in, packet);

      if (buf_in[0] == '{' || buf_in[0] == '[') {
        syslog(LOG_DEBUG, "Queue: Processing as JSON packet");
        process_json_stats_packet(buf_in);
      } else {
        syslog(LOG_DEBUG, "Queue: Processing as standard packet");
        process_stats_packet(buf_in);
      }
      packet = queue_pop_first();
    }
    sleep(100);
  }

  syslog(LOG_INFO, "Thread[Queue]: Ending thread %d\n", (int) *((int *) ptr));
  pthread_exit(0);
}

void p_thread_mgmt(void *ptr) {
  syslog(LOG_INFO, "Thread[Mgmt]: Starting thread %d\n", (int) *((int *) ptr));
    /* begin mgmt listener */

  fd_set master;
  fd_set read_fds;
  struct sockaddr_in serveraddr;
  struct sockaddr_in clientaddr;
  int fdmax;
  int newfd;
  char buf[1024];
  int nbytes;
  int yes = 1;
  int addrlen;
  int i;

  FD_ZERO(&master);
  FD_ZERO(&read_fds);
 
  if((stats_mgmt_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    perror("socke) error");
    exit(1);
  }
  if(setsockopt(stats_mgmt_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
  {
    perror("setsockopt error");
    exit(1);
  }
 
  /* bind */
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = INADDR_ANY;
  serveraddr.sin_port = htons(mgmt_port);
  memset(&(serveraddr.sin_zero), '\0', 8);
 
  if(bind(stats_mgmt_socket, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1) {
    exit(1);
  }
 
  if(listen(stats_mgmt_socket, 10) == -1) {
    exit(1);
  }
 
  FD_SET(stats_mgmt_socket, &master);
  fdmax = stats_mgmt_socket;
 
  for(;;) {
    read_fds = master;
 
    if(select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
      perror("select error");
      exit(1);
    }

    for(i = 0; i <= fdmax; i++) {
      if(FD_ISSET(i, &read_fds)) {
        if(i == stats_mgmt_socket) {
          addrlen = sizeof(clientaddr);
          if((newfd = accept(stats_mgmt_socket, (struct sockaddr *)&clientaddr, (socklen_t *) &addrlen)) == -1) {
            perror("accept error");
          } else {
            FD_SET(newfd, &master);
            if(newfd > fdmax) {
              fdmax = newfd;
            }
            syslog(LOG_INFO, "New connection from %s on socket %d\n", inet_ntoa(clientaddr.sin_addr), newfd);

            /* Send prompt on connection */
            if (friendly) { STREAM_SEND(newfd, MGMT_PROMPT) }
          }
        } else {
          /* handle data from a client */
          if((nbytes = recv(i, buf, sizeof(buf), 0)) <= 0) {
            if(nbytes == 0) {
              syslog(LOG_INFO, "Socket %d hung up\n", i);
            } else {
              perror("recv() error");
            }
 
            close(i);
            FD_CLR(i, &master);
          } else {
            syslog(LOG_DEBUG, "Found data: '%s'\n", buf);
            char *bufptr = &buf[0];
            if (strncasecmp(bufptr, (char *)"help", 4) == 0) {
              STREAM_SEND(i, MGMT_HELP)
              if (friendly) { STREAM_SEND(i, MGMT_PROMPT) }
            } else if (strncasecmp(bufptr, (char *)"counters", 8) == 0) {
              /* send counters */

              statsd_counter_t *s_counter, *tmp;
              HASH_ITER(hh, counters, s_counter, tmp) {
                STREAM_SEND(i, s_counter->key)
                STREAM_SEND(i, ": ")
                STREAM_SEND_LONG_DOUBLE(i, s_counter->value)
                STREAM_SEND(i, "\n")
              }
              if (s_counter) free(s_counter);
              if (tmp) free(tmp);

              STREAM_SEND(i, MGMT_END)
              if (friendly) { STREAM_SEND(i, MGMT_PROMPT) }
            } else if (strncasecmp(bufptr, (char *)"timers", 6) == 0) {
              /* send timers */

              statsd_timer_t *s_timer, *tmp;
              HASH_ITER(hh, timers, s_timer, tmp) {
                STREAM_SEND(i, s_timer->key)
                STREAM_SEND(i, ": ")
                STREAM_SEND_INT(i, s_timer->count)
                if (s_timer->count > 0) {
                  double *j = NULL; bool first = 1;
                  STREAM_SEND(i, " [")
                  while( (j=(double *)utarray_next(s_timer->values, j)) ) {
                    if (first == 1) { first = 0; STREAM_SEND(i, ",") }
                    STREAM_SEND_DOUBLE(i, *j)
                  }
                  STREAM_SEND(i, "]")
                }
                STREAM_SEND(i, "\n")
              }
              if (s_timer) free(s_timer);
              if (tmp) free(tmp);

              STREAM_SEND(i, MGMT_END)
              if (friendly) { STREAM_SEND(i, MGMT_PROMPT) }
            } else if (strncasecmp(bufptr, (char *)"stats", 5) == 0) {
              /* send stats */

              statsd_stat_t *s_stat, *tmp;
              HASH_ITER(hh, stats, s_stat, tmp) {
                if (strlen(s_stat->name.group_name) > 1) {
                  STREAM_SEND(i, s_stat->name.group_name)
                  STREAM_SEND(i, ".")
                }
                STREAM_SEND(i, s_stat->name.key_name)
                STREAM_SEND(i, ": ")
                STREAM_SEND_LONG(i, s_stat->value)
                STREAM_SEND(i, "\n")
              }
              if (s_stat) free(s_stat);
              if (tmp) free(tmp);

              STREAM_SEND(i, MGMT_END)
              if (friendly) { STREAM_SEND(i, MGMT_PROMPT) }
            } else if (strncasecmp(bufptr, (char *)"quit", 4) == 0) {
              /* disconnect */
              close(i);
              FD_CLR(i, &master);
            } else {
              STREAM_SEND(i, MGMT_BADCOMMAND)
              if (friendly) { STREAM_SEND(i, MGMT_PROMPT) }
            }
          }
        }
      }
    }
  }

  /* end mgmt listener */

  syslog(LOG_INFO, "Thread[Mgmt]: Ending thread %d\n", (int) *((int *) ptr));
  pthread_exit(0);
}

void p_thread_flush(void *ptr) {
  syslog(LOG_INFO, "Thread[Flush]: Starting thread %d\n", (int) *((int *) ptr));

  while (1) {
    THREAD_SLEEP(flush_interval);

    gmetric_t gm;

    dump_stats();

    if (enable_gmetric) {
      gmetric_create(&gm);
      if (!gmetric_open(&gm, ganglia_host, ganglia_port)) {
        syslog(LOG_ERR, "Unable to connect to ganglia host %s:%d", ganglia_host, ganglia_port);
        enable_gmetric = 0;
      }
    }

    long ts = time(NULL);
    char *ts_string = ltoa(ts);
    int numStats = 0;
#ifdef SEND_GRAPHITE
    char *statString = NULL;
#endif


    /* ---------------------------------------------------------------------
      Process counter metrics
      -------------------------------------------------------------------- */
    {
      statsd_counter_t *s_counter, *tmp;
      HASH_ITER(hh, counters, s_counter, tmp) {
        long double value = s_counter->value / flush_interval;
#ifdef SEND_GRAPHITE
        char message[BUFLEN];
        sprintf(message, "stats.%s %Lf %ld\nstats_counts_%s %Lf %ld\n", s_counter->key, value, ts, s_counter->key, s_counter->value, ts);
#endif
        if (enable_gmetric) {
          {
            char *k = NULL;
            if (ganglia_metric_prefix != NULL) {
              k = malloc(strlen(s_counter->key) + strlen(ganglia_metric_prefix) + 1);
              sprintf(k, "%s%s", ganglia_metric_prefix, s_counter->key);
            } else {
              k = strdup(s_counter->key);
            }
            SEND_GMETRIC_DOUBLE(k, k, value, "count");
            if (k) free(k);
          }
          {
            //char *k = malloc(strlen(s_counter->key) + 13);
            // sprintf(k, "%s", s_counter->key);
            SEND_GMETRIC_DOUBLE(s_counter->key, s_counter->key, s_counter->value, "count");
            //if (k) free(k);
          }
        }
#ifdef SEND_GRAPHITE
        if (statString) {
          statString = realloc(statString, strlen(statString) + strlen(message));
          strcat(statString, message);
        } else {
          statString = strdup(message);
        }
#endif

        /* Clear counter after we're done with it */
        wait_for_counters_lock();
        s_counter->value = 0;
        remove_counters_lock();

        numStats++;
      }
      if (s_counter) free(s_counter);
      if (tmp) free(tmp);
    }

    /* ---------------------------------------------------------------------
      Process timer metrics
      -------------------------------------------------------------------- */

    {
      statsd_timer_t *s_timer, *tmp;
      HASH_ITER(hh, timers, s_timer, tmp) {
        if (s_timer->count > 0) {
          int pctThreshold = 90; /* TODO FIXME: dynamic assignment */

          /* Sort all values in this timer list */
          wait_for_timers_lock();
          utarray_sort(s_timer->values, double_sort);

          double min = 0;
          double max = 0;
          {
            double *i = NULL; int count = 0;
            while( (i=(double *) utarray_next( s_timer->values, i)) ) {
              if (count == 0) {
                min = *i;
                max = *i;
              } else {
                if (*i < min) min = *i;
                if (*i > max) max = *i;
              }
              count++;
            }
          }

          double mean = min;
          double maxAtThreshold = max;

          if (s_timer->count > 1) {
            // Find the index of the 90th percentile threshold
            int thresholdIndex = ( pctThreshold / 100.0 ) * s_timer->count;
            maxAtThreshold = * ( utarray_eltptr( s_timer->values, thresholdIndex - 1 ) );
            printf("Count = %d Thresh = %d, MaxThreshold = %f\n", s_timer->count, thresholdIndex, maxAtThreshold);

            double sum = 0;
            double *i = NULL; int count = 0;
            while( (i=(double *) utarray_next( s_timer->values, i)) && count < s_timer->count - 1 ) {
              sum += *i;
              count++;
            }
            mean = sum / s_timer->count;
          }

          /* Clear all values for this timer */
          utarray_clear(s_timer->values);
          s_timer->count = 0;
          remove_timers_lock();


#ifdef SEND_GRAPHITE
          char message[BUFLEN];
          sprintf(message, "stats.timers.%s.mean %f %ld\n"
            "stats.timers.%s.upper %f %ld\n"
            "stats.timers.%s.upper_%d %f %ld\n"
            "stats.timers.%s.lower %f %ld\n"
            "stats.timers.%s.count %d %ld\n",
            s_timer->key, mean, ts,
            s_timer->key, max, ts,
            s_timer->key, pctThreshold, maxAtThreshold, ts,
            s_timer->key, min, ts,
            s_timer->key, s_timer->count, ts
          );
#endif

          if (enable_gmetric) {
            {
              // Mean value. Convert to seconds
              char k[strlen(s_timer->key) + 6];
              sprintf(k, "%s_mean", s_timer->key);
              SEND_GMETRIC_DOUBLE(s_timer->key, k, mean/1000, "sec");
            }
            {
              // Max value. Convert to seconds
              char k[strlen(s_timer->key) + 7];
              sprintf(k, "%s_upper", s_timer->key);
              SEND_GMETRIC_DOUBLE(s_timer->key, k, max/1000, "sec");
            }
            {
              // Percentile value. Convert to seconds
              char k[strlen(s_timer->key) + 12];
              sprintf(k, "%s_%dth_pct", s_timer->key, pctThreshold);
              SEND_GMETRIC_DOUBLE(s_timer->key, k, maxAtThreshold/1000, "sec");
            }
            {
              char k[strlen(s_timer->key) + 7];
              sprintf(k, "%s_lower", s_timer->key);
              SEND_GMETRIC_DOUBLE(s_timer->key, k, min/1000, "sec");
            }
            {
              char k[strlen(s_timer->key) + 7];
              sprintf(k, "%s_count", s_timer->key);
              SEND_GMETRIC_INT(s_timer->key, k, s_timer->count, "count");
            }
          }
#ifdef SEND_GRAPHITE
          if (statString) {
            statString = realloc(statString, strlen(statString) + strlen(message));
            strcat(statString, message);
          } else {
            statString = strdup(message);
          }
#endif
        }
        numStats++;
      }
      if (s_timer) free(s_timer);
      if (tmp) free(tmp);
    }

    {
#ifdef SEND_GRAPHITE
      char *message = malloc(sizeof(char) * BUFLEN);
      sprintf(message, "statsd.numStats %d %ld\n", numStats, ts);
#endif
      if (enable_gmetric) {
        SEND_GMETRIC_INT("statsd", "statsd_numstats_collected", numStats, "count");
      }
#ifdef SEND_GRAPHITE
      if (statString) {
        statString = realloc(statString, strlen(statString) + strlen(message));
        strcat(statString, message);
      } else {
        statString = strdup(message);
      }
#endif
    }

    /* TODO: Flush to graphite */
#ifdef SEND_GRAPHITE
    printf("Messages:\n%s", statString);
#endif

    if (enable_gmetric) {
      gmetric_close(&gm);
    }

    if (ts_string) free(ts_string);
#ifdef SEND_GRAPHITE
    if (statString) free(statString);
#endif
  }

  syslog(LOG_INFO, "Thread[Flush]: Ending thread %d\n", (int) *((int *) ptr));
  pthread_exit(0);
}

