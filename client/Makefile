CFLAGS+=-I../include -MMD -MP  -pthread
LDFLAGS+=-pthread
all:client


client:client.o
	gcc -o $@ $^ $(CFLAGS) $(LDFLAGS)

clean:
	rm -rf *.o *.d client

-include $(wildcard *.d)