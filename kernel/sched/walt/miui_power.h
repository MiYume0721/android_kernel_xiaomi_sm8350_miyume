/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _WALT_MIUI_POWER_H
#define _WALT_MIUI_POWER_H

/* Xiaomi popsicle-w-oss packing limits, in scheduler utilization units. */
#define MIUI_PACKING_IDLE_UTIL	30
#define MIUI_PACKING_CLUSTER_PCT	40
#define MIUI_PACKING_FREQ_SCALE	450

/*
 * Preserve each cluster's userspace policy and hysteresis.  In particular,
 * zero and out-of-range thresholds may intentionally keep CPUs active.
 * A 60/30 baseline becomes 70/40, as in Xiaomi's power-enhance policy.
 */
static inline void miui_core_ctl_adjust_thresholds(unsigned int *up,
						 unsigned int *down)
{
	unsigned int delta;

	if (!*up || !*down || *up >= 100 || *down >= *up)
		return;

	delta = 100 - *up;
	if (delta > 10)
		delta = 10;
	*up += delta;
	*down += delta;
}

#endif /* _WALT_MIUI_POWER_H */
