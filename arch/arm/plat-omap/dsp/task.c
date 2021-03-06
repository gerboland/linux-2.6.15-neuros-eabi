/*
 * linux/arch/arm/mach-omap/dsp/task.c
 *
 * OMAP DSP task device driver
 *
 * Copyright (C) 2002-2005 Nokia Corporation
 *
 * Written by Toshihiro Kobayashi <toshihiro.kobayashi@nokia.com>
 * mmap function by Hiroo Ishikawa <ext-hiroo.ishikawa@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * 2005/07/26:  DSP Gateway version 3.3
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/signal.h>
#include <asm/irq.h>
#include <asm/ioctls.h>
#include <asm/arch/dsp.h>
#include "uaccess_dsp.h"
#include "dsp.h"
#include "ipbuf.h"
#include "fifo.h"
#include "proclist.h"

#define is_aligned(adr,align)	(!((adr)&((align)-1)))

/*
 * taskdev.state: device state machine
 * NOTASK:	task is not attached.
 * ATTACHED:	task is attached.
 * GARBAGE:	task is detached. waiting for all processes to close this device.
 * ADDREQ:	requesting for tadd
 * DELREQ:	requesting for tdel. no process is opening this device.
 * ADDFAIL:	tadd failed.
 * ADDING:	tadd in process.
 * DELING:	tdel in process.
 * KILLING:	tkill in process.
 */
#define devstate_name(stat) (\
	((stat) & OMAP_DSP_DEVSTATE_NOTASK)   ? "NOTASK" :\
	((stat) & OMAP_DSP_DEVSTATE_ATTACHED) ? "ATTACHED" :\
	((stat) & OMAP_DSP_DEVSTATE_GARBAGE)  ? "GARBAGE" :\
	((stat) & OMAP_DSP_DEVSTATE_INVALID)  ? "INVALID" :\
	((stat) & OMAP_DSP_DEVSTATE_ADDREQ)   ? "ADDREQ" :\
	((stat) & OMAP_DSP_DEVSTATE_DELREQ)   ? "DELREQ" :\
	((stat) & OMAP_DSP_DEVSTATE_ADDFAIL)  ? "ADDFAIL" :\
	((stat) & OMAP_DSP_DEVSTATE_ADDING)   ? "ADDING" :\
	((stat) & OMAP_DSP_DEVSTATE_DELING)   ? "DELING" :\
	((stat) & OMAP_DSP_DEVSTATE_KILLING)  ? "KILLING" :\
						"unknown")

struct taskdev {
	struct bus_type *bus;
//	struct device_driver *driver;
	struct device dev;	/* Generic device interface */

	long state;
	spinlock_t state_lock;
	wait_queue_head_t state_wait_q;
	unsigned int usecount;
	char name[OMAP_DSP_TNM_LEN];
	struct file_operations fops;
	struct list_head proc_list;
	struct dsptask *task;

	/* read stuff */
	wait_queue_head_t read_wait_q;
	struct semaphore read_sem;

	/* write stuff */
	wait_queue_head_t write_wait_q;
	struct semaphore write_sem;

	/* ioctl stuff */
	wait_queue_head_t ioctl_wait_q;
	struct semaphore ioctl_sem;

	/* device lock */
	struct semaphore lock_sem;
	pid_t lock_pid;
};

#define to_taskdev(n) container_of(n, struct taskdev, dev)

struct rcvdt_bk_struct {
	struct ipblink link;
	unsigned int rp;
	struct ipbuf_p *ipbuf_pvt_r;
};

struct dsptask {
	enum {
		TASK_STATE_ERR = 0,
		TASK_STATE_READY,
		TASK_STATE_CFGREQ
	} state;
	unsigned char tid;
	char name[OMAP_DSP_TNM_LEN];
	unsigned short ttyp;
	struct taskdev *dev;

	/* read stuff */
	union {
		struct fifo_struct fifo;	/* for active word */
		struct rcvdt_bk_struct bk;
	} rcvdt;

	/* write stuff */
	size_t wsz;
	spinlock_t wsz_lock;
	struct ipbuf_p *ipbuf_pvt_w;	/* for private block */

	/* tctl stuff */
	int tctl_stat;

	/* mmap stuff */
	void *map_base;
	size_t map_length;
};

#define sndtyp_acv(ttyp)	((ttyp) & OMAP_DSP_TTYP_ASND)
#define sndtyp_psv(ttyp)	(!((ttyp) & OMAP_DSP_TTYP_ASND))
#define sndtyp_bk(ttyp)		((ttyp) & OMAP_DSP_TTYP_BKDM)
#define sndtyp_wd(ttyp)		(!((ttyp) & OMAP_DSP_TTYP_BKDM))
#define sndtyp_pvt(ttyp)	((ttyp) & OMAP_DSP_TTYP_PVDM)
#define sndtyp_gbl(ttyp)	(!((ttyp) & OMAP_DSP_TTYP_PVDM))
#define rcvtyp_acv(ttyp)	((ttyp) & OMAP_DSP_TTYP_ARCV)
#define rcvtyp_psv(ttyp)	(!((ttyp) & OMAP_DSP_TTYP_ARCV))
#define rcvtyp_bk(ttyp)		((ttyp) & OMAP_DSP_TTYP_BKMD)
#define rcvtyp_wd(ttyp)		(!((ttyp) & OMAP_DSP_TTYP_BKMD))
#define rcvtyp_pvt(ttyp)	((ttyp) & OMAP_DSP_TTYP_PVMD)
#define rcvtyp_gbl(ttyp)	(!((ttyp) & OMAP_DSP_TTYP_PVMD))

static int dsp_rmdev_minor(unsigned char minor);
static int taskdev_init(struct taskdev *dev, char *name, unsigned char minor);
static void taskdev_delete(unsigned char minor);
static void taskdev_attach_task(struct taskdev *dev, struct dsptask *task);
static void taskdev_detach_task(struct taskdev *dev);
static int dsp_tdel_bh(unsigned char minor, unsigned short type);

static ssize_t devname_show(struct device *d, struct device_attribute *attr,
			    char *buf);
static ssize_t devstate_show(struct device *d, struct device_attribute *attr,
			     char *buf);
static ssize_t proc_list_show(struct device *d, struct device_attribute *attr,
			      char *buf);
static ssize_t taskname_show(struct device *d, struct device_attribute *attr,
			     char *buf);
static ssize_t ttyp_show(struct device *d, struct device_attribute *attr,
			 char *buf);
static ssize_t fifosz_show(struct device *d, struct device_attribute *attr,
			   char *buf);
static int fifosz_store(struct device *d, struct device_attribute *attr,
			const char *buf, size_t count);
static ssize_t fifocnt_show(struct device *d, struct device_attribute *attr,
			    char *buf);
static ssize_t ipblink_show(struct device *d, struct device_attribute *attr,
			    char *buf);
static ssize_t wsz_show(struct device *d, struct device_attribute *attr,
			char *buf);
static ssize_t mmap_show(struct device *d, struct device_attribute *attr,
			 char *buf);

static struct device_attribute dev_attr_devname = __ATTR_RO(devname);
static struct device_attribute dev_attr_devstate = __ATTR_RO(devstate);
static struct device_attribute dev_attr_proc_list = __ATTR_RO(proc_list);
static struct device_attribute dev_attr_fifosz = 
	__ATTR(fifosz, S_IWUGO | S_IRUGO, fifosz_show, fifosz_store);
static struct device_attribute dev_attr_fifocnt = __ATTR_RO(fifocnt);
static struct device_attribute dev_attr_taskname = __ATTR_RO(taskname);
static struct device_attribute dev_attr_ttyp = __ATTR_RO(ttyp);
static struct device_attribute dev_attr_ipblink = __ATTR_RO(ipblink);
static struct device_attribute dev_attr_wsz = __ATTR_RO(wsz);
static struct device_attribute dev_attr_mmap = __ATTR_RO(mmap);

static struct bus_type dsptask_bus = {
	.name = "dsptask",
};

static struct class *dsp_task_class;
static struct taskdev *taskdev[TASKDEV_MAX];
static struct dsptask *dsptask[TASKDEV_MAX];
static DECLARE_MUTEX(cfg_sem);
static unsigned short cfg_cmd;
static unsigned char cfg_tid;
static DECLARE_WAIT_QUEUE_HEAD(cfg_wait_q);
static unsigned char n_task;
static void *heap;

#define devstate_lock(dev, devstate)	devstate_lock_timeout(dev, devstate, 0)

/*
 * devstate_lock_timeout():
 * when called with timeout > 0, dev->state can be diffeent from what you want.
 */
static int devstate_lock_timeout(struct taskdev *dev, long devstate,
				 int timeout)
{
	DECLARE_WAITQUEUE(wait, current);
	long current_state = current->state;
	int ret = 0;

	spin_lock(&dev->state_lock);
	add_wait_queue(&dev->state_wait_q, &wait);
	while (!(dev->state & devstate)) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock(&dev->state_lock);
		if (timeout) {
			if ((timeout = schedule_timeout(timeout)) == 0) {
				/* timeout */
				spin_lock(&dev->state_lock);
				break;
			}
		}
		else
			schedule();
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		spin_lock(&dev->state_lock);
	}
	remove_wait_queue(&dev->state_wait_q, &wait);
	set_current_state(current_state);
	return ret;
}

static __inline__ void devstate_unlock(struct taskdev *dev)
{
	spin_unlock(&dev->state_lock);
}

static __inline__ int down_tasksem_interruptible(struct taskdev *dev,
						 struct semaphore *sem)
{
	int ret;

	if (dev->lock_pid == current->pid) {
		/* this process has lock */
		ret = down_interruptible(sem);
	} else {
		if ((ret = down_interruptible(&dev->lock_sem)) != 0)
			return ret;
		ret = down_interruptible(sem);
		up(&dev->lock_sem);
	}
	return ret;
}

static int dsp_task_flush_buf(struct dsptask *task)
{
	unsigned short ttyp = task->ttyp;

	if (sndtyp_wd(ttyp)) {
		/* word receiving */
		flush_fifo(&task->rcvdt.fifo);
	} else {
		/* block receiving */
		struct rcvdt_bk_struct *rcvdt = &task->rcvdt.bk;

		spin_lock(&rcvdt->link.lock);
		if (sndtyp_gbl(ttyp)) {
			/* global IPBUF */
			while (!ipblink_empty(&rcvdt->link)) {
				unsigned short bid = rcvdt->link.top;
				ipblink_del_top(&rcvdt->link, ipbuf);
				unuse_ipbuf(bid);
			}
		} else {
			/* private IPBUF */
			if (!ipblink_empty(&rcvdt->link)) {
				ipblink_del_pvt(&rcvdt->link);
				release_ipbuf_pvt(rcvdt->ipbuf_pvt_r);
			}
		}
		spin_unlock(&rcvdt->link.lock);
	}

	return 0;
}

static int dsp_task_set_fifosz(struct dsptask *task, unsigned long sz)
{
	unsigned short ttyp = task->ttyp;
	int stat;

	if (!(sndtyp_wd(ttyp) && sndtyp_acv(ttyp))) {
		printk(KERN_ERR
		       "omapdsp: buffer size can be changed only for "
		       "active word sending task.\n");
		return -EINVAL;
	}
	if ((sz == 0) || (sz & 1)) {
		printk(KERN_ERR "omapdsp: illegal buffer size! (%ld)\n"
			        "it must be even and non-zero value.\n", sz);
		return -EINVAL;
	}

	stat = realloc_fifo(&task->rcvdt.fifo, sz);
	if (stat == -EBUSY) {
		printk(KERN_ERR "omapdsp: buffer is not empty!\n");
		return stat;
	} else if (stat < 0) {
		printk(KERN_ERR
		       "omapdsp: unable to change receive buffer size. "
		       "(%ld bytes for %s)\n", sz, task->name);
		return stat;
	}

	return 0;
}

