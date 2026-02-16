# VCPU Scheduler 

- Please write your algorithm and logic for the VCPU scheduler in this readme. 
- Include details about how the algorithm is designed and explain the overall flow of your program. 
- Use pseudocode or simple explanations where necessary to make it easy to understand.
- Don't forget to cite any resources that you've used to complete the assignment.

# VCPU Scheduler

## Algorithm Design

The goal of this scheduler is to balance the CPU load of active Virtual Machines (vCPUs) across the available Physical CPUs (pCPUs) while minimizing unnecessary migrations ("pin changes") to maintain stability.

### 1. Metric: CPU Utilization
The scheduler calculates the load for each vCPU by tracking the cumulative `cpuTime` provided by the hypervisor.
* **Formula:** `Usage = (Current_cpuTime - Prev_cpuTime) / Interval`
* **Handling New VMs:** When a VM is detected for the first time, its usage is skipped for that interval to prevent erroneous "0.0" or "100.0" spikes caused by uninitialized history data.

### 2. Balance Metric: Standard Deviation
To determine if the system is unbalanced, the algorithm calculates the **Standard Deviation (StdDev)** of the loads across all pCPUs.
* **Threshold:** A rebalance is only triggered if `StdDev > 5.0`. This prevents the scheduler from reacting to background noise or negligible imbalances, ensuring the system remains stable when "good enough".

### 3. Victim Selection (Heaviest First)
If rebalancing is required, the scheduler identifies:
* **Source:** The **Busiest pCPU** (highest current load).
* **Target:** The **Idlest pCPU** (lowest current load).
* **Victim vCPU:** The vCPU on the Source pCPU with the **highest individual load**. Moving the heaviest task minimizes the number of moves required to achieve balance.

### 4. Stability Check: Projected Standard Deviation
Before performing any migration, the algorithm performs a **lookahead check**:
1.  It simulates moving the selected victim vCPU to the target pCPU mathematically.
2.  It calculates the **Projected StdDev** of this theoretical configuration.
3.  **Decision Rule:** The move is executed **only if** `Projected StdDev < Current StdDev`.
    * This prevents "ping-ponging," where moving a task causes the target CPU to become overloaded, leading to an infinite loop of swapping.

---

## Program Flow

The program runs as a daemon loop with the following steps:

1.  **Initialization:** Connect to the `qemu:///system` hypervisor and detect the number of physical CPUs.
2.  **Data Collection:**
    * Query the list of active domains (VMs).
    * Iterate through every vCPU of every domain.
    * Update the persistent `NodeData` structure with the current `cpuTime`.
    * Aggregate individual vCPU loads to calculate the total load for each pCPU.
3.  **Analysis:**
    * Calculate the mean load and Standard Deviation of the pCPUs.
    * Identify the Busiest and Idlest pCPUs.
4.  **Decision Making:**
    * **Check 1:** Is `StdDev > 5.0`? If no, sleep and wait for the next interval.
    * **Check 2:** Find the heaviest vCPU on the busiest core.
    * **Check 3:** Would moving this vCPU reduce the StdDev?
5.  **Execution:**
    * If all checks pass, pin the victim vCPU to the Idlest pCPU using `virDomainPinVcpu`.
6.  **Cleanup:** Free memory allocated for domain info and maps to prevent leaks.

---

## Pseudocode

```text
While (Running):
    pCPU_Loads = [0, 0, 0, 0]
    
    For each VM in Active_Domains:
        Usage = (VM.Time_Now - VM.Time_Prev) / Interval
        pCPU_Loads[VM.Current_pCPU] += Usage
        Update_History(VM)

    Current_StdDev = Calculate_StdDev(pCPU_Loads)

    If (Current_StdDev > 5.0):
        Source = Index_Of_Max(pCPU_Loads)
        Target = Index_Of_Min(pCPU_Loads)
        
        Victim_VM = Find_Heaviest_VM_On(Source)
        
        # Stability Check
        Simulated_Loads = pCPU_Loads
        Simulated_Loads[Source] -= Victim_VM.Load
        Simulated_Loads[Target] += Victim_VM.Load
        
        Projected_StdDev = Calculate_StdDev(Simulated_Loads)
        
        If (Projected_StdDev < Current_StdDev):
            Pin(Victim_VM, Target)
            Print "Moved vCPU for better balance."
            
    Sleep(Interval)