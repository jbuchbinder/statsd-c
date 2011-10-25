/*
 *          STATSD-C
 *          C port of Etsy's node.js-based statsd server
 *
 *          http://github.com/jbuchbinder/statsd-c
 *
 */

#ifndef __STRINGS_H__
#define __STRINGS_H__ 1

void sanitize_key(char *k);
void sanitize_value(char *k);
void appendstring(char *orig, char *addition);
char *ltoa(long l);
char *ldtoa(long double ld);

#endif /* __STRINGS_H__ */

