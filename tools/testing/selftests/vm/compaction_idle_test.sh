#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Developer source-tree test, not an installed kselftest or device stress test.
set -eu
test_src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_root=$(CDPATH= cd -- "$test_src/../../../.." && pwd)
test_tmp=$(mktemp -d "${TMPDIR:-/tmp}/compaction-idle-test.XXXXXX")
trap 'rm -f "$test_tmp/compaction_idle_impl.h" "$test_tmp/compaction_idle_test"; rmdir "$test_tmp"' EXIT
trap 'exit 1' HUP INT TERM

perl -0777 -ne '
  /(^#define\s+COMPACT_IDLE_MAX_DEFER_SHIFT\s+[^\n]+)/m
    or die "missing idle backoff limit";
  print "$1\n";
  /(^static unsigned int proactive_compact_idle_shift\([^;{]*\)\s*\n\{.*?^\})/ms
    or die "missing idle policy function";
  print "$1\n";
' "$test_root/mm/compaction.c" > "$test_tmp/compaction_idle_impl.h"

"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I "$test_tmp" "$test_src/compaction_idle_test.c" -o "$test_tmp/compaction_idle_test"
"$test_tmp/compaction_idle_test"
