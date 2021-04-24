#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include "dhmp.h"

#define NODE_MAX 1800000
#define node_num 326186
struct parad 
{
	int rednum;
	int d;
	double ra,rb;
	int u,v;
}; 

void  *para[NODE_MAX];
struct parad *tpara;
void pagerank()
{
	
	struct timespec task_time_start, task_time_end;
	unsigned long task_time_diff_ns;
	int ncnt=0;
	int ecnt=0;
	double eps=0.1;
	int a,b,c,u,v;
	double x,y;
	int i,count ;
	char StrLine[1024];
	int node = node_num;

	FILE *fp = fopen("/home/lhd/dblp-2010.graph-txt","r");
	FILE *fp1 = fopen("/home/lhd/dblp.txt","w+");
	dhmp_client_init();
	tpara = calloc(1,sizeof(struct parad));
	for(i = 0;i<NODE_MAX;i++)
	{
		para[i] = dhmp_malloc(  sizeof(struct parad));
		dhmp_write(para[i],tpara, sizeof(struct parad));
	}

	clock_gettime(CLOCK_MONOTONIC, &task_time_start);
	
	i = 0;u = 0;
	for(count=0;count<node;++count)
	{	
		if(feof(fp)) break;
        fgets(StrLine,1024,fp);  
        {
         	int ti = 0;
         	int len = strlen(StrLine)-1;
         	while(ti<len)
         	{
         		v = 0;
         		while(StrLine[ti]<='9' && StrLine[ti] >= '0')
         		{
         			v = v*10 + StrLine[ti] - '0';
         			ti++;
         		}
         		{
   			dhmp_read(para[i], tpara, sizeof(struct parad));
					tpara->u = u;
			tpara->v = v;
					dhmp_write(para[i],tpara, sizeof(struct parad));
					dhmp_read(para[u], tpara, sizeof(struct parad));
					a = tpara->rednum;
					if(!a) 
					{
						tpara->rednum = ncnt;
						dhmp_write(para[u], tpara, sizeof(struct parad));
						ncnt++;
					}

					dhmp_read(para[v], tpara, sizeof(struct parad));
					if(!tpara->rednum) 
					{
						tpara->rednum = ncnt;
						dhmp_write(para[v], tpara, sizeof(struct parad));	
						ncnt++;
					}
				dhmp_read(para[a], tpara, sizeof(struct parad));
				tpara->d++;
					dhmp_write(para[a], tpara, sizeof(struct parad));	
					i++;
         		}

         		ti++;
         	}
        }
        u++;
	}
	ecnt=i;
	for(i=0;i<ncnt;++i)
	{
		dhmp_read(para[i], tpara, sizeof(struct parad));	
		tpara->ra = (double)1/ncnt;
		dhmp_write(para[i], tpara, sizeof(struct parad));	
	}
	printf("ncnt ==%d %d\n",ncnt,ecnt);


	while(eps>0.0000001)
	{
		printf("eps%.7lf\n",eps);
		eps=0;
		for( i=0;i<ecnt;++i)
		{
			dhmp_read(para[i], tpara, sizeof(struct parad));	
			u = tpara->u;
			v = tpara->v;
			dhmp_read(para[u], tpara, sizeof(struct parad));
			a = tpara->rednum;	
			dhmp_read(para[v], tpara, sizeof(struct parad));	
			b = tpara->rednum;
			dhmp_read(para[a], tpara, sizeof(struct parad));	
			c = tpara->d;
			x = tpara->ra;
			dhmp_read(para[b], tpara, sizeof(struct parad));	
			y = tpara->rb;
			tpara->rb = y + x/c;
			dhmp_write(para[b], tpara, sizeof(struct parad));	
		}
		for( i=0;i<ncnt;++i)
		{
			dhmp_read(para[i], tpara, sizeof(struct parad));	
			x = tpara->rb;
			y = tpara->ra;
			x = x*0.8+(0.2*1/ncnt); 
			eps += y > x ? (y-x) : (x-y);
			tpara->ra = x;
			tpara->rb = 0;
			dhmp_write(para[i], tpara, sizeof(struct parad));	
		}
	}

	clock_gettime(CLOCK_MONOTONIC, &task_time_end);
	
	task_time_diff_ns = ((task_time_end.tv_sec * 1000000000) + task_time_end.tv_nsec) -
                        ((task_time_start.tv_sec * 1000000000) + task_time_start.tv_nsec);
	printf("runtime %lf\n", (double)task_time_diff_ns/1000000);

	for(i=0;i<ncnt;++i)
	{
		dhmp_read(para[i], tpara, sizeof(struct parad));	
		a = tpara->rednum;
		dhmp_read(para[a], tpara, sizeof(struct parad));	
		fprintf(fp1,"%d %lf\n",i,tpara->ra);
	}

	for(i = 0;i<NODE_MAX;i++)
	{
		dhmp_free(para[i]);
	}
	dhmp_client_destroy();
}

int main()
{
	pagerank();
	return 0;
}

