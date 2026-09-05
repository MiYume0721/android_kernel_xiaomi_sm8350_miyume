// SPDX-License-Identifier: GPL-2.0
/* Host-only policy test; the implementation is extracted from compaction.c. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define min(a, b) ((a) < (b) ? (a) : (b))
#include "compaction_idle_impl.h"

int main(void)
{
	unsigned int low, score, prev, shift, forced, reclaiming, actual;
	uint64_t cases = 0;
	unsigned int expected_delay[] = { 500, 1000, 2000, 2000, 2000 };
	unsigned int i;

	_Static_assert(COMPACT_IDLE_MAX_DEFER_SHIFT == 2, "2 s idle check cap");
	puts("TAP version 13\n1..3");
	for (low = 0; low <= 100; low++)
		for (score = 0; score <= 100; score++)
			for (prev = 0; prev <= 101; prev++)
				for (shift = 0; shift <= COMPACT_IDLE_MAX_DEFER_SHIFT; shift++)
					for (forced = 0; forced <= 1; forced++)
						for (reclaiming = 0; reclaiming <= 1; reclaiming++) {
							actual = proactive_compact_idle_shift(shift,
									score, prev, low, forced, reclaiming);
							assert(actual <= COMPACT_IDLE_MAX_DEFER_SHIFT);
							if (forced || reclaiming || score > low || score != prev)
								assert(actual == 0);
							else
								assert(actual == (shift == 2 ? 2 : shift + 1));
							cases++;
						}
	printf("ok 1 - %llu idle policy input combinations\n", (unsigned long long)cases);

	shift = 0;
	prev = 101;
	for (i = 0; i < sizeof(expected_delay) / sizeof(expected_delay[0]); i++) {
		shift = proactive_compact_idle_shift(shift, 20, prev, 80, false, false);
		assert((500U << shift) == expected_delay[i]);
		prev = 20;
	}
	puts("ok 2 - stable low fragmentation: 500, 1000, 2000 ms then capped");

	assert(!proactive_compact_idle_shift(2, 21, 20, 80, false, false));
	assert(!proactive_compact_idle_shift(2, 81, 81, 80, false, false));
	assert(!proactive_compact_idle_shift(2, 20, 20, 80, true, false));
	assert(!proactive_compact_idle_shift(2, 20, 20, 80, false, true));
	/* Demand work resets prev to its sentinel before the next idle check. */
	assert(!proactive_compact_idle_shift(0, 20, 101, 80, false, false));
	puts("ok 3 - score change, high score, forced request and reclaim reset backoff");
	return 0;
}
