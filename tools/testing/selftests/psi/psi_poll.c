// SPDX-License-Identifier: GPL-2.0
/* Private PSI triggers only: no LMKD configuration or memory pressure changes. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <time.h>
#include <unistd.h>
#include "../kselftest.h"

static const char *path = "/proc/pressure/cpu";
static const char *spec = "some 10000 500000\n";

static uint64_t now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int trigger(const char *config)
{
	int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	int saved;

	if (fd < 0)
		return -1;
	if (write(fd, config, strlen(config)) == (ssize_t)strlen(config))
		return fd;
	saved = errno;
	close(fd);
	errno = saved;
	return -1;
}

static void result(bool ok, const char *name)
{
	if (ok)
		ksft_test_result_pass("%s\n", name);
	else
		ksft_test_result_fail("%s (errno=%d: %s)\n",
				      name, errno, strerror(errno));
}

static void reject(const char *config, const char *name)
{
	int fd = trigger(config);

	result(fd < 0 && errno == EINVAL, name);
	if (fd >= 0)
		close(fd);
}

struct churn_ctx {
	int errors;
};

static void *churn(void *arg)
{
	struct churn_ctx *ctx = arg;
	int i;

	for (i = 0; i < 32; i++) {
		struct epoll_event ev = { .events = EPOLLPRI };
		int fd = trigger(i & 1 ? spec : "some 20000 1000000\n");
		int ep = epoll_create1(EPOLL_CLOEXEC);

		if (fd < 0 || ep < 0 || epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev))
			ctx->errors++;
		/* Closing a registered fd must safely detach its waitqueue. */
		if (fd >= 0)
			close(fd);
		if (ep >= 0) {
			if (epoll_wait(ep, &ev, 1, 0) < 0)
				ctx->errors++;
			close(ep);
		}
	}
	return NULL;
}

struct load_ctx {
	int cpu;
	int error;
	uint64_t until;
};

static void *cpu_load(void *arg)
{
	struct load_ctx *ctx = arg;
	cpu_set_t mask;
	unsigned long work = 1;
	int i;

	CPU_ZERO(&mask);
	CPU_SET(ctx->cpu, &mask);
	if (sched_setaffinity(0, sizeof(mask), &mask)) {
		ctx->error = errno;
		return NULL;
	}
	/* Two normal-priority workers contend on one CPU for at most 2 s. */
	while (now_ms() < ctx->until) {
		for (i = 0; i < 10000; i++) {
			work = work * 1664525 + 1013904223;
			asm volatile("" : "+r" (work));
		}
	}
	return NULL;
}

int main(int argc, char **argv)
{
	struct epoll_event ev = { .events = EPOLLPRI };
	struct pollfd pfd;
	struct churn_ctx churns[2] = { 0 };
	struct load_ctx loads[2] = { 0 };
	pthread_t workers[2];
	cpu_set_t allowed;
	uint64_t end, last_poll = 0, last_epoll = 0;
	unsigned int poll_events = 0, epoll_events = 0;
	bool poll_ok = true, epoll_ok = true, rate_ok = true;
	int fd, epfd, ep, cpu, i, started;
	char buf[512];

	if (argc == 2) {
		path = argv[1];
	} else if (argc != 1) {
		fprintf(stderr, "usage: %s [cpu.pressure path]\n", argv[0]);
		return 1;
	}
	ksft_print_header();
	fd = trigger(spec);
	if (fd < 0) {
		if (errno == EACCES || errno == EPERM || errno == ENOENT ||
		    errno == EOPNOTSUPP)
			ksft_exit_skip("PSI trigger unavailable: %s\n", strerror(errno));
		ksft_exit_fail_msg("cannot create PSI trigger: %s\n", strerror(errno));
	}
	ksft_set_plan(11);
	result(pread(fd, buf, sizeof(buf), 0) > 0, "pressure file readable");
	result(write(fd, spec, strlen(spec)) == -1 && errno == EBUSY,
	       "second trigger on same open file rejected");
	close(fd);
	reject("invalid\n", "malformed trigger rejected");
	reject("some 0 500000\n", "zero threshold rejected");
	reject("some 500001 500000\n", "threshold above window rejected");
	reject("some 1 499999\n", "window below 500 ms rejected");
	reject("some 1 10000001\n", "window above 10 s rejected");

	/* No permanent trigger here: exercise last-close vs first-create too. */
	for (started = 0; started < 2; started++)
		if (pthread_create(&workers[started], NULL, churn, &churns[started]))
			break;
	for (i = 0; i < started; i++)
		pthread_join(workers[i], NULL);
	result(started == 2 && !churns[0].errors && !churns[1].errors,
	       "concurrent trigger creation and epoll close (64 iterations)");

	CPU_ZERO(&allowed);
	if (sched_getaffinity(0, sizeof(allowed), &allowed))
		ksft_exit_fail_msg("sched_getaffinity failed\n");
	for (cpu = 0; cpu < CPU_SETSIZE; cpu++)
		if (CPU_ISSET(cpu, &allowed))
			break;
	if (cpu == CPU_SETSIZE)
		ksft_exit_fail_msg("no allowed CPU\n");
	fd = trigger(spec);
	epfd = trigger(spec);
	ep = epoll_create1(EPOLL_CLOEXEC);
	if (fd < 0 || epfd < 0 || ep < 0 ||
	    epoll_ctl(ep, EPOLL_CTL_ADD, epfd, &ev))
		ksft_exit_fail_msg("poll setup failed\n");
	pfd.fd = fd;
	pfd.events = POLLPRI;
	end = now_ms() + 2000;
	for (started = 0; started < 2; started++) {
		loads[started].cpu = cpu;
		loads[started].until = end;
		if (pthread_create(&workers[started], NULL, cpu_load, &loads[started]))
			break;
	}
	while (now_ms() < end) {
		int ret = poll(&pfd, 1, 20);
		uint64_t now = now_ms();

		if (ret < 0 && errno != EINTR)
			poll_ok = false;
		if (ret > 0 && (pfd.revents & (POLLERR | POLLHUP)))
			poll_ok = false;
		if (ret > 0 && (pfd.revents & POLLPRI)) {
			/* Allow one poll timeout of user-space timestamp jitter. */
			if (last_poll && now - last_poll < 475)
				rate_ok = false;
			last_poll = now;
			poll_events++;
		}
		ret = epoll_wait(ep, &ev, 1, 0);
		if (ret < 0 && errno != EINTR)
			epoll_ok = false;
		if (ret > 0 && (ev.events & (EPOLLERR | EPOLLHUP)))
			epoll_ok = false;
		if (ret > 0 && (ev.events & EPOLLPRI)) {
			if (last_epoll && now - last_epoll < 475)
				rate_ok = false;
			last_epoll = now;
			epoll_events++;
		}
	}
	for (i = 0; i < started; i++)
		pthread_join(workers[i], NULL);
	close(fd);
	close(epfd);
	close(ep);
	ksft_print_msg("events: poll=%u epoll=%u; load threads=%d\n",
		       poll_events, epoll_events, started);
	result(started == 2 && !loads[0].error && !loads[1].error &&
	       poll_ok && poll_events >= 2, "poll receives repeated CPU pressure");
	result(epoll_ok && epoll_events >= 2, "epoll receives repeated CPU pressure");
	result(rate_ok && poll_events && epoll_events,
	       "trigger events remain rate limited");
	if (ksft_get_fail_cnt())
		ksft_exit_fail();
	ksft_exit_pass();
}
