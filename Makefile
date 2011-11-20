#
#	STATSD-C
#	C port of Etsy's node.js-based statsd server
#
#	http://github.com/jbuchbinder/statsd-c
#

VERSION=`cat VERSION`
BINARY= statsd
OBJECTS= \
	src/statsd.o \
	src/queue.o \
	src/serialize.o \
	src/strings.o \
	src/embeddedgmetric/embeddedgmetric.o \
	src/embeddedgmetric/modp_numtoa.o \
	src/json-c/arraylist.o \
	src/json-c/debug.o \
	src/json-c/json_object.o \
	src/json-c/json_tokener.o \
	src/json-c/json_util.o \
	src/json-c/linkhash.o \
	src/json-c/printbuf.o

CLIENT_BINARY= statsd_client
CLIENT_OBJECTS= \
	src/statsd_client.o

ALL_COMPILED_OBJECTS= \
	$(BINARY) \
	$(CLIENT_BINARY) \
	$(OBJECTS) \
	$(CLIENT_OBJECTS)

# Compilation settings
CC= gcc
CFLAGS= -fPIC -Wall -pthread -lpthread \
	-Isrc/json-c -Isrc/embeddedgmetric \
	-Wno-format-security -Wno-int-to-pointer-cast \
	-DSTATSD_VERSION=\"$(VERSION)\"

# ANSI formatting
BOLD=\033[1m
UNBOLD=\033[0m

all: compile
	@echo "Built version $(VERSION)"

compile: clean $(BINARY) $(CLIENT_BINARY)

clean:
	@for F in $(ALL_COMPILED_OBJECTS); do \
		echo " RM $(BOLD)$$F $(UNBOLD)"; \
		rm -f $$F; \
	done

$(BINARY): $(OBJECTS)
	@echo " LD $(BOLD)$(BINARY)$(UNBOLD)"
	@$(CC) $(CFLAGS) -o $@ $(OBJECTS)

$(CLIENT_BINARY): $(CLIENT_OBJECTS)
	@echo " LD $(BOLD)$(CLIENT_BINARY)$(UNBOLD)"
	@$(CC) $(CFLAGS) -o $@ $(CLIENT_OBJECTS)

.c.o:
	@echo " CC $(BOLD)$@$(UNBOLD)"
	@gcc ${CFLAGS} -o $@ -c $< 

test: compile
	@echo "* $(BOLD)Running test \"$(BINARY)\" ... $(UNBOLD)"
	./$(BINARY) -df -s state.json -F 5

