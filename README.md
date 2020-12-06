# Simple block device
Simple block device module for Linux kernel.
By default it is allocate 100 MB of memory and supports read/write operations
## This block device supports these parameters
* 'name' - name of device
* 'nsectors' - size of device (in sectros: bytes / 512)
* 'permissions' - permissions for read and write: 0 - read+write, 1 - only read
## You also can interact with this module by using sysfs files
* 'num_of_sectors' - for dynamically changing size
* 'permissions' - for dynamically changing permissions
