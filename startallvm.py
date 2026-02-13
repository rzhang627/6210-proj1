#!/usr/bin/env python

from __future__ import print_function
from vm import VMManager
import time
import subprocess

VM_PREFIX = "aos"

if __name__ == '__main__':
    manager = VMManager()
    vms = manager.getFilteredVms(VM_PREFIX)
    
    for vmname in vms:
        manager.startVM(vmname)
        subprocess.call(['uvt-kvm', 'wait', vmname])
    time.sleep(5)
