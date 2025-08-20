SRC_FILES=$(filter-out test.c, $(wildcard *.c))

OBJS := $(SRC_FILES:.c=.o) 

ifeq ($(shell uname -m), x86_64)
	CFLAGS+=-mavx2
endif

all: sg

test_stack: test/test_stack.c stack.c
	$(CC) -O3 test/test_stack.c stack.c

sg: ${OBJS}
	$(CC) -O3 -g -o simdgrep $^

%.o: %.c
	$(CC) $(CFLAGS) -g -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
