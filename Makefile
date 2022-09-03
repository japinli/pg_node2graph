#-----------------------------------------------------------------------------
#
# Makefile
#     Makefile for pg_node2graph
#
#-----------------------------------------------------------------------------

all: pg_node2graph

.PHONY: clean install uninstall

pg_node2graph: pg_node2graph.cc
	g++ $(CFLAGS) -std=c++11 -o $@ $^

install: pg_node2graph
	cp pg_node2graph /usr/local/bin

uninstall:
	rm /usr/local/bin/pg_node2graph

clean:
	rm pg_node2graph
