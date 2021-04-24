#include <stdio.h>

#include "dhmp.h"

#define MB_1 1048576
#define KB_256 262144
#define KB_64 65535

#define R 1000
#define NUM_SIZE 5000000
const double A = 1.3;  //定义参数A>1的浮点数, 后来测试小于1的,似乎也可以,倾斜指数）
const double C = 1.0;  //这个C是不重要的,一般取1, 可以看到下面计算中分子分母可以约掉这个C

double pf[R]; //值为0~1之间, 是单个f(r)的累加值
int rand_num[NUM_SIZE]={0};

void generate()
{
    int i;
    double sum = 0.0;

    for (i = 0; i < R; i++)
        sum += C/pow((double)(i+2), A);

    for (i = 0; i < R; i++)
    {
        if (i == 0)
            pf[i] = C/pow((double)(i+2), A)/sum;
        else
            pf[i] = pf[i-1] + C/pow((double)(i+2), A)/sum;
    }
}

void pick()
{
	int i, index;

    generate();

    srand(time(0));
    //产生n个数
    for ( i= 0; i < NUM_SIZE; i++)
    {
        index = 0;
        double data = (double)rand()/RAND_MAX;  //生成一个0~1的数
        while (index<R-1&&data > pf[index])   //找索引,直到找到一个比他小的值,那么对应的index就是随机数了
            index++;
		rand_num[i]=index;
    }
}

int main(int argc,char *argv[])
{
	void *addr[R];
	int i,size=65536,rnum,readnum,writenum,mode,readafterwritenum;
	const int workload = NUM_SIZE/100;
	char *str;
	struct timespec task_time_start, task_time_end;
	unsigned long task_time_diff_ns;
	bool show_flag=false;
	
	if(argc<4)
	{
		printf("input param error. input:<filname> <readnum> <writenum> <readafterwritenum>\n");
		printf("note:<readN>+<writeN>+<r_af_wN> == 100\n");
		return -1;
	}
	else
	{
		readnum=atoi(argv[1]);
		writenum=atoi(argv[2]);
		readafterwritenum = atoi(argv[3]);
	}

	pick();
	readnum = readnum * workload;
	writenum = writenum * workload;
	readafterwritenum = readafterwritenum * workload;

	size = MB_1;  //本地以最大粒度申请空间
	str=malloc(size);
	if(!str){
		printf("alloc mem error");
		return -1;
	}
	snprintf(str, size, "hello world hello world hello world hello world hello world");
	
	dhmp_client_init();
int obj_num = R;	
	for(i=0;i<R;)
	{
		addr[i++]=dhmp_malloc(MB_1);
		if(i >= obj_num) continue;
		addr[i++]=dhmp_malloc(KB_256);
		if(i >= obj_num) continue;
		addr[i++]=dhmp_malloc(KB_64);
	}
	clock_gettime(CLOCK_MONOTONIC, &task_time_start);
	
	for(i=0;i<readnum;i++)
	{
		 rnum = rand_num[i];
		if(rnum%3 == 0) size = MB_1;
		else if(rnum%3 == 1) size = KB_256;
		else  size = KB_64;
		dhmp_read(addr[rnum], str, size);
	}

	int temp = writenum+readnum;
	for(;i<temp;i++)
	{
		rnum = rand_num[i];
		if(rnum%3 == 0) size = MB_1;
		else if(rnum%3 == 1) size = KB_256;
		else  size = KB_64;
		dhmp_write(addr[rnum], str, size);
	}

	temp += readafterwritenum;
	for(;i<temp;i++)
	{
		rnum = rand_num[i];
		if(rnum%3 == 0) size = MB_1;
		else if(rnum%3 == 1) size = KB_256;
		else  size = KB_64;
		dhmp_write(addr[rnum], str, size);
		dhmp_read(addr[rnum], str, size);
	}	

	clock_gettime(CLOCK_MONOTONIC, &task_time_end);

	for(i=0;i<R;i++)
		dhmp_free(addr[i]);
	
	dhmp_client_destroy();
	task_time_diff_ns = ((task_time_end.tv_sec * 1000000000) + task_time_end.tv_nsec) -
                        ((task_time_start.tv_sec * 1000000000) + task_time_start.tv_nsec);

	printf("size %d readnum %d writenum %d ",size,readnum,writenum);
	printf("runtime %lf\n", (double)task_time_diff_ns/1000000);

	return 0;
}


