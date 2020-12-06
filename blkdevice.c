#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h> /* printk() */
#include <linux/fs.h>     /* everything... */
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/kobject.h>

/* 
 * User's NAME of device
 */
static char* name = NULL;
module_param(name, charp,  S_IRUGO | S_IWUSR); /* everyone can read, user can write */

/* 
 * User's SIZE of device 
 */
static int nsectors = 204800;                   /* number of sectors equal to 100 MB */
module_param(nsectors, int, S_IRUGO | S_IWUSR); /* everyone can read, user can write */

/*
 * Permissiong for read/write operations
 */
static int permissions = 0;
module_param(permissions, bool, S_IRUGO | S_IWUSR); /* everyone can read, user can write */

/*
 * Standard size of sector
 */
#define KERNEL_SECTOR_SIZE 512

/*
 * Params of device
 */
#define MAJORS 0          /* System gives major number by itself by default */
#define MINORS 16
static int major_result;

/*
 * Struct to describe device
 */
static struct blk_device {
	unsigned long size; /* in bytes */
	spinlock_t lock;
	u8 *data;
	struct gendisk *gd;
};

/*
 * request_queue holds list of request for blkdevice
 */
static struct request_queue *rqueue = NULL;
static struct blk_device *blkdevice = NULL;

/*
 * Handler for file system requests (read and write)
 */
static void blkdevice_transfer(struct blk_device *dev, 
                               sector_t sector, 
                               unsigned long nsect, 
                               char *buffer, 
                               int write)
{
	unsigned long offset = sector * KERNEL_SECTOR_SIZE;
	unsigned long nbytes = nsect * KERNEL_SECTOR_SIZE;

	/*
	 * Check for size
	 */
	if ((offset + nbytes) > dev->size) {
		printk (KERN_NOTICE "Beyond-end write (%ld %ld)\n", offset, nbytes);
		return;
	}

	/*
	 * Check for permissions
	 */
	if (write && permissions == 0) {                              /* write */
		memcpy(dev->data + offset, buffer, nbytes);
	} else if (write && permissions == 1) {                       /* write prohibited */
		printk(KERN_ALERT "You don't have permissions to write\n");
	} else {                                                      /* read  */
		memcpy(buffer, dev->data + offset, nbytes);
	}
}

/*
 * Going threw the request_queue and chek each request for its type
 */
static void blkdevice_request(struct request_queue *rqueue)
{
	struct request *req;

	req = blk_fetch_request(rqueue);
	while (req != NULL) {
		
		if (req->cmd_type != REQ_TYPE_FS) {
			printk (KERN_ALERT "Not read/write request\n");
			__blk_end_request_all(req, -EIO);
			continue;
		}
		blkdevice_transfer(blkdevice, 
                                   blk_rq_pos(req),
                                   blk_rq_cur_sectors(req),
                                   req->buffer,
                                   rq_data_dir(req));

		if (!__blk_end_request_cur(req, 0)) {
			req = blk_fetch_request(rqueue);
		}
	}
}

/*
 * Mapping implemented functions to special struct
 * Only read/write function is implemented, so the struct is empty
 */
static struct block_device_operations sbd_ops = {
	.owner  = THIS_MODULE,
};

/*
 * kobject for working with size
 */
static int new_sectors = 0;
static struct kobject *new_sectors_kobject;
static ssize_t new_sectors_show(struct kobject *kobj, 
                                struct kobj_attribute *attr,
                                char *buf)
{
	new_sectors = nsectors;
	return sprintf(buf, "%d\n", new_sectors);
}

static ssize_t new_sectors_store(struct kobject *kobj, 
                                 struct kobj_attribute *attr,
                                 char *buf, 
                                 size_t count)
{
	sscanf(buf, "%d", &new_sectors);
	if (new_sectors <= nsectors) {
		printk(KERN_ALERT "You are trying to create a device with less volume\nExit\n");
		return count;
	} else {
		u8 *tmp = vmalloc(new_sectors * KERNEL_SECTOR_SIZE);
		if (tmp == NULL) {
			printk(KERN_ALERT "Allocation failure\nExit\n");
			return count;
		}
		memcpy(tmp, blkdevice->data, blkdevice->size);
		vfree(blkdevice->data);
		blkdevice->data = tmp;
		tmp = NULL;
		printk(KERN_ALERT "Size changed\n");
	}
}
static struct kobj_attribute new_sectors_attribute = 
__ATTR(nsectors, S_IRUGO | S_IWUSR, new_sectors_show, new_sectors_store);

/*
 * kobject for working with read/write permissions
 * 0 = read+write (by default)
 * 1 = read only
 */
static struct kobject *permissions_kobject;
static ssize_t permissions_show(struct kobject *kobj, 
                                struct kobj_attribute *attr,
                                char *buf)
{
	return sprintf(buf, "%d\n", permissions);
}

static ssize_t permissions_store(struct kobject *kobj, 
                                 struct kobj_attribute *attr,
                                 char *buf, 
                                 size_t count)
{
	sscanf(buf, "%du", &permissions);
	return count;
}
static struct kobj_attribute permissions_attribute = 
__ATTR(permissions, S_IRUGO | S_IWUSR, permissions_show, permissions_store);


/*
 * Get resources and initialize them
 */
