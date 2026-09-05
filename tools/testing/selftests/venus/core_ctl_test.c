// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include "kernel/sched/walt/miui_power.h"

#define __user
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define mutex_lock(x) ((void)(x))
#define mutex_unlock(x) ((void)(x))
#define spin_lock_irqsave(lock, flags) ((void)(lock), (flags) = 0)
#define spin_unlock_irqrestore(lock, flags) ((void)(lock), (void)(flags))
struct ctl_table { void *data; };
struct cluster_data {
	bool inited;
	unsigned int busy_up_thres[6], busy_down_thres[6];
};
static int miui_power_lock, state_lock, parse_error, applications;
static struct cluster_data clusters[3];
unsigned int miui_power_enhance;
#define for_each_cluster(c, i) for (; (i) < 3 && ((c) = &clusters[i]); (i)++)
static int proc_douintvec_minmax(struct ctl_table *t, int write, void *buf,
				size_t *len, loff_t *pos)
{
	if (write)
		*(unsigned int *)t->data = *(unsigned int *)buf;
	else
		*(unsigned int *)buf = *(unsigned int *)t->data;
	return parse_error;
}
static void apply_need(struct cluster_data *c) { applications++; }
int sys_miui_power_enhance_handler(struct ctl_table *, int, void *, size_t *, loff_t *);
static void core_ctl_busy_thresholds(const struct cluster_data *, unsigned int,
				     unsigned int *, unsigned int *);

static int set_policy(unsigned int value)
{
	struct ctl_table table = { .data = &miui_power_enhance };
	size_t len = sizeof(value);
	loff_t pos = 0;

	return sys_miui_power_enhance_handler(&table, 1, &value, &len, &pos);
}

int main(void)
{
	unsigned int up, down, base_up, base_down;
	struct ctl_table table = { .data = &miui_power_enhance };
	size_t len = sizeof(up);
	loff_t pos = 0;
	int count;

	clusters[0].inited = clusters[1].inited = true;
	clusters[0].busy_up_thres[0] = 60;
	clusters[0].busy_down_thres[0] = 30;
	assert(set_policy(4) == 0 && applications == 0);
	assert(set_policy(12) == 0 && applications == 2);
	core_ctl_busy_thresholds(&clusters[0], 0, &up, &down);
	assert(up == 70 && down == 40);
	assert(clusters[0].busy_up_thres[0] == 60);
	assert(set_policy(12) == 0 && applications == 2);
	/* A ROM update while enabled becomes the new baseline, not a stale backup. */
	clusters[0].busy_up_thres[0] = 80;
	clusters[0].busy_down_thres[0] = 50;
	core_ctl_busy_thresholds(&clusters[0], 0, &up, &down);
	assert(up == 90 && down == 60);
	count = applications;
	assert(set_policy(1) == -EINVAL && miui_power_enhance == 12);
	assert(set_policy(2) == -EINVAL && miui_power_enhance == 12);
	assert(set_policy(UINT_MAX) == -EINVAL && miui_power_enhance == 12);
	parse_error = -EFAULT;
	assert(set_policy(0) == -EFAULT && miui_power_enhance == 12);
	parse_error = 0;
	assert(applications == count);
	assert(sys_miui_power_enhance_handler(&table, 0, &up, &len, &pos) == 0);
	assert(up == 12 && applications == count);
	assert(set_policy(8) == 0 && applications == count);
	assert(set_policy(0) == 0 && applications == count + 2);
	core_ctl_busy_thresholds(&clusters[0], 0, &up, &down);
	assert(up == 80 && down == 50);

	for (base_up = 0; base_up <= 102; base_up++) {
		for (base_down = 0; base_down <= 102; base_down++) {
			up = base_up;
			down = base_down;
			miui_core_ctl_adjust_thresholds(&up, &down);
			if (!base_up || !base_down || base_up >= 100 || base_down >= base_up) {
				assert(up == base_up && down == base_down);
			} else {
				assert(up <= 100 && up > down);
				assert(up - down == base_up - base_down);
				assert(up - base_up <= 10 && up > base_up);
			}
		}
	}
	up = UINT_MAX;
	down = 50;
	miui_core_ctl_adjust_thresholds(&up, &down);
	assert(up == UINT_MAX && down == 50);
	puts("PASS core_ctl: masks, read/error atomicity, toggles, ROM baseline, 10609 threshold pairs");
	return 0;
}
