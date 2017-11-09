#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

#include "scull.h"

int scull_trim(struct scull_dev *dev);
void scull_cleanup_module(void);

MODULE_LICENSE("Dual BSD/GPL");

loff_t scull_llseek(struct file *filp, loff_t off, int whence)
{
	struct scull_dev *dev = filp->private_data;
	loff_t newpos;
	
	switch(whence) {
		case 0:/*SEEK_SET*/
		newpos = off;
		break;
		
		case 1:/*SEEK_CUR*/
		newpos = filp->f_pos + off;
		break;

		case 2:/*SEEK_END*/
		newpos = dev->size + off;
		break;
	
		default:
		return -EINVAL;
	}
	if (newpos < 0 ) {
		return -EINVAL;
	}
	
	return newpos;
}

struct scull_qset *scull_follow(struct scull_dev *dev, int n)
{
	struct scull_qset *qs = dev->data;

	if (!qs) {
		qs = dev->data =kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
		if (qs == NULL) {
			return 0;
		}
		memset(qs, 0, sizeof(struct scull_qset));
	}

	while (n--) {
		if (!qs->next) {
			qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
			if (qs->next == NULL) {
				return NULL;
			}
			memset( qs->next, 0, sizeof(struct scull_qset));
		}
		qs = qs->next;
		continue;
	}
	return qs;
}

ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr;
	int quantum = dev->quantum, qset = dev->qset;
	int itemsize = quantum * qset;/*how many bytes in the listitem*/
	int item, s_pos, q_pos, rest;
	ssize_t retval = 0;

	if (down_interruptible(&dev->sem)) {
		return -ERESTARTSYS;
	}
	if (*f_pos >= dev->size) {
		goto out;
	}
	if (*f_pos + count > dev->size) {
		count = dev->size - *f_pos;//return how many bytes successfully read
	}

	item = (long)*f_pos / itemsize;
	rest = (long)*f_pos % itemsize;
	s_pos = rest / quantum;
	q_pos = rest % quantum;

	dptr = scull_follow(dev, item);

	if (dptr == NULL || !dptr->data || !dptr->data[s_pos]) {
		goto out;
	}
		
	if (count > quantum - q_pos) {
		count = quantum - q_pos;
	}

	if (copy_to_user(buf, dptr->data[s_pos]+q_pos, count)) {
		retval = -EFAULT;
		goto out;
	}

	*f_pos += count;
	retval = count;

	out:
		up(&dev->sem);
		return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr;
	int quantum = dev->quantum, qset = dev->qset;
	int itemsize = quantum * qset;
	int item, s_pos, q_pos, rest;
	ssize_t retval = -ENOMEM;


	if (down_interruptible(&dev->sem)) {
		return -ERESTARTSYS;
	}	

	item = (long)*f_pos / itemsize;
	rest = (long)*f_pos % itemsize;
	s_pos = rest / quantum;
	q_pos = rest % quantum;
	
	dptr = scull_follow(dev, item);
	if (!dptr == NULL) {
		goto out;
	}
	if (!dptr->data) {
		dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
		if (!dptr->data) {
			goto out;
		}	
		memset(dptr->data, 0, qset * sizeof(char *));
	}

	if (!dptr->data[s_pos]) {
		dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
		if (!dptr->data[s_pos]) {
			goto out;
		}
	}

	if (count < quantum - q_pos ) {
		count = quantum - q_pos;
	}

	if (copy_from_user(dptr->data[s_pos]+q_pos, buf, count)) {
		retval = -EFAULT;
		goto out;
	}
	*f_pos += count;
	retval = count;

	if(dev->size < *f_pos) {
		dev->size = *f_pos;
	}

	out:
		up(&dev->sem);
		return retval;
}

