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
#include <sys/types.h>
#include <rpc/rpc.h>
#include <getopt.h>
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_TIME_H
#include <time.h>
#endif

uint32_t *resolve_host(const char *addr);
void usage(char *argv[]);

double get_time () {
  struct timeval t;
  struct timezone tzp;
  gettimeofday(&t, &tzp);
  return t.tv_sec + t.tv_usec*1e-6;
}

int main(int argc, char *argv[]) {
  struct sockaddr_in *sa;

  char buf[1024];
  char *host = NULL, *counter = NULL, *timer = NULL;
  uint32_t *ip;
  long value;
  int port = 8125, sample_rate = 1, performance_test = 0;

  int opt;
  while ((opt = getopt(argc, argv, "hH:p:c:v:t:s:P")) != -1) {
    switch (opt) {
      case 'h':
        usage(argv);
        return 1;
        break;
      case 'c':
        counter = malloc((strlen(optarg) + 1) * sizeof(char));
        strcpy(counter, optarg);
        break;
      case 't':
        timer = malloc((strlen(optarg) + 1) * sizeof(char));
        strcpy(timer, optarg);
        break;
      case 'p':
        port = atoi(optarg);
        break;
      case 's':
        sample_rate = atoi(optarg);
        break;
      case 'H':
        host = malloc((strlen(optarg) + 1) * sizeof(char));
        strcpy(host, optarg);
        break;
      case 'v':
        value = atol(optarg);
        break;
      case 'P':
        performance_test = 1;
        break;
    }
  }

  /* Sanity checking */
  if ((timer != NULL && counter != NULL) || (counter == NULL && timer == NULL)) {
    usage(argv);
    return 1;
  }

  /* Format message */
  if (timer != NULL) {
    sprintf(buf, "%s:%ld||%d", timer, value, sample_rate);
  } else if (counter != NULL) {
    sprintf(buf, "%s:%ld|c", counter, value);
  }

  /* Send message */
  int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  ip = resolve_host(host);
  sa = malloc(sizeof(struct sockaddr_in *));
/*  memset(&sa, 0, sizeof(struct sockaddr_in)); */
  sa->sin_family = AF_INET;
  sa->sin_port = htons(port);
  memcpy(&sa->sin_addr, &ip, sizeof(ip));
  if (performance_test) {
    double starting_time, ending_time;
    int err = 0;
    int iter;
    starting_time = get_time();
    for (iter=0; iter<10000; iter++) {
      if (!sendto(s, (char*) buf, strlen(buf), 0, (struct sockaddr*) sa, sizeof(struct sockaddr_in))) {
        err++;
      }
    }
    ending_time = get_time();
    printf("10k queries in %f seconds\n", ending_time - starting_time);
    return 0;
  } else {
    if (!sendto(s, (char*) buf, strlen(buf), 0, (struct sockaddr*) sa, sizeof(struct sockaddr_in))) {
      return 1;
    }
  }
  return 0;
}

uint32_t *resolve_host(const char *addr) {
    struct hostent* result = NULL;
#ifdef __linux__
    struct hostent he;
    char tmpbuf[1024];
    int local_errno = 0;
    if (gethostbyname_r(addr, &he, tmpbuf, sizeof(tmpbuf),
                        &result, &local_errno)) {
        return 0;
    }
#else
    result = gethostbyname(addr);
#endif

    if (result == NULL || result->h_addr_list[0] == NULL || result->h_length != 4) {
        return 0;
    }

    uint32_t* ip = (uint32_t*) result->h_addr_list[0];
    return ip;
}

void usage(char *argv[]) {
  fprintf(stderr, "Usage: %s [-hP] [-H host] [-p port] [-c counter] [-t timer] [-v value]\n", argv[0]);
  fprintf(stderr, "\t-h               This help screen\n");
  fprintf(stderr, "\t-H host          Destination statsd server name/ip\n");
  fprintf(stderr, "\t-p port          Destination statsd server port (defaults to 8125)\n");
  fprintf(stderr, "\t-c counter       Counter name (required, or timer)\n");
  fprintf(stderr, "\t-t timer         Timer name (required, or counter)\n");
  fprintf(stderr, "\t-v value         Value (required)\n");
  fprintf(stderr, "\t-P               Performance testing mode (disabled by default)\n");
  fprintf(stderr, "\nBoth a counter and timer cannot exist at the same time.\n");
}

