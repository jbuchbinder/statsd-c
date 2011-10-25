/*
 *          STATSD-C
 *          C port of Etsy's node.js-based statsd server
 *
 *          http://github.com/jbuchbinder/statsd-c
 *
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "serialize.h"
#include "stats.h"
#include "timers.h"
#include "counters.h"
#include "strings.h"
#include "embeddedgmetric.h"

#define BUFLEN 1024

/* Default statsd ports */
#define PORT 8125
#define MGMT_PORT 8126
#define GANGLIA_PORT 8649

/* Define stat flush interval in sec */
#define FLUSH_INTERVAL 10

#define THREAD_SLEEP(x) { pthread_mutex_t fakeMutex = PTHREAD_MUTEX_INITIALIZER; pthread_cond_t fakeCond = PTHREAD_COND_INITIALIZER; struct timespec timeToWait; struct timeval now; int rt; gettimeofday(&now,NULL); timeToWait.tv_sec = now.tv_sec + x; timeToWait.tv_nsec = now.tv_usec; pthread_mutex_lock(&fakeMutex); rt = pthread_cond_timedwait(&fakeCond, &fakeMutex, &timeToWait); if (rt != 0) { } pthread_mutex_unlock(&fakeMutex); }
#define STREAM_SEND(x,y) if (send(x, y, strlen(y), 0) == -1) { perror("send error"); }
#define STREAM_SEND_LONG(x,y) { \
	char *z = malloc(sizeof(char *)); \
	sprintf(z, "%ld", y); \
	if (send(x, z, strlen(z), 0) == -1) { perror("send error"); } \
	if (z) free(z); \
	}
#define STREAM_SEND_INT(x,y) { \
	char *z = malloc(sizeof(char *)); \
	sprintf(z, "%d", y); \
	if (send(x, z, strlen(z), 0) == -1) { perror("send error"); } \
	if (z) free(z); \
	}
#define STREAM_SEND_DOUBLE(x,y) { \
	char *z = malloc(sizeof(char *)); \
	sprintf(z, "%f", y); \
	if (send(x, z, strlen(z), 0) == -1) { perror("send error"); } \
	if (z) free(z); \
	}
#define STREAM_SEND_LONG_DOUBLE(x,y) { \
	char *z = malloc(sizeof(char *)); \
	sprintf(z, "%Lf", y); \
	if (send(x, z, strlen(z), 0) == -1) { perror("send error"); } \
	if (z) free(z); \
	}
#define UPDATE_LAST_MSG_SEEN() { \
	char *time_sec = malloc(sizeof(char *)); \
	sprintf(time_sec, "%ld", time(NULL)); \
	update_stat( "messages", "last_msg_seen", time_sec); \
	if (time_sec) free(time_sec); \
	}

#define MGMT_END "END\n\n"
#define MGMT_BADCOMMAND "ERROR\n"
#define MGMT_PROMPT "statsd> "
#define MGMT_HELP "Commands: stats, counters, timers, quit\n\n"

/*
 * GMETRIC SENDING
 */

#define SEND_GMETRIC_DOUBLE(myname, myvalue, myunit) { \
	char buf[GMETRIC_MAX_MESSAGE_LEN]; \
	char *bufptr = &buf[0]; \
	gmetric_message_t msg; \
	msg.format = GMETRIC_FORMAT_31; \
	msg.type = GMETRIC_VALUE_DOUBLE; \
	msg.name = myname; \
	msg.value.v_double = myvalue; \
	msg.units = myunit; \
	msg.slope = GMETRIC_SLOPE_BOTH; \
	msg.tmax = flush_interval; \
	msg.dmax = 0; \
	int len = gmetric_message_create_xdr(bufptr, sizeof(buf), &msg); \
	if (len != -1) { \
		syslog(LOG_INFO, "Sending gmetric message length %d", len); \
		gmetric_send_xdr(&gm, bufptr, len); \
	} else { \
		syslog(LOG_ERR, "Failed to send gmetric %s", myname); \
	} \
	}
#define SEND_GMETRIC_INT(myname, myvalue, myunit) { \
	char buf[GMETRIC_MAX_MESSAGE_LEN]; \
	char *bufptr = &buf[0]; \
	gmetric_message_t msg; \
	msg.format = GMETRIC_FORMAT_31; \
	msg.type = GMETRIC_VALUE_INT; \
	msg.name = myname; \
	msg.value.v_double = myvalue; \
	msg.units = myunit; \
	msg.slope = GMETRIC_SLOPE_BOTH; \
	msg.tmax = flush_interval; \
	msg.dmax = 0; \
	int len = gmetric_message_create_xdr(bufptr, sizeof(buf), &msg); \
	if (len != -1) { \
		syslog(LOG_INFO, "Sending gmetric message length %d", len); \
		gmetric_send_xdr(&gm, bufptr, len); \
	} else { \
		syslog(LOG_ERR, "Failed to send gmetric %s", myname); \
	} \
	}
