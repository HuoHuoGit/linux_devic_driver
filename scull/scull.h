#ifndef _SCULL_H_
#define _SCULL_H_

#include <linux/ioctl.h>
#include <linux/cdev.h>

#define SCULL_QUANTUM 4000
#define SCULL_QSET 1000

struct scull_qset {
	void **data;
	struct scull_qset *next;
};

struct scull_dev {
	struct scull_qset *data;
	int quantum;
	int qset;
	unsigned long size;
	unsigned int access_key;
	struct semaphore sem;
	struct cdev scdev;
};

#endif
