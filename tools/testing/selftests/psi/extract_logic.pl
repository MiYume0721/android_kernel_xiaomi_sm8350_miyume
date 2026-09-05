#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0
# Emit the actual checkout functions for a single-threaded host logic test.
use strict;
use warnings;
use FindBin;

my $root = "$FindBin::Bin/../../../..";
sub read_source {
    my ($path) = @_;
    open my $fh, '<', "$root/$path" or die "$path: $!";
    local $/;
    return <$fh>;
}
sub block {
    my ($src, $name) = @_;
    $src =~ /(^\Q$name\E\s*\{.*?^\};)/ms or die "missing $name";
    print "$1\n";
}
sub function {
    my ($src, $name) = @_;
    $src =~ /(^static\s+[^;{]*\b\Q$name\E\s*\([^;{]*\)\s*\n\{.*?^\})/ms
        or die "missing function $name";
    print "$1\n";
}

my $zs = read_source('mm/zsmalloc.c');
my $pt = read_source('include/linux/psi_types.h');
my $psi = read_source('kernel/sched/psi.c');
print "/* Generated from mm/zsmalloc.c and kernel/sched/psi.c. */\n";
for my $macro (qw(ZSPAGE_MAGIC FULLNESS_BITS CLASS_BITS ISOLATED_BITS MAGIC_VAL_BITS)) {
    $zs =~ /(^#define\s+\Q$macro\E\s+[^\n]+)/m or die "missing $macro";
    print "$1\n";
}
for my $type ('enum fullness_group', 'enum zs_stat_type',
              'struct zs_size_stat', 'struct size_class', 'struct zspage') {
    block($zs, $type);
}
for my $fn (qw(is_zspage_isolated get_zspage_inuse get_zspage_mapping
               set_zspage_mapping zs_stat_inc zs_stat_dec zs_stat_get
               get_fullness_group insert_zspage remove_zspage fix_fullness_group
               find_get_zspage isolate_zspage putback_zspage)) {
    function($zs, $fn);
}
for my $type ('enum psi_states', 'enum psi_aggregators',
              'struct psi_window', 'struct psi_trigger') {
    block($pt, $type);
}
print <<'STUBS';
/* Only the fields used by the tested functions; no scheduler emulation. */
struct psi_group {
    int trigger_lock;
    struct list_head triggers;
    u64 total[NR_PSI_AGGREGATORS][NR_PSI_STATES - 1];
    u64 polling_total[NR_PSI_STATES - 1];
    u64 poll_min_period, polling_next_update, polling_until;
    u32 poll_states;
    int poll_scheduled;
    struct task_struct *poll_task;
    struct timer_list poll_timer;
};
static void collect_percpu_times(struct psi_group *g, int aggregator, u32 *states)
{
    (void)aggregator;
    gate_at_collect = g->poll_scheduled;
    *states = simulated_changed_states;
}
#define trace_event_helper(g) ((void)(g))
#define UPDATES_PER_WINDOW 10
STUBS
for my $fn (qw(window_reset window_update init_triggers update_triggers
               psi_schedule_poll_work psi_poll_work)) {
    function($psi, $fn);
}
