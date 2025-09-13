SRC_FILES=$(filter-out test.c, $(wildcard *.c))

OBJS := $(SRC_FILES:.c=.o) sljitLir.o

CFLAGS+=-Wall
ifeq ($(shell uname -m), x86_64)
	CFLAGS+=-mavx2
endif

ifeq ($(DEBUG),1)
	CFLAGS+=-g -O2
else
	CFLAGS+=-O3
endif

PCRE_LIB=pcre2/.libs/libpcre2-8.a

all: sg

sg: ${OBJS} 
	$(CC) $(CFLAGS) -lpthread -o simdgrep $^ $(PCRE_LIB)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

sljitLir.o: pcre2/deps/sljit/sljit_src/sljitLir.c
	$(CC) $(CFLAGS) -c $< -o $@

test_stack: test/test_stack.c stack.c
	$(CC) -O3 test/test_stack.c stack.c

clean:
	rm -f $(OBJS) $(TARGET)
