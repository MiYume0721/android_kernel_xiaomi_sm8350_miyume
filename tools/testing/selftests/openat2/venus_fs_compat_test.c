// SPDX-License-Identifier: GPL-2.0-only
/* Raw-syscall regression checks for the Venus 5.4 VFS backports. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../kselftest.h"
#include "../../../../include/uapi/linux/openat2.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#ifndef SYS_openat2
#define SYS_openat2 437
#endif
#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif
#ifndef SYS_pidfd_getfd
#define SYS_pidfd_getfd 438
#endif
#ifndef SYS_faccessat2
#define SYS_faccessat2 439
#endif

static unsigned int tests;

static void check(bool ok, const char *name)
{
	tests++;
	if (ok)
		ksft_test_result_pass("%s\n", name);
	else
		ksft_test_result_fail("%s (errno=%d)\n", name, errno);
}

static void skip(const char *name)
{
	tests++;
	ksft_test_result_skip("%s\n", name);
}

static int open2(int dfd, const char *path, unsigned long flags,
		 unsigned int resolve)
{
	struct open_how how = { .flags = flags, .resolve = resolve };
	int fd, retries = 128;

	do {
		fd = syscall(SYS_openat2, dfd, path, &how, sizeof(how));
	} while (fd < 0 && errno == EAGAIN && --retries);
	return fd;
}

static void path_tests(int dfd)
{
	static const struct {
		const char *name, *path;
		unsigned long flags;
		unsigned int resolve;
		int error;
	} cases[] = {
		{ "beneath file", "file", O_RDONLY, RESOLVE_BENEATH, 0 },
		{ "beneath local dotdot", "dir/../file", O_RDONLY, RESOLVE_BENEATH, 0 },
		{ "beneath escape", "../file", O_RDONLY, RESOLVE_BENEATH, EXDEV },
		{ "beneath absolute", "/file", O_RDONLY, RESOLVE_BENEATH, EXDEV },
		{ "beneath absolute symlink", "abslink", O_RDONLY, RESOLVE_BENEATH, EXDEV },
		{ "in-root absolute", "/file", O_RDONLY, RESOLVE_IN_ROOT, 0 },
		{ "in-root dotdot", "../../file", O_RDONLY, RESOLVE_IN_ROOT, 0 },
		{ "in-root absolute symlink", "abslink", O_RDONLY, RESOLVE_IN_ROOT, 0 },
		{ "no-symlinks rejects link", "link", O_RDONLY, RESOLVE_NO_SYMLINKS, ELOOP },
		{ "no-symlinks rejects intermediate link", "dirlink/../file", O_RDONLY, RESOLVE_NO_SYMLINKS, ELOOP },
		{ "no-symlinks O_PATH nofollow", "link", O_PATH | O_NOFOLLOW, RESOLVE_NO_SYMLINKS, 0 },
		{ "no-magiclinks allows normal symlink", "link", O_RDONLY, RESOLVE_NO_MAGICLINKS, 0 },
		{ "conflicting scope flags", "file", O_RDONLY, RESOLVE_BENEATH | RESOLVE_IN_ROOT, EINVAL },
	};
	unsigned int i;
	int fd, rootfd;
	char magic[64];
	struct open_how how = { .flags = O_CREAT | O_DIRECTORY, .mode = 0600 };
	struct stat st;

	fd = syscall(SYS_openat2, dfd, "must-not-exist", &how, sizeof(how));
	check(fd < 0 && errno == EINVAL, "O_CREAT O_DIRECTORY rejected");
	if (fd >= 0)
		close(fd);
	check(fstatat(dfd, "must-not-exist", &st, 0) < 0 && errno == ENOENT,
	      "invalid directory create has no side effect");
	unlinkat(dfd, "must-not-exist", 0);
	fd = openat(dfd, "file", O_PATH | O_RDWR);
	check(fd >= 0 && (fcntl(fd, F_GETFL) & O_PATH),
	      "legacy openat still ignores extra O_PATH flags");
	if (fd >= 0)
		close(fd);
	fd = syscall(SYS_openat2, dfd, "file", (void *)-1, sizeof(how));
	check(fd < 0 && errno == EFAULT, "openat2 bad userspace pointer");
	fd = syscall(SYS_openat2, dfd, "file", &how, sysconf(_SC_PAGESIZE) + 1);
	check(fd < 0 && errno == E2BIG, "openat2 oversized userspace structure");

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		struct stat expected;
		bool ok;
		const char *target = "file";

		if (cases[i].flags & O_NOFOLLOW)
			target = "link";

		fd = open2(dfd, cases[i].path, cases[i].flags, cases[i].resolve);
		if (cases[i].error)
			ok = fd < 0 && errno == cases[i].error;
		else
			ok = fd >= 0 && !fstat(fd, &st) &&
			     !fstatat(dfd, target, &expected, AT_SYMLINK_NOFOLLOW) &&
			     st.st_ino == expected.st_ino && st.st_dev == expected.st_dev;
		check(ok, cases[i].name);
		if (fd >= 0)
			close(fd);
	}
	snprintf(magic, sizeof(magic), "/proc/self/fd/%d/file", dfd);
	fd = open2(AT_FDCWD, magic, O_RDONLY, RESOLVE_NO_MAGICLINKS);
	check(fd < 0 && errno == ELOOP, "proc magic link denied");
	if (fd >= 0)
		close(fd);
	rootfd = open("/", O_PATH | O_DIRECTORY);
	fd = open2(rootfd, "proc/self", O_PATH, RESOLVE_NO_XDEV);
	check(fd < 0 && errno == EXDEV, "NO_XDEV rejects proc mount");
	if (fd >= 0)
		close(fd);
	close(rootfd);
}

static void access_tests(int dfd, int filefd)
{
	int ret, status;
	pid_t child;

	ret = syscall(SYS_faccessat2, dfd, "file", R_OK, 0);
	check(ret == 0, "faccessat2 real-id access");
	ret = syscall(SYS_faccessat2, filefd, "", R_OK, AT_EMPTY_PATH);
	check(ret == 0, "faccessat2 empty path uses fd");
	ret = syscall(SYS_faccessat2, dfd, "dangling", F_OK, AT_SYMLINK_NOFOLLOW);
	check(ret == 0, "faccessat2 nofollow accepts dangling symlink");
	ret = syscall(SYS_faccessat2, dfd, "dangling", F_OK, 0);
	check(ret < 0 && errno == ENOENT, "faccessat2 follows dangling symlink");
	ret = syscall(SYS_faccessat2, dfd, "file", R_OK, 0x40000000);
	check(ret < 0 && errno == EINVAL, "faccessat2 invalid flags");
	ret = syscall(SYS_faccessat2, dfd, "file", 8, 0);
	check(ret < 0 && errno == EINVAL, "faccessat2 invalid mode");
	if (geteuid()) {
		skip("real/effective UID split requires root");
		return;
	}
	fflush(NULL);
	child = fork();
	if (!child) {
		if (setresuid(65534, 0, 0))
			_exit(4);
		ret = syscall(SYS_faccessat2, filefd, "", R_OK, AT_EMPTY_PATH);
		if (ret != -1 || errno != EACCES)
			_exit(1);
		ret = syscall(SYS_faccessat2, filefd, "", R_OK,
			      AT_EMPTY_PATH | AT_EACCESS);
		_exit(ret ? 2 : 0);
	}
	if (child < 0 || waitpid(child, &status, 0) != child) {
		check(false, "fork UID test");
		return;
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 4)
		skip("setresuid denied by environment");
	else
		check(WIFEXITED(status) && !WEXITSTATUS(status),
		      "real/effective UID distinction preserves DAC");
}

static void pidfd_tests(int filefd)
{
	int pidfd, fd, ret, pair[2];
	char byte = 0;

	pidfd = syscall(SYS_pidfd_open, getpid(), 0);
	if (pidfd < 0) {
		check(false, "pidfd_open self");
		return;
	}
	fd = syscall(SYS_pidfd_getfd, pidfd, filefd, 0);
	if (fd < 0 && (errno == EPERM || errno == EACCES)) {
		skip("pidfd_getfd self blocked by environment security policy");
		close(pidfd);
		return;
	}
	check(fd >= 0, "pidfd_getfd self");
	if (fd >= 0) {
		ret = fcntl(fd, F_GETFD);
		check(ret >= 0 && (ret & FD_CLOEXEC), "received fd is CLOEXEC");
		ret = lseek(filefd, 1, SEEK_SET);
		check(ret == 1 && read(fd, &byte, 1) == 1 && byte == 'b' &&
		      lseek(filefd, 0, SEEK_CUR) == 2, "received fd shares file offset");
		close(fd);
	}
	ret = syscall(SYS_pidfd_getfd, pidfd, filefd, 1);
	check(ret < 0 && errno == EINVAL, "pidfd_getfd invalid flags");
	ret = syscall(SYS_pidfd_getfd, -1, filefd, 0);
	check(ret < 0 && errno == EBADF, "pidfd_getfd invalid pidfd");
	ret = syscall(SYS_pidfd_getfd, pidfd, -1, 0);
	check(ret < 0 && errno == EBADF, "pidfd_getfd missing target fd");
	ret = syscall(SYS_pidfd_getfd, filefd, filefd, 0);
	check(ret < 0 && errno == EBADF, "pidfd_getfd rejects ordinary fd");
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) {
		check(false, "socketpair setup");
	} else {
		fd = syscall(SYS_pidfd_getfd, pidfd, pair[0], 0);
		check(fd >= 0, "pidfd_getfd socket");
		if (fd >= 0) {
			ret = write(pair[1], "s", 1);
			check(ret == 1 && recv(fd, &byte, 1, MSG_DONTWAIT) == 1 &&
			      byte == 's', "received socket remains usable");
			close(fd);
		}
		close(pair[0]);
		close(pair[1]);
	}
	close(pidfd);
}

int main(void)
{
	char scratch[PATH_MAX];
	const char *tmpdir = getenv("TMPDIR");
	int dfd, fd;

	ksft_print_header();
	snprintf(scratch, sizeof(scratch), "%s/venus-fs-compat.XXXXXX",
		 tmpdir ? tmpdir : "/tmp");
	if (!mkdtemp(scratch))
		ksft_exit_fail_msg("mkdtemp failed: %s\n", strerror(errno));
	dfd = open(scratch, O_RDONLY | O_DIRECTORY);
	fd = openat(dfd, "file", O_CREAT | O_EXCL | O_RDWR, 0600);
	if (fd < 0 || write(fd, "abc", 3) != 3 || mkdirat(dfd, "dir", 0700) ||
	    symlinkat("file", dfd, "link") || symlinkat("/file", dfd, "abslink") ||
	    symlinkat("dir", dfd, "dirlink") || symlinkat("missing", dfd, "dangling"))
		ksft_exit_fail_msg("fixture setup failed: %s\n", strerror(errno));
	path_tests(dfd);
	access_tests(dfd, fd);
	pidfd_tests(fd);
	close(fd);
	unlinkat(dfd, "link", 0);
	unlinkat(dfd, "abslink", 0);
	unlinkat(dfd, "dirlink", 0);
	unlinkat(dfd, "dangling", 0);
	unlinkat(dfd, "file", 0);
	unlinkat(dfd, "dir", AT_REMOVEDIR);
	close(dfd);
	rmdir(scratch);
	ksft_set_plan(tests);
	return ksft_get_fail_cnt() ? KSFT_FAIL : KSFT_PASS;
}
