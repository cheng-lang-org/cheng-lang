CC ?= cc
OPENSSL_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
OPENSSL_LIBS := $(shell pkg-config --libs openssl 2>/dev/null)
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -Wno-deprecated-declarations

.PHONY: all run clean

all: bench_ref

bench_ref: bench_ref.c
	$(CC) $(CFLAGS) $(OPENSSL_CFLAGS) bench_ref.c -o bench_ref $(OPENSSL_LIBS)

run: bench_ref
	./bench_ref

clean:
	rm -f bench_ref
