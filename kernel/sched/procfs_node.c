// SPDX-License-Identifier: GPL-2.0-only
/* sysfs_node.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2020 Google LLC
 */
#include <linux/lockdep.h>
#include <linux/kobject.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include "sched.h"

struct proc_dir_entry *vendor_sched;

unsigned int pmu_poll_time_ms = 10;
bool pmu_poll_enabled;
extern void pmu_poll_enable(void);
extern void pmu_poll_disable(void);
extern int pmu_poll_init(void);

#define MAX_PROC_SIZE 128

#define PROC_ENTRY(__name) {__stringify(__name), &__name##_proc_ops}

#define PROC_OPS_RW(__name) \
		static int __name##_proc_open(\
			struct inode *inode, struct file *file) \
		{ \
			return single_open(file,\
			__name##_show, PDE_DATA(inode));\
		} \
		static const struct file_operations  __name##_proc_ops = { \
			.open	=  __name##_proc_open, \
			.read	= seq_read, \
			.llseek	= seq_lseek,\
			.release = single_release,\
			.write	=  __name##_store,\
		}

static int pmu_poll_time_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%u\n", pmu_poll_time_ms);
	return 0;
}

static ssize_t pmu_poll_time_store(struct file *filp,
							const char __user *ubuf,
							size_t count, loff_t *pos)
{
	unsigned int val;
	char buf[MAX_PROC_SIZE];

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;

	buf[count] = '\0';

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;

	if (val < 10 || val > 1000000)
		return -EINVAL;

	pmu_poll_time_ms = val;

	return count;
}

PROC_OPS_RW(pmu_poll_time);

static int pmu_poll_enable_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", pmu_poll_enabled ? "true" : "false");
	return 0;
}

static ssize_t pmu_poll_enable_store(struct file *filp,
							const char __user *ubuf,
							size_t count, loff_t *pos)
{
	bool enable;
	char buf[MAX_PROC_SIZE];

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;

	buf[count] = '\0';

	if (kstrtobool(buf, &enable))
		return -EINVAL;

	if (pmu_poll_enabled == enable)
		return count;

	if (enable)
		pmu_poll_enable();
	else
		pmu_poll_disable();

	return count;
}

PROC_OPS_RW(pmu_poll_enable);

struct pentry {
	const char *name;
	const struct file_operations *fops;
};
static struct pentry entries[] = {
	// pmu limit attribute
	PROC_ENTRY(pmu_poll_time),
	PROC_ENTRY(pmu_poll_enable),
};

int create_procfs_node(void)
{
	int i;

	vendor_sched = proc_mkdir("vendor_sched", NULL);

	if (!vendor_sched)
		goto out;

	/* create procfs */
	for (i = 0; i < ARRAY_SIZE(entries); i++) {
		umode_t mode;

		if (entries[i].fops->write == NULL) {
			mode = 0444;
		} else if(entries[i].fops->read== NULL) {
			mode = 0200;
		} else {
			mode = 0644;
		}

		if (!proc_create(entries[i].name, mode,
					vendor_sched, entries[i].fops)) {
			pr_debug("%s(), create %s failed\n",
					__func__, entries[i].name);
			remove_proc_entry("vendor_sched", NULL);

			goto out;
		}
	}

	return 0;

out:
	return -ENOMEM;
}

static int vh_sched_init(void)
{
	int ret;

	ret = pmu_poll_init();
	if (ret) {
		pr_err("pmu poll init failed\n");
		return ret;
	}

	ret = create_procfs_node();
	if (ret)
		return ret;

        pr_info("pmu init successfully!\n");

	return 0;
}

module_init(vh_sched_init);
