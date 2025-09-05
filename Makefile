SRC_FILES=$(filter-out test.c pcre.c, $(wildcard *.c))

OBJS := $(SRC_FILES:.c=.o) 

ifeq ($(shell uname -m), x86_64)
	CFLAGS+=-mavx2
endif

ifeq ($(DEBUG),1)
	CFLAGS+=-g -O0
else
	CFLAGS+=-O3
endif

all: sg

test_stack: test/test_stack.c stack.c
	$(CC) -O3 test/test_stack.c stack.c

sg: ${OBJS}
	$(CC) $(CFLAGS) -lpthread -o simdgrep $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