static int taskdev_lock(struct taskdev *dev)
{
	if (down_interruptible(&dev->lock_sem))
		return -ERESTARTSYS;
	dev->lock_pid = current->pid;
	return 0;
}

static int taskdev_unlock(struct taskdev *dev)
{
	if (dev->lock_pid != current->pid) {
		printk(KERN_ERR
		       "omapdsp: an illegal process attempted to "
		       "unlock the dsptask lock!\n");
		return -EINVAL;
	}
	dev->lock_pid = 0;
	up(&dev->lock_sem);
	return 0;
}

static int dsp_task_config(struct dsptask *task, unsigned char tid)
{
	unsigned short ttyp;
	struct mbcmd mb;
	int ret;

	task->tid = tid;
	dsptask[tid] = task;

	/* TCFG request */
	task->state = TASK_STATE_CFGREQ;
	if (down_interruptible(&cfg_sem)) {
		ret = -ERESTARTSYS;
		goto fail_out;
	}
	cfg_cmd = MBCMD(TCFG);
	mbcmd_set(mb, MBCMD(TCFG), tid, 0);
	dsp_mbcmd_send_and_wait(&mb, &cfg_wait_q);
	cfg_cmd = 0;
	up(&cfg_sem);

	if (task->state != TASK_STATE_READY) {
		printk(KERN_ERR "omapdsp: task %d configuration error!\n", tid);
		ret = -EINVAL;
		goto fail_out;
	}

	if (strlen(task->name) <= 1)
		sprintf(task->name, "%d", tid);
	printk(KERN_INFO "omapdsp: task %d: name %s\n", tid, task->name);

	ttyp = task->ttyp;

	/*
	 * task info sanity check
	 */

	/* task type check */
	if (rcvtyp_psv(ttyp) && rcvtyp_pvt(ttyp)) {
		printk(KERN_ERR "omapdsp: illegal task type(0x%04x), tid=%d\n",
		       tid, ttyp);
		ret = -EINVAL;
		goto fail_out;
	}

	/* private buffer address check */
	if (sndtyp_pvt(ttyp) &&
	    (ipbuf_p_validate(task->rcvdt.bk.ipbuf_pvt_r, DIR_D2A) < 0)) {
		ret = -EINVAL;
		goto fail_out;
	}
	if (rcvtyp_pvt(ttyp) &&
	    (ipbuf_p_validate(task->ipbuf_pvt_w, DIR_A2D) < 0)) {
		ret = -EINVAL;
		goto fail_out;
	}
	
	/* mmap buffer configuration check */
	if ((task->map_length > 0) &&
	    ((!is_aligned((unsigned long)task->map_base, PAGE_SIZE)) ||
	     (!is_aligned(task->map_length, PAGE_SIZE)) ||
	     (dsp_mem_type(task->map_base, task->map_length) != MEM_TYPE_EXTERN))) {
		printk(KERN_ERR
		       "omapdsp: illegal mmap buffer address(0x%p) or "
		       "length(0x%x).\n"
		       "  It needs to be page-aligned and located at "
		       "external memory.\n",
		       task->map_base, task->map_length);
		ret = -EINVAL;
		goto fail_out;
	}

	/*
	 * initialization
	 */

	/* read initialization */
	if (sndtyp_wd(ttyp)) {
		/* word */
		size_t fifosz;

		fifosz = sndtyp_psv(ttyp) ? 2 :	/* passive */
					    32;	/* active */
		if (init_fifo(&task->rcvdt.fifo, fifosz) < 0) {
			printk(KERN_ERR
			       "omapdsp: unable to allocate receive buffer. "
			       "(%d bytes for %s)\n", fifosz, task->name);
			ret = -ENOMEM;
			goto fail_out;
		}
	} else {
		/* block */
		INIT_IPBLINK(&task->rcvdt.bk.link);
		task->rcvdt.bk.rp = 0;
	}

	/* write initialization */
	spin_lock_init(&task->wsz_lock);
	task->wsz = rcvtyp_acv(ttyp) ? 0 :		/* active */
		    rcvtyp_wd(ttyp)  ? 2 :		/* passive word */
		    		       ipbcfg.lsz*2;	/* passive block */

	return 0;

fail_out:
	dsptask[tid] = NULL;
	return ret;
}

static void dsp_task_init(struct dsptask *task)
{
	dsp_mbsend(MBCMD(TCTL), task->tid, OMAP_DSP_MBCMD_TCTL_TINIT);
}

int dsp_task_config_all(unsigned char n)
{
	int i, ret;
	struct taskdev *devheap;
	struct dsptask *taskheap;
	size_t devheapsz, taskheapsz;

	memset(taskdev, 0, sizeof(void *) * TASKDEV_MAX);
	memset(dsptask, 0, sizeof(void *) * TASKDEV_MAX);

	n_task = n;
	printk(KERN_INFO "omapdsp: found %d task(s)\n", n_task);
	if (n_task == 0)
		return 0;

	/*
	 * reducing kmalloc!
	 */
	devheapsz  = sizeof(struct taskdev) * n_task;
	taskheapsz = sizeof(struct dsptask) * n_task;
	heap = kmalloc(devheapsz + taskheapsz, GFP_KERNEL);
	if (heap == NULL) {
		n_task = 0;
		return -ENOMEM;
	}
	memset(heap, 0, devheapsz + taskheapsz);
	devheap  = heap;
	taskheap = heap + devheapsz;

	for (i = 0; i < n_task; i++) {
		struct taskdev *dev  = &devheap[i];
		struct dsptask *task = &taskheap[i];

		if ((ret = dsp_task_config(task, i)) < 0)
			return ret;
		if ((ret = taskdev_init(dev, task->name, i)) < 0)
			return ret;
		taskdev_attach_task(dev, task);
		dsp_task_init(task);
		printk(KERN_INFO "omapdsp: taskdev %s enabled.\n", dev->name);
	}

	return 0;
}

static void dsp_task_unconfig(struct dsptask *task)
{
	unsigned char tid = task->tid;

	preempt_disable();
	dsp_task_flush_buf(task);
	if (sndtyp_wd(task->ttyp) && (task->state == TASK_STATE_READY))
		free_fifo(&task->rcvdt.fifo);
	dsptask[tid] = NULL;
	preempt_enable();
}

void dsp_task_unconfig_all(void)
{
	unsigned char minor;
	unsigned char tid;
	struct dsptask *task;

	for (minor = 0; minor < n_task; minor++) {
		/*
		 * taskdev[minor] can be NULL in case of
		 * configuration failure
		 */
		if (taskdev[minor])
			taskdev_delete(minor);
	}
	for (; minor < TASKDEV_MAX; minor++) {
		if (taskdev[minor])
			dsp_rmdev_minor(minor);
	}

	for (tid = 0; tid < n_task; tid++) {
		/*
		 * dsptask[tid] can be NULL in case of
		 * configuration failure
		 */
		task = dsptask[tid];
		if (task)
			dsp_task_unconfig(task);
	}
	for (; tid < TASKDEV_MAX; tid++) {
		task = dsptask[tid];
		if (task) {
			/*
			 * on-demand tasks should be deleted in
			 * rmdev_minor(), but just in case.
			 */
			dsp_task_unconfig(task);
			kfree(task);
		}
	}

	if (heap) {
		kfree(heap);
		heap = NULL;
	}

	n_task = 0;
}

static struct device_driver dsptask_driver = {
	.name	= "dsptask",
	.bus	= &dsptask_bus,
};

unsigned char dsp_task_count(void)
{
	return n_task;
}

int dsp_taskmod_busy(void)
{
	struct taskdev *dev;
	unsigned char minor;

	for (minor = 0; minor < TASKDEV_MAX; minor++) {
		dev = taskdev[minor];
		if (dev == NULL)
			continue;
		if (dev->usecount > 0) {
			printk("dsp_taskmod_busy(): %s: usecount=%d\n",
			       dev->name, dev->usecount);
			return 1;
		}
/*
		if ((dev->state & (OMAP_DSP_DEVSTATE_ADDREQ |
				   OMAP_DSP_DEVSTATE_DELREQ)) {
*/
		if (dev->state & OMAP_DSP_DEVSTATE_ADDREQ) {
			printk("dsp_taskmod_busy(): %s is in %s\n",
			       dev->name, devstate_name(dev->state));
			return 1;
		}
	}
	return 0;
}

/*
 * DSP task device file operations
 */
static ssize_t dsp_task_read_wd_acv(struct file *file, char *buf, size_t count,
				    loff_t *ppos)
{
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
	struct taskdev *dev = taskdev[minor];
	int have_devstate_lock = 0;
	int ret = 0;

	if (count == 0) {
		return 0;
	} else if (count & 0x1) {
		printk(KERN_ERR
		       "omapdsp: odd count is illegal for DSP task device.\n");
		return -EINVAL;
	}

	if (down_tasksem_interruptible(dev, &dev->read_sem))
		return -ERESTARTSYS;
	if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED) < 0) {
		ret = -ERESTARTSYS;
		goto up_out;
	}
	have_devstate_lock = 1;

	if (fifo_empty(&dev->task->rcvdt.fifo)) {
		long current_state = current->state;
		DECLARE_WAITQUEUE(wait, current);

		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&dev->read_wait_q, &wait);
		if (fifo_empty(&dev->task->rcvdt.fifo)) {	/* last check */
			devstate_unlock(dev);
			have_devstate_lock = 0;
			schedule();
		}
		set_current_state(current_state);
		remove_wait_queue(&dev->read_wait_q, &wait);
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			goto up_out;
		}
		if (!have_devstate_lock) {
			if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED) < 0) {
				ret = -ERESTARTSYS;
				goto up_out;
			}
			have_devstate_lock = 1;
		}
		if (fifo_empty(&dev->task->rcvdt.fifo))	/* should not occur */
			goto up_out;
	}

	ret = copy_to_user_fm_fifo(buf, &dev->task->rcvdt.fifo, count);

up_out:
	if (have_devstate_lock)
		devstate_unlock(dev);
	up(&dev->read_sem);
	return ret;
}

