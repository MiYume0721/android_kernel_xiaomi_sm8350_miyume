// SPDX-License-Identifier: GPL-2.0
/* Source-extracted logic tests, NOT a kernel/concurrency/allocator runtime test. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef int spinlock_t;
typedef struct { unsigned int wakes; } wait_queue_head_t;
struct kernfs_open_file { wait_queue_head_t *kn; };
struct task_struct { int unused; };
struct timer_list { unsigned long expires; unsigned int arms; };
struct list_head { struct list_head *next, *prev; };

#define container_of(p, type, member) ((type *)((char *)(p) - offsetof(type, member)))
#define list_entry(p, type, member) container_of(p, type, member)
#define list_first_entry_or_null(h, type, member) \
	(list_empty(h) ? NULL : list_entry((h)->next, type, member))
#define list_for_each_entry(p, h, member) \
	for (p = list_entry((h)->next, __typeof__(*p), member); \
	     &(p)->member != (h); p = list_entry((p)->member.next, __typeof__(*p), member))
#define BUG_ON(x) assert(!(x))
#define VM_BUG_ON(x) BUG_ON(x)
#define ULLONG_MAX UINT64_MAX
#define likely(x) (x)
#define rcu_read_lock() ((void)0)
#define rcu_read_unlock() ((void)0)
#define rcu_dereference(p) (p)
#define mutex_lock(p) ((void)(p))
#define mutex_unlock(p) ((void)(p))
/* Single-threaded model: memory ordering requires a real kernel stress test. */
#define smp_mb() ((void)0)
#define div64_u64(a, b) ((a) / (b))
#define sched_clock() simulated_now
#define trace_psi_event(state, threshold) ((void)(state), (void)(threshold))
#define wake_up_interruptible(q) ((q)->wakes++)
#define kernfs_notify(q) ((q)->wakes++)
#define atomic_set(p, v) (*(p) = (v))

static u64 simulated_now;
static u32 simulated_changed_states;
static int gate_at_collect;
static unsigned long jiffies;

static int atomic_xchg(int *p, int value)
{
	int old = *p;

	*p = value;
	return old;
}

static int cmpxchg(int *p, int expected, int value)
{
	int old = *p;

	if (old == expected)
		*p = value;
	return old;
}

static void mod_timer(struct timer_list *timer, unsigned long expires)
{
	timer->expires = expires;
	timer->arms++;
}

static unsigned long nsecs_to_jiffies(u64 ns)
{
	return ns / 1000000;
}

static void INIT_LIST_HEAD(struct list_head *h)
{
	h->next = h;
	h->prev = h;
}

static bool list_empty(struct list_head *h)
{
	return h->next == h;
}

static void list_add(struct list_head *n, struct list_head *h)
{
	n->next = h->next;
	n->prev = h;
	h->next->prev = n;
	h->next = n;
}

static void list_del_init(struct list_head *n)
{
	n->prev->next = n->next;
	n->next->prev = n->prev;
	INIT_LIST_HEAD(n);
}

#include "logic_generated.h"

static void test_fullness(void)
{
	struct size_class c = { .index = 254 };
	struct zspage z = { .magic = ZSPAGE_MAGIC };
	unsigned int n, used, count = 0, decoded_class;
	enum fullness_group previous, fg, decoded_fg;
	int i;

	_Static_assert(NR_FULLNESS_GROUPS == 12, "twelve fullness groups");
	_Static_assert((int)OBJ_ALLOCATED >= (int)NR_FULLNESS_GROUPS, "non-overlapping stats");
	_Static_assert(NR_FULLNESS_GROUPS <= 1 << FULLNESS_BITS, "packed fullness fits");
	for (i = 0; i < NR_FULLNESS_GROUPS; i++)
		INIT_LIST_HEAD(&c.fullness_list[i]);
	/* 4 KiB / 32-byte objects, up to four pages: covers all capacities. */
	for (n = 1; n <= 512; n++) {
		c.objs_per_zspage = n;
		previous = 0;
		for (used = 0; used <= n; used++) {
			z.inuse = used;
			fg = get_fullness_group(&c, &z);
			assert(fg >= previous && fg < NR_FULLNESS_GROUPS);
			assert((fg == ZS_INUSE_RATIO_0) == (used == 0));
			assert((fg == ZS_INUSE_RATIO_100) == (used == n));
			if (used && used < n) {
				assert(100 * used >= (fg - 1) * 10 * n);
				assert(100 * used < fg * 10 * n);
			}
			set_zspage_mapping(&z, c.index, fg);
			get_zspage_mapping(&z, &decoded_class, &decoded_fg);
			assert(decoded_class == c.index && decoded_fg == fg);
			previous = fg;
			count++;
		}
	}
	/* Allocation/free across every bucket updates lists and counters. */
	c.objs_per_zspage = 100;
	z.inuse = 0;
	set_zspage_mapping(&z, c.index, ZS_INUSE_RATIO_0);
	insert_zspage(&c, &z, ZS_INUSE_RATIO_0);
	for (i = 0; i <= 200; i++) {
		unsigned long total = 0;
		int bucket;

		z.inuse = i <= 100 ? i : 200 - i;
		fg = fix_fullness_group(&c, &z);
		for (bucket = 0; bucket < NR_FULLNESS_GROUPS; bucket++) {
			total += zs_stat_get(&c, bucket);
			assert(list_empty(&c.fullness_list[bucket]) == (bucket != (int)fg));
		}
		assert(total == 1 && zs_stat_get(&c, fg) == 1);
		assert(!zs_stat_get(&c, OBJ_ALLOCATED) && !zs_stat_get(&c, OBJ_USED));
	}
	remove_zspage(&c, &z, ZS_INUSE_RATIO_0);
	printf("ok 1 - zsmalloc %u occupancy/bitfield cases and 201 transitions\n", count);
}

