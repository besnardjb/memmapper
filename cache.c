#define _GNU_SOURCE 1
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define KB (1024)
#define MB (1024 * KB)
#define GB (1024 * MB)
#define MIN_SIZE (size_t)(4 * KB)     // 4KB
#define MAX_SIZE ((size_t)16 * GB) // 512MB
#define NUM_REPS 100000            // Number of repetitions
#define US (1000000)

int main() {
	// Seed the random number generator
	srand((unsigned int)time(NULL));

	// Allocate source and target buffers
	char* source = (char*)mmap(NULL, MAX_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	char* target = (char*)mmap(NULL, MAX_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (source == MAP_FAILED || target == MAP_FAILED) {
		perror("Memory allocation failed");
		exit(EXIT_FAILURE);
	}
	printf("#Size (KB) \tReps \tLatency (us) \tBandwidth (GB/s)\n");

	for (size_t size = MIN_SIZE; size <= MAX_SIZE; size *= 2) {
		// Initialize source buffer with random data
		for (size_t i = 0; i < size; ++i) {
			source[i] = rand() % 256;
		}
		size_t nb_reps = NUM_REPS;
		if(size > 16 * MB)
		{
			nb_reps /= 100;
			if(size > 1 * GB)
				nb_reps /= 10;
		}
		// Measure time for memcpy operation
		clock_t start_time = clock();

		for (size_t rep = 0; rep < nb_reps; ++rep) {
			memcpy(target, source, size);
		}

		clock_t end_time = clock();

		// Calculate average time per copy operation
		double elapsed_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
		double bandwidth = (double)(size * nb_reps) / (double)elapsed_time;
		double average_time = elapsed_time / nb_reps;

		// Print the result
		printf("%8lu\t%lu\t%8.3lf\t%8.2lf\n", size  / KB, nb_reps, average_time * US, bandwidth / GB);
	}

	// Free allocated memory
	munmap(source, MAX_SIZE);
	munmap(target, MAX_SIZE);

	return 0;
}
