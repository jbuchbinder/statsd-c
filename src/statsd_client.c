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
#include <sys/types.h>
#include <rpc/rpc.h>
#include <getopt.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t *resolve_host(const char *addr);
void usage(char *argv[]);

int main(int argc, char *argv[]) {
  struct sockaddr_in *sa;

  char buf[1024];
  char *host = NULL, *counter = NULL, *timer = NULL;
  uint32_t *ip;
  long value;
  int port = 8125, sample_rate = 1;

  int opt;
  while ((opt = getopt(argc, argv, "hH:p:c:v:t:s:")) != -1) {
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
  memset(&sa, 0, sizeof(struct sockaddr_in *));
  sa->sin_family = AF_INET;
  sa->sin_port = htons(port);
  memcpy(&sa->sin_addr, &ip, sizeof(ip));
  if (!sendto(s, (char*) buf, strlen(buf), 0, (struct sockaddr*) sa, sizeof(struct sockaddr_in))) {
    return 1;
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
  fprintf(stderr, "Usage: %s [-h] [-H host] [-p port] [-c counter] [-t timer] [-v value]\n", argv[0]);
  fprintf(stderr, "\t-h               This help screen\n");
  fprintf(stderr, "\t-H host          Destination statsd server name/ip\n");
  fprintf(stderr, "\t-p port          Destination statsd server port (defaults to 8125)\n");
  fprintf(stderr, "\t-c counter       Counter name (required, or timer)\n");
  fprintf(stderr, "\t-t timer         Timer name (required, or counter)\n");
  fprintf(stderr, "\t-v value         Value (required)\n");
  fprintf(stderr, "\nBoth a counter and timer cannot exist at the same time.\n");
}

