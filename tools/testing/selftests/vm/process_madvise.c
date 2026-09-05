// SPDX-License-Identifier: GPL-2.0-only
/*
 * Exercise native and compat iovec import on this process's own mappings.
 * Build for both arm64 and arm32 to cover a 64-bit kernel's compat path.
 * --legacy requires booting this tree with process_madvise_abi=legacy;
 * it is not an automatic fallback from the upstream calling convention.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

#include "../kselftest.h"

#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif
#ifndef SYS_process_madvise
#define SYS_process_madvise 440
#endif
#ifndef MADV_COLD
#define MADV_COLD 20
#endif

static bool legacy;

static long advise(int fd, const struct iovec *vec, size_t count,
		   int behavior, unsigned int flags)
{
	if (legacy)
		/* P_PIDFD = 3 in the six-argument legacy interface. */
		return syscall(SYS_process_madvise, 3, fd, vec, count,
			       behavior, flags);
	return syscall(SYS_process_madvise, fd, vec, count, behavior, flags);
}

static void expect(long ret, long expected, int error, const char *name)
{
	int saved_errno = errno;

	if (ret == expected && (ret != -1 || saved_errno == error))
		ksft_test_result_pass("%s\n", name);
	else
		ksft_test_result_fail("%s: ret=%ld expected=%ld errno=%d expected_errno=%d\n",
				      name, ret, expected,
				      ret == -1 ? saved_errno : 0, error);
}

int main(int argc, char **argv)
{
	struct iovec vec[9], *edge;
	unsigned char *data, *guard;
	size_t ps = sysconf(_SC_PAGESIZE), i;
	long ret;
	int fd;
	bool intact = true;

	if (argc == 2 && !strcmp(argv[1], "--legacy"))
		legacy = true;
	else if (argc != 1) {
		fprintf(stderr, "Usage: %s [--legacy]\n", argv[0]);
		return KSFT_FAIL;
	}

	ksft_print_header();
	ksft_print_msg("ABI=%zu-bit iovec=%zu mode=%s; private memory only\n",
		       sizeof(void *) * 8, sizeof(struct iovec),
		       legacy ? "legacy P_PIDFD" : "upstream");
	alarm(15);
	fd = syscall(SYS_pidfd_open, getpid(), 0);
	if (fd < 0 && errno == ENOSYS)
		ksft_exit_skip("pidfd_open unavailable\n");
	if (fd < 0)
		ksft_exit_fail_msg("pidfd_open: %s\n", strerror(errno));

	/* No iovec data needed: distinguish missing privilege from bad layout. */
	ret = advise(fd, NULL, 0, MADV_COLD, 0);
	if (ret < 0 && (errno == ENOSYS || errno == EPERM || errno == EACCES)) {
		close(fd);
		ksft_exit_skip("process_madvise unavailable or denied: %s\n",
			       strerror(errno));
	}
	if (ret != 0)
		ksft_exit_fail_msg("zero-vector probe: ret=%ld errno=%d; check ABI mode\n",
				   ret, errno);

	data = mmap(NULL, 9 * ps, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	guard = mmap(NULL, 2 * ps, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (data == MAP_FAILED || guard == MAP_FAILED)
		ksft_exit_fail_msg("mmap: %s\n", strerror(errno));
	if (mprotect(guard + ps, ps, PROT_NONE))
		ksft_exit_fail_msg("guard page: %s\n", strerror(errno));
	memset(data, 0x5a, 9 * ps);
	memset(vec, 0, sizeof(vec));
	for (i = 0; i < 9; i++) {
		vec[i].iov_base = data + i * ps;
		vec[i].iov_len = ps;
	}

	ksft_set_plan(16);
	expect(advise(fd, vec, 1, MADV_COLD, 0), ps, 0,
	       "one native-layout iovec");
	expect(advise(fd, vec, 2, MADV_COLD, 0), 2 * ps, 0,
	       "two iovecs preserve element stride");
	expect(advise(fd, vec, 9, MADV_COLD, 0), 9 * ps, 0,
	       "nine iovecs use allocated import buffer");
	expect(advise(fd, NULL, 0, MADV_COLD, 0), 0, 0,
	       "empty vector");
	expect(advise(fd, (void *)-1, 1, MADV_COLD, 0), -1, EFAULT,
	       "unreadable vector pointer");
	expect(advise(fd, vec, 1025, MADV_COLD, 0), -1, EINVAL,
	       "reject count above UIO_MAXIOV");
	expect(advise(fd, vec, 1, MADV_COLD, 1), -1, EINVAL,
	       "reject invalid flags");
	expect(advise(-1, vec, 1, MADV_COLD, 0), -1,
	       legacy ? EINVAL : EBADF,
	       "reject bad pidfd");
	expect(advise(fd, vec, 1, MADV_NORMAL, 0), -1, EINVAL,
	       "reject unsupported advice");
	vec[0].iov_base = data + 1;
	expect(advise(fd, vec, 1, MADV_COLD, 0), -1, EINVAL,
	       "reject unaligned target address");
	vec[0].iov_base = data;
	vec[0].iov_len = SIZE_MAX;
	expect(advise(fd, vec, 1, MADV_COLD, 0), -1, EINVAL,
	       "reject negative signed iov_len");
	vec[0].iov_len = ps;

	/* An ARM32 element is 8 bytes, not the native kernel's 16 bytes. */
	edge = (struct iovec *)(guard + ps - sizeof(*edge));
	*edge = vec[0];
	expect(advise(fd, edge, 1, MADV_COLD, 0), ps, 0,
	       "iovec ends exactly at readable page boundary");
	expect(advise(fd, (void *)((char *)edge + 1), 1, MADV_COLD, 0),
	       -1, EFAULT, "truncated iovec crosses guard page");
	if (mprotect(guard, ps, PROT_READ))
		ksft_exit_fail_msg("read-only vector: %s\n", strerror(errno));
	expect(advise(fd, edge, 1, MADV_COLD, 0), ps, 0,
	       "vector requires no write permission");
	expect(advise(fd, (void *)(guard + ps), 1, MADV_COLD, 0), -1, EFAULT,
	       "PROT_NONE vector rejected");
	for (i = 0; i < 9 * ps; i++)
		intact &= data[i] == 0x5a;
	if (intact)
		ksft_test_result_pass("all target contents preserved\n");
	else
		ksft_test_result_fail("all target contents preserved\n");

	munmap(data, 9 * ps);
	munmap(guard, 2 * ps);
	close(fd);
	if (ksft_get_fail_cnt())
		return ksft_exit_fail();
	return ksft_exit_pass();
}
