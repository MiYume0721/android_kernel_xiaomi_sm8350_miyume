/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_CLOSE_RANGE_H
#define _UAPI_LINUX_CLOSE_RANGE_H

/*
 * close_range() flags.
 */
#define CLOSE_RANGE_UNSHARE	(1 << 1)	/* Unshare the file descriptor table before closing file descriptors */
#define CLOSE_RANGE_CLOEXEC	(1 << 2)	/* Set the FD_CLOEXEC flag instead of the closing file descriptors */

#endif /* _UAPI_LINUX_CLOSE_RANGE_H */
