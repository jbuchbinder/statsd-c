/*
 *          STATSD-C
 *          C port of Etsy's node.js-based statsd server
 *
 *          http://github.com/jbuchbinder/statsd-c
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sanitize_key(char *k) {
  char *dest = malloc(strlen(k) + 1);
  char *p = k;
  int c = 0;
  while (*p != '\0') {
    if (*p == '.') {
      *(dest + c) = '_';
      c++;
    } else if (*p == '\\' || *p == '/') {
      *(dest + c) = '-';
      c++;
    } else if ( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' ) {
      *(dest + c) = *p;
      c++;
    }
    p++;
  }
  *(dest + c) = '\0';
  memcpy(k, dest, c + 1);
  if (dest) free(dest);
}

void sanitize_value(char *k) {
  char *dest = malloc(strlen(k) + 1);
  char *p = k;
  int c = 0;
  while (*p != '\0') {
    if ( *p == '.' || *p == '-' || (*p >= '0' && *p <= '9') ) {
      *(dest + c) = *p;
      c++;
    }
    p++;
  }
  *(dest + c) = '\0';
  memcpy(k, dest, c + 1);
  if (dest) free(dest);
}

char *ltoa(long l) {
  char *tmp = malloc(20 * sizeof(char));
  sprintf(tmp, "%ld", l);
  return tmp;
}

char *ldtoa(long double ld) {
  char *tmp = malloc(20 * sizeof(char));
  sprintf(tmp, "%Lf", ld);
  return tmp;
}