static ssize_t dsp_task_read_bk_acv(struct file *file, char *buf, size_t count,
				    loff_t *ppos)
{
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
	struct taskdev *dev = taskdev[minor];
	struct rcvdt_bk_struct *rcvdt;
	int have_devstate_lock = 0;
	ssize_t ret = 0;

	if (count == 0) {
		return 0;
	} else if (count & 0x1) {
		printk(KERN_ERR
		       "omapdsp: odd count is illegal for DSP task device.\n");
		return -EINVAL;
	} else if ((int)buf & 0x1) {
		printk(KERN_ERR
		       "omapdsp: buf should be word aligned for "
		       "dsp_task_read().\n");
		return -EINVAL;
	}

	if (down_tasksem_interruptible(dev, &dev->read_sem))
		return -ERESTARTSYS;
	if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED) < 0) {
		ret = -ERESTARTSYS;
		goto up_out;
	}
	have_devstate_lock = 1;

	if (ipblink_empty(&dev->task->rcvdt.bk.link)) {
		long current_state;
		DECLARE_WAITQUEUE(wait, current);

		add_wait_queue(&dev->read_wait_q, &wait);
		current_state = current->state;
		set_current_state(TASK_INTERRUPTIBLE);
		if (ipblink_empty(&dev->task->rcvdt.bk.link)) {	/* last check */
			devstate_unlock(dev);
			have_devstate_lock = 0;
			schedule();
		}
		set_current_state(current_state);
		remove_wait_queue(&dev->read_wait_q, &wait);
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			goto up_out;
		}
		if (!have_devstate_lock) {
			if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED) < 0) {
				ret = -ERESTARTSYS;
				goto up_out;
			}
			have_devstate_lock = 1;
		}
		/* signal or 0-byte send from DSP */
		if (ipblink_empty(&dev->task->rcvdt.bk.link))
			goto up_out;
	}

	rcvdt = &dev->task->rcvdt.bk;
	/* copy from delayed IPBUF */
	if (sndtyp_pvt(dev->task->ttyp)) {
		/* private */
		if (!ipblink_empty(&rcvdt->link)) {
			struct ipbuf_p *ipbp = rcvdt->ipbuf_pvt_r;
			unsigned char *base, *src;
			size_t bkcnt;

			if (dsp_mem_enable(ipbp) < 0) {
				ret = -ERESTARTSYS;
				goto up_out;
			}
			base = MKVIRT(ipbp->ah, ipbp->al);
			bkcnt = ((unsigned long)ipbp->c) * 2 - rcvdt->rp;
			if (dsp_address_validate(base, bkcnt,
						 "task %s read buffer",
						 dev->task->name) < 0) {
				ret = -ERESTARTSYS;
				goto pv_out1;
			}
			if (dsp_mem_enable(base) < 0) {
				ret = -ERESTARTSYS;
				goto pv_out1;
			}
			src = base + rcvdt->rp;
			if (bkcnt > count) {
				if (copy_to_user_dsp(buf, src, count)) {
					ret = -EFAULT;
					goto pv_out2;
				}
				ret = count;
				rcvdt->rp += count;
			} else {
				if (copy_to_user_dsp(buf, src, bkcnt)) {
					ret = -EFAULT;
					goto pv_out2;
				}
				ret = bkcnt;
				spin_lock(&rcvdt->link.lock);
				ipblink_del_pvt(&rcvdt->link);
				spin_unlock(&rcvdt->link.lock);
				release_ipbuf_pvt(ipbp);
				rcvdt->rp = 0;
			}
pv_out2:
			dsp_mem_disable(src);
pv_out1:
			dsp_mem_disable(ipbp);
		}
	} else {
		/* global */
		if (dsp_mem_enable_ipbuf() < 0) {
			ret = -ERESTARTSYS;
			goto up_out;
		}
		while (!ipblink_empty(&rcvdt->link)) {
			unsigned char *src;
			size_t bkcnt;
			unsigned short bid = rcvdt->link.top;
			struct ipbuf *ipbp = ipbuf[bid];

			src = ipbp->d + rcvdt->rp;
			bkcnt = ((unsigned long)ipbp->c) * 2 - rcvdt->rp;
			if (bkcnt > count) {
				if (copy_to_user_dsp(buf, src, count)) {
					ret = -EFAULT;
					goto gb_out;
				}
				ret += count;
				rcvdt->rp += count;
				break;
			} else {
				if (copy_to_user_dsp(buf, src, bkcnt)) {
					ret = -EFAULT;
					goto gb_out;
				}
				ret += bkcnt;
				buf += bkcnt;
				count -= bkcnt;
				spin_lock(&rcvdt->link.lock);
				ipblink_del_top(&rcvdt->link, ipbuf);
				spin_unlock(&rcvdt->link.lock);
				unuse_ipbuf(bid);
				rcvdt->rp = 0;
			}
		}
gb_out:
		dsp_mem_disable_ipbuf();
	}

up_out:
	if (have_devstate_lock)
		devstate_unlock(dev);
	up(&dev->read_sem);
	return ret;
}

static ssize_t dsp_task_read_wd_psv(struct file *file, char *buf, size_t count,
				    loff_t *ppos)
{
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
	struct taskdev *dev = taskdev[minor];
	struct mbcmd mb;
	unsigned char tid;
	int ret = 0;

	if (count == 0) {
		return 0;
	} else if (count & 0x1) {
		printk(KERN_ERR
		       "omapdsp: odd count is illegal for DSP task device.\n");
		return -EINVAL;
	} else {
		/* force! */
		count = 2;
	}

	if (down_tasksem_interruptible(dev, &dev->read_sem))
		return -ERESTARTSYS;
	if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED) < 0) {
		ret = -ERESTARTSYS;
		goto up_out;
	}
	tid = dev->task->tid;
	devstate_unlock(dev);

	mbcmd_set(mb, MBCMD(WDREQ), tid, 0);
	dsp_mbcmd_send_and_wait(&mb, &dev->read_wait_q);

	if (signal_pending(current)) {
		ret = -ERESTARTSYS;
		goto up_out;
	}
	if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED) < 0) {
		ret = -ERESTARTSYS;
		goto up_out;
	}
	if (fifo_empty(&dev->task->rcvdt.fifo))	/* should not occur */
		goto unlock_out;

	ret = copy_to_user_fm_fifo(buf, &dev->task->rcvdt.fifo, count);

unlock_out:
	devstate_unlock(dev);
up_out:
	up(&dev->read_sem);
	return ret;
}

static ssize_t dsp_task_read_bk_psv(struct file *file, char *buf, size_t count,
				    loff_t *ppos)
{
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
	struct taskdev *dev = taskdev[minor];
	struct rcvdt_bk_struct *rcvdt;
	struct mbcmd mb;
	unsigned char tid;
	int ret = 0;

	if (count == 0) {
		return 0;
	} else if (count & 0x1) {
		printk(KERN_ERR
		       "omapdsp: odd count is illegal for DSP task device.\n");
		return -EINVAL;
	} else if ((int)buf & 0x1) {
		printk(KERN_ERR
		       "omapdsp: buf should be word aligned for "
		       "dsp_task_read().\n");
		return -EINVAL;
	}

	if (down_tasksem_interruptible(dev, &dev->read_sem))
		return -ERESTARTSYS;
	if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED) < 0) {
		ret = -ERESTARTSYS;
		goto up_out;
	}
	tid = dev->task->tid;
	devstate_unlock(dev);

	mbcmd_set(mb, MBCMD(BKREQ), tid, count/2);
	dsp_mbcmd_send_and_wait(&mb, &dev->read_wait_q);

	if (signal_pending(current)) {
		ret = -ERESTARTSYS;
		goto up_out;
	}
	if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED) < 0) {
		ret = -ERESTARTSYS;
		goto up_out;
	}
	rcvdt = &dev->task->rcvdt.bk;
	/* signal or 0-byte send from DSP */
	if (ipblink_empty(&rcvdt->link))
		goto unlock_out;

	/*
	 * We will not receive more than requested count.
	 */
	if (sndtyp_pvt(dev->task->ttyp)) {
		/* private */
		struct ipbuf_p *ipbp = rcvdt->ipbuf_pvt_r;
		size_t rcvcnt;
		void *src;

		if (dsp_mem_enable(ipbp) < 0) {
			ret = -ERESTARTSYS;
			goto unlock_out;
		}
		src = MKVIRT(ipbp->ah, ipbp->al);
		rcvcnt = ((unsigned long)ipbp->c) * 2;
		if (dsp_address_validate(src, rcvcnt, "task %s read buffer",
					 dev->task->name) < 0) {
			ret = -ERESTARTSYS;
			goto pv_out1;
		}
		if (dsp_mem_enable(src) < 0) {
			ret = -ERESTARTSYS;
			goto pv_out1;
		}
		if (count > rcvcnt)
			count = rcvcnt;
		if (copy_to_user_dsp(buf, src, count)) {
			ret = -EFAULT;
			goto pv_out2;
		}
		spin_lock(&rcvdt->link.lock);
		ipblink_del_pvt(&rcvdt->link);
		spin_unlock(&rcvdt->link.lock);
		release_ipbuf_pvt(ipbp);
		ret = count;
pv_out2:
		dsp_mem_disable(src);
pv_out1:
		dsp_mem_disable(ipbp);
	} else {
		/* global */
		unsigned short bid = rcvdt->link.top;
		struct ipbuf *ipbp = ipbuf[bid];
		size_t rcvcnt;

		if (dsp_mem_enable_ipbuf() < 0) {
			ret = -ERESTARTSYS;
			goto unlock_out;
		}
		rcvcnt = ((unsigned long)ipbp->c) * 2;
		if (count > rcvcnt)
			count = rcvcnt;
		if (copy_to_user_dsp(buf, ipbp->d, count)) {
			ret = -EFAULT;
			goto gb_out;
		}
		spin_lock(&rcvdt->link.lock);
		ipblink_del_top(&rcvdt->link, ipbuf);
		spin_unlock(&rcvdt->link.lock);
		unuse_ipbuf(bid);
		ret = count;
gb_out:
		dsp_mem_disable_ipbuf();
	}

unlock_out:
	devstate_unlock(dev);
up_out:
	up(&dev->read_sem);
	return ret;
}

static ssize_t dsp_task_write_wd(struct file *file, const char *buf,
				 size_t count, loff_t *ppos)
{
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
	struct taskdev *dev = taskdev[minor];
	unsigned short wd;
	int have_devstate_lock = 0;
	int ret = 0;

	if (count == 0) {
		return 0;
	} else if (count & 0x1) {
		printk(KERN_ERR
		       "omapdsp: odd count is illegal for DSP task device.\n");
		return -EINVAL;
	} else {
		/* force! */
		count = 2;
	}

	if (down_tasksem_interruptible(dev, &dev->write_sem))
		return -ERESTARTSYS;
	if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED) < 0) {
		ret = -ERESTARTSYS;
		goto up_out;
	}
	have_devstate_lock = 1;

	if (dev->task->wsz == 0) {
		long current_state;
		DECLARE_WAITQUEUE(wait, current);

		add_wait_queue(&dev->write_wait_q, &wait);
		current_state = current->state;
		set_current_state(TASK_INTERRUPTIBLE);
		if (dev->task->wsz == 0) {	/* last check */
			devstate_unlock(dev);
			have_devstate_lock = 0;
			schedule();
		}
		set_current_state(current_state);
		remove_wait_queue(&dev->write_wait_q, &wait);
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			goto up_out;
		}
		if (!have_devstate_lock) {
			if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED) < 0) {
				ret = -ERESTARTSYS;
				goto up_out;
			}
			have_devstate_lock = 1;
		}
		if (dev->task->wsz == 0)	/* should not occur */
			goto up_out;
	}

	if (copy_from_user(&wd, buf, count)) {
		ret = -EFAULT;
		goto up_out;
	}

	spin_lock(&dev->task->wsz_lock);
	if (dsp_mbsend(MBCMD(WDSND), dev->task->tid, wd) < 0) {
		spin_unlock(&dev->task->wsz_lock);
		goto up_out;
	}
	ret = count;
	if (rcvtyp_acv(dev->task->ttyp))
		dev->task->wsz = 0;
	spin_unlock(&dev->task->wsz_lock);

up_out:
	if (have_devstate_lock)
		devstate_unlock(dev);
	up(&dev->write_sem);
	return ret;
}

