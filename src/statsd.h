/*
 *          STATSD-C
 *          C port of Etsy's node.js-based statsd server
 *
 *          http://github.com/jbuchbinder/statsd-c
 *
 */

#ifndef STATSD_H
#define STATSD_H 1

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

#define SEND_GMETRIC_DOUBLE(mygroup, myname, myvalue, myunit) { \
	gmetric_message_t msg = { \
		.format = GMETRIC_FORMAT_31, \
		.type = GMETRIC_VALUE_DOUBLE, \
		.name = myname, \
                .group = mygroup, \
		.hostname = ganglia_spoof, \
		.value.v_double = myvalue, \
		.units = myunit, \
		.slope = GMETRIC_SLOPE_BOTH, \
		.tmax = 60, \
		.dmax = flush_interval + 10, \
		.spoof = 1 \
	}; \
	int len = gmetric_send(&gm, &msg); \
	if (len != -1) { \
		syslog(LOG_INFO, "Sending gmetric DOUBLE message %s length %d", myname, len); \
	} else { \
		syslog(LOG_ERR, "Failed to send gmetric %s", myname); \
	} \
	}
#define SEND_GMETRIC_INT(mygroup, myname, myvalue, myunit) { \
	gmetric_message_t msg = { \
		.format = GMETRIC_FORMAT_31, \
		.type = GMETRIC_VALUE_INT, \
		.name = myname, \
                .group = mygroup, \
		.hostname = ganglia_spoof, \
		.value.v_double = myvalue, \
		.units = myunit, \
		.slope = GMETRIC_SLOPE_BOTH, \
		.tmax = 60, \
		.dmax = flush_interval + 10, \
		.spoof = 1 \
	}; \
	int len = gmetric_send(&gm, &msg); \
	if (len != -1) { \
		syslog(LOG_INFO, "Sending gmetric INT message %s length %d", myname, len); \
	} else { \
		syslog(LOG_ERR, "Failed to send gmetric %s", myname); \
	} \
	}
#define SEND_GMETRIC_STRING(myname, myvalue, myunit) { \
	gmetric_message_t msg = { \
		.format = GMETRIC_FORMAT_31, \
		.type = GMETRIC_VALUE_STRING, \
		.name = myname, \
		.hostname = ganglia_spoof, \
		.value.v_double = myvalue, \
		.units = myunit, \
		.slope = GMETRIC_SLOPE_BOTH, \
		.tmax = flush_interval, \
		.dmax = 30, \
		.spoof = 1 \
	}; \
	int len = gmetric_send(&gm, &msg); \
	if (len != -1) { \
		syslog(LOG_INFO, "Sending gmetric STRING message %s length %d", myname, len); \
	} else { \
		syslog(LOG_ERR, "Failed to send gmetric %s", myname); \
	} \
	}

#endif /* STATSD_H */

