#include <stdio.h>

#include "dhmp.h"
const int obj_num = 50000;
#define NUM_SIZE 50000

int main(int argc,char *argv[])
{
	void *addr[obj_num];
	int i,size=1048576,rnum,readnum,writenum,readafterwritenum;
	char *str;
	const int workload = NUM_SIZE/100;
	struct timespec task_time_start, task_time_end;
	unsigned long task_time_diff_ns;
	
	if(argc<5)
	{
		printf("input param error. input:<filname> <size> <readnum> <writenum> <readafterwritenum>\n");
		printf("note:<readN>+<writeN>+<r_af_wN> == 100\n");
		return -1;
	}
	else
	{
		size=atoi(argv[1]);
		readnum=atoi(argv[2]);
		writenum=atoi(argv[3]);
		readafterwritenum = atoi(argv[4]);
	}
	readnum = readnum * workload;
	writenum = writenum * workload;
	readafterwritenum = readafterwritenum * workload;

	str=malloc(size);
	if(!str){
		printf("alloc mem error");
		return -1;
	}
	snprintf(str, size, "hello world hello world hello world hello world hello world");
	
	dhmp_client_init();
	
	for(i=0;i<obj_num;i++)
	{
		if(i%10000==0)
			sleep(1);
		addr[i]=dhmp_malloc(size);
	}
	clock_gettime(CLOCK_MONOTONIC, &task_time_start);
	
	for(i=0;i<readnum;i++)
	{
 srand(time(0));	
	rnum=rand()%obj_num;
		dhmp_read(addr[rnum], str, size);
	}
	
	for(i=0;i<writenum;i++)
	{srand(time(0));
		rnum=rand()%obj_num;
		dhmp_write(addr[rnum], str, size);
	}

	for(i=0;i<readafterwritenum;i++)
	{srand(time(0));
		rnum=rand()%obj_num;
		dhmp_write(addr[rnum], str, size);
		dhmp_read(addr[rnum], str, size);
	}

	clock_gettime(CLOCK_MONOTONIC, &task_time_end);

	sleep(3);
	for(i=0;i<obj_num;i++)
	{
		if(i%10000==0)
			sleep(1);
		dhmp_free(addr[i]);
	}
	
	dhmp_client_destroy();
	task_time_diff_ns = ((task_time_end.tv_sec * 1000000000) + task_time_end.tv_nsec) -
                        ((task_time_start.tv_sec * 1000000000) + task_time_start.tv_nsec);
  	printf("runtime %lf\n", (double)task_time_diff_ns/1000000);
	return 0;
}



