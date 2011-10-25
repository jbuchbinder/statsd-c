#
#	STATSD-C
#	C port of Etsy's node.js-based statsd server
#
#	http://github.com/jbuchbinder/statsd-c
#

BINARY= statsd
OBJECTS= \
	src/statsd.o \
	src/serialize.o \
	src/json-c/arraylist.o \
	src/json-c/debug.o \
	src/json-c/json_object.o \
	src/json-c/json_tokener.o \
	src/json-c/json_util.o \
	src/json-c/linkhash.o \
	src/json-c/printbuf.o

# Compilation settings
CC= gcc
CFLAGS= -fPIC -Wall -pthread -lpthread -Isrc/json-c -Wno-format-security -Wno-int-to-pointer-cast

# ANSI formatting
BOLD=\033[1m
UNBOLD=\033[0m

all: compile

compile: clean statsd

clean:
	@for F in $(BINARY) $(OBJECTS); do \
		echo " RM $(BOLD)$$F $(UNBOLD)"; \
		rm -f $$F; \
	done

$(BINARY): $(OBJECTS)
	@echo " LD $(BOLD)$(BINARY)$(UNBOLD)"
	@$(CC) $(CFLAGS) -o $(BINARY) $(OBJECTS)

.c.o:
	@echo " CC $(BOLD)$@$(UNBOLD)"
	@gcc ${CFLAGS} -o $@ -c $< 

test: compile
	@echo "* $(BOLD)Running test \"$(BINARY)\" ... $(UNBOLD)"
	./$(BINARY) -df -s state.json

