#include <stdio.h>
#include <stdlib.h>
#include <libvirt/libvirt.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#define MIN(a, b) ((a) < (b) ? a : b)
#define MAX(a, b) ((a) > (b) ? a : b)

int is_exit = 0; // DO NOT MODIFY THIS VARIABLE

void CPUScheduler(virConnectPtr conn, int interval);

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
void signal_callback_handler()
{
	printf("Caught Signal");
	is_exit = 1;
}

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
int main(int argc, char *argv[])
{
	virConnectPtr conn;

	if (argc != 2)
	{
		printf("Incorrect number of arguments\n");
		return 0;
	}

	// Gets the interval passes as a command line argument and sets it as the STATS_PERIOD for collection of balloon memory statistics of the domains
	int interval = atoi(argv[1]);

	conn = virConnectOpen("qemu:///system");
	if (conn == NULL)
	{
		fprintf(stderr, "Failed to open connection\n");
		return 1;
	}

	// Get the total number of pCpus in the host
	signal(SIGINT, signal_callback_handler);

	while (!is_exit)
	// Run the CpuScheduler function that checks the CPU Usage and sets the pin at an interval of "interval" seconds
	{
		CPUScheduler(conn, interval);
		sleep(interval);
	}

	// Closing the connection
	virConnectClose(conn);
	return 0;
}

/* COMPLETE THE IMPLEMENTATION */
void CPUScheduler(virConnectPtr conn, int interval)
{
	// first check the number of active vCPUs
	virDomainPtr *domains;
	int num_domains;
	unsigned int flags = VIR_CONNECT_LIST_DOMAINS_ACTIVE;
	num_domains = virConnectListAllDomains(conn, &domains, flags);

	if (num_domains < 0)
	{
		printf("Failed to get the number of active vCPUs\n");
		return;
	}

	printf("Found %d active vCPUs\n", num_domains);

	// for a given interval we must track each guest's vCpu utilization to balance workload between pCPUs
	// run the virDomainGet* functions from libvirt-domain to gather vCPU statistics

	for (int i = 0; i < num_domains; i++) {
        virDomainPtr dom = domains[i];
        
        // gather statistics
        virDomainFree(dom);
    }
    
    // free the array of pointers
    free(domains);
}