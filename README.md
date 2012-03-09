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

Build with `cmake . && make`. A simple client to submit data called
"statsd_client" is built as well.

FEATURES
--------

* Wire compatible with original statsd, or use the handy JSON format instead...
* Small, fast, efficient, with no VM overhead.
* Able to de/serialize state to/from disk.
* Direct stat flush to ganglia's gmond.

USAGE
-----

    Usage: statsd [-hDdfFc] [-p port] [-m port] [-s file] [-G host] [-g port] [-S spoofhost] [-P prefix] [-l lockfile]
        -p port           set statsd udp listener port (default 8125)
        -m port           set statsd management port (default 8126)
        -s file           serialize state to and from file (default disabled)
        -G host           ganglia host (default disabled)
        -g port           ganglia port (default 8649)
        -S spoofhost      ganglia spoof host (default statsd:statsd)
        -P prefix         ganglia metric prefix (default is none)
        -l lockfile       lock file (only used when daemonizing)
        -h                this help display
        -d                enable debug
        -D                daemonize
        -f                enable friendly mode (breaks wire compatibility)
        -F seconds        set flush interval in seconds (default 10)
        -c                clear stats on startup

JSON FORMAT
-----------

Input can be specified as either a JSON object or an array of JSON objects.
An example of some different inputs would be:

* Increase counter 'test_counter' by 300:

    {'counter':'test_counter','value':300}

* Add two counters:

    [{'counter':'count1','value':1.0},{'counter':'count2','value':1.0}]

* Add a counter with a set sample rate:

    {'counter':'sample','value':5,'sample_rate':10}

* Set a timer:

    {'timer':'test_timer',value:12345}

