CC = gcc
MPICC ?= mpicc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L
LDLIBS = -lcrypto
PYTHON ?= python3
COMMON = src/common.c src/search_support.c
HEADERS = src/common.h src/search_support.h

.PHONY: all build test benchmark demo proposal
all: build
build: build/aes_tool build/search_seq build/search_mpi

build/.dir:
	@mkdir -p build
	@touch $@

build/aes_tool: src/aes_tool.c src/common.c src/common.h | build/.dir
	$(CC) $(CPPFLAGS) $(CFLAGS) src/aes_tool.c src/common.c -o $@ $(LDLIBS)

build/search_seq: src/search_seq.c $(COMMON) $(HEADERS) | build/.dir
	$(CC) $(CPPFLAGS) $(CFLAGS) src/search_seq.c $(COMMON) -o $@ $(LDLIBS)

build/search_mpi: src/search_mpi.c $(COMMON) $(HEADERS) | build/.dir
	$(MPICC) $(CPPFLAGS) $(CFLAGS) -fopenmp src/search_mpi.c $(COMMON) -o $@ $(LDLIBS)

build/test_core: tests/test_core.c $(COMMON) $(HEADERS) | build/.dir
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc tests/test_core.c $(COMMON) -o $@ $(LDLIBS)

test: build build/test_core
	./build/test_core
	$(PYTHON) tests/test_integration.py

benchmark: build
	$(PYTHON) scripts/benchmark.py

demo:
	$(PYTHON) scripts/demo.py

proposal:
	$(PYTHON) scripts/build_proposal.py
