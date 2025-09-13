export CC=/usr/local/musl/bin/musl-gcc
export CFLAGS=-static
sh build-pcre2.sh
make all
