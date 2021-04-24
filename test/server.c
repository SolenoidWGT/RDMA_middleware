#include <stdio.h>

#include "dhmp.h"
//unsigned long get_mr_time = 0;
int main()
{
	dhmp_server_init();
	dhmp_server_destroy();
	return 0;
}
