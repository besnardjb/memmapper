#define _GNU_SOURCE 1
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
		if (hwloc_set_cpubind(topology, cpuset, HWLOC_CPUBIND_PROCESS) == -1) {
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
		char* buffer = (char *)mmap(NULL, buffer_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

		if (buffer == NULL) {
			perror("malloc");
			return NULL;
		}

		memset(buffer, 1, buffer_size);

		return buffer;
	}

	double __compute_bw(int remote_pid, char* buffer, size_t total_size)
	{
		double total_time = 0.0;
		//double min = 10, max = 0;
		struct iovec local[1], remote[1];

		local[0].iov_base = buffer;
		remote[0].iov_base = buffer;
		local[0].iov_len = datasize;
		remote[0].iov_len = datasize;
		for(size_t rep = 0; rep < reps; rep++)
		{
			if(temp == COLD)
			{
				local[0].iov_base = (char*)buffer + rep * total_size;
				remote[0].iov_base = (char*)buffer + rep * total_size;
			}

			double start = __get_ts();
			ssize_t written = process_vm_writev(remote_pid, local, 1, remote, 1, 0);
			double end = __get_ts();
			if (written == -1) {
				perror("process_vm_writev");
				return EXIT_FAILURE;
			}

			total_time += end - start;
#if 0
			if(end-start > max)
			{
				max = end-start;
			}
			else if (end-start < min)
			{
				min = end-start;
			}
#endif
		}
		//fprintf(stderr, "%f - %f - %f\n", min, total_time / reps, max);
		return ((double)total_size * reps / total_time);
	}

	void __wait(void* start_page)
	{
		while(*((int*)start_page) == 0) sched_yield();

	}

	void __run(int s, int d, void* start_page)
	{
		char* buffer;

		pid_t pid = fork();
		if(pid == 0)
		{
			/* child reader: wait for writev */
			__pin_to_core(topology, d);
			buffer = __do_alloc();
			pause();
			munmap(buffer, (temp == COLD) ? datasize * reps : datasize);
			exit(0);
		} else {
			/* child writer: writev */
			if(fork() == 0)
			{
				__pin_to_core(topology, s);
				buffer = __do_alloc();
				memset(buffer, 1, datasize);

				// nasty hack to ensure target PID is ready :(
				sleep(1);

				__wait(start_page);

				double bw = __compute_bw(pid, buffer, datasize);
				fprintf(stderr, "%03d-%03d:%lf\n", s, d, bw / (1024 * 1024 * 1024));
				munmap(buffer, (temp == COLD) ? datasize * reps : datasize);
				kill(pid, SIGTERM);
				exit(0);
			}
		}
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

		while( (opt = getopt(argc, argv, "iItTpPs:r:h")) != -1)
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
			size_t count = 0;
			*((int*)start_page) = 0;
			fprintf(stderr, "# ====\n");

			if(mode == INTRAMODE){
				for(size_t i = 0; i < round; i+=2)
				{
					count++;
					__run(i, (i + 1) % (nbcores/2), start_page);
				}
				round++;
			} else {

				for(size_t i = 0; i < round; i++)
				{
					count++;
					__run(i, i + nbcores/2, start_page);
				}
			}
			double start = __get_ts();
			*((int*)start_page) = 1;
			while (wait(NULL) > 0);
			double end = __get_ts();
			fprintf(stderr, "[%03lu]TOTAL:%lf\n", count, (double)count * (double)reps * (double)datasize / (double)(end - start) / (1024 * 1024 * 1024) );
		}

		return EXIT_SUCCESS;
	}
