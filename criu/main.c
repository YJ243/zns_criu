#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

#include "controller.h"

//pthread_mutex_t  mutex = PTHREAD_MUTEX_INITIALIZER;

int num_192kb = 0, size = 0;
int benchmarks; // writeseq = 0, readseq = 1, readrandom = 2
int zone[256] = {0,};
int from;
int my_container_num;
int total_container_num;
int rw;
int r_thread;
int w_spread;

FILE* fp0;
FILE* fp1;

void *zone_write(void *arg) {
	struct timeval g_start, g_end;
	void * test_data = malloc(_192KB * num_192kb);
	memset(test_data, 'W', _192KB * num_192kb);
	int i = 0;
	int num = *(int*)arg;
	float time = 0;

	//pthread_mutex_lock(&mutex);
	//pthread_mutex_unlock(&mutex);

	zns_set_zone(num, MAN_OPEN);
	gettimeofday(&g_start, NULL);

	while(i < ((zns_info -> zonef.zsze * 512)/(_192KB * num_192kb)/w_spread)){
		zns_write(test_data, _192KB * num_192kb, num);
		i++;
	}
	gettimeofday(&g_end, NULL);

	time = (g_end.tv_sec - g_start.tv_sec) + ((g_end.tv_usec - g_start.tv_usec) * 0.000001);

	fprintf(fp0, "W %u : %f\n", num, time);

	printf("W1 %u : %f\n", num, time);
	//printf("%f\n", time);

	free(test_data);

	pthread_exit((void *) 0);
}

void *zone_read(void *arg) {
	struct timeval g_start, g_end;
	int rnd_num = 0;
	float r_time = 0;
	int i = 0;
	int num = *(int*)arg;
	void * test_data2 = malloc(_192KB * num_192kb);

	//pthread_mutex_lock(&mutex);
	//pthread_mutex_unlock(&mutex);

	srand((unsigned int)time(NULL));


	while(i < ((zns_info -> zonef.zsze * 512)/(_192KB * num_192kb))){
		//if (benchmarks == 1) {
		
		gettimeofday(&g_start, NULL);                                                                 
		zns_read(test_data2, _192KB * num_192kb, num, (_192KB * num_192kb * i)/512);
		gettimeofday(&g_end, NULL);

		r_time = (g_end.tv_sec - g_start.tv_sec) + ((g_end.tv_usec - g_start.tv_usec) * 0.000001);
		printf("%f\n", r_time);
		//}
		/*
		   else if (benchmarks == 2) {
		   rnd_num = rand() % (zns_info -> zonef.zsze - _192KB * num_192kb);
		   zns_read(test_data2, _192KB * num_192kb, num, rnd_num/512);
		   }
		 */
		i++;
		//printf("%d\n", i);
	}



	//fprintf(fp1, "R %u : %f\n", num, r_time);
	//printf("R1 %u : %f\n", num, r_time);
	//printf("%f\n", num, r_time);

	free(test_data2);

	pthread_exit((void *) 0);
}

int wait(int my_num, int container_cnt)
{
	int start = 0;
	char myname[24] = {0,};
	char buf[24] = {0,};
	char fname[24] = {0,};

	sprintf(myname, "/shared/%d", my_num);

	FILE *fp;
	fp = fopen(myname, "w");
	fprintf(fp, "I am ready\n");
	fclose(fp);

	while(start == 0)
	{
		for(int i = 1; i < container_cnt + 1; i++)
		{
			sprintf(fname, "/shared/%d", i);
			if(access(fname, F_OK) != -1)
			{
				if(i == container_cnt)
					start = 1;
			}
			else
				break;
		}
	}

	return 1;
}

int main(int argc, char** argv) {
	int status;
	char junk; int n;
	int zone_num = 0;
	struct timeval g_start;
	time_t t; struct tm *lt; struct timeval tv;
	pthread_t threads[256];

	if(argc < 7)
	{
		printf("rw, num_192KB, from_zone, size, r_thread, w_spread\n");
		//printf("rw num_192KB, from_zone, size, benchmark, my_contaomer_num, total_container_num\n");
		exit(0);
	}

	//Parameter
	rw = atoi(argv[1]);
	num_192kb = atoi(argv[2]);
	from = atoi(argv[3]);
	size = atoi(argv[4]);
	r_thread = atoi(argv[5]);
	w_spread = atoi(argv[6]);
	//benchmarks = atoi(argv[5]);
	//my_container_num = atoi(argv[6]);
	//total_container_num = atoi(argv[7]);
	/*
	   wait(my_container_num, total_container_num)

	   if((t = gettimeofday(&tv, NULL)) == -1)
	   { perror("gettimeofday() call error"); return -1;}
	   if((lt = localtime(&tv.tv_sec)) == NULL)
	   { perror("localtime() call error"); return -1; }
	   printf("start time: %04d-%02d-%02d %02d:%02d:%02d.%06d\n", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec, tv.tv_usec);
	 */
	//ZNS SSD Init
	zns_get_info("/dev/nvme0n1");
	zns_get_zone_desc(REPORT_ALL, REPORT_ALL_STATE, 0, 0, true);
	//print_zns_info();
	srand((unsigned int)time(NULL));

	zone_num = from;
		//printf("A4\n");
	if(w_spread <= 1)
		w_spread = 1;

	if(rw == 0)
	{
		size = size * w_spread;
		for(int num = 0; num < size; num++)
		{
			zone[num] = zone_num;
			zone_num++;
		}

		fp0 = fopen("./w_output.txt", "w");
		for(int num = 0; num < size; num++)
		{
			pthread_create(&threads[num], NULL, &zone_write, (void *)&zone[num]);
			pthread_join(threads[num], (void **)&status);
		}

		fclose(fp0);
        /*
		for(int num = 0; num < size; num++) {
			pthread_join(threads[num], (void **)&status);
		}
		*/
        
	}

	if(rw == 1)
	{
		for(int num = 0; num < size; num++)
		{
			zone[num] = zone_num;
			zone_num++;
		}

		fp1 = fopen("/data/r_output.txt", "w");
		for(int num = 0; num < size; num++)
		{
			for(int num2 = 0; num2 < r_thread; num2++)
			{
				pthread_create(&threads[num*r_thread + num2], NULL, &zone_read, (void *)&zone[num]);
			}
		}

		fclose(fp1);

		for(int num = 0; num < size * r_thread; num++) {
			pthread_join(threads[num], (void **)&status);
		}
	}

	if(rw == 2)
	{
		for(int num = 0; num < size; num++)
		{
			zone[num] = zone_num;
			zone_num++;
		}

		fp1 = fopen("/data/r_output.txt", "w");
		for(int num = 0; num < size; num++)
		{
			pthread_create(&threads[num], NULL, &zone_read, (void *)&zone[num]);
		}

		fclose(fp1);

		for(int num = 0; num < size; num++) {
			pthread_join(threads[num], (void **)&status);
		}
	}

	//printf("------------------------\n");
	struct timeval g_start2, g_end2;
	float time = 0;
	
	sleep(1);
	
	for(int num = 0; num < size; num++) {
		gettimeofday(&g_start2, NULL);

		zns_set_zone(zone[num], MAN_RESET);
		//zns_set_zone(zone[num], MAN_CLOSE);
		gettimeofday(&g_end2, NULL);

		//time = (g_end.tv_sec - g_start2.tv_sec) + ((g_end.tv_usec - g_start2.tv_usec) * 0.000001);
		time = (g_end2.tv_usec - g_start2.tv_usec);

		printf("RESET %u : %f\n", num, time);

	}

	return 0;
}
