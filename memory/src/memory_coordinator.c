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

int is_exit = 0; // DO NOT MODIFY THE VARIABLE

void MemoryScheduler(virConnectPtr conn, int interval);

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

	signal(SIGINT, signal_callback_handler);

	while (!is_exit)
	{
		// Calls the MemoryScheduler function after every 'interval' seconds
		MemoryScheduler(conn, interval);
		sleep(interval);
	}

	// Close the connection
	virConnectClose(conn);
	return 0;
}

/*
COMPLETE THE IMPLEMENTATION
*/
void MemoryScheduler(virConnectPtr conn, int interval)
{
	// --- Constants (Libvirt uses Kilobytes) ---
    const unsigned long long HOST_MIN_FREE_KB = 200 * 1024; // 200 MB buffer for Host
    const unsigned long long VM_MIN_UNUSED_KB = 100 * 1024; // 100 MB buffer for VM
    // Safety Floor: Never reduce VM size below this absolute limit (e.g. 200MB)
    const unsigned long long VM_ABSOLUTE_MIN_KB = 200 * 1024; 

    const unsigned long long MAX_RELEASE_CHUNK = 50 * 1024; // Max drop per interval
    const unsigned long long MAX_GROWTH_CHUNK  = 50 * 1024; // Max growth per interval

    // --- Step 1: Get Host Memory Statistics ---
    unsigned long long host_free_mem_kb = 0;
    int nparams = 0;
    
    if (virNodeGetMemoryStats(conn, VIR_NODE_MEMORY_STATS_ALL_CELLS, NULL, &nparams, 0) == 0 && nparams > 0) {
        virNodeMemoryStatsPtr params = malloc(sizeof(virNodeMemoryStats) * nparams);
        if (virNodeGetMemoryStats(conn, VIR_NODE_MEMORY_STATS_ALL_CELLS, params, &nparams, 0) == 0) {
            for (int i = 0; i < nparams; i++) {
                if (strcmp(params[i].field, VIR_NODE_MEMORY_STATS_FREE) == 0) {
                    host_free_mem_kb = params[i].value;
                    break;
                }
            }
        }
        free(params);
    }

    // --- Step 2: List Active VMs ---
    virDomainPtr *domains;
    int num_domains = virConnectListAllDomains(conn, &domains, VIR_CONNECT_LIST_DOMAINS_ACTIVE);
    if (num_domains < 0) return; 

    // --- Step 3: Scheduler Logic ---
    for (int i = 0; i < num_domains; i++) {
        virDomainPtr dom = domains[i];

        // 3a. Enable stats collection 
        virDomainSetMemoryStatsPeriod(dom, interval, 0);

        // 3b. Fetch Memory Stats
        virDomainMemoryStatStruct stats[VIR_DOMAIN_MEMORY_STAT_NR];
        int nr_stats = virDomainMemoryStats(dom, stats, VIR_DOMAIN_MEMORY_STAT_NR, 0);

        unsigned long long vm_unused_kb = 0;
        unsigned long long vm_actual_kb = 0;
        int found_unused = 0; 
        int found_actual = 0;

        for (int j = 0; j < nr_stats; j++) {
            if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED) {
                vm_unused_kb = stats[j].val;
                found_unused = 1;
            }
            if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON) {
                vm_actual_kb = stats[j].val;
                found_actual = 1;
            }
        }

        if (found_unused && found_actual) {
            
            // === CASE 1: VM Needs Memory ===
            if (vm_unused_kb < VM_MIN_UNUSED_KB) {
                if (host_free_mem_kb > HOST_MIN_FREE_KB) {
                    unsigned long long deficit = VM_MIN_UNUSED_KB - vm_unused_kb;
                    unsigned long long to_add = (deficit > MAX_GROWTH_CHUNK) ? deficit : MAX_GROWTH_CHUNK;

                    if (host_free_mem_kb > (HOST_MIN_FREE_KB + to_add)) {
                        unsigned long long new_mem = vm_actual_kb + to_add;
                        if (virDomainSetMemory(dom, new_mem) == 0) {
                            host_free_mem_kb -= to_add; 
                        }
                    }
                }
            }
            
            // === CASE 2: VM has Excess (Reclaim Memory) ===
            else if (vm_unused_kb > VM_MIN_UNUSED_KB) {
                
                unsigned long long excess = vm_unused_kb - VM_MIN_UNUSED_KB;
                
                if (excess > (10 * 1024)) { // Hysteresis > 10MB
                    unsigned long long to_release = MIN(excess, MAX_RELEASE_CHUNK);
                    
                    // --- NEW SAFETY CHECK HERE ---
                    // Predict the new size
                    unsigned long long predicted_mem = vm_actual_kb - to_release;

                    // If dropping below 200MB, clamp it so we stop exactly at 200MB
                    if (predicted_mem < VM_ABSOLUTE_MIN_KB) {
                         if (vm_actual_kb > VM_ABSOLUTE_MIN_KB) {
                             // We can take *something*, just not the full amount
                             to_release = vm_actual_kb - VM_ABSOLUTE_MIN_KB;
                         } else {
                             // VM is already at or below 200MB. Take nothing.
                             to_release = 0;
                         }
                    }

                    // Proceed only if we have memory to take
                    if (to_release > 0) {
                        unsigned long long new_mem = vm_actual_kb - to_release;
                        if (virDomainSetMemory(dom, new_mem) == 0) {
                            host_free_mem_kb += to_release;
                        }
                    }
                }
            }
        }
        virDomainFree(dom);
    }
    free(domains);
}