static ssize_t dsp_task_write_bk(struct file *file, const char *buf,
				 size_t count, loff_t *ppos)
{
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
	struct taskdev *dev = taskdev[minor];
	int have_devstate_lock = 0;
	int ret = 0;

	if (count == 0) {
		return 0;
	} else if (count & 0x1) {
		printk(KERN_ERR
		       "omapdsp: odd count is illegal for DSP task device.\n");
		return -EINVAL;
	} else if ((int)buf & 0x1) {
		printk(KERN_ERR
		       "omapdsp: buf should be word aligned for "
		       "dsp_task_write().\n");
		return -EINVAL;
	}

	if (down_tasksem_interruptible(dev, &dev->write_sem))
		return -ERESTARTSYS;
	if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED) < 0) {
		ret = -ERESTARTSYS;
		goto up_out;
	}
	have_devstate_lock = 1;

	if (dev->task->wsz == 0) {
		long current_state;
		DECLARE_WAITQUEUE(wait, current);

		add_wait_queue(&dev->write_wait_q, &wait);
		current_state = current->state;
		set_current_state(TASK_INTERRUPTIBLE);
		if (dev->task->wsz == 0) {	/* last check */
			devstate_unlock(dev);
			have_devstate_lock = 0;
			schedule();
		}
		set_current_state(current_state);
		remove_wait_queue(&dev->write_wait_q, &wait);
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			goto up_out;
		}
		if (!have_devstate_lock) {
			if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED) < 0) {
				ret = -ERESTARTSYS;
				goto up_out;
			}
			have_devstate_lock = 1;
		}
		if (dev->task->wsz == 0)	/* should not occur */
			goto up_out;
	}

	if (count > dev->task->wsz)
		count = dev->task->wsz;

	if (rcvtyp_pvt(dev->task->ttyp)) {
		/* private */
		struct ipbuf_p *ipbp = dev->task->ipbuf_pvt_w;
		unsigned char *dst;

		if (dsp_mem_enable(ipbp) < 0) {
			ret = -ERESTARTSYS;
			goto up_out;
		}
		dst = MKVIRT(ipbp->ah, ipbp->al);
		if (dsp_address_validate(dst, count, "task %s write buffer",
					 dev->task->name) < 0) {
			ret = -ERESTARTSYS;
			goto pv_out1;
		}
		if (dsp_mem_enable(dst) < 0) {
			ret = -ERESTARTSYS;
			goto pv_out1;
		}
		if (copy_from_user_dsp(dst, buf, count)) {
			ret = -EFAULT;
			goto pv_out2;
		}
		ipbp->c = count/2;
		ipbp->s = dev->task->tid;
		spin_lock(&dev->task->wsz_lock);
		if (dsp_mbsend(MBCMD(BKSNDP), dev->task->tid, 0) == 0) {
			if (rcvtyp_acv(dev->task->ttyp))
				dev->task->wsz = 0;
			ret = count;
		}
		spin_unlock(&dev->task->wsz_lock);
pv_out2:
		dsp_mem_disable(dst);
pv_out1:
		dsp_mem_disable(ipbp);
	} else {
		/* global */
		struct ipbuf *ipbp;
		unsigned short bid;

		if (dsp_mem_enable_ipbuf() < 0) {
			ret = -ERESTARTSYS;
			goto up_out;
		}
		bid = get_free_ipbuf(dev->task->tid);
		if (bid == OMAP_DSP_BID_NULL)
			goto gb_out;
		ipbp = ipbuf[bid];
		if (copy_from_user_dsp(ipbp->d, buf, count)) {
			release_ipbuf(bid);
			ret = -EFAULT;
			goto gb_out;
		}
		ipbp->c  = count/2;
		ipbp->sa = dev->task->tid;
		spin_lock(&dev->task->wsz_lock);
		if (dsp_mbsend(MBCMD(BKSND), dev->task->tid, bid) == 0) {
			if (rcvtyp_acv(dev->task->ttyp))
				dev->task->wsz = 0;
			ret = count;
			ipb_bsycnt_inc(&ipbcfg);
		} else
			release_ipbuf(bid);
		spin_unlock(&dev->task->wsz_lock);
gb_out:
		dsp_mem_disable_ipbuf();
	}

up_out:
	if (have_devstate_lock)
		devstate_unlock(dev);
	up(&dev->write_sem);
	return ret;
}

static unsigned int dsp_task_poll(struct file * file, poll_table * wait)
{
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
	struct taskdev *dev = taskdev[minor];
	struct dsptask *task = dev->task;
	unsigned int mask = 0;

	poll_wait(file, &dev->read_wait_q, wait);
	poll_wait(file, &dev->write_wait_q, wait);
	if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED) < 0)
		return 0;
	if (sndtyp_psv(task->ttyp) ||
	    (sndtyp_wd(task->ttyp) && !fifo_empty(&task->rcvdt.fifo)) ||
	    (sndtyp_bk(task->ttyp) && !ipblink_empty(&task->rcvdt.bk.link)))
		mask |= POLLIN | POLLRDNORM;
	if (task->wsz)
		mask |= POLLOUT | POLLWRNORM;
	devstate_unlock(dev);

	return mask;
}

static int dsp_task_ioctl(struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	unsigned int minor = MINOR(inode->i_rdev);
	struct taskdev *dev = taskdev[minor];
	struct mbcmd mb;
	unsigned char tid;
	struct mb_exarg mbarg, *mbargp;
	int mbargc;
	unsigned short mbargv[1];
	int interactive;
	int ret;

	/* LOCK / UNLOCK operations */
	switch (cmd) {
	case OMAP_DSP_TASK_IOCTL_LOCK:
		return taskdev_lock(dev);
	case OMAP_DSP_TASK_IOCTL_UNLOCK:
		return taskdev_unlock(dev);
	}

	/*
	 * actually only interractive commands need to lock
	 * the semaphore, but here all commands do it for simplicity.
	 */
	if (down_tasksem_interruptible(dev, &dev->ioctl_sem))
		return -ERESTARTSYS;
	if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED) < 0) {
		ret = -ERESTARTSYS;
		goto up_out;
	}

	if ((cmd >= 0x0080) && (cmd < 0x0100)) {
		/*
		 * 0x0080 - 0x00ff
		 * reserved for backward compatibility
		 * user-defined TCTL commands: no arg, non-interactive
		 */
		printk(KERN_WARNING "omapdsp: "
		       "TCTL commands in 0x0080 - 0x0100 are obsolete.\n"
		       "they won't be supported in the future.\n");
		mbargc = 0;
		interactive = 0;
	} else if (cmd < 0x8000) {
		/*
		 * 0x0000 - 0x7fff (except 0x0080 - 0x00ff)
		 * system reserved TCTL commands
		 */
		switch (cmd) {
		case OMAP_DSP_MBCMD_TCTL_TEN:
		case OMAP_DSP_MBCMD_TCTL_TDIS:
			mbargc = 0;
			interactive = 0;
			break;
		default:
			ret = -ENOIOCTLCMD;
			goto unlock_out;
		}
	}
	/*
	 * 0x8000 - 0xffff
	 * user-defined TCTL commands
	 */
	else if (cmd < 0x8100) {
		/* 0x8000-0x80ff: no arg, non-interactive */
		mbargc = 0;
		interactive = 0;
	} else if (cmd < 0x8200) {
		/* 0x8100-0x81ff: 1 arg, non-interactive */
		mbargc = 1;
		mbargv[0] = arg & 0xffff;
		interactive = 0;
	} else if (cmd < 0x9000) {
		/* 0x8200-0x8fff: reserved */
		ret = -ENOIOCTLCMD;
		goto unlock_out;
	} else if (cmd < 0x9100) {
		/* 0x9000-0x90ff: no arg, interactive */
		mbargc = 0;
		interactive = 1;
	} else if (cmd < 0x9200) {
		/* 0x9100-0x91ff: 1 arg, interactive */
		mbargc = 1;
		mbargv[0] = arg & 0xffff;
		interactive = 1;
	} else if (cmd < 0x10000) {
		/* 0x9200-0xffff: reserved */
		ret =  -ENOIOCTLCMD;
		goto unlock_out;
	} else {
		/*
		 * 0x10000 -
		 * non TCTL ioctls
		 */
		switch (cmd) {
		case OMAP_DSP_TASK_IOCTL_BFLSH:
			ret = dsp_task_flush_buf(dev->task);
			break;
		case OMAP_DSP_TASK_IOCTL_SETBSZ:
			ret = dsp_task_set_fifosz(dev->task, arg);
			break;
		case OMAP_DSP_TASK_IOCTL_GETNAME:
			ret = 0;
			if (copy_to_user((void *)arg, dev->name,
					 strlen(dev->name) + 1))
				ret = -EFAULT;
			break;
		default:
			ret = -ENOIOCTLCMD;
		}
		goto unlock_out;
	}

	/*
	 * issue TCTL
	 */
	tid = dev->task->tid;
	mbcmd_set(mb, MBCMD(TCTL), tid, cmd);
	if (mbargc > 0) {
		mbarg.argc = mbargc;
		mbarg.tid  = tid;
		mbarg.argv = mbargv;
		mbargp = &mbarg;
	} else
		mbargp = NULL;

	if (interactive) {
		dev->task->tctl_stat = -ERESTARTSYS;
		devstate_unlock(dev);

		dsp_mbcmd_send_and_wait_exarg(&mb, mbargp, &dev->ioctl_wait_q);
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			goto up_out;
		}
		if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED) < 0) {
			ret = -ERESTARTSYS;
			goto up_out;
		}
		ret = dev->task->tctl_stat;
		if (ret < 0) {
			printk(KERN_ERR "omapdsp: TCTL not responding.\n");
			goto unlock_out;
		}
	} else {
		dsp_mbcmd_send_exarg(&mb, mbargp);
		ret = 0;
	}

unlock_out:
	devstate_unlock(dev);
up_out:
	up(&dev->ioctl_sem);
	return ret;
}

static void dsp_task_mmap_open(struct vm_area_struct *vma)
{
	struct taskdev *dev = (struct taskdev *)vma->vm_private_data;
	struct dsptask *task;
	size_t len = vma->vm_end - vma->vm_start;

	BUG_ON(!(dev->state & OMAP_DSP_DEVSTATE_ATTACHED));
	task = dev->task;
	exmap_use(task->map_base, len);
}

static void dsp_task_mmap_close(struct vm_area_struct *vma)
{
	struct taskdev *dev = (struct taskdev *)vma->vm_private_data;
	struct dsptask *task;
	size_t len = vma->vm_end - vma->vm_start;

	BUG_ON(!(dev->state & OMAP_DSP_DEVSTATE_ATTACHED));
	task = dev->task;
	exmap_unuse(task->map_base, len);
}

/**
 * On demand page allocation is not allowed. The mapping area is defined by
 * corresponding DSP tasks.
 */
static struct page *dsp_task_mmap_nopage(struct vm_area_struct *vma,
					 unsigned long address, int *type)
{
	return NOPAGE_SIGBUS;
}

static struct vm_operations_struct dsp_task_vm_ops = {
	.open = dsp_task_mmap_open,
	.close = dsp_task_mmap_close,
	.nopage = dsp_task_mmap_nopage,
};