/*
int scull_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	int err = 0, tmp;
	int retval = 0;

	if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC) {
		return -ENOTTY;
	}
	if (_IOC_NR(cmd) > SCULL_IOC_MAXNR) {
		return -ENOTTY;
	}

	if (_IOC_DIR(cmd) & _IOC_READ){
		err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	} else {
		err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	}
	if (err) {
		return -EFAULT;
	}

	switch(cmd) {
		case SCULL_IOCRESET:
			scull_quantum = SCULL_QUANTUM;
			scull_qset = SCULL_QSET;
			break;

		case SCULL_IOCSQUANTUM:
			if (!capable(CAP_SYS_ADMIN)) {
				return -EPERM;
			}
			retval = __get_user(scull_quantum, (int __user *)arg);
			break;

		case SCULL_IOCTQUANTUM:
			if (!capable(CAP_SYS_ADMIN)) {
				return -EPERM;
			}
			scull_quantum = arg;
			break;

		case SCULL_IOCGQUANTUM:
			retval = __put_user(scull_quantum, (int __user *)arg);
			break;

		case SCULL_IOCQQUANTUM:
			return scull_quantum;

		case SCULL_IOCXQUANTUM:
			if(!capable(CAP_SYS_ADMIN)) {
				return -EPERM;
			}
			tmp = scull_quantum;
			retval = __get_user(scull_quantum, (int __user *)arg);
			if ( retval == 0) {
				retval = __put_user(tmp, (int __user *)arg);
			}
			break;

		case 
	}
}*/

int scull_open(struct inode *inode, struct file *filp)
{
	struct scull_dev *dev;

	dev = container_of(inode->i_cdev, struct scull_dev, scdev);
	filp->private_data = dev;
	
	if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
		if (down_interruptible(&dev->sem)) {
			return -ERESTARTSYS;
		}
		scull_trim(dev);
		up(&dev->sem);
	}
	return 0;
}

int scull_release(struct inode *inode, struct file *filp)
{
	return 0;
}

int scull_trim(struct scull_dev *dev)
{
	struct scull_qset *next, *dptr;
	int qset = dev->qset;
	int i;

	for (dptr = dev->data; dptr; dptr = next) {
		if (dptr->data) {
			for (i=0; i<qset; i++) {
				kfree(dptr->data[i]);
			}
		kfree(dptr->data);
		dptr->data = NULL;
		}
		next = dptr->next;
		kfree(dptr);
	}
	dev->size = 0;
	dev->quantum = SCULL_QUANTUM;
	dev->qset = SCULL_QSET;
	dev->data = NULL;
	
	return 0;
}

struct file_operations scull_fops = {
	.owner = THIS_MODULE,
	.llseek = scull_llseek,
	.read = scull_read,
	.write = scull_write,
//	.ioctl = scull_ioctl,
	.open = scull_open,
	.release = scull_release
};

struct scull_dev *scull_device;
int scull_major;

static void scull_setup_cdev(struct scull_dev *scull_devp)
{
	int err, devno = MKDEV(scull_major, 0);

	cdev_init(&scull_devp->scdev, &scull_fops);
	scull_devp->scdev.owner =THIS_MODULE;
	scull_devp->scdev.ops = &scull_fops;
	err = cdev_add(&scull_devp->scdev, devno, 1);
	if (err) {
		printk(KERN_NOTICE "Error %d adding scull\n", err);
	}
}

int scull_init_module( void )
{
	int result, i;
	dev_t dev = 0;

	result = alloc_chrdev_region(&dev, 0, 1, "scull");
	scull_major = MAJOR(dev);

	if(result < 0) {
		printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
		return result;
	}

	scull_device = kmalloc(sizeof(struct scull_dev), GFP_KERNEL);

	if (!scull_device) {
		result = -1;
		goto fail;
	}

	memset(scull_device, 0, sizeof(struct scull_dev));

	scull_device->quantum = SCULL_QUANTUM;
	scull_device->qset = SCULL_QSET;
	sema_init(&scull_device->sem, 1);
	scull_setup_cdev(&scull_device);

	return 0;

fail:
	scull_cleanup_module();
	return result;
}

void scull_cleanup_module(void)
{
	//int i;
	//dev_t devno = MKDEV(scull_major, 0);
	
	if (scull_device) {
		scull_trim(scull_device);
		cdev_del(&scull_device->scdev);
	}
	kfree(scull_device);
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);