#define SEND_GMETRIC_STRING(myname, myvalue, myunit) { \
	char buf[GMETRIC_MAX_MESSAGE_LEN]; \
	char *bufptr = &buf[0]; \
	gmetric_message_t msg; \
	msg.format = GMETRIC_FORMAT_31; \
	msg.type = GMETRIC_VALUE_STRING; \
	msg.name = myname; \
	msg.value.v_double = myvalue; \
	msg.units = myunit; \
	msg.slope = GMETRIC_SLOPE_BOTH; \
	msg.tmax = flush_interval; \
	msg.dmax = 0; \
	int len = gmetric_message_create_xdr(bufptr, sizeof(buf), &msg); \
	if (len != -1) { \
		syslog(LOG_INFO, "Sending metric message length %d", len); \
		gmetric_send_xdr(&gm, bufptr, len); \
	} else { \
		syslog(LOG_ERR, "Failed to send gmetric %s", myname); \
	} \
	}

/*
 * GLOBAL VARIABLES
 */

statsd_stat_t *stats = NULL;
sem_t stats_lock;
statsd_counter_t *counters = NULL;
sem_t counters_lock;
statsd_timer_t *timers = NULL;
sem_t timers_lock;

int stats_udp_socket, stats_mgmt_socket;
pthread_t thread_stat;
pthread_t thread_udp;
pthread_t thread_mgmt;
pthread_t thread_flush;
int port = PORT, mgmt_port = MGMT_PORT, ganglia_port = GANGLIA_PORT, flush_interval = FLUSH_INTERVAL;
int debug = 0, friendly = 0, clear_stats = 0, daemonize = 0, enable_gmetric = 0;
char *serialize_file = NULL, *ganglia_host = NULL;

/*
 * FUNCTION PROTOTYPES
 */

void add_timer( char *key, double value );
void update_stat( char *group, char *key, char *value);
void update_counter( char *key, double value, double sample_rate );
void update_timer( char *key, double value );
void dump_stats();
void p_thread_udp(void *ptr);
void p_thread_mgmt(void *ptr);
void p_thread_stat(void *ptr);
void p_thread_flush(void *ptr);

void init_stats() {
  char *startup_time = malloc(sizeof(char *));
  sprintf(startup_time, "%ld", time(NULL));

  if (serialize_file && !clear_stats) {
    syslog(LOG_DEBUG, "Deserializing stats from file.");
    statsd_deserialize(serialize_file);
  }

  remove_stats_lock();

  update_stat( "graphite", "last_flush", startup_time );
  update_stat( "messages", "last_msg_seen", startup_time );
  update_stat( "messages", "bad_lines_seen", "0" );

  if (startup_time) free(startup_time);
}

void cleanup() {
  pthread_cancel(thread_flush);
  pthread_cancel(thread_stat);
  pthread_cancel(thread_udp);
  pthread_cancel(thread_mgmt);

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
}

void die_with_error(char *s) {
  perror(s);
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

int main(int argc, char *argv[]) {
  int pids[4] = { 1, 2, 3, 4 };
  int opt;

  signal (SIGINT, sigint_handler);
  signal (SIGQUIT, sigquit_handler);

  sem_init(&stats_lock, 0, 1);
  sem_init(&timers_lock, 0, 1);
  sem_init(&counters_lock, 0, 1);

  while ((opt = getopt(argc, argv, "dDfhp:m:s:cg:G:F:")) != -1) {
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
      case 'h':
        fprintf(stderr, "Usage: %s [-hDdfFc] [-p port] [-m port] [-s file] [-G host] [-g port]\n", argv[0]);
        fprintf(stderr, "\t-p port           set statsd udp listener port (default 8125)\n");
        fprintf(stderr, "\t-m port           set statsd management port (default 8126)\n");
        fprintf(stderr, "\t-s file           serialize state to and from file (default disabled)\n");
        fprintf(stderr, "\t-G host           ganglia host (default disabled)\n");
        fprintf(stderr, "\t-g port           ganglia port (default 8649)\n");
        fprintf(stderr, "\t-h                this help display\n");
        fprintf(stderr, "\t-d                enable debug\n");
        fprintf(stderr, "\t-D                daemonize\n");
        fprintf(stderr, "\t-f                enable friendly mode (breaks wire compatibility)\n");
        fprintf(stderr, "\t-F seconds        set flush interval in seconds (default 10)\n");
        fprintf(stderr, "\t-c                clear stats on startup\n");
        exit(1);
      default:
        break;
    }
  }

  if (debug) {
    setlogmask(LOG_UPTO(LOG_DEBUG));
    openlog("statsd-c", LOG_CONS | LOG_NDELAY | LOG_PERROR | LOG_PID, LOG_USER);
  } else {
    setlogmask(LOG_UPTO(LOG_INFO));
    openlog("statsd-c", LOG_CONS, LOG_USER);
  }

  /* Initialization of certain stats, here. */
  init_stats();

  pthread_create (&thread_stat,  NULL, (void *) &p_thread_stat,  (void *) &pids[0]);
  pthread_create (&thread_udp,   NULL, (void *) &p_thread_udp,   (void *) &pids[1]);
  pthread_create (&thread_mgmt,  NULL, (void *) &p_thread_mgmt,  (void *) &pids[2]);
  pthread_create (&thread_flush, NULL, (void *) &p_thread_flush, (void *) &pids[3]);

  pthread_join(thread_stat,  NULL);
  pthread_join(thread_udp,   NULL);
  pthread_join(thread_mgmt,  NULL);
  pthread_join(thread_flush, NULL);

  return 0;
}

