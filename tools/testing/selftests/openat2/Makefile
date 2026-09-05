# SPDX-License-Identifier: GPL-2.0-or-later
CFLAGS += -Wall -O2 -g
TEST_GEN_PROGS := openat2_test venus_fs_compat_test rename_attack_test
include ../lib.mk

$(OUTPUT)/openat2_test: openat2_test.c helpers.c helpers.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ openat2_test.c helpers.c $(LDLIBS)

$(OUTPUT)/rename_attack_test: rename_attack_test.c helpers.c helpers.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ rename_attack_test.c helpers.c $(LDLIBS)
