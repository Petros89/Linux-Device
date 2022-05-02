/*
 * main.c -- the bare scull char module
 *
 * This code is based on the scullpipe code from LDD book.
 *         Petros Apostolou - March 2022
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 */

#include <linux/sched.h>

#include <linux/configfs.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/seq_file.h>
#include <linux/cdev.h>

#include <linux/uaccess.h>	/* copy_*_user */
#include <linux/sched/signal.h>
#include "scullbuffer.h"	/* local definitions */

/*
 * Our parameters which can be set at load time.
 */

struct scull_buffer {
        wait_queue_head_t inq, outq;       /* read and write queues */
        char *buffer, *end;                /* begin of buf, end of buf */
        int buffersize;                    /* used in pointer arithmetic */
        char *rp, *wp;                     /* where to read, where to write */
        int  itemcount;      		   /* Number of items in the buffer */
        int nreaders, nwriters;            /* number of openings for r/w */
        struct semaphore sem;              /* mutual exclusion semaphore */
        struct cdev cdev;                  /* Char device structure */
};

/* parameters */
static int scull_b_nr_devs = SCULL_B_NR_DEVS;	/* number of buffer devices */
dev_t scull_b_devno;			/* Our first device number */

static struct scull_buffer *scull_b_devices;

#define init_MUTEX(_m) sema_init(_m, 1);

int scull_major =   SCULL_MAJOR;
int scull_minor =   0;
int NITEMS	=  20;
int itemsize = SCULL_B_ITEM_SIZE;

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_b_nr_devs, int, 0);
module_param(NITEMS, int, 0);

MODULE_AUTHOR("Student CSCI 5103-S2022 - adding code to the framework");
MODULE_LICENSE("Dual BSD/GPL");

/*
 * Open and close
 */
static int scull_b_open(struct inode *inode, struct file *filp)
{
	struct scull_buffer *dev;
        
	/* IMPLEMENT THIS FUNCTION - Inherited and slightly modified from pipe.c */ 
	dev = container_of(inode->i_cdev, struct scull_buffer, cdev);
	filp->private_data = dev;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	if (!dev->buffer) {
		/* allocate the buffer */
		dev->buffer = kmalloc(NITEMS * itemsize, GFP_KERNEL);
		if (!dev->buffer) {
			up(&dev->sem);
			return -ENOMEM;
		}

		dev->buffersize = NITEMS * itemsize;
		dev->end = dev->buffer + dev->buffersize; /* determine end of dev */
		dev->rp = dev->wp = dev->buffer; /* rd and wr from the beginning */

                /* let's prepare the waitqueue for readers */
		init_waitqueue_head(&(dev->inq));
		init_waitqueue_head(&(dev->outq));
	}

	/* use f_mode,not  f_flags: it's cleaner (fs/open.c tells why) */
	if (filp->f_mode & FMODE_READ)
		dev->nreaders++;
	if (filp->f_mode & FMODE_WRITE)
		dev->nwriters++;
	up(&dev->sem);

	return nonseekable_open(inode, filp);
}

static int scull_b_release(struct inode *inode, struct file *filp)
{
	struct scull_buffer *dev = filp->private_data;

	/* IMPLEMENT THIS FUNCTION - Copied from pipe.c */
	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	if (filp->f_mode & FMODE_READ) {
		printk("A reader is leaving\n");
		dev->nreaders--;
	}
	if (filp->f_mode & FMODE_WRITE) {
		printk("A writer is leaving\n");
		dev->nwriters--;	
	}

	if (dev->nreaders == 0) {
		wake_up_interruptible(&dev->outq);
	}
	// last writer leaving, send a notice to all readers.
	if (dev->nwriters == 0) {
		wake_up_interruptible(&dev->inq);
	}

	if (dev->nreaders + dev->nwriters == 0) {
		kfree(dev->buffer);
		dev->buffer = NULL; /* the other fields are not checked on open */
		dev->itemcount = 0;
		dev->rp = dev->wp = NULL;
	}
	// up
	up(&dev->sem);

	return 0;
}

