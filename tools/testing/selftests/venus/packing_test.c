// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "kernel/sched/walt/miui_power.h"

#define READ_ONCE(x) (x)
#define TASK_WAKING 1
#define TASK_BOOST_STRICT_MAX 2
#define nr_cpu_ids 8
typedef struct { unsigned int bits; } cpumask_t;
static cpumask_t online, isolated;
#define cpu_active_mask (&online)
#define cpu_isolated_mask (&isolated)
struct task_struct {
	int state, boost;
	unsigned long util;
	bool placement_boost;
	cpumask_t affinity;
	cpumask_t *cpus_ptr;
};
struct walt_sched_cluster { int id; cpumask_t cpus; };
struct rq {
	struct { struct walt_sched_cluster *cluster; } wrq;
	struct task_struct *curr;
	int idle_idx;
	unsigned long util, capacity, current_capacity, freq;
	bool reserved, irq, idle, fits, accounted;
};
struct find_best_target_env {
	bool need_idle, boosted, is_rtg, strict_max;
	int start_cpu, skip_cpu;
};
static struct rq rqs[8];
static struct task_struct running[8], task;
static struct walt_sched_cluster clusters[3];
static struct find_best_target_env env;
static bool spread;
static int num_sched_clusters = 3;
unsigned int miui_power_enhance;
static struct rq *cpu_rq(int cpu) { assert(cpu >= 0 && cpu < 8); return &rqs[cpu]; }
#define cpumask_and(dst, a, b) ((dst)->bits = (a)->bits & (b)->bits)
#define cpumask_andnot(dst, a, b) ((dst)->bits = (a)->bits & ~(b)->bits)
static int cpumask_first(const cpumask_t *m) { return m->bits ? __builtin_ctz(m->bits) : 8; }
static int cpumask_next(int prev, const cpumask_t *m)
{
	int cpu;

	for (cpu = prev + 1; cpu < 8; cpu++) {
		if (m->bits & (1U << cpu))
			return cpu;
	}
	return 8;
}
#define for_each_cpu(c, m) \
	for ((c) = cpumask_first(m); (c) < 8; (c) = cpumask_next(c, m))
#define task_placement_boost_enabled(p) ((p)->placement_boost)
#define prefer_spread_on_idle(c, n) (spread)
#define uclamp_task_util(p) ((p)->util)
#define arch_scale_freq_capacity(c) (cpu_rq(c)->freq)
#define cpu_util(c) (cpu_rq(c)->util)
#define capacity_orig_of(c) (cpu_rq(c)->capacity)
#define is_reserved(c) (cpu_rq(c)->reserved)
#define sched_cpu_high_irqload(c) (cpu_rq(c)->irq)
#define per_task_boost(p) ((p)->boost)
#define idle_cpu(c) (cpu_rq(c)->idle)
#define idle_get_state_idx(r) ((r)->idle_idx)
#define task_fits_max(p, c) (cpu_rq(c)->fits)
#define task_in_cum_window_demand(r, p) ((r)->accounted)
#define cpu_util_cum(c, d) (cpu_rq(c)->util + (d))
#define add_capacity_margin(u, c) ((u) + (u) / 4)
#define capacity_curr_of(c) (cpu_rq(c)->current_capacity)
static int walt_miui_packing_cpu(struct task_struct *, int, struct find_best_target_env *);

static void reset(void)
{
	int i;

	memset(rqs, 0, sizeof(rqs));
	memset(running, 0, sizeof(running));
	memset(&env, 0, sizeof(env));
	memset(&task, 0, sizeof(task));
	for (i = 0; i < 3; i++)
		clusters[i].id = i;
	clusters[0].cpus.bits = 0x0f;
	clusters[1].cpus.bits = 0x70;
	clusters[2].cpus.bits = 0x80;
	for (i = 0; i < 8; i++) {
		rqs[i].wrq.cluster = &clusters[i < 4 ? 0 : i < 7 ? 1 : 2];
		rqs[i].curr = &running[i];
		rqs[i].capacity = i < 4 ? 300 : i < 7 ? 700 : 1024;
		rqs[i].current_capacity = 100;
		rqs[i].freq = 400;
		rqs[i].fits = true;
		rqs[i].util = 20;
	}
	rqs[1].util = 10;
	task.state = TASK_WAKING;
	task.util = 5;
	task.affinity.bits = online.bits = 0xff;
	task.cpus_ptr = &task.affinity;
	isolated.bits = 0;
	spread = false;
	env.skip_cpu = -1;
	miui_power_enhance = 4;
}

#define EXPECT(change, want) do { reset(); change; assert(walt_miui_packing_cpu(&task, 0, &env) == (want)); } while (0)
int main(void)
{
	EXPECT((void)0, 1);
	EXPECT(miui_power_enhance = 0, -1);
	EXPECT(miui_power_enhance = 8, -1);
	EXPECT(task.state = 0, -1);
	EXPECT(task.util = 15, -1);
	EXPECT(task.util = 14, 1);
	EXPECT(env.need_idle = true, -1);
	EXPECT(env.boosted = true, -1);
	EXPECT(env.is_rtg = true, -1);
	EXPECT(env.strict_max = true, -1);
	EXPECT(task.placement_boost = true, -1);
	EXPECT(spread = true, -1);
	EXPECT(online.bits = 0, -1);
	EXPECT(task.affinity.bits = 0, -1);
	EXPECT(task.affinity.bits = 1, 0);
	EXPECT(isolated.bits = 3, 2);
	EXPECT(rqs[0].freq = 451, -1);
	EXPECT(rqs[0].freq = 450, 1);
	EXPECT(rqs[1].util = 20, 0);
	/* The third CPU does not displace an eligible first/second choice. */
	EXPECT(rqs[2].util = 0, 1);
	EXPECT(rqs[1].irq = true, 0);
	EXPECT(rqs[1].reserved = true, 0);
	EXPECT(env.skip_cpu = 1, 0);
	EXPECT(running[1].boost = TASK_BOOST_STRICT_MAX, 0);
	EXPECT(rqs[1].fits = false, 0);
	EXPECT(rqs[1].current_capacity = 1, 0);
	EXPECT(rqs[1].idle = true; rqs[1].idle_idx = 2, 0);
	EXPECT(rqs[1].idle = true; rqs[1].idle_idx = 1, 1);
	EXPECT(rqs[1].util = 30, 0);
	EXPECT(rqs[2].util = rqs[3].util = 300, -1);
	reset();
	env.start_cpu = 4;
	assert(walt_miui_packing_cpu(&task, 4, &env) == 4);
	env.start_cpu = 7;
	assert(walt_miui_packing_cpu(&task, 7, &env) == -1);
	assert(walt_miui_packing_cpu(&task, -1, &env) == -1);
	puts("PASS packing: 4+3+1 topology, affinity, empty/isolation masks, load/frequency/latency guards");
	return 0;
}
