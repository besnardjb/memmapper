#define _GNU_SOURCE
#include <sys/mman.h>
#include <stdio.h>

int main(void)
{
	void* ret = mmap(NULL, (2 * 1024 * 1024) * 100, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_HUGETLB, -1, 0);

	printf("%p\n", ret);
	perror("mmap");
}
