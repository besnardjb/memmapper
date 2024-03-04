#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <cuda_runtime.h>

#define KB 1024
#define MB (1024 * KB)
#define GB (1024 * MB)
#define DATA_SIZE (25 * MB)
#define REPS 100000


int main(int argc, char** argv) {
	// Set up CUDA variables
	float *cpuData, *gpuData;
	size_t dataSize = DATA_SIZE;

	if(argc > 1)
	{
		char* dummy;
		dataSize = strtol(argv[1], &dummy, 10);
	}
	// Allocate memory on the CPU
	cpuData = (float *)mmap(NULL, dataSize, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	// Allocate memory on the GPU
	cudaMalloc((void **)&gpuData, dataSize);

	// Check for allocation errors
	if (cpuData == NULL || gpuData == NULL) {
		perror("Memory allocation failed");
		exit(EXIT_FAILURE);
	}

	// Measure CPU to GPU memory transfer time
	cudaEvent_t start1, stop1, start2, stop2;
	cudaEventCreate(&start1);
	cudaEventCreate(&stop1);


	// Transfer data from CPU to GPU
	float transferTime;
	cudaEventRecord(start1, 0);
	for(size_t i = 0; i < REPS; i++)
	{
		cudaMemcpy(gpuData, cpuData, dataSize, cudaMemcpyHostToDevice);
	}
	cudaEventRecord(stop1, 0);
	cudaEventSynchronize(stop1);

	cudaEventElapsedTime((float*)&transferTime, start1, stop1);


	// Calculate and print transfer time
	printf("#datasize = %lu\n", DATA_SIZE);
	printf("%f\n", (double)REPS * (double)DATA_SIZE / (transferTime ));

#if 0
	cudaEventCreate(&start2);

	cudaEventCreate(&stop2);
	cudaEventRecord(start2, 0);
	for(size_t i = 0; i < REPS; i++)
	{
		cudaMemcpy(cpuData, gpuData, dataSize, cudaMemcpyDeviceToHost);
	}
	cudaEventRecord(stop2, 0);
	cudaEventSynchronize(stop2);

	cudaEventElapsedTime((float*)&transferTime, start2, stop2);
	printf("D->H: %f MB/s\n", (double)REPS * (double)DATA_SIZE / (transferTime * MB));
#endif

	// Free allocated memory
	munmap(cpuData, dataSize);
	cudaFree(gpuData);

	return 0;
}
