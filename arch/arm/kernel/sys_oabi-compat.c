/*
 *  arch/arm/kernel/sys_oabi-compat.c
 *
 *  Compatibility wrappers for syscalls that are used from
 *  old ABI user space binaries with an EABI kernel.
 *
 *  Author:	Nicolas Pitre
 *  Created:	Oct 7, 2005
 *  Copyright:	MontaVista Software, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

/*
 * The legacy ABI and the new ARM EABI have different rules making some
 * syscalls incompatible especially with structure arguments.
 * Most notably, Eabi says 64-bit members should be 64-bit aligned instead of
 * simply word aligned.  EABI also pads structures to the size of the largest
 * member it contains instead of the invariant 32-bit.
 *
 * The following syscalls are affected:
 *
 * sys_stat64:
 * sys_lstat64:
 * sys_fstat64:
 *
 *   struct stat64 has different sizes and some members are shifted
 *   Compatibility wrappers are needed for them and provided below.
 *
 * sys_fcntl64:
 *
 *   struct flock64 has different sizes and some members are shifted
 *   A compatibility wrapper is needed and provided below.
 *
 * sys_statfs64:
 * sys_fstatfs64:
 *
 *   struct statfs64 has extra padding with EABI growing its size from
 *   84 to 88.  This struct is now __attribute__((packed,aligned(4)))
 *   with a small assembly wrapper to force the sz argument to 84 if it is 88
 *   to avoid copying the extra padding over user space unexpecting it.
 *
 * sys_newuname:
 *
 *   struct new_utsname has no padding with EABI.  No problem there.
 *
 * sys_epoll_ctl:
 * sys_epoll_wait:
 *
 *   struct epoll_event has its second member shifted also affecting the
 *   structure size. Compatibility wrappers are needed and provided below.
 *
 * sys_ipc:
 * sys_semop:
 * sys_semtimedop:
 *
 *   struct sembuf loses its padding with EABI.  Since arrays of them are
 *   used they have to be copyed to remove the padding. Compatibility wrappers
 *   provided below.
 */

#include <linux/syscalls.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/eventpoll.h>
#include <linux/sem.h>
#include <asm/ipc.h>
#include <asm/uaccess.h>

struct oldabi_stat64 {
	unsigned long long st_dev;
	unsigned int	__pad1;
	unsigned long	__st_ino;
	unsigned int	st_mode;
	unsigned int	st_nlink;

	unsigned long	st_uid;
	unsigned long	st_gid;

	unsigned long long st_rdev;
	unsigned int	__pad2;

	long long	st_size;
	unsigned long	st_blksize;
	unsigned long long st_blocks;

	unsigned long	st_atime;
	unsigned long	st_atime_nsec;

	unsigned long	st_mtime;
	unsigned long	st_mtime_nsec;

	unsigned long	st_ctime;
	unsigned long	st_ctime_nsec;

	unsigned long long st_ino;
} __attribute__ ((packed,aligned(4)));

static long cp_oldabi_stat64(struct kstat *stat,
			     struct oldabi_stat64 __user *statbuf)
{
	struct oldabi_stat64 tmp;

	tmp.st_dev = huge_encode_dev(stat->dev);
	tmp.__pad1 = 0;
	tmp.__st_ino = stat->ino;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	tmp.st_uid = stat->uid;
	tmp.st_gid = stat->gid;
	tmp.st_rdev = huge_encode_dev(stat->rdev);
	tmp.st_size = stat->size;
	tmp.st_blocks = stat->blocks;
	tmp.__pad2 = 0;
	tmp.st_blksize = stat->blksize;
	tmp.st_atime = stat->atime.tv_sec;
	tmp.st_atime_nsec = stat->atime.tv_nsec;
	tmp.st_mtime = stat->mtime.tv_sec;
	tmp.st_mtime_nsec = stat->mtime.tv_nsec;
	tmp.st_ctime = stat->ctime.tv_sec;
	tmp.st_ctime_nsec = stat->ctime.tv_nsec;
	tmp.st_ino = stat->ino;
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

asmlinkage long sys_oabi_stat64(char __user * filename,
				struct oldabi_stat64 __user * statbuf)
{
	struct kstat stat;
	int error = vfs_stat(filename, &stat);
	if (!error)
		error = cp_oldabi_stat64(&stat, statbuf);
	return error;
}

asmlinkage long sys_oabi_lstat64(char __user * filename,
				 struct oldabi_stat64 __user * statbuf)
{
	struct kstat stat;
	int error = vfs_lstat(filename, &stat);
	if (!error)
		error = cp_oldabi_stat64(&stat, statbuf);
	return error;
}

asmlinkage long sys_oabi_fstat64(unsigned long fd,
				 struct oldabi_stat64 __user * statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);
	if (!error)
		error = cp_oldabi_stat64(&stat, statbuf);
	return error;
}

struct oabi_flock64 {
	short	l_type;
	short	l_whence;
	loff_t	l_start;
	loff_t	l_len;
	pid_t	l_pid;
} __attribute__ ((packed,aligned(4)));