static int __init blockdevice_init(void)
{
	printk(KERN_ALERT "Module inserted\n");

	/*
	 * Get memory for blkdevice
	 */
	blkdevice = kmalloc(sizeof(struct blk_device), GFP_KERNEL);
	if (blkdevice == NULL) {
		printk(KERN_ALERT "Failed to allocate memory for blkdevice\n");
		return -ENOMEM;
	}

	/*
	 * Preparing memory
	 */
	memset(blkdevice, 0, sizeof(struct blk_device));

	/*
	 * Setting up device
	 */
	blkdevice->size = nsectors * KERNEL_SECTOR_SIZE;
	spin_lock_init(&blkdevice->lock);

	/*
	 * May not be contiguous
	 * vmalloc is used because we need large amount of memory
	 */
	blkdevice->data = vmalloc(blkdevice->size);
	if (blkdevice->data == NULL) {
		printk(KERN_ALERT "Failed to allocate memory for data\n");

		kfree(blkdevice);
		return -ENOMEM;
	}

	/*
	 * Get a request_queue
	 * providing request handler and spinlock
	 * force logical block size to 512 bytes
	 */
	rqueue = blk_init_queue(blkdevice_request, &(blkdevice->lock)); 
	if (rqueue == NULL) {
		printk(KERN_ALERT "Failed to init queue\n");
		goto queue_fail;
	}
	blk_queue_logical_block_size(rqueue, KERNEL_SECTOR_SIZE);

	/*
	 * Registering our blk_device
	 * MAJOR = 0 means kernel gives major number by itself
	 * use "blkdevice" by default name and buffer 'name' if user set this param
	 */
	if (name == NULL) {
		strcpy(name, "blkdevice");
	}
	major_result = register_blkdev(MAJORS, name);
	if (major_result <= 0) {
		printk(KERN_WARNING "Failed to get major number\n");
		goto register_fail;
	}

	/*
	 * Initializing the gendisk structure
	 */
	blkdevice->gd = alloc_disk(MINORS);
	if (!blkdevice->gd) {
		printk(KERN_WARNING "Failed to alloc_disk\n");
		goto alloc_disk_fail;
	}
	blkdevice->gd->major = major_result;
	blkdevice->gd->first_minor = 0;
	blkdevice->gd->fops = &sbd_ops;
	blkdevice->gd->private_data = &blkdevice;
	strcpy(blkdevice->gd->disk_name, "blkdevice0");
	set_capacity(blkdevice->gd, nsectors);
	blkdevice->gd->queue = rqueue;

	/* 
	 * Adding disk to the system
	 */
	add_disk(blkdevice->gd);

	/*
	 * Creating kobject and sysfs for new_sectors
	 */
	new_sectors_kobject = kobject_create_and_add("num_of_sectors", &THIS_MODULE->mkobj.kobj);

	if (!new_sectors_kobject) {
		printk(KERN_ALERT "Failed to create kobject for number of sectors");
		goto alloc_disk_fail;
	}

	int error;
	error = sysfs_create_file(new_sectors_kobject, &new_sectors_attribute.attr);

	if (error) {
		printk(KERN_CRIT "\n Error in creating sysfs_file for num_of_sectors\n");
		goto sysfs_num_of_sectors_fail;
	}

	/*
	 * Creating kobject and sysfs for permissions
	 */
	permissions_kobject = kobject_create_and_add("permissions", &THIS_MODULE->mkobj.kobj);

	if (!permissions_kobject) {
		printk(KERN_ALERT "Failed to create kobject for permissions");
		goto sysfs_num_of_sectors_fail;
	}

	error = sysfs_create_file(permissions_kobject, &permissions_attribute.attr);

	if (error) {
		printk(KERN_CRIT "\n Error in creating sysfs_file for permissions\n");
		goto sysfs_permissions_fail;
	}

	printk (KERN_ALERT "Finished Initialization of the module\n");;
	return 0;

sysfs_permissions_fail:
	kobject_put(permissions_kobject);
	sysfs_remove_file(new_sectors_kobject, &new_sectors_attribute.attr);

sysfs_num_of_sectors_fail:
	kobject_put(new_sectors_kobject);	

alloc_disk_fail:
	unregister_blkdev(major_result, "sbd");

register_fail:
	blk_cleanup_queue(rqueue);

queue_fail:
	vfree(blkdevice->data);
	kfree(blkdevice);
	return -ENOMEM;
}

/*
 * Clean everything and return resources when removing module
 */
static void __exit blockdevice_exit(void)
{	
	printk(KERN_ALERT "Removing module\n");

	del_gendisk(blkdevice->gd);
	put_disk(blkdevice->gd);
	unregister_blkdev(major_result, name);
	blk_cleanup_queue(rqueue);
	vfree(blkdevice->data);
	kfree(blkdevice);

	kobject_put(new_sectors_kobject);
	kobject_put(permissions_kobject);

	sysfs_remove_file(new_sectors_kobject, &new_sectors_attribute.attr);
	sysfs_remove_file(permissions_kobject, &permissions_attribute.attr);

	printk(KERN_ALERT "Everything is clean\nModule removed\n");
}

module_init(blockdevice_init);
module_exit(blockdevice_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ilia Morozov - morozov97@mail.ru");
MODULE_DESCRIPTION("Simple block device");
