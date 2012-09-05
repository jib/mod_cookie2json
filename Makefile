#!/usr/bin/make -f
#
all:
	apxs2 -a -c -Wl,-Wall -Wl,-lm -I. mod_cookie2json.c


