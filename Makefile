SRC_FILES=$(filter-out test.c, $(wildcard *.c))

OBJS := $(SRC_FILES:.c=.o) 

ifeq ($(shell uname -m), x86_64)
	CFLAGS+=-mavx2
endif

ifeq ($(DEBUG),1)
	CFLAGS+=-g -O2
else
	CFLAGS+=-O3
endif

ifeq ($(shell uname -s),Darwin)
    PCRE_LIB=/opt/homebrew/lib/libpcre2-8.a
endif
ifeq ($(shell uname -s),Linux)
    PCRE_LIB=/usr/local/lib/libpcre2-8.a
endif

all: sg

test_stack: test/test_stack.c stack.c
	$(CC) -O3 test/test_stack.c stack.c

sg: ${OBJS}
	$(CC) $(CFLAGS) -lpthread -o simdgrep $^ $(PCRE_LIB)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
