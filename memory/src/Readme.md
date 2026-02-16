# Memory Coordinator

- Please write your algorithm and logic for the memory coordinator in this readme. 
- Include details about how the algorithm is designed and explain the overall flow of your program.
- Use pseudocode or simple explanations where necessary to make it easy to understand.
- Don't forget to cite any resources that you've used to complete the assignment.

## Algorithm Design

The Memory Coordinator manages the physical memory allocation of active Virtual Machines (VMs) using the **Virtio Balloon Driver**. The goal is to ensure that no VM starves for memory while reclaiming unused memory from VMs that no longer need it.

### 1. Metric: Memory Pressure
The coordinator monitors the memory status of each domain using the Libvirt API (`virDomainMemoryStats`).
* **Actual Memory:** The total amount of physical RAM currently allocated to the VM.
* **Unused Memory:** The amount of RAM inside the VM that is free and available for use.

### 2. Policy: Demand-Based Allocation
The algorithm operates on a generic "threshold" logic to keep the **Unused Memory** within a healthy range (e.g., between 100MB and 200MB).
* **Starvation Check (Scale Up):** If a VM's unused memory drops below a safety threshold (indicating high pressure), the coordinator **increases** its allocation. This allows the VM to continue running workloads without crashing or swapping.
* **Waste Check (Scale Down):** If a VM's unused memory exceeds a maximum threshold (indicating wasted resources), the coordinator **decreases** its allocation. This returns memory to the host pool, making it available for other VMs.

### 3. Stability & Limits
To prevent instability:
* **Step Size:** Memory is adjusted in fixed increments (e.g., 50MB-100MB per interval) rather than large jumps, preventing oscillation.
* **Bounds:** The coordinator respects minimum and maximum memory limits for each VM to ensure the OS has enough to boot and doesn't exceed the host's capacity.

---

## Program Flow

The program runs as a persistent daemon loop:

1.  **Initialization:**
    * Connect to the hypervisor (`qemu:///system`).
    * Set the update interval (provided via command line arguments).

2.  **Monitoring Loop:**
    * **List Domains:** Query the hypervisor for all active domains.
    * **Get Stats:** For each domain, retrieve `VIR_DOMAIN_MEMORY_STAT_UNUSED`.
    * **Calculate Action:**
        * Compare the current `Unused` memory against the defined **Low** and **High** thresholds.
        * Determine if `virDomainSetMemory` needs to be called to increase or decrease the balloon size.
    * **Apply Changes:** Execute the memory adjustment if necessary.
    * **Sleep:** Wait for the next interval.

3.  **Cleanup:**
    * On exit, close the connection to the hypervisor and free resources.

---

## Pseudocode

```text
While (Running):
    Active_Domains = Get_Active_Domains()

    For each VM in Active_Domains:
        Stats = Get_Memory_Stats(VM)
        Current_Alloc = Stats.Actual_Balloon
        Unused_Mem = Stats.Unused

        # Case 1: VM is starving (needs more memory)
        If (Unused_Mem < MIN_THRESHOLD):
            New_Alloc = Current_Alloc + INCREASE_STEP
            Set_Memory(VM, New_Alloc)
            Print "Increasing memory for VM..."

        # Case 2: VM has excess memory (reclaim it)
        Else If (Unused_Mem > MAX_THRESHOLD):
            New_Alloc = Current_Alloc - DECREASE_STEP
            
            # Ensure we don't shrink below a safe minimum
            If (New_Alloc > SAFE_MIN_LIMIT):
                Set_Memory(VM, New_Alloc)
                Print "Reclaiming memory from VM..."

    Sleep(Interval)