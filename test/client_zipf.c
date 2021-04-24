#include <stdio.h>

#include "dhmp.h"
#define R ((uint64_t)50000)  //address range 
#define NUM_SIZE ((uint64_t)500000) //operater num
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
	struct timespec task_time_start, task_time_end,task1,task2;
	unsigned long task_time_diff_ns,task_time1,task_time2;
	bool show_flag=false;
	
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

	pick();
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
	 clock_gettime(CLOCK_MONOTONIC, &task1);
	for(i=0;i<R;i++)
		addr[i]=dhmp_malloc(size);
	clock_gettime(CLOCK_MONOTONIC, &task2);
	task_time1 = ((task2.tv_sec * 1000000000) + task2.tv_nsec) -
                        ((task1.tv_sec * 1000000000) + task1.tv_nsec);
	clock_gettime(CLOCK_MONOTONIC, &task_time_start);
	
	for(i=0;i<readnum;i++)
		dhmp_read(addr[rand_num[i]], str, size);

	int temp = writenum+readnum;
	for(;i<temp;i++)
		dhmp_write(addr[rand_num[i]], str, size);

	temp += readafterwritenum;
	for(;i<temp;i++)
	{
		dhmp_write(addr[rand_num[i]], str, size);
		dhmp_read(addr[rand_num[i]], str, size);
	}	
	
	clock_gettime(CLOCK_MONOTONIC, &task_time_end);
 clock_gettime(CLOCK_MONOTONIC, &task1);
	for(i=0;i<R;i++)
		dhmp_free(addr[i]);
	clock_gettime(CLOCK_MONOTONIC, &task2);
        task_time2 = ((task2.tv_sec * 1000000000) + task2.tv_nsec) -
                        ((task1.tv_sec * 1000000000) + task1.tv_nsec);
	dhmp_client_destroy();
	task_time_diff_ns = ((task_time_end.tv_sec * 1000000000) + task_time_end.tv_nsec) -
                        ((task_time_start.tv_sec * 1000000000) + task_time_start.tv_nsec);

	printf("size %d readnum %d writenum %d ",size,readnum,writenum);
	printf("runtime %lf %lf %lf\n", (double)task_time_diff_ns/1000000,(double)task_time1/1000000,(double)task_time2/1000000);

	return 0;
}