void add_timer( char *key, double value ) {
  statsd_timer_t *t;
  wait_for_timers_lock();
  HASH_FIND_STR( timers, key, t );
  if (t) {
    /* Add to old entry */
    int pos = t->count++;
    t->values[pos - 1] = value;
  } else {
    /* Create new entry */
    t = malloc(sizeof(statsd_timer_t));

    strcpy(t->key, key);
    t->count = 1;
    t->values[t->count - 1] = value;

    HASH_ADD_STR( timers, key, t );
  }
  remove_timers_lock();
}

/**
 * Record or update stat value.
 */
void update_stat( char *group, char *key, char *value ) {
  syslog(LOG_DEBUG, "update_stat ( %s, %s, %s )\n", group, key, value);
  statsd_stat_t *s;
  statsd_stat_name_t l;

  wait_for_stats_lock();

  memset(&l, 0, sizeof(statsd_stat_name_t));
  strcpy(l.group_name, group);
  strcpy(l.key_name, key);
  syslog(LOG_DEBUG, "HASH_FIND '%s' '%s'\n", l.group_name, l.key_name);
  HASH_FIND( hh, stats, &l, sizeof(statsd_stat_name_t), s );

  if (s) {
    syslog(LOG_DEBUG, "Updating old stat entry");

    s->value = atol( value );
  } else {
    syslog(LOG_DEBUG, "Adding new stat entry");
    s = malloc(sizeof(statsd_stat_t));
    memset(s, 0, sizeof(statsd_stat_t));

    strcpy(s->name.group_name, group);
    strcpy(s->name.key_name, key);
    s->value = atol(value);
    s->locked = 0;

    HASH_ADD( hh, stats, name, sizeof(statsd_stat_name_t), s );
  }
  remove_stats_lock();
}

