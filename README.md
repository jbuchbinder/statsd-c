STATSD-C
========

SUMMARY
-------

Reimplementation of [Etsy's infamous "statsd"](http://github.com/etsy/statsd) in C.

Your mileage may vary. It works for me, and it *should* be wire compatible with the original node.js-based statsd.

There are some libraries used/included:

* [embeddedmgetric](http://code.google.com/p/embeddedgmetric/) : Ganglia gmetric support (BSD)
* [json-c](http://oss.metaparadigm.com/json-c/) : JSON (de)serialization (BSD)
* [uthash](http://uthash.sourceforge.net/ ): Hash management "library" (BSD)

Build with "make".

USAGE
-----

    Usage: statsd [-hDdfc] [-p port] [-m port] [-s file]
        -p port           set statsd udp listener port (default 8125)
        -m port           set statsd management port (default 8126)
        -s file           serialize state to and from file (default disabled)
        -h                this help display
        -d                enable debug
        -D                daemonize
        -f                enable friendly mode (breaks wire compatibility)

