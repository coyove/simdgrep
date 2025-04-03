SRC_FILES=$(filter-out test.c, $(wildcard *.c))

OBJS := $(SRC_FILES:.c=.o) 

ifeq ($(shell uname -m), x86_64)
	CFLAGS+=-mavx2
endif

all: sg

test_stack: test/test_stack.c stack.c
	$(CC) -O3 test/test_stack.c stack.c

sg: ${OBJS}
	$(CC) -O3 -o simdgrep $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

mac_dispatch.o: mac_dispatch.m
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
