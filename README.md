# Simple block device
Simple block device module for Linux kernel.
By default it is allocate 100 MB of memory and supports read/write operations
## Compiling
This module was compiled for CentOS 7-4.1708 with the kernel 3.10.0-693 for x86-64 architecture and tested via Oracle VM VirtualBox
## This block device supports these parameters
* 'name' - name of device
* 'nsectors' - size of device (in sectros: bytes / 512)
* 'permissions' - permissions for read and write: 0 - read+write, 1 - only read
## You also can interact with this module by using sysfs files
/sys/module/%name of device% (blkdevice or yours)/
* /sys/module/%name of device% (blkdevice or yours)/num_of_sectors/num_of_sectors - for dynamically changing size
* /sys/module/%name of device% (blkdevice or yours)/permissions/permissions - for dynamically changing permissions
