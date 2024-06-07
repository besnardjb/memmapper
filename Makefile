CC=gcc
NVCC=nvcc
CFLAGS=-Wall -Wextra -O3 $(shell pkg-config --cflags hwloc)

LDFLAGS=-lpthread $(shell pkg-config --libs hwloc)

all: mapper cma shmem cuda cache

mapper: mapper.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

cma: cma.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@ -Wall -march=native -O3

shmem: shmem.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@ -Wall -march=native -O3

#cuda: cuda.c
#	-$(NVCC) $^ -o $@ --optimize 3


cache: cache.c
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@ -Wall -march=native -O3


clean:
	rm -f mapper cma shmem cuda cache
.PHONY: clean
