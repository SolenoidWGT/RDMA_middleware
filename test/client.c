#include <stdio.h>
#include <sys/time.h>
#include "dhmp.h"

#define COUNT 100000
#define SIZE 1024*8

//unsigned long get_mr_time = 0;

int main(int argc,char *argv[])
{
	void *addr[COUNT];
	char *str,*str1;
    int i;
	struct timespec task_time_start, task_time_end;
	unsigned long task_time_diff_ns;

	str=malloc(SIZE);
	str1=malloc(SIZE);
	memset(str1, 0, SIZE);
	if(!str){
		printf("alloc mem error");
		return -1;
	}
	snprintf(str, SIZE, "hello world hello, hhhhh");
	
	dhmp_client_init(SIZE);
	
	for(i=0;i<COUNT;i++)
	{
		addr[i]=dhmp_malloc(SIZE);
	}
	printf("malloc ok\n");
	clock_gettime(CLOCK_MONOTONIC, &task_time_start);

	for (i = 0; i < COUNT; i++)
	{
		size_t len = strlen(str);
		// model_C_sread(addr[i], size, str);
		// dhmp_write(addr[i], str, SIZE);
		dhmp_send(addr[i], str, SIZE, true);
		// dhmp_send(addr[i], str1, len, false);
		// dhmp_read(addr[i],str1,len);
		// printf("%s\n", str1);
		str[len - 1] = 'a' + (i%26);
	}

	clock_gettime(CLOCK_MONOTONIC, &task_time_end);

	for (i = COUNT - 1; i > 0; i--)
	{
		dhmp_free(addr[i]);
	}

	dhmp_client_destroy();
	task_time_diff_ns = ((task_time_end.tv_sec * 1000000000) + task_time_end.tv_nsec) -
                        ((task_time_start.tv_sec * 1000000000) + task_time_start.tv_nsec);
  	printf("runtime %lf\n", (double)task_time_diff_ns/1000000);
	// printf("get_mr_time %lf\n", (double)get_mr_time/1000000);
	return 0;
}