/*
 * Data management: read and write
*/
static ssize_t scull_b_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{

	struct scull_buffer *dev = filp->private_data;

	// IMPLEMENT THIS FUNCTION - copied from pipe
	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	PDEBUG("\" (scull_b_read) dev->wp:%p    dev->rp:%p\" \n",dev->wp,dev->rp);

	while (dev->rp == dev->wp) { /* nothing to read */
		up(&dev->sem); /* release the lock */
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		PDEBUG("\"%s\" reading: going to sleep\n", current->comm);
		if (wait_event_interruptible(dev->inq, (dev->rp != dev->wp)))
			return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
		/* otherwise loop, but first reacquire the lock */
		if (down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	}
	/* ok, data is there, return something */
	if (dev->wp > dev->rp)
		count = min(count, (size_t)(dev->wp - dev->rp));
	else /* the write pointer has wrapped, return data up to dev->end */
		count = min(count, (size_t)(dev->end - dev->rp));
	if (copy_to_user(buf, dev->rp, count)) {
		up (&dev->sem);
		return -EFAULT;
	}
	dev->rp += count;
	if (dev->rp == dev->end)
		dev->rp = dev->buffer; /* wrapped */
	up (&dev->sem);

	/* finally, awake any writers and return */
	wake_up_interruptible(&dev->outq);
	PDEBUG("\"%s\" did read %li bytes\n",current->comm, (long)count);
	return count;
} 

/*
Linux synchronization primitives for allocating write memory for circular buffer
There are 2 required functions func(spacefree) and func(scull_getwritespace)
both copied from pipe.c
*/

/* How much space is free? */
static int spacefree(struct scull_buffer *dev)
{
	if (dev->rp == dev->wp)
		return dev->buffersize - 1;
	return ((dev->rp + dev->buffersize - dev->wp) % dev->buffersize) - 1;
}



/* Wait for space for writing; caller must hold device semaphore.  On
 * error the semaphore will be released before returning. */
static int scull_getwritespace(struct scull_buffer *dev, struct file *filp)
{
	while (spacefree(dev) == 0) { /* full */
		DEFINE_WAIT(wait);
		
		up(&dev->sem);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		PDEBUG("\"%s\" writing: going to sleep\n",current->comm);
		prepare_to_wait(&dev->outq, &wait, TASK_INTERRUPTIBLE);
		if (spacefree(dev) == 0)
			schedule();
		finish_wait(&dev->outq, &wait);
		if (signal_pending(current))
			return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
		if (down_interruptible(&dev->sem))
			return -ERESTARTSYS;
	}
	return 0;
}	

static ssize_t scull_b_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct scull_buffer *dev = filp->private_data;

	/* IMPLEMENT THIS FUNCTION - copied from pipe.c */ 
	int result;
    
	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	/* Make sure there's space to write */
	result = scull_getwritespace(dev, filp);
	if (result)
		return result; /* scull_getwritespace called up(&dev->sem) */

	/* ok, space is there, accept something */
	count = min(count, (size_t)spacefree(dev));
	if (dev->wp >= dev->rp)
		count = min(count, (size_t)(dev->end - dev->wp)); /* to end-of-buf */
	else /* the write pointer has wrapped, fill up to rp-1 */
		count = min(count, (size_t)(dev->rp - dev->wp - 1));
	PDEBUG("Going to accept %li bytes to %p from %p\n", (long)count, dev->wp, buf);
	if (copy_from_user(dev->wp, buf, count)) {
		up (&dev->sem);
		return -EFAULT;
	}
	dev->wp += count;
	if (dev->wp == dev->end)
		dev->wp = dev->buffer; /* wrapped */
	PDEBUG("\" (scull_b_write) dev->wp:%p    dev->rp:%p\" \n",dev->wp,dev->rp);
	up(&dev->sem);

	/* finally, awake any reader */
	wake_up_interruptible(&dev->inq);  /* blocked in read() and select() */

	PDEBUG("\"%s\" did write %li bytes\n",current->comm, (long)count);
	return count;
}


/*
 * The file operations for the buffer device
 * This is S2022 framework codebase.
 */
struct file_operations scull_buffer_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.read =		scull_b_read,
	.write =	scull_b_write,
	.open =		scull_b_open,
	.release =	scull_b_release,
};

/*
 * Set up a cdev entry.
 */
static void scull_b_setup_cdev(struct scull_buffer *dev, int index)
{
	/* IMPLEMENT THIS FUNCTION - copied from pipe.c */
	int err;
	int devno = MKDEV(scull_major, scull_minor + index);
	// set up char_dev: the variable is called (dev->cdev)
	cdev_init(&dev->cdev, &scull_buffer_fops);
	dev->cdev.owner = THIS_MODULE;
	// the 0 means devno(basically nothing I think, doesn't matter here)
	err = cdev_add(&dev->cdev, devno, 1);

	/* Fail gracefully if need be */
	if (err)
		printk(KERN_NOTICE "Error %d adding scullbuffer %d", err, index);
}

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized
 */
void scull_b_cleanup_module(void)
{
	// IMPLEMENT THIS FUNCTION
	//
	int i;

	if (!scull_b_devices)
		return; /* nothing else to release */

	for (i = 0; i < scull_b_nr_devs; i++) {
		cdev_del(&scull_b_devices[i].cdev);
		kfree(scull_b_devices[i].buffer);
	}
	kfree(scull_b_devices);
	unregister_chrdev_region(scull_b_devno, scull_b_nr_devs);
	scull_b_devices = NULL; /* pedantic */
}

int scull_b_init_module(void)
{
	// IMPLEMENT THIS FUNCTION
	//
	int result, i;
	dev_t dev = 0;

	if (scull_major) {
		dev = MKDEV(scull_major, scull_minor);
		result = register_chrdev_region(dev, scull_b_nr_devs, "scullbuffer");
	} else {
		result = alloc_chrdev_region(&dev, scull_minor, scull_b_nr_devs, "scullbuffer");
		scull_major = MAJOR(dev);
	}
	if (result < 0) {
		printk(KERN_WARNING "scullbuffer: can't get major %d\n", scull_major);
		return result;
	}

        /* 
	 * allocate the devices -- we can't have them static, as the number
	 * can be specified at load time
	 */
	scull_b_devices = kmalloc(scull_b_nr_devs * sizeof(struct scull_buffer), GFP_KERNEL);
	if (!scull_b_devices) {
		result = -ENOMEM;
		goto fail;  /* Make this more graceful */
	}
	memset(scull_b_devices, 0, scull_b_nr_devs * sizeof(struct scull_buffer));

        /* Initialize each device. */
	for (i = 0; i < scull_b_nr_devs; i++) {
		init_waitqueue_head(&(scull_b_devices[i].inq));
		init_waitqueue_head(&(scull_b_devices[i].outq));
		init_MUTEX(&scull_b_devices[i].sem);
		scull_b_setup_cdev(&scull_b_devices[i], i);
	}

        /* At this point call the init function for creating the scull_pipe devices */
	dev = MKDEV(scull_major, scull_minor + scull_b_nr_devs);

	return 0; /* succeed */
  	
  fail:
	scull_b_cleanup_module();
	return result;
}


module_init(scull_b_init_module);
module_exit(scull_b_cleanup_module);
