# PSI / zsmalloc backport tests

`psi_poll.c` is an on-device test. It opens only its own pressure triggers,
checks invalid parameters and EBUSY, races trigger creation/close in two
threads, then checks independent poll/epoll notifications and rate limiting.
The CPU load is limited to two normal-priority threads contending on one
allowed CPU for two seconds. It does not create memory pressure, change
LMKD settings, change existing triggers, or modify zram/swap.

Run as root on the target kernel (the backport retains the 500 ms to 10 s
trigger ABI). An optional argument selects a private cgroup's `cpu.pressure`
file; the test does not create/remove cgroups or move tasks, so the caller
must already be in that cgroup for the CPU-load portion. This is not a
cgroup-removal race test. On newer kernels, an unprivileged caller may be
subject to different trigger window restrictions.

```sh
make -C tools/testing/selftests/psi
./tools/testing/selftests/psi/psi_poll
```

For Android, compile `psi_poll.c` with NDK ARM64/ARM32 clang, `-static -pthread`,
then run the corresponding binary using `su -c`. Restart host ADB before
connecting this Venus device. Do not treat execution on the old kernel as
validation of the new image.

## Host-only source logic tests

These tests extract selected functions and zsmalloc structures directly
from the current checkout. They check occupancy boundaries, packed fields,
list/counter transitions, allocation/compaction selection, PSI deferred
events and poll gating. Lists, waitqueues, timers, locks and atomics use
single-threaded test substitutes. Thus these tests do **not** prove actual
RCU/timer concurrency, kernel memory safety, compressed data integrity,
power savings, or runtime allocator performance.

From the kernel root, set `TEST_OUT` to a dedicated existing test-output
directory (not the source directory):

```sh
perl tools/testing/selftests/psi/extract_logic.pl > "$TEST_OUT/logic_generated.h"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-sign-compare -Wno-pointer-sign \
  -fsanitize=address,undefined -I "$TEST_OUT" \
  tools/testing/selftests/psi/logic_test.c -o "$TEST_OUT/logic_test"
"$TEST_OUT/logic_test"
```

`extract_logic.pl` and `logic_test.c` are developer source-tree tests, not
installed kselftests. The top-level `psi` target installs only `psi_poll`.

After flashing, also rerun the existing syscall regression tests and observe
LMKD/PSI logs, zram `mm_stat`, workload performance, suspend/AOD/FOD and
unplugged standby. Do not compact/reset/swapoff the phone's active `zram0`
for a synthetic correctness test. A data-integrity test should use a separate
temporary zram device and remove only that device afterwards.
