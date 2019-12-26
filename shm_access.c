/*  shm_access.c - The simplest kernel module.

* Copyright (C) 2013 - 2016 pc94@yeah.net, Inc
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.

*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License along
*   with this program. If not, see <http://www.gnu.org/licenses/>.

*/
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/ptrace.h>

#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

#ifndef VM_RESERVED
#define VM_RESERVED (VM_DONTEXPAND | VM_DONTDUMP)
#endif 

/* Standard module information, edit as appropriate */
MODULE_LICENSE("GPL");
MODULE_AUTHOR
    ("pc94@yeah.net");
MODULE_DESCRIPTION
    ("shm_access - Share DDR memory access module for Xilinx ZynqMP MultiProcessor");

#define DRIVER_NAME "shm_access"

/* Simple example of how to receive command line parameters to your module.
   Delete if you don't need them */
unsigned long mem_start = 0x40000000;
unsigned long mem_size = 0x10000000;

module_param(mem_start, long, S_IRUGO);
module_param(mem_size, long, S_IRUGO);

struct shm_access_local {
	unsigned long mem_start;
	unsigned long mem_end;
	void __iomem *base_addr;
};

struct shm_access_local *lp = NULL;

static int shm_access_probe(void)
{
	int rc = 0;
	lp = (struct shm_access_local *) kmalloc(sizeof(struct shm_access_local), GFP_KERNEL);
	if (!lp) {
		printk(KERN_ERR"%s<probe>: Could not allocate shm_access device\n",
			DRIVER_NAME);
		return -ENOMEM;
	}
	lp->mem_start = mem_start;
	lp->mem_end = mem_start + mem_size - 1;

	if (!request_mem_region(lp->mem_start,
				lp->mem_end - lp->mem_start + 1,
				DRIVER_NAME)) {
		printk(KERN_ERR"%s<probe>: Could not lock memory region at %p\n",
			DRIVER_NAME,
			(void *)lp->mem_start);
		rc = -EBUSY;
		goto error1;
	}

	lp->base_addr = (void*)ioremap_nocache(lp->mem_start, lp->mem_end - lp->mem_start + 1);
	if (!lp->base_addr) {
		printk(KERN_ERR"%s<probe>: Could not map I/O memory\n",
			DRIVER_NAME);
		rc = -EIO;
		goto error2;
	}

	printk(KERN_INFO"%s<probe>: 0x%08x mapped to 0x%08x\n",
		DRIVER_NAME,
		(unsigned int __force)lp->mem_start,
		(unsigned int __force)lp->base_addr);
	return 0;
error2:
	release_mem_region(lp->mem_start, lp->mem_end - lp->mem_start + 1);
error1:
	kfree(lp);
	return rc;
}

static int shm_access_remove(void)
{
	if(lp != NULL)
	{
		iounmap(lp->base_addr);
		release_mem_region(lp->mem_start, lp->mem_end - lp->mem_start + 1);
		kfree(lp);
		lp = NULL;
	}
	return 0;
}

static int shm_access_open(struct inode *inode, struct file *filp)
{
	try_module_get(THIS_MODULE);
	return 0;
}

static int shm_access_close(struct inode *inode, struct file *filp)
{
	module_put(THIS_MODULE);
	return 0;
}

static loff_t shm_access_lseek(struct file *file, loff_t offset, int orig)
{
	loff_t ret = 0;

	inode_lock(file_inode(file));
	switch (orig)
	{
	case SEEK_CUR:
		offset += file->f_pos;
		break;
	case SEEK_SET:
		break;
	case SEEK_END:
		offset = mem_size - offset;
		break;
	default:
		printk(KERN_ERR"%s<lseek>: SEEK_TYPE unsupported\n",
			DRIVER_NAME);
		ret = -EINVAL;
		break;
	}
	if(ret >= 0)
	{
		if((unsigned long long)offset >= mem_size)
		{
			printk(KERN_ERR"%s<lseek>: offset 0x%08x is out of memory size 0x%08x\n",
				DRIVER_NAME,
				offset,
				mem_size);
			ret = -EOVERFLOW;
		}
		else
		{
			file->f_pos = offset;
			ret = file->f_pos;
			force_successful_syscall_return();
		}
	}
	inode_unlock(file_inode(file));
	return ret;
}