static int dsp_task_mmap(struct file *filp, struct vm_area_struct *vma)
{
	void *tmp_vadr;
	unsigned long tmp_padr, tmp_vmadr, off;
	size_t req_len, tmp_len;
	unsigned int minor = MINOR(filp->f_dentry->d_inode->i_rdev);
	struct taskdev *dev = taskdev[minor];
	struct dsptask *task;
	int ret = 0;

	if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED) < 0)
		return -ERESTARTSYS;
	task = dev->task;

	/*
	 * Don't swap this area out
	 * Don't dump this area to a core file
	 */
	vma->vm_flags |= VM_RESERVED | VM_IO;

	/* Do not cache this area */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	req_len = vma->vm_end - vma->vm_start;
	off = vma->vm_pgoff << PAGE_SHIFT;
	tmp_vmadr = vma->vm_start;
	tmp_vadr = task->map_base + off;
	do {
		tmp_padr = dsp_virt_to_phys(tmp_vadr, &tmp_len);
		if (tmp_padr == 0) {
			printk(KERN_ERR
			       "omapdsp: task %s: illegal address "
			       "for mmap: %p", task->name, tmp_vadr);
			/* partial mapping will be cleared in upper layer */
			ret = -EINVAL;
			goto unlock_out;
		}
		if (tmp_len > req_len)
			tmp_len = req_len;

		printk(KERN_DEBUG
		       "omapdsp: mmap info: "
		       "vmadr = %08lx, padr = %08lx, len = %x\n",
		       tmp_vmadr, tmp_padr, tmp_len);
		if (remap_pfn_range(vma, tmp_vmadr, tmp_padr >> PAGE_SHIFT,
				    tmp_len, vma->vm_page_prot) != 0) {
			printk(KERN_ERR
			       "omapdsp: task %s: remap_page_range() failed.\n",
			       task->name);
			/* partial mapping will be cleared in upper layer */
			ret = -EINVAL;
			goto unlock_out;
		}

		req_len   -= tmp_len;
		tmp_vmadr += tmp_len;
		tmp_vadr  += tmp_len;
	} while (req_len);

	vma->vm_ops = &dsp_task_vm_ops;
	vma->vm_private_data = dev;
	exmap_use(task->map_base, vma->vm_end - vma->vm_start);

unlock_out:
	devstate_unlock(dev);
	return ret;
}

static int dsp_task_open(struct inode *inode, struct file *file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	struct taskdev *dev;
	int ret = 0;

	if ((minor >= TASKDEV_MAX) || ((dev = taskdev[minor]) == NULL))
		return -ENODEV;
	if (devstate_lock(dev, OMAP_DSP_DEVSTATE_NOTASK |
			       OMAP_DSP_DEVSTATE_ATTACHED) < 0)
		return -ERESTARTSYS;
#ifndef CONFIG_OMAP_DSP_TASK_MULTIOPEN
	if (dev->usecount > 0) {
		ret = -EBUSY;
		goto unlock_out;
	}
#endif

	if (dev->state & OMAP_DSP_DEVSTATE_NOTASK) {
		dev->state = OMAP_DSP_DEVSTATE_ADDREQ;
		/* wake up twch daemon for tadd */
		dsp_twch_touch();
		devstate_unlock(dev);
		if (devstate_lock(dev, OMAP_DSP_DEVSTATE_ATTACHED |
				       OMAP_DSP_DEVSTATE_ADDFAIL) < 0) {
			spin_lock(&dev->state_lock);
			if (dev->state & OMAP_DSP_DEVSTATE_ADDREQ)
				dev->state = OMAP_DSP_DEVSTATE_NOTASK;
			spin_unlock(&dev->state_lock);
			return -ERESTARTSYS;
		}
		if (dev->state & OMAP_DSP_DEVSTATE_ADDFAIL) {
			printk(KERN_ERR "omapdsp: task attach failed for %s!\n",
			       dev->name);
			ret = -EBUSY;
			dev->state = OMAP_DSP_DEVSTATE_NOTASK;
			wake_up_interruptible_all(&dev->state_wait_q);
			goto unlock_out;
		}
	}

	/* state_lock covers usecount, proc_list as well. */
	dev->usecount++;
	proc_list_add(&dev->proc_list, current);
	file->f_op = &dev->fops;
	devstate_unlock(dev);

	return 0;

unlock_out:
	devstate_unlock(dev);
	return ret;
}

static int dsp_task_release(struct inode *inode, struct file *file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	struct taskdev *dev = taskdev[minor];

	/* state_lock covers usecount, proc_list as well. */
	spin_lock(&dev->state_lock);

	/* state can be ATTACHED, KILLING or GARBAGE here. */
	switch (dev->state & OMAP_DSP_DEVSTATE_STATE_MASK) {

	case OMAP_DSP_DEVSTATE_KILLING:
		dev->usecount--;
		break;

	case OMAP_DSP_DEVSTATE_GARBAGE:
		if(--dev->usecount == 0) {
			dev->state = OMAP_DSP_DEVSTATE_NOTASK;
			wake_up_interruptible_all(&dev->state_wait_q);
		}
		break;

	case OMAP_DSP_DEVSTATE_ATTACHED:
		if (dev->lock_pid == current->pid)
			taskdev_unlock(dev);
		proc_list_del(&dev->proc_list, current);
		if (--dev->usecount == 0) {
			if (minor >= n_task) {	/* dynamic task */
				dev->state = OMAP_DSP_DEVSTATE_DELREQ;
				/* wake up twch daemon for tdel */
				dsp_twch_touch();
			}
		}
		break;

	}

	spin_unlock(&dev->state_lock);
	return 0;
}

/*
 * mkdev / rmdev
 */
