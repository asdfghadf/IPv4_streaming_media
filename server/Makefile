CFLAGS+=-I../include -pthread -MMD -MP -lhiredis -lcjson $(shell mysql_config --cflags) 
LDFLAGS+=-pthread -lhiredis -lcjson $(shell mysql_config --libs)
all:server

server:server.o song.o mytbf.o medialib.o mp3_parser.o redis_songlist.o fsm.o
	gcc $(CFLAGS) -o $@ $^  $(LDFLAGS)


clean:
	rm -rf *.o *.d server
-include $(wildcard *.d)