static ssize_t shm_access_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	ssize_t read;
	if((p + count) > mem_size)
		read = mem_size - p;
	else
		read = count;
	if(lp == NULL)
	{
		printk(KERN_ERR"%s<read>: lp is NULL\n",
			DRIVER_NAME);
		return -EFAULT;
	}
	if(copy_to_user(buf, lp->base_addr + p, read))
	{
		printk(KERN_ERR"%s<read>: copy_to_user failure\n",
			DRIVER_NAME);
		return -EFAULT;
	}
	p += read;
	count -= read;
	*ppos = p;
	return read;
}

static ssize_t shm_access_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	ssize_t wrote = 0;

	if((p + count) > mem_size)
		wrote = mem_size - p;
	else
		wrote = count;

	if(lp == NULL)
	{
		printk(KERN_ERR"%s<write>: lp is NULL\n",
			DRIVER_NAME);
		return -EFAULT;
	}
	if(copy_from_user(lp->base_addr + p, buf, wrote))
	{
		printk(KERN_ERR"%s<write>: copy_to_user failure\n",
			DRIVER_NAME);
		return -EFAULT;
	}
	p += wrote;
	count -= wrote;
	*ppos = p;
	return wrote;
}

static int shm_access_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;

	if(vma->vm_pgoff != 0)
	{
		printk(KERN_ERR"%s<mmap>: vm_pgoff 0x%08x\n",
			DRIVER_NAME,
			vma->vm_pgoff);
		return -EINVAL;
	}
	if(size > mem_size)
	{
		printk(KERN_ERR"%s<mmap>: mmap size 0x%08x is large than I/O memory size 0x%08x.\n",
			DRIVER_NAME,
			size,
			mem_size);
		return -EINVAL;
	}
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= VM_IO;
	vma->vm_flags |= VM_RESERVED;

	if(io_remap_pfn_range(vma,
		vma->vm_start,
		mem_start >> PAGE_SHIFT,
		size,
		vma->vm_page_prot))
	{
		printk(KERN_ERR"%s<mmap>: remap_pfn_range failure\n",
			DRIVER_NAME);
		return -EAGAIN;
	}
	return 0;
}

static struct file_operations shm_access_fops = {
	.owner		= THIS_MODULE,
	.llseek		= shm_access_lseek,
	.read		= shm_access_read,
	.write		= shm_access_write,
	.open		= shm_access_open,
	.release	= shm_access_close,
	.mmap		= shm_access_mmap,
};

static int major;
static struct class *shm_access_class;
static int __init shm_access_init(void)
{
	if((major = register_chrdev(0, DRIVER_NAME, &shm_access_fops)) < 0)
	{
		printk(KERN_ERR"%s<init>: unable to get major\n",
			DRIVER_NAME);
		return -EINVAL;
	}

	shm_access_class = class_create(THIS_MODULE, DRIVER_NAME);
	if( IS_ERR(shm_access_class))
	{
		printk(KERN_ERR"%s<init>: create class shm_access error\n",
			DRIVER_NAME);
		return PTR_ERR(shm_access_class);
	}
	device_create(shm_access_class, NULL, MKDEV(major, 0), NULL, DRIVER_NAME);

	return shm_access_probe();
}


static void __exit shm_access_exit(void)
{
	shm_access_remove();
	device_destroy(shm_access_class, MKDEV(major, 0));
	class_destroy(shm_access_class);
	unregister_chrdev(major, DRIVER_NAME);
}

module_init(shm_access_init);
module_exit(shm_access_exit);
