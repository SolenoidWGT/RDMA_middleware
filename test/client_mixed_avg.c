#include <stdio.h>

#include "dhmp.h"
#define R 1000
#define MB_1 1048576
#define KB_256 262144
#define KB_64 65535
#define NUM_SIZE 5000000
const int obj_num = NUM_SIZE;

int main(int argc,char *argv[])
{
	void *addr[obj_num];
	int i,size=1048576,rnum,readnum,writenum,readafterwritenum;
	char *str;
	const int workload = NUM_SIZE/100;
	struct timespec task_time_start, task_time_end;
	unsigned long task_time_diff_ns;
	
	if(argc<4)
	{
		printf("input param error. input:<filname>  <readnum> <writenum> <readafterwritenum>\n");
		printf("note:<readN>+<writeN>+<r_af_wN> == 100\n");
		return -1;
	}
	else
	{
		readnum=atoi(argv[1]);
		writenum=atoi(argv[2]);
		readafterwritenum = atoi(argv[3]);
	}
	readnum = readnum * workload;
	writenum = writenum * workload;
	readafterwritenum = readafterwritenum * workload;
	size = MB_1;

	str=malloc(size);
	if(!str){
		printf("alloc mem error");
		return -1;
	}
	snprintf(str, size, "hello world hello world hello world hello world hello world");
	
	dhmp_client_init();
	
	for(i=0;i<obj_num;)
	{
		if(i%1000==0)
			sleep(1);
		addr[i++]=dhmp_malloc(MB_1);
		if(i >= obj_num) continue;
		addr[i++]=dhmp_malloc(KB_256);
		if(i >= obj_num) continue;
		addr[i++]=dhmp_malloc(KB_64);
	}
	
clock_gettime(CLOCK_MONOTONIC, &task_time_start);
	
	for(i=0;i<readnum;i++)
	{
		srand(time(0));	
		rnum=rand()%obj_num;
		if(rnum%3 == 0) size = MB_1;
		else if(rnum%3 == 1) size = KB_256;
		else  size = KB_64;
		dhmp_read(addr[rnum], str, size);
	}
	
	for(i=0;i<writenum;i++)
	{
		srand(time(0));
		rnum=rand()%obj_num;
		if(rnum%3 == 0) size = MB_1;
		else if(rnum%3 == 1) size = KB_256;
		else  size = KB_64;
		dhmp_write(addr[rnum], str, size);
	}

	for(i=0;i<readafterwritenum;i++)
	{
		srand(time(0));
		rnum=rand()%obj_num;
		if(rnum%3 == 0) size = MB_1;
		else if(rnum%3 == 1) size = KB_256;
		else  size = KB_64;
		dhmp_write(addr[rnum], str, size);
		dhmp_read(addr[rnum], str, size);
	}

	clock_gettime(CLOCK_MONOTONIC, &task_time_end);

	sleep(5);
	for(i=0;i<obj_num;i++)
	{
		if(i%1000==0)
			sleep(1);
		dhmp_free(addr[i]);
	}
	
	dhmp_client_destroy();
	task_time_diff_ns = ((task_time_end.tv_sec * 1000000000) + task_time_end.tv_nsec) -
                        ((task_time_start.tv_sec * 1000000000) + task_time_start.tv_nsec);
  	printf("runtime %lf\n", (double)task_time_diff_ns/1000000);
	return 0;
}



