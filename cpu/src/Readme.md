# VCPU Scheduler 

- Please write your algorithm and logic for the VCPU scheduler in this readme. 
- Include details about how the algorithm is designed and explain the overall flow of your program. 
- Use pseudocode or simple explanations where necessary to make it easy to understand.
- Don't forget to cite any resources that you've used to complete the assignment.

Here is the raw Markdown content for your cpu/src/Readme.md. You can copy and paste the code block below directly into your file.

Markdown
# VCPU Scheduler

## Algorithm and Logic Design

The goal of this VCPU scheduler is to dynamically balance the CPU load of active virtual machines (Domains) across the host's physical CPUs (pCPUs). The scheduler operates as a userspace daemon that wakes up at a user-defined interval to assess the current system state and mitigate load imbalances.

### Core Logic
The algorithm follows a **Measure-Analyze-Act** loop:

1.  **Measure (Data Collection):**
    * The scheduler queries the hypervisor via `libvirt` to list all active domains.
    * For every vCPU in each domain, it fetches `cpu_time` (accumulated nanoseconds of usage).
    * **Metric Calculation:** Since `cpu_time` is cumulative, we store the state from the *previous* iteration. The utilization percentage is calculated as:
        $$\text{Usage} (\%) = \frac{\text{Current CPU Time} - \text{Previous CPU Time}}{\text{Interval Time}} \times 100$$
    * These vCPU loads are then aggregated onto the pCPUs they are currently pinned to, giving us the total load for each physical core.

2.  **Analyze (Imbalance Detection):**
    * We calculate the **Standard Deviation ($\sigma$)** of the pCPU loads.
    * A threshold is defined (typically $\sigma \le 5.0$).
    * If the standard deviation is below the threshold, the system is considered **Balanced**, and no actions are taken (to preserve stability).
    * If $\sigma > 5.0$, the system is **Unbalanced**.

3.  **Act (Rebalancing Strategy):**
    * The algorithm identifies the pCPU with the **highest load** (Source) and the pCPU with the **lowest load** (Target).
    * It selects a vCPU from the Source pCPU to migrate to the Target pCPU.
    * **Heuristic:** The scheduler attempts to move a vCPU such that the move reduces the load difference between the Source and Target, bringing the system closer to the average load.
    * This pin is applied immediately using `virDomainPinVcpu`.

## Program Flow

The program structure consists of initialization followed by an infinite loop:

1.  **Initialization:**
    * Open connection to QEMU/KVM hypervisor (`virConnectOpen`).
    * Discover host topology (number of pCPUs).
    * Allocate memory for tracking `DomainState` (previous CPU times).

2.  **The Scheduler Loop:**
    * **Sleep:** Wait for the specified `interval`.
    * **Cleanup:** Free data from domains that have shut down since the last interval.
    * **Update Stats:** Fetch current time and CPU stats for all active domains.
    * **Map & Aggregate:** Construct a map of `pCPU -> [vCPU1, vCPU2, ...]`. Compute total load per pCPU.
    * **Check Balance:** Compute Standard Deviation of pCPU loads.
    * **Rebalance (if needed):**
        * Find `pCPU_max` and `pCPU_min`.
        * Identify best vCPU to move.
        * Call `virDomainPinVcpu` to update affinity.
    * **Commit State:** Update `previous_cpu_time` = `current_cpu_time` for the next iteration.

## Pseudocode

```c
struct DomainState {
    unsigned long long prev_cpu_time;
    // ... other tracking info
};

void CPUScheduler(double interval) {
    virConnectPtr conn = virConnectOpen("qemu:///system");
    int num_pcpus = virNodeGetInfo(conn, ...);

    while (true) {
        sleep(interval);
        
        // 1. Get Active Domains
        virDomainPtr* domains = virConnectListAllDomains(conn, ...);
        
        // 2. Calculate Load per pCPU
        double pcpu_loads[num_pcpus] = {0};
        
        foreach (domain in domains) {
            unsigned long long current_time = virDomainGetCPUStats(domain);
            double load = (current_time - domain->prev_cpu_time) / interval;
            
            int current_pin = get_current_pcpu_pin(domain);
            pcpu_loads[current_pin] += load;
            
            // Update history for next time
            domain->prev_cpu_time = current_time;
        }

        // 3. Check for Imbalance
        double std_dev = calculate_std_dev(pcpu_loads);
        
        if (std_dev > 5.0) {
            // 4. Rebalance
            int source_pcpu = find_max_load_pcpu(pcpu_loads);
            int target_pcpu = find_min_load_pcpu(pcpu_loads);
            
            virDomainPtr vcpu_to_move = find_best_vcpu(source_pcpu);
            
            virDomainPinVcpu(vcpu_to_move, target_pcpu);
        }
    }
}