static void test_selection(void)
{
	unsigned int mask;

	/* Every combination of nonempty fullness lists, including empty/full. */
	for (mask = 0; mask < 1U << NR_FULLNESS_GROUPS; mask++) {
		struct size_class c = { .objs_per_zspage = 100 };
		struct zspage pages[NR_FULLNESS_GROUPS] = { 0 };
		struct zspage *src, *dst;
		int i, alloc = -1, low = -1, high = -1;

		for (i = 0; i < NR_FULLNESS_GROUPS; i++) {
			INIT_LIST_HEAD(&c.fullness_list[i]);
			pages[i].inuse = i == ZS_INUSE_RATIO_100 ? 100 :
					 i == ZS_INUSE_RATIO_0 ? 0 : (i - 1) * 10 + 1;
			if (!(mask & (1U << i)))
				continue;
			insert_zspage(&c, &pages[i], i);
			if (i <= ZS_INUSE_RATIO_99)
				alloc = i;
			if (i >= ZS_INUSE_RATIO_10 && i <= ZS_INUSE_RATIO_99) {
				if (low < 0)
					low = i;
				high = i;
			}
		}
		assert(find_get_zspage(&c) == (alloc < 0 ? NULL : &pages[alloc]));
		src = isolate_zspage(&c, true);
		assert(src == (low < 0 ? NULL : &pages[low]));
		if (src)
			assert(putback_zspage(&c, src) == low);
		dst = isolate_zspage(&c, false);
		assert(dst == (high < 0 ? NULL : &pages[high]));
		if (dst)
			assert(putback_zspage(&c, dst) == high);
	}
	puts("ok 2 - zsmalloc allocation/source/destination order: 4096 list combinations");
}

static void test_events(void)
{
	struct psi_group g = { .poll_min_period = 10 };
	struct psi_trigger t = { .state = PSI_MEM_SOME, .threshold = 10,
		.win.size = 100 };
	struct psi_trigger second = { .state = PSI_MEM_SOME, .threshold = 10,
		.win.size = 100 };
	wait_queue_head_t kernfs_wait = { 0 };
	struct kernfs_open_file of = { .kn = &kernfs_wait };

	INIT_LIST_HEAD(&g.triggers);
	list_add(&t.node, &g.triggers);
	list_add(&second.node, &g.triggers);
	second.of = &of;
	/* Existing cumulative pressure is not a new trigger's baseline zero. */
	g.total[PSI_POLL][PSI_MEM_SOME] = 1000;
	init_triggers(&g, 1000);
	update_triggers(&g, 1010);
	assert(!t.event && !second.event);
	g.total[PSI_POLL][PSI_MEM_SOME] += 20;
	update_triggers(&g, 1020);
	assert(t.event && second.event && t.event_wait.wakes == 1 && kernfs_wait.wakes == 1);
	t.event = 0;
	second.event = 0;
	g.total[PSI_POLL][PSI_MEM_SOME] += 20;
	update_triggers(&g, 1040);
	assert(t.pending_event && second.pending_event && !t.event);
	update_triggers(&g, 1119);
	assert(!t.event);
	/* No new pressure: pending event must fire at the next permitted time. */
	update_triggers(&g, 1120);
	assert(t.event && second.event && !t.pending_event);
	assert(t.event_wait.wakes == 2 && kernfs_wait.wakes == 2);
	/* Pending event with new but subthreshold growth must also fire. */
	t.event = 0;
	t.pending_event = true;
	window_reset(&t.win, 1210, g.total[PSI_POLL][PSI_MEM_SOME], 0);
	g.total[PSI_POLL][PSI_MEM_SOME]++;
	update_triggers(&g, 1220);
	assert(t.event && !t.pending_event && t.event_wait.wakes == 3);
	update_triggers(&g, 1221);
	assert(t.event_wait.wakes == 3);
	puts("ok 3 - PSI shared state, initial window, deferred events and notifications");
}

static void test_poll_gate(void)
{
	struct task_struct task;
	struct psi_group g = { .poll_task = &task, .poll_min_period = 10 };
	unsigned int arms;

	INIT_LIST_HEAD(&g.triggers);
	psi_schedule_poll_work(&g, 1, false);
	assert(g.poll_scheduled && g.poll_timer.arms == 1);
	psi_schedule_poll_work(&g, 1, false);
	assert(g.poll_timer.arms == 1);
	psi_schedule_poll_work(&g, 20, true);
	assert(g.poll_timer.arms == 2);
	g.polling_until = 1000;
	g.polling_next_update = 700;
	simulated_now = 600;
	psi_poll_work(&g);
	assert(gate_at_collect == 1 && g.poll_timer.arms == 3);
	simulated_now = 1001;
	arms = g.poll_timer.arms;
	psi_poll_work(&g);
	assert(gate_at_collect == 0 && !g.poll_scheduled);
	assert(g.poll_timer.arms == arms && g.polling_next_update == ULLONG_MAX);
	simulated_changed_states = 1U << PSI_MEM_SOME;
	g.poll_states = simulated_changed_states;
	psi_poll_work(&g);
	assert(g.polling_until == 1101 && g.poll_scheduled);
	assert(g.poll_timer.arms == arms + 1);
	g.poll_task = NULL;
	psi_schedule_poll_work(&g, 1, true);
	assert(!g.poll_scheduled && g.poll_timer.arms == arms + 1);
	puts("ok 4 - PSI duplicate wakeup gate, expiry, restart and detached worker");
}

int main(void)
{
	puts("TAP version 13\n1..4");
	test_fullness();
	test_selection();
	test_events();
	test_poll_gate();
	return 0;
}