int dsp_mkdev(char *name)
{
	struct taskdev *dev;
	int status;
	unsigned char minor;

	if (!dsp_is_ready()) {
		printk(KERN_ERR "omapdsp: dsp has not been configured.\n");
		return -EINVAL;
	}
	for (minor = n_task; minor < TASKDEV_MAX; minor++) {
		if (taskdev[minor] == NULL)
			goto do_make;
	}
	printk(KERN_ERR "omapdsp: Too many task devices.\n");
	return -EBUSY;

do_make:
	if ((dev = kmalloc(sizeof(struct taskdev), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	memset(dev, 0, sizeof(struct taskdev));
	if ((status = taskdev_init(dev, name, minor)) < 0) {
		kfree(dev);
		return status;
	}
	return minor;
}

int dsp_rmdev(char *name)
{
	unsigned char minor;
	int ret;

	if (!dsp_is_ready()) {
		printk(KERN_ERR "omapdsp: dsp has not been configured.\n");
		return -EINVAL;
	}
	for (minor = n_task; minor < TASKDEV_MAX; minor++) {
		if (taskdev[minor] && !strcmp(taskdev[minor]->name, name)) {
			if ((ret = dsp_rmdev_minor(minor)) < 0)
				return ret;
			return minor;
		}
	}
	return -EINVAL;
}

static int dsp_rmdev_minor(unsigned char minor)
{
	struct taskdev *dev = taskdev[minor];

	spin_lock(&dev->state_lock);

	switch (dev->state & OMAP_DSP_DEVSTATE_STATE_MASK) {

	case OMAP_DSP_DEVSTATE_NOTASK:
		/* fine */
		break;

	case OMAP_DSP_DEVSTATE_ATTACHED:
		/* task is working. kill it. */
		{
			siginfo_t info;
			struct proc_list *pl;

			dev->state = OMAP_DSP_DEVSTATE_KILLING;
			info.si_signo = SIGBUS;
			info.si_errno = 0;
			info.si_code = SI_KERNEL;
			info._sifields._sigfault._addr = NULL;
			list_for_each_entry(pl, &dev->proc_list, list_head) {
				send_sig_info(SIGBUS, &info, pl->tsk);
			}
			spin_unlock(&dev->state_lock);
			dsp_tdel_bh(minor, OMAP_DSP_MBCMD_TDEL_KILL);
			goto invalidate;
		}

	case OMAP_DSP_DEVSTATE_ADDREQ:
		/* open() is waiting. drain it. */
		dev->state = OMAP_DSP_DEVSTATE_ADDFAIL;
		wake_up_interruptible_all(&dev->state_wait_q);
		break;

	case OMAP_DSP_DEVSTATE_DELREQ:
		/* nobody is waiting. */
		dev->state = OMAP_DSP_DEVSTATE_NOTASK;
		wake_up_interruptible_all(&dev->state_wait_q);
		break;

	case OMAP_DSP_DEVSTATE_ADDING:
	case OMAP_DSP_DEVSTATE_DELING:
	case OMAP_DSP_DEVSTATE_KILLING:
	case OMAP_DSP_DEVSTATE_GARBAGE:
	case OMAP_DSP_DEVSTATE_ADDFAIL:
		/* transient state. wait for a moment. */
		break;

	}

	spin_unlock(&dev->state_lock);

invalidate:
	/* wait for some time and hope the state is settled */
	devstate_lock_timeout(dev, OMAP_DSP_DEVSTATE_NOTASK, HZ);
	if (!(dev->state & OMAP_DSP_DEVSTATE_NOTASK)) {
		printk(KERN_WARNING
		       "omapdsp: illegal device state (%s) on rmdev %s.\n",
		       devstate_name(dev->state), dev->name);
	}
	dev->state = OMAP_DSP_DEVSTATE_INVALID;
	devstate_unlock(dev);

	taskdev_delete(minor);
	kfree(dev);

	return 0;
}

struct file_operations dsp_task_fops = {
	.owner   = THIS_MODULE,
	.poll    = dsp_task_poll,
	.ioctl   = dsp_task_ioctl,
	.open    = dsp_task_open,
	.release = dsp_task_release,
};

static void dsptask_dev_release(struct device *dev)
{
}

static int taskdev_init(struct taskdev *dev, char *name, unsigned char minor)
{
	struct class_device *cdev;

	taskdev[minor] = dev;

	INIT_LIST_HEAD(&dev->proc_list);
	init_waitqueue_head(&dev->read_wait_q);
	init_waitqueue_head(&dev->write_wait_q);
	init_waitqueue_head(&dev->ioctl_wait_q);
	init_MUTEX(&dev->read_sem);
	init_MUTEX(&dev->write_sem);
	init_MUTEX(&dev->ioctl_sem);
	init_MUTEX(&dev->lock_sem);
	dev->lock_pid = 0;

	strncpy(dev->name, name, OMAP_DSP_TNM_LEN);
	dev->name[OMAP_DSP_TNM_LEN-1] = '\0';
	dev->state = (minor < n_task) ? OMAP_DSP_DEVSTATE_ATTACHED :
					OMAP_DSP_DEVSTATE_NOTASK;
	dev->usecount = 0;
	memcpy(&dev->fops, &dsp_task_fops, sizeof(struct file_operations));

	dev->dev.parent = &dsp_device.dev;
	dev->dev.bus = &dsptask_bus;
	sprintf(dev->dev.bus_id, "dsptask%d", minor);
	dev->dev.release = dsptask_dev_release;
	device_register(&dev->dev);
	device_create_file(&dev->dev, &dev_attr_devname);
	device_create_file(&dev->dev, &dev_attr_devstate);
	device_create_file(&dev->dev, &dev_attr_proc_list);
	cdev = class_device_create(dsp_task_class, NULL,
				   MKDEV(OMAP_DSP_TASK_MAJOR, minor),
				   NULL, "dsptask%d", minor);

	init_waitqueue_head(&dev->state_wait_q);
	spin_lock_init(&dev->state_lock);

	return 0;
}

static void taskdev_delete(unsigned char minor)
{
	struct taskdev *dev = taskdev[minor];

	if (!dev)
		return;
	device_remove_file(&dev->dev, &dev_attr_devname);
	device_remove_file(&dev->dev, &dev_attr_devstate);
	device_remove_file(&dev->dev, &dev_attr_proc_list);
	class_device_destroy(dsp_task_class, MKDEV(OMAP_DSP_TASK_MAJOR, minor));
	device_unregister(&dev->dev);
	proc_list_flush(&dev->proc_list);
	taskdev[minor] = NULL;
}

static void taskdev_attach_task(struct taskdev *dev, struct dsptask *task)
{
	unsigned short ttyp = task->ttyp;

	dev->task = task;
	task->dev = dev;
	dev->fops.read =
		sndtyp_acv(ttyp) ?
			sndtyp_wd(ttyp) ? dsp_task_read_wd_acv:
			/* sndtyp_bk */   dsp_task_read_bk_acv:
		/* sndtyp_psv */
			sndtyp_wd(ttyp) ? dsp_task_read_wd_psv:
			/* sndtyp_bk */   dsp_task_read_bk_psv;
	dev->fops.write =
		rcvtyp_wd(ttyp) ? dsp_task_write_wd:
		/* rcvbyp_bk */	  dsp_task_write_bk;
	if (task->map_length)
		dev->fops.mmap = dsp_task_mmap;

	device_create_file(&dev->dev, &dev_attr_taskname);
	device_create_file(&dev->dev, &dev_attr_ttyp);
	if (sndtyp_wd(ttyp)) {
		device_create_file(&dev->dev, &dev_attr_fifosz);
		device_create_file(&dev->dev, &dev_attr_fifocnt);
	} else
		device_create_file(&dev->dev, &dev_attr_ipblink);
	device_create_file(&dev->dev, &dev_attr_wsz);
	if (task->map_length)
		device_create_file(&dev->dev, &dev_attr_mmap);
}

static void taskdev_detach_task(struct taskdev *dev)
{
	unsigned short ttyp = dev->task->ttyp;

	device_remove_file(&dev->dev, &dev_attr_taskname);
	device_remove_file(&dev->dev, &dev_attr_ttyp);
	if (sndtyp_wd(ttyp)) {
		device_remove_file(&dev->dev, &dev_attr_fifosz);
		device_remove_file(&dev->dev, &dev_attr_fifocnt);
	} else
		device_remove_file(&dev->dev, &dev_attr_ipblink);
	device_remove_file(&dev->dev, &dev_attr_wsz);
	if (dev->task->map_length)
		device_remove_file(&dev->dev, &dev_attr_mmap);

	if (dev->task) {
		dev->task = NULL;
		dev->fops.read = NULL;
		dev->fops.write = NULL;
		printk(KERN_INFO "omapdsp: taskdev %s disabled.\n", dev->name);
	}
}

/*
 * tadd / tdel / tkill
 */
int dsp_tadd(unsigned char minor, unsigned long adr)
{
	struct taskdev *dev;
	struct dsptask *task;
	struct mbcmd mb;
	struct mb_exarg arg;
	unsigned char tid, tid_response;
	unsigned short argv[2];
	int ret = minor;

	if ((minor >= TASKDEV_MAX) || ((dev = taskdev[minor]) == NULL)) {
		printk(KERN_ERR
		       "omapdsp: no task device with minor %d\n", minor);
		return -EINVAL;
	}

	spin_lock(&dev->state_lock);
	if (!(dev->state & OMAP_DSP_DEVSTATE_ADDREQ)) {
		printk(KERN_ERR
		       "omapdsp: taskdev %s is not requesting for tadd. "
		       "(state is %s)\n", dev->name, devstate_name(dev->state));
		spin_unlock(&dev->state_lock);
		return -EINVAL;
	}
	dev->state = OMAP_DSP_DEVSTATE_ADDING;
	spin_unlock(&dev->state_lock);

	if (adr == OMAP_DSP_TADD_ABORTADR) {
		/* aborting tadd intentionally */
		printk(KERN_INFO "omapdsp: tadd address is ABORTADR.\n");
		goto fail_out;
	}
	if (adr >= DSPSPACE_SIZE) {
		printk(KERN_ERR
		       "omapdsp: illegal address 0x%08lx for tadd\n", adr);
		ret = -EINVAL;
		goto fail_out;
	}

	adr >>= 1;	/* word address */
	argv[0] = adr >> 16;	/* addrh */
	argv[1] = adr & 0xffff;	/* addrl */

	if (down_interruptible(&cfg_sem)) {
		ret = -ERESTARTSYS;
		goto fail_out;
	}
	cfg_tid = OMAP_DSP_TID_ANON;
	cfg_cmd = MBCMD(TADD);
	mbcmd_set(mb, MBCMD(TADD), 0, 0);
	arg.tid  = OMAP_DSP_TID_ANON;
	arg.argc = 2;
	arg.argv = argv;

	dsp_mem_sync_inc();
	dsp_mbcmd_send_and_wait_exarg(&mb, &arg, &cfg_wait_q);

	tid = cfg_tid;
	cfg_tid = OMAP_DSP_TID_ANON;
	cfg_cmd = 0;
	up(&cfg_sem);

	if (tid == OMAP_DSP_TID_ANON) {
		printk(KERN_ERR "omapdsp: tadd failed!\n");
		ret = -EINVAL;
		goto fail_out;
	}
	if ((tid < n_task) || dsptask[tid]) {
		printk(KERN_ERR "omapdsp: illegal tid (%d)!\n", tid);
		ret = -EINVAL;
		goto fail_out;
	}
	if ((task = kmalloc(sizeof(struct dsptask), GFP_KERNEL)) == NULL) {
		ret = -ENOMEM;
		goto del_out;
	}
	memset(task, 0, sizeof(struct dsptask));

	if ((ret = dsp_task_config(task, tid)) < 0)
		goto free_out;
	taskdev_attach_task(dev, task);

	if (strcmp(dev->name, task->name)) {
		printk(KERN_ERR
		       "omapdsp: task name (%s) doesn't match with "
		       "device name (%s).\n", task->name, dev->name);
		ret = -EINVAL;
		goto free_out;
	}

	dsp_task_init(task);
	printk(KERN_INFO "omapdsp: taskdev %s enabled.\n", dev->name);
	dev->state = OMAP_DSP_DEVSTATE_ATTACHED;
	wake_up_interruptible_all(&dev->state_wait_q);
	return minor;

free_out:
	kfree(task);

del_out:
	printk(KERN_ERR "omapdsp: deleting the task...\n");

	dev->state = OMAP_DSP_DEVSTATE_DELING;

	if (down_interruptible(&cfg_sem)) {
		printk(KERN_ERR "omapdsp: aborting tdel process. "
				"DSP side could be corrupted.\n");
		goto fail_out;
	}
	cfg_tid = OMAP_DSP_TID_ANON;
	cfg_cmd = MBCMD(TDEL);
	mbcmd_set(mb, MBCMD(TDEL), tid, OMAP_DSP_MBCMD_TDEL_KILL);
	dsp_mbcmd_send_and_wait(&mb, &cfg_wait_q);
	tid_response = cfg_tid;
	cfg_tid = OMAP_DSP_TID_ANON;
	cfg_cmd = 0;
	up(&cfg_sem);

	if (tid_response != tid)
		printk(KERN_ERR "omapdsp: tdel failed. "
				"DSP side could be corrupted.\n");

fail_out:
	dev->state = OMAP_DSP_DEVSTATE_ADDFAIL;
	wake_up_interruptible_all(&dev->state_wait_q);
	return ret;
}

int dsp_tdel(unsigned char minor)
{
	struct taskdev *dev;

	if ((minor >= TASKDEV_MAX) || ((dev = taskdev[minor]) == NULL)) {
		printk(KERN_ERR
		       "omapdsp: no task device with minor %d\n", minor);
		return -EINVAL;
	}
	spin_lock(&dev->state_lock);
	if (!(dev->state & OMAP_DSP_DEVSTATE_DELREQ)) {
		printk(KERN_ERR
		       "omapdsp: taskdev %s is not requesting for tdel. "
		       "(state is %s)\n", dev->name, devstate_name(dev->state));
		spin_unlock(&dev->state_lock);
		return -EINVAL;
	}
	dev->state = OMAP_DSP_DEVSTATE_DELING;
	spin_unlock(&dev->state_lock);

	return dsp_tdel_bh(minor, OMAP_DSP_MBCMD_TDEL_SAFE);
}

int dsp_tkill(unsigned char minor)
{
	struct taskdev *dev;
	siginfo_t info;
	struct proc_list *pl;

	if ((minor >= TASKDEV_MAX) || ((dev = taskdev[minor]) == NULL)) {
		printk(KERN_ERR
		       "omapdsp: no task device with minor %d\n", minor);
		return -EINVAL;
	}
	spin_lock(&dev->state_lock);
	if (!(dev->state & OMAP_DSP_DEVSTATE_ATTACHED)) {
		printk(KERN_ERR
		       "omapdsp: task has not been attached for taskdev %s\n",
		       dev->name);
		spin_unlock(&dev->state_lock);
		return -EINVAL;
	}
	dev->state = OMAP_DSP_DEVSTATE_KILLING;
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = SI_KERNEL;
	info._sifields._sigfault._addr = NULL;
	list_for_each_entry(pl, &dev->proc_list, list_head) {
		send_sig_info(SIGBUS, &info, pl->tsk);
	}
	spin_unlock(&dev->state_lock);

	return dsp_tdel_bh(minor, OMAP_DSP_MBCMD_TDEL_KILL);
}

static int dsp_tdel_bh(unsigned char minor, unsigned short type)
{
	struct taskdev *dev = taskdev[minor];
	struct dsptask *task;
	struct mbcmd mb;
	unsigned char tid, tid_response;
	int ret = minor;

	task = dev->task;
	tid = task->tid;
	if (down_interruptible(&cfg_sem)) {
		if (type == OMAP_DSP_MBCMD_TDEL_SAFE) {
			dev->state = OMAP_DSP_DEVSTATE_DELREQ;
			return -ERESTARTSYS;
		} else {
			tid_response = OMAP_DSP_TID_ANON;
			ret = -ERESTARTSYS;
			goto detach_out;
		}
	}
	cfg_tid = OMAP_DSP_TID_ANON;
	cfg_cmd = MBCMD(TDEL);
	mbcmd_set(mb, MBCMD(TDEL), tid, type);
	dsp_mbcmd_send_and_wait(&mb, &cfg_wait_q);
	tid_response = cfg_tid;
	cfg_tid = OMAP_DSP_TID_ANON;
	cfg_cmd = 0;
	up(&cfg_sem);

detach_out:
	taskdev_detach_task(dev);
	dsp_task_unconfig(task);
	kfree(task);

	if (tid_response != tid) {
		printk(KERN_ERR "omapdsp: %s failed!\n",
		       (type == OMAP_DSP_MBCMD_TDEL_SAFE) ? "tdel" : "tkill");
		ret = -EINVAL;
	}
	spin_lock(&dev->state_lock);
	dev->state = (dev->usecount > 0) ? OMAP_DSP_DEVSTATE_GARBAGE :
					   OMAP_DSP_DEVSTATE_NOTASK;
	wake_up_interruptible_all(&dev->state_wait_q);
	spin_unlock(&dev->state_lock);

	return ret;
}

/*
 * state inquiry
 */
long taskdev_state_stale(unsigned char minor)
{
	if (taskdev[minor]) {
		long state = taskdev[minor]->state;
		taskdev[minor]->state |= OMAP_DSP_DEVSTATE_STALE;
		return state;
	} else
		return OMAP_DSP_DEVSTATE_NOTASK;
}

/*
 * functions called from mailbox1 interrupt routine
 */
void mbx1_wdsnd(struct mbcmd *mb)
{
	unsigned char tid = mb->cmd_l;
	struct dsptask *task = dsptask[tid];

	if ((tid >= TASKDEV_MAX) || (task == NULL)) {
		printk(KERN_ERR "mbx: WDSND with illegal tid! %d\n", tid);
		return;
	}
	if (sndtyp_bk(task->ttyp)) {
		printk(KERN_ERR
		       "mbx: WDSND from block sending task! (task%d)\n", tid);
		return;
	}
	if (sndtyp_psv(task->ttyp) &&
	    !waitqueue_active(&task->dev->read_wait_q)) {
		printk(KERN_WARNING
		       "mbx: WDSND from passive sending task (task%d) "
		       "without request!\n", tid);
		return;
	}

	write_word_to_fifo(&task->rcvdt.fifo, mb->data);
	wake_up_interruptible(&task->dev->read_wait_q);
}

void mbx1_wdreq(struct mbcmd *mb)
{
	unsigned char tid = mb->cmd_l;
	struct dsptask *task = dsptask[tid];

	if ((tid >= TASKDEV_MAX) || (task == NULL)) {
		printk(KERN_ERR "mbx: WDREQ with illegal tid! %d\n", tid);
		return;
	}
	if (rcvtyp_psv(task->ttyp)) {
		printk(KERN_ERR
		       "mbx: WDREQ from passive receiving task! (task%d)\n",
		       tid);
		return;
	}

	spin_lock(&task->wsz_lock);
	task->wsz = 2;
	spin_unlock(&task->wsz_lock);
	wake_up_interruptible(&task->dev->write_wait_q);
}

void mbx1_bksnd(struct mbcmd *mb)
{
	unsigned char tid = mb->cmd_l;
	unsigned short bid = mb->data;
	struct dsptask *task = dsptask[tid];
	unsigned short cnt;

	if (bid >= ipbcfg.ln) {
		printk(KERN_ERR "mbx: BKSND with illegal bid! %d\n", bid);
		return;
	}
	ipb_bsycnt_dec(&ipbcfg);
	if ((tid >= TASKDEV_MAX) || (task == NULL)) {
		printk(KERN_ERR "mbx: BKSND with illegal tid! %d\n", tid);
		goto unuse_ipbuf_out;
	}
	if (sndtyp_wd(task->ttyp)) {
		printk(KERN_ERR
		       "mbx: BKSND from word sending task! (task%d)\n", tid);
		goto unuse_ipbuf_out;
	}
	if (sndtyp_pvt(task->ttyp)) {
		printk(KERN_ERR
		       "mbx: BKSND from private sending task! (task%d)\n", tid);
		goto unuse_ipbuf_out;
	}
	if (sync_with_dsp(&ipbuf[bid]->sd, tid, 10) < 0) {
		printk(KERN_ERR "mbx: BKSND - IPBUF sync failed!\n");
		return;
	}

	/* should be done in DSP, but just in case. */
	ipbuf[bid]->next = OMAP_DSP_BID_NULL;

	cnt = ipbuf[bid]->c;
	if (cnt > ipbcfg.lsz) {
		printk(KERN_ERR "mbx: BKSND cnt(%d) > ipbuf line size(%d)!\n",
		       cnt, ipbcfg.lsz);
		goto unuse_ipbuf_out;
	}

	if (cnt == 0) {
		/* 0-byte send from DSP */
		unuse_ipbuf_nowait(bid);
		goto done;
	}
	spin_lock(&task->rcvdt.bk.link.lock);
	ipblink_add_tail(&task->rcvdt.bk.link, bid, ipbuf);
	spin_unlock(&task->rcvdt.bk.link.lock);
	/* we keep coming bid and return alternative line to DSP. */
	balance_ipbuf();

done:
	wake_up_interruptible(&task->dev->read_wait_q);
	return;

unuse_ipbuf_out:
	unuse_ipbuf_nowait(bid);
	return;
}

void mbx1_bkreq(struct mbcmd *mb)
{
	unsigned char tid = mb->cmd_l;
	unsigned short cnt = mb->data;
	struct dsptask *task = dsptask[tid];

	if ((tid >= TASKDEV_MAX) || (task == NULL)) {
		printk(KERN_ERR "mbx: BKREQ with illegal tid! %d\n", tid);
		return;
	}
	if (rcvtyp_wd(task->ttyp)) {
		printk(KERN_ERR
		       "mbx: BKREQ from word receiving task! (task%d)\n", tid);
		return;
	}
	if (rcvtyp_pvt(task->ttyp)) {
		printk(KERN_ERR
		       "mbx: BKREQ from private receiving task! (task%d)\n",
		       tid);
		return;
	}
	if (rcvtyp_psv(task->ttyp)) {
		printk(KERN_ERR
		       "mbx: BKREQ from passive receiving task! (task%d)\n",
		       tid);
		return;
	}

	spin_lock(&task->wsz_lock);
	task->wsz = cnt*2;
	spin_unlock(&task->wsz_lock);
	wake_up_interruptible(&task->dev->write_wait_q);
}

void mbx1_bkyld(struct mbcmd *mb)
{
	unsigned short bid = mb->data;

	if (bid >= ipbcfg.ln) {
		printk(KERN_ERR "mbx: BKYLD with illegal bid! %d\n", bid);
		return;
	}

	/* should be done in DSP, but just in case. */
	ipbuf[bid]->next = OMAP_DSP_BID_NULL;

	/* we don't need to sync with DSP */
	ipb_bsycnt_dec(&ipbcfg);
	release_ipbuf(bid);
}

void mbx1_bksndp(struct mbcmd *mb)
{
	unsigned char tid = mb->cmd_l;
	struct dsptask *task = dsptask[tid];
	struct rcvdt_bk_struct *rcvdt = &task->rcvdt.bk;
	struct ipbuf_p *ipbp = rcvdt->ipbuf_pvt_r;

	if ((tid >= TASKDEV_MAX) || (task == NULL)) {
		printk(KERN_ERR "mbx: BKSNDP with illegal tid! %d\n", tid);
		return;
	}
	if (sndtyp_wd(task->ttyp)) {
		printk(KERN_ERR
		       "mbx: BKSNDP from word sending task! (task%d)\n", tid);
		return;
	}
	if (sndtyp_gbl(task->ttyp)) {
		printk(KERN_ERR
		       "mbx: BKSNDP from non-private sending task! (task%d)\n",
		       tid);
		return;
	}

	/*
	 * we should not have delayed block at this point
	 * because read() routine releases the lock of the buffer and
	 * until then DSP can't send next data.
	 */

	if (sync_with_dsp(&ipbp->s, tid, 10) < 0) {
		printk(KERN_ERR "mbx: BKSNDP - IPBUF sync failed!\n");
		return;
	}
	printk(KERN_DEBUG "mbx: ipbuf_pvt_r->a = 0x%08lx\n",
	       MKLONG(ipbp->ah, ipbp->al));
	spin_lock(&rcvdt->link.lock);
	ipblink_add_pvt(&rcvdt->link);
	spin_unlock(&rcvdt->link.lock);
	wake_up_interruptible(&task->dev->read_wait_q);
}

void mbx1_bkreqp(struct mbcmd *mb)
{
	unsigned char tid = mb->cmd_l;
	struct dsptask *task = dsptask[tid];
	struct ipbuf_p *ipbp = task->ipbuf_pvt_w;

	if ((tid >= TASKDEV_MAX) || (task == NULL)) {
		printk(KERN_ERR "mbx: BKREQP with illegal tid! %d\n", tid);
		return;
	}
	if (rcvtyp_wd(task->ttyp)) {
		printk(KERN_ERR
		       "mbx: BKREQP from word receiving task! (task%d)\n", tid);
		return;
	}
	if (rcvtyp_gbl(task->ttyp)) {
		printk(KERN_ERR
		       "mbx: BKREQP from non-private receiving task! (task%d)\n", tid);
		return;
	}
	if (rcvtyp_psv(task->ttyp)) {
		printk(KERN_ERR
		       "mbx: BKREQP from passive receiving task! (task%d)\n", tid);
		return;
	}

	if (sync_with_dsp(&ipbp->s, OMAP_DSP_TID_FREE, 10) < 0) {
		printk(KERN_ERR "mbx: BKREQP - IPBUF sync failed!\n");
		return;
	}
	printk(KERN_DEBUG "mbx: ipbuf_pvt_w->a = 0x%08lx\n",
	       MKLONG(ipbp->ah, ipbp->al));
	spin_lock(&task->wsz_lock);
	task->wsz = ipbp->c*2;
	spin_unlock(&task->wsz_lock);
	wake_up_interruptible(&task->dev->write_wait_q);
}

void mbx1_tctl(struct mbcmd *mb)
{
	unsigned char tid = mb->cmd_l;
	struct dsptask *task = dsptask[tid];

	if ((tid >= TASKDEV_MAX) || (task == NULL)) {
		printk(KERN_ERR "mbx: TCTL with illegal tid! %d\n", tid);
		return;
	}

	if (!waitqueue_active(&task->dev->ioctl_wait_q)) {
		printk(KERN_WARNING "mbx: unexpected TCTL from DSP!\n");
		return;
	}

	task->tctl_stat = mb->data;
	wake_up_interruptible(&task->dev->ioctl_wait_q);
}

void mbx1_tcfg(struct mbcmd *mb)
{
	unsigned char tid = mb->cmd_l;
	struct dsptask *task = dsptask[tid];
	unsigned short *tnm;
	volatile unsigned short *buf;
	int i;

	if ((tid >= TASKDEV_MAX) || (task == NULL)) {
		printk(KERN_ERR "mbx: TCFG with illegal tid! %d\n", tid);
		return;
	}
	if ((task->state != TASK_STATE_CFGREQ) || (cfg_cmd != MBCMD(TCFG))) {
		printk(KERN_WARNING "mbx: unexpected TCFG from DSP!\n");
		return;
	}

	if (sync_with_dsp(&ipbuf_sys_da->s, tid, 10) < 0) {
		printk(KERN_ERR "mbx: TCFG - IPBUF sync failed!\n");
		goto out;
	}

	/*
	 * read configuration data on system IPBUF
	 */
	buf = ipbuf_sys_da->d;
	task->ttyp                 = buf[0];
	task->rcvdt.bk.ipbuf_pvt_r = MKVIRT(buf[1], buf[2]);
	task->ipbuf_pvt_w          = MKVIRT(buf[3], buf[4]);
	task->map_base             = MKVIRT(buf[5], buf[6]);
	task->map_length           = MKLONG(buf[7], buf[8]) << 1;	/* word -> byte */
	tnm                        = MKVIRT(buf[9], buf[10]);
	release_ipbuf_pvt(ipbuf_sys_da);

	/*
	 * copy task name string
	 */
	if (dsp_address_validate(tnm, OMAP_DSP_TNM_LEN, "task name buffer") <0) {
		task->name[0] = '\0';
		goto out;
	}

	for (i = 0; i < OMAP_DSP_TNM_LEN-1; i++) {
		/* avoiding byte access */
		unsigned short tmp = tnm[i];
		task->name[i] = tmp & 0x00ff;
		if (!tmp)
			break;
	}
	task->name[OMAP_DSP_TNM_LEN-1] = '\0';

	task->state = TASK_STATE_READY;
out:
	wake_up_interruptible(&cfg_wait_q);
}

void mbx1_tadd(struct mbcmd *mb)
{
	unsigned char tid = mb->cmd_l;

	if ((!waitqueue_active(&cfg_wait_q)) || (cfg_cmd != MBCMD(TADD))) {
		printk(KERN_WARNING "mbx: unexpected TADD from DSP!\n");
		return;
	}
	cfg_tid = tid;
	wake_up_interruptible(&cfg_wait_q);
}

void mbx1_tdel(struct mbcmd *mb)
{
	unsigned char tid = mb->cmd_l;

	if ((!waitqueue_active(&cfg_wait_q)) || (cfg_cmd != MBCMD(TDEL))) {
		printk(KERN_WARNING "mbx: unexpected TDEL from DSP!\n");
		return;
	}
	cfg_tid = tid;
	wake_up_interruptible(&cfg_wait_q);
}

void mbx1_err_fatal(unsigned char tid)
{
	struct dsptask *task = dsptask[tid];
	struct proc_list *pl;
	siginfo_t info;

	if ((tid >= TASKDEV_MAX) || (task == NULL)) {
		printk(KERN_ERR "mbx: FATAL ERR with illegal tid! %d\n", tid);
		return;
	}

	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = SI_KERNEL;
	info._sifields._sigfault._addr = NULL;
	spin_lock(&task->dev->state_lock);
	list_for_each_entry(pl, &task->dev->proc_list, list_head) {
		send_sig_info(SIGBUS, &info, pl->tsk);
	}
	spin_unlock(&task->dev->state_lock);
}

static short *dbg_buf;
static unsigned short dbg_buf_sz, dbg_line_sz;
static int dbg_rp;

int dsp_dbg_config(short *buf, unsigned short sz, unsigned short lsz)
{
#ifdef OLD_BINARY_SUPPORT
	if ((mbx_revision == MBREV_3_0) || (mbx_revision == MBREV_3_2)) {
		dbg_buf = NULL;
		dbg_buf_sz = 0;
		dbg_line_sz = 0;
		dbg_rp = 0;
		return 0;
	}
#endif

	if (dsp_address_validate(buf, sz, "debug buffer") < 0)
		return -1;

	if (lsz > sz) {
		printk(KERN_ERR
		       "omapdsp: dbg_buf lsz (%d) is greater than its "
		       "buffer size (%d)\n", lsz, sz);
		return -1;
	}

	dbg_buf = buf;
	dbg_buf_sz = sz;
	dbg_line_sz = lsz;
	dbg_rp = 0;

	return 0;
}

void dsp_dbg_stop(void)
{
	dbg_buf = NULL;
}

#ifdef OLD_BINARY_SUPPORT
static void mbx1_dbg_old(struct mbcmd *mb);
#endif

void mbx1_dbg(struct mbcmd *mb)
{
	unsigned char tid = mb->cmd_l;
	int cnt = mb->data;
	char s[80], *s_end = &s[79], *p;
	unsigned short *src;
	int i;

#ifdef OLD_BINARY_SUPPORT
	if ((mbx_revision == MBREV_3_0) || (mbx_revision == MBREV_3_2)) {
		mbx1_dbg_old(mb);
		return;
	}
#endif

	if (((tid >= TASKDEV_MAX) || (dsptask[tid] == NULL)) &&
	    (tid != OMAP_DSP_TID_ANON)) {
		printk(KERN_ERR "mbx: DBG with illegal tid! %d\n", tid);
		return;
	}
	if (dbg_buf == NULL) {
		printk(KERN_ERR "mbx: DBG command received, but "
		       "dbg_buf has not been configured yet.\n");
		return;
	}

	if (dsp_mem_enable(dbg_buf) < 0)
		return;

	src = &dbg_buf[dbg_rp];
	p = s;
	for (i = 0; i < cnt; i++) {
		unsigned short tmp;
		/*
		 * Be carefull that dbg_buf should not be read with
		 * 1-byte access since it might be placed in DARAM/SARAM
		 * and it can cause unexpected byteswap.
		 * For example,
		 *   *(p++) = *(src++) & 0xff;
		 * causes 1-byte access!
		 */
		tmp = *src++;
		*(p++) = tmp & 0xff;
		if (*(p-1) == '\n') {
			*p = '\0';
			printk(KERN_INFO "%s", s);
			p = s;
			continue;
		}
		if (p == s_end) {
			*p = '\0';
			printk(KERN_INFO "%s\n", s);
			p = s;
			continue;
		}
	}
	if (p > s) {
		*p = '\0';
		printk(KERN_INFO "%s\n", s);
	}
	if ((dbg_rp += cnt + 1) > dbg_buf_sz - dbg_line_sz)
		dbg_rp = 0;

	dsp_mem_disable(dbg_buf);
}

#ifdef OLD_BINARY_SUPPORT
static void mbx1_dbg_old(struct mbcmd *mb)
{
	unsigned char tid = mb->cmd_l;
	char s[80], *s_end = &s[79], *p;
	unsigned short *src;
	volatile unsigned short *buf;
	int cnt;
	int i;

	if (((tid >= TASKDEV_MAX) || (dsptask[tid] == NULL)) &&
	    (tid != OMAP_DSP_TID_ANON)) {
		printk(KERN_ERR "mbx: DBG with illegal tid! %d\n", tid);
		return;
	}
	if (sync_with_dsp(&ipbuf_sys_da->s, tid, 10) < 0) {
		printk(KERN_ERR "mbx: DBG - IPBUF sync failed!\n");
		return;
	}
	buf = ipbuf_sys_da->d;
	cnt = buf[0];
	src = MKVIRT(buf[1], buf[2]);
	if (dsp_address_validate(src, cnt, "dbg buffer") < 0)
		return;

	if (dsp_mem_enable(src) < 0)
		return;

	p = s;
	for (i = 0; i < cnt; i++) {
		unsigned short tmp;
		/*
		 * Be carefull that ipbuf should not be read with
		 * 1-byte access since it might be placed in DARAM/SARAM
		 * and it can cause unexpected byteswap.
		 * For example,
		 *   *(p++) = *(src++) & 0xff;
		 * causes 1-byte access!
		 */
		tmp = *src++;
		*(p++) = tmp & 0xff;
		if (*(p-1) == '\n') {
			*p = '\0';
			printk(KERN_INFO "%s", s);
			p = s;
			continue;
		}
		if (p == s_end) {
			*p = '\0';
			printk(KERN_INFO "%s\n", s);
			p = s;
			continue;
		}
	}
	if (p > s) {
		*p = '\0';
		printk(KERN_INFO "%s\n", s);
	}

	release_ipbuf_pvt(ipbuf_sys_da);
	dsp_mem_disable(src);
}
#endif /* OLD_BINARY_SUPPORT */

/*
 * sysfs files
 */
static ssize_t devname_show(struct device *d, struct device_attribute *attr,
			    char *buf)
{
	struct taskdev *dev = to_taskdev(d);
	return sprintf(buf, "%s\n", dev->name);
}

static ssize_t devstate_show(struct device *d, struct device_attribute *attr,
			     char *buf)
{
	struct taskdev *dev = to_taskdev(d);
	return sprintf(buf, "%s\n", devstate_name(dev->state));
}

static ssize_t proc_list_show(struct device *d, struct device_attribute *attr,
			      char *buf)
{
	struct taskdev *dev;
	struct proc_list *pl;
	int len = 0;

	dev = to_taskdev(d);
	spin_lock(&dev->state_lock);
	list_for_each_entry(pl, &dev->proc_list, list_head) {
		len += sprintf(buf + len, "%d\n", pl->tsk->pid);
	}
	spin_unlock(&dev->state_lock);

	return len;
}

static ssize_t taskname_show(struct device *d, struct device_attribute *attr,
			     char *buf)
{
	struct taskdev *dev = to_taskdev(d);
	int len;

	len = sprintf(buf, "%s\n", dev->task->name);

	return len;
}

static ssize_t ttyp_show(struct device *d, struct device_attribute *attr,
			 char *buf)
{
	unsigned short ttyp = to_taskdev(d)->task->ttyp;
	int len = 0;

	len += sprintf(buf + len, "0x%04x\n", ttyp);
	len += sprintf(buf + len, "%s %s send\n",
			(sndtyp_acv(ttyp)) ? "active" :
					     "passive",
			(sndtyp_wd(ttyp))  ? "word" :
			(sndtyp_pvt(ttyp)) ? "private block" :
					     "global block");
	len += sprintf(buf + len, "%s %s receive\n",
			(rcvtyp_acv(ttyp)) ? "active" :
					     "passive",
			(rcvtyp_wd(ttyp))  ? "word" :
			(rcvtyp_pvt(ttyp)) ? "private block" :
					     "global block");

	return len;
}

static ssize_t fifosz_show(struct device *d, struct device_attribute *attr,
			   char *buf)
{
	struct fifo_struct *fifo = &to_taskdev(d)->task->rcvdt.fifo;
	return sprintf(buf, "%d\n", fifo->sz);
}

static int fifosz_store(struct device *d, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct dsptask *task = to_taskdev(d)->task;
	unsigned long fifosz;
	int ret;

	fifosz = simple_strtol(buf, NULL, 10);
	ret = dsp_task_set_fifosz(task, fifosz);

	return (ret < 0) ? ret : strlen(buf);
}

static ssize_t fifocnt_show(struct device *d, struct device_attribute *attr,
			    char *buf)
{
	struct fifo_struct *fifo = &to_taskdev(d)->task->rcvdt.fifo;
	return sprintf(buf, "%d\n", fifo->cnt);
}

static __inline__ char *bid_name(unsigned short bid)
{
	static char s[6];

	switch (bid) {
	case OMAP_DSP_BID_NULL:
		return "NULL";
	case OMAP_DSP_BID_PVT:
		return "PRIVATE";
	default:
		sprintf(s, "%d", bid);
		return s;
	}
}

static ssize_t ipblink_show(struct device *d, struct device_attribute *attr,
			    char *buf)
{
	struct rcvdt_bk_struct *rcvdt = &to_taskdev(d)->task->rcvdt.bk;
	int len;

	spin_lock(&rcvdt->link.lock);
	len = sprintf(buf, "top  %s\ntail %s\n",
		      bid_name(rcvdt->link.top), bid_name(rcvdt->link.tail));
	spin_unlock(&rcvdt->link.lock);

	return len;
}

static ssize_t wsz_show(struct device *d, struct device_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%d\n", to_taskdev(d)->task->wsz);
}

static ssize_t mmap_show(struct device *d, struct device_attribute *attr,
			 char *buf)
{
	struct dsptask *task = to_taskdev(d)->task;
	return sprintf(buf, "0x%p 0x%x\n", task->map_base, task->map_length);
}

/*
 * called from ipbuf_read_proc()
 */
int ipbuf_is_held(unsigned char tid, unsigned short bid)
{
	struct dsptask *task = dsptask[tid];
	unsigned short b;
	int ret = 0;

	if (task == NULL)
		return 0;

	spin_lock(&task->rcvdt.bk.link.lock);
	ipblink_for_each(b, &task->rcvdt.bk.link, ipbuf) {
		if (b == bid) {	/* found */
			ret = 1;
			break;
		}
	}
	spin_unlock(&task->rcvdt.bk.link.lock);

	return ret;
}

int __init dsp_taskmod_init(void)
{
	int retval;

	memset(taskdev, 0, sizeof(void *) * TASKDEV_MAX);
	memset(dsptask, 0, sizeof(void *) * TASKDEV_MAX);

	retval = register_chrdev(OMAP_DSP_TASK_MAJOR, "dsptask",
				 &dsp_task_fops);
	if (retval < 0) {
		printk(KERN_ERR
		       "omapdsp: failed to register task device: %d\n", retval);
		return retval;
	}

	bus_register(&dsptask_bus);
	retval = driver_register(&dsptask_driver);
	if (retval) {
		printk(KERN_ERR
		       "omapdsp: failed to register DSP task driver: %d\n",
		       retval);
		bus_unregister(&dsptask_bus);
		unregister_chrdev(OMAP_DSP_TASK_MAJOR, "dsptask");
		return -EINVAL;
	}
	dsp_task_class = class_create(THIS_MODULE, "dsptask");

	return 0;
}

void dsp_taskmod_exit(void)
{
	class_destroy(dsp_task_class);
	driver_unregister(&dsptask_driver);
	bus_unregister(&dsptask_bus);
	unregister_chrdev(OMAP_DSP_TASK_MAJOR, "dsptask");
}