void update_counter( char *key, double value, double sample_rate ) {
  syslog(LOG_DEBUG, "update_counter ( %s, %f, %f )\n", key, value, sample_rate);
  statsd_counter_t *c;
  wait_for_counters_lock();
  HASH_FIND_STR( counters, key, c );
  if (c) {
    syslog(LOG_DEBUG, "Updating old counter entry");
    if (sample_rate == 0) {
      c->value = c->value + value;
    } else {
      c->value = c->value + ( value * ( 1 / sample_rate ) );
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

    HASH_ADD_STR( counters, key, c );
  }
  remove_counters_lock();
}

void update_timer( char *key, double value ) {
  syslog(LOG_DEBUG, "update_timer ( %s, %f )\n", key, value);
  statsd_timer_t *t;
  wait_for_timers_lock();
  HASH_FIND_STR( timers, key, t );
  remove_timers_lock();
  if (t) {
    syslog(LOG_DEBUG, "Updating old timer entry");
    wait_for_timers_lock();
    t->values[t->count] = value;
    t->count++;
    remove_timers_lock();
  } else {
    syslog(LOG_DEBUG, "Adding new timer entry");
    t = malloc(sizeof(statsd_timer_t));

    strcpy(t->key, key);
    t->count = 0;

    wait_for_timers_lock();
    t->values[t->count] = value;
    t->count++;
    remove_timers_lock();

    wait_for_timers_lock();
    HASH_ADD_STR( timers, key, t );
    remove_timers_lock();
  }
}

void dump_stats() {
  if (debug) {
    {
      printf("\n\nStats dump\n==============================\n");
      statsd_stat_t *s, *tmp;
      wait_for_stats_lock();
      HASH_ITER(hh, stats, s, tmp) {
        printf("\t%s.%s: %ld\n", s->name.group_name, s->name.key_name, s->value);
      }
      remove_stats_lock();
      if (s) free(s);
      if (tmp) free(tmp);
      printf("==============================\n\n");
    }

    {
      printf("\n\nCounters dump\n==============================\n");
      statsd_counter_t *c, *tmp;
      wait_for_counters_lock();
      HASH_ITER(hh, counters, c, tmp) {
        printf("\t%s: %Lf\n", c->key, c->value);
      }
      remove_counters_lock();
      if (c) free(c);
      if (tmp) free(tmp);
      printf("==============================\n\n");
    }
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
      key_name = malloc( strlen(token) + 1);
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
              s_sample_rate = malloc(strlen(subtoken) + 1);
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
          sample_rate = strtod( (char *) *(s_sample_rate + 1), (char **) NULL );
        }
        update_counter(key_name, value, sample_rate);
        syslog(LOG_DEBUG, "UDP: Found key name '%s'\n", key_name);
        syslog(LOG_DEBUG, "UDP: Found value '%f'\n", value);
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
    syslog(LOG_DEBUG, "UDP: Bound to socket.");

    while (1) {
      waitd.tv_sec = 1;
      waitd.tv_usec = 0;
      FD_ZERO(&read_flags);
      FD_ZERO(&write_flags);
      FD_SET(stats_udp_socket, &read_flags);

      stat = select(stats_udp_socket+1, &read_flags, &write_flags, (fd_set*)0, &waitd);
      /* If we can't do anything for some reason, wait a bit */
      if (stat < 0) {
        sleep(1);
        continue;
      }

      char buf_in[BUFLEN];
      if (FD_ISSET(stats_udp_socket, &read_flags)) {
        FD_CLR(stats_udp_socket, &read_flags);
        memset(&buf_in, 0, sizeof(buf_in));
        if (read(stats_udp_socket, buf_in, sizeof(buf_in)-1) <= 0) {
          close(stats_udp_socket);
          break;
        }

        syslog(LOG_DEBUG, "UDP: Received packet from %s:%d\nData: %s\n\n", 
            inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port), buf_in);

        process_stats_packet(buf_in);
      }
    }

    if (stats_udp_socket) close(stats_udp_socket);

    /* end udp listener */
  syslog(LOG_INFO, "Thread[Udp]: Ending thread %d\n", (int) *((int *) ptr));
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
              wait_for_counters_lock();
              HASH_ITER(hh, counters, s_counter, tmp) {
                STREAM_SEND(i, s_counter->key)
                STREAM_SEND(i, ": ")
                STREAM_SEND_LONG_DOUBLE(i, s_counter->value)
                STREAM_SEND(i, "\n")
              }
              remove_counters_lock();
              if (s_counter) free(s_counter);
              if (tmp) free(tmp);

              STREAM_SEND(i, MGMT_END)
              if (friendly) { STREAM_SEND(i, MGMT_PROMPT) }
            } else if (strncasecmp(bufptr, (char *)"timers", 6) == 0) {
              /* send timers */

              statsd_timer_t *s_timer, *tmp;
              wait_for_timers_lock();
              HASH_ITER(hh, timers, s_timer, tmp) {
                STREAM_SEND(i, s_timer->key)
                STREAM_SEND(i, ": ")
                STREAM_SEND_INT(i, s_timer->count)
                if (s_timer->count > 0) {
                  int j;
		  STREAM_SEND(i, " [")
                  for (j=0; j<s_timer->count; j++) {
                    if (j != 0) { STREAM_SEND(i, ",") }
                    STREAM_SEND_DOUBLE(i, s_timer->values[j])
                  }
		  STREAM_SEND(i, "]")
                }
                STREAM_SEND(i, "\n")
              }
              remove_timers_lock();
              if (s_timer) free(s_timer);
              if (tmp) free(tmp);

              STREAM_SEND(i, MGMT_END)
              if (friendly) { STREAM_SEND(i, MGMT_PROMPT) }
            } else if (strncasecmp(bufptr, (char *)"stats", 5) == 0) {
              /* send stats */

              statsd_stat_t *s_stat, *tmp;
              wait_for_stats_lock();
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
              remove_stats_lock();
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

void p_thread_stat(void *ptr) {
  syslog(LOG_INFO, "Thread[Stat]: Starting thread %d\n", (int) *((int *) ptr));
  while (1) {
    dump_stats();
    sleep(10);
  }
  syslog(LOG_INFO, "Thread[Stat]: Ending thread %d\n", (int) *((int *) ptr));
  pthread_exit(0);
}

void p_thread_flush(void *ptr) {
  syslog(LOG_INFO, "Thread[Flush]: Starting thread %d\n", (int) *((int *) ptr));

  while (1) {
    THREAD_SLEEP(flush_interval);

    gmetric_t gm;

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
    char *statString = NULL;

    {
      statsd_counter_t *s_counter, *tmp;
      wait_for_counters_lock();
      HASH_ITER(hh, counters, s_counter, tmp) {
        long double value = s_counter->value / flush_interval;
#ifdef SEND_GRAPHITE
        char *message = malloc(sizeof(char) * BUFLEN);
        sprintf(message, "stats.%s %Lf %ld\nstats_counts_%s %Lf %ld\n", s_counter->key, value, ts, s_counter->key, s_counter->value, ts);
#endif
        if (enable_gmetric) {
          {
            char *k = malloc(strlen(s_counter->key) + 6);
            sprintf(k, "stats_%s", s_counter->key);
            SEND_GMETRIC_DOUBLE(k, value, "no units");
            if (k) free(k);
          }
          {
            char *k = malloc(strlen(s_counter->key) + 13);
            sprintf(k, "stats_counts_%s", s_counter->key);
            SEND_GMETRIC_DOUBLE(k, s_counter->value, "no units");
            if (k) free(k);
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
        s_counter->value = 0;

        numStats++;
      }
      remove_counters_lock();
      if (s_counter) free(s_counter);
      if (tmp) free(tmp);
    }

    {
      statsd_timer_t *s_timer, *tmp;
      wait_for_timers_lock();
      HASH_ITER(hh, timers, s_timer, tmp) {
        if (s_timer->count > 0) {
          int pctThreshold = 90; /* TODO FIXME: dynamic assignment */
          double min;
          double max;
          {
            int i;
            for(i = 0; i < s_timer->count; i++) {
              if (i == 0) {
                min = s_timer->values[i];
                max = s_timer->values[i];
              } else {
                if (s_timer->values[i] < min) min = s_timer->values[i];
                if (s_timer->values[i] > max) max = s_timer->values[i];
              }
            } 
          }

          double mean = min;
          double maxAtThreshold = max;

          if (s_timer->count > 1) {
            double thresholdIndex = ((100 - pctThreshold) / 100) * s_timer->count;
            int numInThreshold = s_timer->count - thresholdIndex;
            maxAtThreshold = s_timer->values[numInThreshold - 1];

            double sum = 0;
            int i;
            for (i = 0; i < numInThreshold; i++) {
              sum += s_timer->values[i];
            }
            mean = sum / numInThreshold;
          }

#ifdef SEND_GRAPHITE
          char *message = malloc(sizeof(char) * BUFLEN);
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
              char *k = malloc(strlen(s_timer->key) + 18);
              sprintf(k, "stats_timers_%s_mean", s_timer->key);
              SEND_GMETRIC_DOUBLE(k, mean, "no units");
              if (k) free(k);
            }
            {
              char *k = malloc(strlen(s_timer->key) + 19);
              sprintf(k, "stats_timers_%s_upper", s_timer->key);
              SEND_GMETRIC_DOUBLE(k, max, "no units");
              if (k) free(k);
            }
            {
              char *k = malloc(strlen(s_timer->key) + 30);
              sprintf(k, "stats_timers_%s_upper_%d", s_timer->key, pctThreshold);
              SEND_GMETRIC_DOUBLE(k, maxAtThreshold, "no units");
              if (k) free(k);
            }
            {
              char *k = malloc(strlen(s_timer->key) + 19);
              sprintf(k, "stats_timers_%s_lower", s_timer->key);
              SEND_GMETRIC_DOUBLE(k, min, "no units");
              if (k) free(k);
            }
            {
              char *k = malloc(strlen(s_timer->key) + 19);
              sprintf(k, "stats_timers_%s_count", s_timer->key);
              SEND_GMETRIC_INT(k, s_timer->count, "no units");
              if (k) free(k);
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
      remove_timers_lock();
      if (s_timer) free(s_timer);
      if (tmp) free(tmp);
    }

    {
#ifdef SEND_GRAPHITE
      char *message = malloc(sizeof(char) * BUFLEN);
      sprintf(message, "statsd.numStats %d %ld\n", numStats, ts);
#endif
      if (enable_gmetric) {
        SEND_GMETRIC_DOUBLE("statd_numStats", numStats, "no units");
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

