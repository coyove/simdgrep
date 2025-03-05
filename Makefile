SRC_FILES=$(filter-out test.c, $(wildcard *.c))

test_stack: test.c stack.c
	$(CC) -O3 test.c stack.c

all: ${SRC_FILES}
	$(CC) -O3 -mavx2 -o simdgrep ${SRC_FILES}
