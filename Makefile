CC=clang
CFLAGS= -std=c99 -g -MD -pthread
LIBS= -lpthread
TARGET=simpletorrent
SOURCES=$(shell find src/ -name "*.c")
OBJS=$(SOURCES:.c=.o)

all: ${TARGET}
	cp ./bin/$(TARGET) ./bin/test1 
	cp ./bin/$(TARGET) ./bin/test2

debug:CFLAGS = -std=c99 -g -MD -D DEBUG -pthread
debug:all

${TARGET}: ${OBJS}
	${CC} ${CFLAGS} -o bin/${TARGET} ${LIBS} ${OBJS}

%.o: $.c
	$(CC) -c $(CFLAGS) $@ $<

-include $(patsubst %.o, %.d, $(OBJS))

clean:
	rm -rf bin/${TARGET}
	rm -rf src/*.core
	rm -rf $(OBJS) $(OBJS:.o=.d)
	rm -rf ${TARGET} 
	rm -rf *.core

.PHONY: all clean
