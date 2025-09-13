git submodule update --init --recursive
cd pcre2
sh autogen.sh
./configure --enable-static --enable-jit --enable-utf --disable-cpp
make