asmlinkage long sys_oabi_fcntl64(unsigned int fd, unsigned int cmd,
				 unsigned long arg)
{
	struct oabi_flock64 user;
	struct flock64 kernel;
	mm_segment_t fs = USER_DS; /* initialized to kill a warning */
	unsigned long local_arg = arg;
	int ret;

	switch (cmd) {
	case F_GETLK64:
	case F_SETLK64:
	case F_SETLKW64:
		if (copy_from_user(&user, (struct oabi_flock64 __user *)arg,
				   sizeof(user)))
			return -EFAULT;
		kernel.l_type	= user.l_type;
		kernel.l_whence	= user.l_whence;
		kernel.l_start	= user.l_start;
		kernel.l_len	= user.l_len;
		kernel.l_pid	= user.l_pid;
		local_arg = (unsigned long)&kernel;
		fs = get_fs();
		set_fs(KERNEL_DS);
	}

	ret = sys_fcntl64(fd, cmd, local_arg);

	switch (cmd) {
	case F_GETLK64:
		if (!ret) {
			user.l_type	= kernel.l_type;
			user.l_whence	= kernel.l_whence;
			user.l_start	= kernel.l_start;
			user.l_len	= kernel.l_len;
			user.l_pid	= kernel.l_pid;
			if (copy_to_user((struct oabi_flock64 __user *)arg,
					 &user, sizeof(user)))
				ret = -EFAULT;
		}
	case F_SETLK64:
	case F_SETLKW64:
		set_fs(fs);
	}

	return ret;
}

struct oabi_epoll_event {
	__u32 events;
	__u64 data;
} __attribute__ ((packed,aligned(4)));

asmlinkage long sys_oabi_epoll_ctl(int epfd, int op, int fd,
				   struct oabi_epoll_event __user *event)
{
	struct oabi_epoll_event user;
	struct epoll_event kernel;
	mm_segment_t fs;
	long ret;

	if (op == EPOLL_CTL_DEL)
		return sys_epoll_ctl(epfd, op, fd, NULL);
	if (copy_from_user(&user, event, sizeof(user)))
		return -EFAULT;
	kernel.events = user.events;
	kernel.data   = user.data;
	fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_epoll_ctl(epfd, op, fd, &kernel);
	set_fs(fs);
	return ret;
}

asmlinkage long sys_oabi_epoll_wait(int epfd,
				    struct oabi_epoll_event __user *events,
				    int maxevents, int timeout)
{
	struct epoll_event *kbuf;
	mm_segment_t fs;
	long ret, err, i;

	if (maxevents <= 0 || maxevents > (INT_MAX/sizeof(struct epoll_event)))
		return -EINVAL;
	kbuf = kmalloc(sizeof(*kbuf) * maxevents, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;
	fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_epoll_wait(epfd, kbuf, maxevents, timeout);
	set_fs(fs);
	err = 0;
	for (i = 0; i < ret; i++) {
		__put_user_error(kbuf[i].events, &events->events, err);
		__put_user_error(kbuf[i].data,   &events->data,   err);
		events++;
	}
	kfree(kbuf);
	return err ? -EFAULT : ret;
}

struct oabi_sembuf {
	unsigned short	sem_num;
	short		sem_op;
	short		sem_flg;
	unsigned short	__pad;
};

asmlinkage long sys_oabi_semtimedop(int semid,
				    struct oabi_sembuf __user *tsops,
				    unsigned nsops,
				    const struct timespec __user *timeout)
{
	struct sembuf *sops;
	struct timespec local_timeout;
	long err;
	int i;

	if (nsops < 1)
		return -EINVAL;
	sops = kmalloc(sizeof(*sops) * nsops, GFP_KERNEL);
	if (!sops)
		return -ENOMEM;
	err = 0;
	for (i = 0; i < nsops; i++) {
		__get_user_error(sops[i].sem_num, &tsops->sem_num, err);
		__get_user_error(sops[i].sem_op,  &tsops->sem_op,  err);
		__get_user_error(sops[i].sem_flg, &tsops->sem_flg, err);
		tsops++;
	}
	if (timeout) {
		/* copy this as well before changing domain protection */
		err |= copy_from_user(&local_timeout, timeout, sizeof(*timeout));
		timeout = &local_timeout;
	}
	if (err) {
		err = -EFAULT;
	} else {
		mm_segment_t fs = get_fs();
		set_fs(KERNEL_DS);
		err = sys_semtimedop(semid, sops, nsops, timeout);
		set_fs(fs);
	}
	kfree(sops);
	return err;
}

asmlinkage long sys_oabi_semop(int semid, struct oabi_sembuf __user *tsops,
			       unsigned nsops)
{
	return sys_oabi_semtimedop(semid, tsops, nsops, NULL);
}

extern asmlinkage int sys_ipc(uint call, int first, int second, int third,
			      void __user *ptr, long fifth);

asmlinkage int sys_oabi_ipc(uint call, int first, int second, int third,
			    void __user *ptr, long fifth)
{
	switch (call & 0xffff) {
	case SEMOP:
		return  sys_oabi_semtimedop(first,
					    (struct oabi_sembuf __user *)ptr,
					    second, NULL);
	case SEMTIMEDOP:
		return  sys_oabi_semtimedop(first,
					    (struct oabi_sembuf __user *)ptr,
					    second,
					    (const struct timespec __user *)fifth);
	default:
		return sys_ipc(call, first, second, third, ptr, fifth);
	}
}
