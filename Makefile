SRC_FILES=$(filter-out test.c, $(wildcard *.c))

ifeq ($(shell uname -m), x86_64)
	CFLAGS+=-mavx2
endif

all: sg

test_stack: test.c stack.c
	$(CC) -O3 test.c stack.c

sg: ${SRC_FILES}
	$(CC) -O3 -o simdgrep ${SRC_FILES}
