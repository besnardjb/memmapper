#define _GNU_SOURCE 1
#include <pthread.h>
#include <sys/time.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <hwloc.h>

#define MAX_REPS 100
#define DATA_SIZE (25 * 1024 * 1024) // 25MB
#define INTRAMODE 1
#define INTERMODE 0
#define COLD 1
#define HOT 0

int mode = INTRAMODE;
int temp = HOT;
size_t datasize = (size_t)DATA_SIZE;
size_t reps = (size_t)MAX_REPS;

hwloc_topology_t topology;
double __get_ts(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (double)tv.tv_sec + tv.tv_usec * 1e-6;
}

int __pin_to_core(hwloc_topology_t topology, int core_id) {
	hwloc_cpuset_t cpuset;
	hwloc_topology_init(&topology);
	hwloc_topology_load(topology);

	// Allocate and initialize cpuset
	cpuset = hwloc_bitmap_alloc();
	hwloc_bitmap_zero(cpuset);
	hwloc_bitmap_set(cpuset, core_id);

	// Bind the current process to the specified core
	hwloc_obj_t core = hwloc_get_obj_by_type(topology, HWLOC_OBJ_CORE, core_id);
	if(hwloc_set_cpubind(topology, core->cpuset, HWLOC_CPUBIND_THREAD) == -1)
	{
		perror("hwloc_set_cpubind");
		return -1;
	}

	// Release resources
	hwloc_bitmap_free(cpuset);
	hwloc_topology_destroy(topology);

	return 0;
}

char* __do_alloc()
{
	// Allocate memory for the buffer
	size_t buffer_size = datasize;

	if(temp == COLD)
	{
		buffer_size *= reps;
	}
	char* buffer = (char *)mmap(NULL, buffer_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (buffer == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}

	memset(buffer, 1, buffer_size);

	return buffer;
}

double __compute_bw(char* remote, size_t total_size)
{
	double total_time = 0.0;
	char* buffer = __do_alloc();

	for(size_t rep = 0; rep < reps; rep++)
	{
		char* pointer = buffer;
		if(temp == COLD)
		{
			pointer = (char*)buffer + rep * total_size;
			pointer = (char*)buffer + rep * total_size;
		}

		double start = __get_ts();
		memcpy(remote, pointer, datasize);
		double end = __get_ts();

		total_time += end - start;
	}
	munmap(buffer, (temp == COLD) ? datasize * reps : datasize);
	return ((double)total_size * reps / total_time);
}

typedef struct
{
	void* ret;
	void* pagesync;
	pthread_mutex_t* lock;
	int src;
	int dest;
	size_t count;
} pthread_args;


void __wait(void* start_page)
{
	while(*((int*)start_page) == 0) sched_yield();

}
void* __run_passive(void* arg)
{
	pthread_args* a = (pthread_args*)arg;
	/* child reader: wait for writev */
	__pin_to_core(topology, a->dest);
	a->ret = __do_alloc();
	return NULL;
}
void* __run_active(void* arg)
{
	pthread_args* a = (pthread_args*)arg;
	__pin_to_core(topology, a->src);
	__wait(a->pagesync);

	double bw = __compute_bw(a->ret, datasize);
	fprintf(stderr, "[%03lu]%03d-->%03d:%lf\n", a->count, a->src, a->dest, bw / (1024 * 1024 * 1024));
	pthread_mutex_lock(a->lock);
	*((int*)a->pagesync + sizeof(int*)) += 1;
	pthread_mutex_unlock(a->lock);
	munmap(a->ret, (temp == COLD) ? datasize * reps : datasize);
	free(a);
	return NULL;
}

void __run(size_t count, int s, int d, void* start_page, pthread_mutex_t* cpt)
{
	pthread_args* a = malloc(sizeof(pthread_args));
	a->src = s;
	a->dest = d;
	a->count = count;
	a->pagesync = start_page;
	a->ret = NULL;
	a->lock = cpt;

	pthread_t th[2];

	pthread_create(th, NULL, __run_passive, a);
	pthread_join(th[0], NULL);

	assert(a->ret != NULL);
	pthread_create(th, NULL, __run_active, a);

}

void help()
{
	fprintf(stderr, "Usage: ./cma [-i/-I] [-c/-r] [-c]\n\n");
	fprintf(stderr, "Transfer: intra-socket (-i) or inter-socket (-I)\n");
	fprintf(stderr, "Prefetch: Cold (-c) or Hot (-r)\n");
}

int main(int argc, char** argv) {
	size_t nbcores= 0;
	void* start_page = mmap(NULL, sizeof(int), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	int opt;
	pthread_mutex_t lock;
	pthread_mutex_init(&lock, 0);

	while( (opt = getopt(argc, argv, "iItTs:r:h")) != -1)
	{
		char* tmp;
		switch(opt)
		{
			case 'i': mode = INTRAMODE; break;
			case 'I': mode = INTERMODE; break;
			case 't': temp = COLD; break;
			case 'T': temp = HOT; break;
			case 's': datasize = strtol(optarg, &tmp, 10); break;
			case 'r': reps = strtol(optarg, &tmp, 10); break;
			default :
			case 'h': help(); exit(0); break;

		}
	}


	// Create topology object
	hwloc_topology_init(&topology);

	// Load the topology
	hwloc_topology_load(topology);
	nbcores = hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_CORE);
	assert(nbcores > 0);

	fprintf(stderr, "# REPS : %lu\n", reps);
	fprintf(stderr, "# BUFSZ: %lu Mb\n", datasize / (1024 * 1024));
	fprintf(stderr, "# intra: %d / cold: %d\n", mode, temp);
	fprintf(stderr, "# MEASURE as GB/s/core\n");

	size_t nb_rounds = nbcores / 2;
	for(size_t round = 1; round <= nb_rounds; round++)
	{
		*((int*)start_page) = 0;
		*((int*)start_page + sizeof(int*)) = 0;
		fprintf(stderr, "# ====\n");
		int count = 0;

		if(mode == INTRAMODE){
			for(size_t i = 0; i < round; i+=2)
			{
				count++;
				__run((round+1) / 2, i, (i + 1) % (nbcores/2), start_page, &lock);
			}
			round++;

		} else {

			for(size_t i = 0; i < round; i++)
			{
				count++;
				__run(round, i, i + nbcores/2, start_page, &lock);
			}
		}
		double start = __get_ts();
		*((int*)start_page) = 1;
		while(*((int*)start_page + sizeof(int*)) < count) sched_yield();
		double end = __get_ts();
		fprintf(stderr, "[%03d]TOTAL:%lf\n", count, (double)count * (double)reps * (double)datasize / (double)(end - start) / (1024 * 1024 * 1024) );
	}

	return EXIT_SUCCESS;
}
