#-----------------------------------------------------------------------------
#
# Makefile
#     Makefile for pg_node2graph
#
#-----------------------------------------------------------------------------

all: pg_node2graph

.PHONY: clean install uninstall config.h

config.h:
	@echo '#define VERSION "0.2"' > config.h

pg_node2graph: pg_node2graph.cc config.h
	g++ $(CFLAGS) -std=c++11 -o $@ $<

install: pg_node2graph
	cp pg_node2graph /usr/local/bin

uninstall:
	rm /usr/local/bin/pg_node2graph

clean:
	rm pg_node2graph config.h
