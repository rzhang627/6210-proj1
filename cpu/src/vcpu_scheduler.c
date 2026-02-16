#include <stdio.h>
#include <stdlib.h>
#include <libvirt/libvirt.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <time.h>
#define MIN(a, b) ((a) < (b) ? a : b)
#define MAX(a, b) ((a) > (b) ? a : b)

int is_exit = 0; // DO NOT MODIFY THIS VARIABLE

void CPUScheduler(virConnectPtr conn, int interval);

struct NodeData {
    unsigned char uuid[VIR_UUID_BUFLEN]; // Identifies the VM
    int vcpu_id;                         // Identifies the specific vCPU
    unsigned long long prev_cpu_time;    // Previous CPU time (nanoseconds)
    double current_usage;                // Stored usage % (to avoid re-calc bugs)
};

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
	// --- 1. GET HOST TOPOLOGY ---
    virNodeInfo node_info;
    if (virNodeGetInfo(conn, &node_info) < 0) return;
    int num_pcpus = node_info.cpus;

    // --- 2. PREPARE DATA STRUCTURES ---
    static struct NodeData *stats_db = NULL;
    static int db_capacity = 0;
    static int db_count = 0;

    double *pcpu_loads = calloc(num_pcpus, sizeof(double));

    virDomainPtr *domains;
    unsigned int flags = VIR_CONNECT_LIST_DOMAINS_ACTIVE;
    int num_domains = virConnectListAllDomains(conn, &domains, flags);

    if (num_domains < 0) {
        free(pcpu_loads);
        return;
    }

    // Resize DB if needed
    int estimated_vcpus = num_domains * 4;
    if (stats_db == NULL || estimated_vcpus > db_capacity) {
        db_capacity = estimated_vcpus + 20;
        stats_db = realloc(stats_db, sizeof(struct NodeData) * db_capacity);
    }

    // --- 3. DATA COLLECTION LOOP ---
    for (int i = 0; i < num_domains; i++) {
        virDomainInfo domain_info;
        if (virDomainGetInfo(domains[i], &domain_info) < 0) continue;

        unsigned char uuid[VIR_UUID_BUFLEN];
        if (virDomainGetUUID(domains[i], uuid) < 0) continue;

        virVcpuInfoPtr vcpu_info = malloc(sizeof(virVcpuInfo) * domain_info.nrVirtCpu);
        if (virDomainGetVcpus(domains[i], vcpu_info, domain_info.nrVirtCpu, NULL, 0) < 0) {
            free(vcpu_info);
            continue;
        }

        for (int j = 0; j < domain_info.nrVirtCpu; j++) {
            // Find or Create Entry
            int idx = -1;
            for (int k = 0; k < db_count; k++) {
                if (memcmp(stats_db[k].uuid, uuid, VIR_UUID_BUFLEN) == 0 && 
                    stats_db[k].vcpu_id == vcpu_info[j].number) {
                    idx = k;
                    break;
                }
            }

            if (idx == -1) {
                // New Entry
                if (db_count >= db_capacity) {
                    db_capacity += 20;
                    stats_db = realloc(stats_db, sizeof(struct NodeData) * db_capacity);
                }
                idx = db_count++;
                memcpy(stats_db[idx].uuid, uuid, VIR_UUID_BUFLEN);
                stats_db[idx].vcpu_id = vcpu_info[j].number;
                stats_db[idx].prev_cpu_time = vcpu_info[j].cpuTime;
                stats_db[idx].current_usage = 0.0; 
                // Skip calculation for new vCPU so we don't get 0.0 load errors
                continue;
            }

            // Calculate Utilization
            unsigned long long diff = (vcpu_info[j].cpuTime > stats_db[idx].prev_cpu_time) 
                                    ? (vcpu_info[j].cpuTime - stats_db[idx].prev_cpu_time) : 0;
            
            double usage = (diff) / (double)(interval * 1000000000.0) * 100.0;
            if (usage > 100.0) usage = 100.0;

            // Update DB
            stats_db[idx].prev_cpu_time = vcpu_info[j].cpuTime;
            stats_db[idx].current_usage = usage; // STORE IT HERE for Step 5

            // Update pCPU Load
            int current_pcpu = vcpu_info[j].cpu;
            if (current_pcpu < num_pcpus) {
                pcpu_loads[current_pcpu] += usage;
            }
        }
        free(vcpu_info);
    }

    // --- 4. ANALYZE LOAD ---
    double total_load = 0;
    for (int i = 0; i < num_pcpus; i++) total_load += pcpu_loads[i];
    double mean = total_load / num_pcpus;

    double variance_sum = 0;
    int busiest_pcpu = 0;
    int idlest_pcpu = 0;

    for (int i = 0; i < num_pcpus; i++) {
        variance_sum += pow(pcpu_loads[i] - mean, 2);
        if (pcpu_loads[i] > pcpu_loads[busiest_pcpu]) busiest_pcpu = i;
        if (pcpu_loads[i] < pcpu_loads[idlest_pcpu]) idlest_pcpu = i;
    }

    double std_dev = sqrt(variance_sum / num_pcpus);
    printf("StdDev: %.2f | Busiest CPU%d: %.2f | Idlest CPU%d: %.2f\n", 
           std_dev, busiest_pcpu, pcpu_loads[busiest_pcpu], idlest_pcpu, pcpu_loads[idlest_pcpu]);

    // --- 5. REBALANCE IF NECESSARY ---
    if (std_dev > 5.0) {
        printf("Rebalancing required...\n");

        virDomainPtr best_domain = NULL;
        int best_vcpu = -1;
        double max_usage_found = -1.0;

        // Iterate domains to find the HEAVIEST vCPU on the BUSIEST pCPU
        for (int i = 0; i < num_domains; i++) {
            virDomainInfo d_info;
            if (virDomainGetInfo(domains[i], &d_info) < 0) continue;

            unsigned char uuid[VIR_UUID_BUFLEN];
            if (virDomainGetUUID(domains[i], uuid) < 0) continue;

            virVcpuInfoPtr v_info = malloc(sizeof(virVcpuInfo) * d_info.nrVirtCpu);
            if (virDomainGetVcpus(domains[i], v_info, d_info.nrVirtCpu, NULL, 0) < 0) {
                free(v_info);
                continue;
            }

            for (int j = 0; j < d_info.nrVirtCpu; j++) {
                if (v_info[j].cpu == busiest_pcpu) {
                    // Look up the USAGE we calculated in Step 3
                    for (int k = 0; k < db_count; k++) {
                        if (memcmp(stats_db[k].uuid, uuid, VIR_UUID_BUFLEN) == 0 && 
                            stats_db[k].vcpu_id == v_info[j].number) {
                            
                            // Use stored usage (robust against recalculation timing)
                            if (stats_db[k].current_usage > max_usage_found) {
                                max_usage_found = stats_db[k].current_usage;
                                best_domain = domains[i];
                                best_vcpu = v_info[j].number;
                            }
                            break;
                        }
                    }
                }
            }
            free(v_info);
        }

        // Pin the victim
        if (best_domain != NULL && max_usage_found > 0.0) {
            unsigned char *cpumap = calloc(VIR_CPU_MAPLEN(num_pcpus), sizeof(unsigned char));
            VIR_USE_CPU(cpumap, idlest_pcpu);

            if (virDomainPinVcpu(best_domain, best_vcpu, cpumap, VIR_CPU_MAPLEN(num_pcpus)) == 0) {
                printf(" -> Moved vCPU %d (Load %.2f) from CPU %d to CPU %d\n", 
                       best_vcpu, max_usage_found, busiest_pcpu, idlest_pcpu);
            }
            free(cpumap);
        }
    }

    // --- 6. CLEANUP ---
    for (int i = 0; i < num_domains; i++) virDomainFree(domains[i]);
    free(domains);
    free(pcpu_loads);
}