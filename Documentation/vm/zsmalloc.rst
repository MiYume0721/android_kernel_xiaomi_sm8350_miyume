.. _zsmalloc:

========
zsmalloc
========

This allocator is designed for use with zram. Thus, the allocator is
supposed to work well under low memory conditions. In particular, it
never attempts higher order page allocation which is very likely to
fail under memory pressure. On the other hand, if we just use single
(0-order) pages, it would suffer from very high fragmentation --
any object of size PAGE_SIZE/2 or larger would occupy an entire page.
This was one of the major issues with its predecessor (xvmalloc).

To overcome these issues, zsmalloc allocates a bunch of 0-order pages
and links them together using various 'struct page' fields. These linked
pages act as a single higher-order page i.e. an object can span 0-order
page boundaries. The code refers to these linked pages as a single entity
called zspage.

For simplicity, zsmalloc can only allocate objects of size up to PAGE_SIZE
since this satisfies the requirements of all its current users (in the
worst case, page is incompressible and is thus stored "as-is" i.e. in
uncompressed form). For allocation requests larger than this size, failure
is returned (see zs_malloc).

Additionally, zs_malloc() does not return a dereferenceable pointer.
Instead, it returns an opaque handle (unsigned long) which encodes actual
location of the allocated object. The reason for this indirection is that
zsmalloc does not keep zspages permanently mapped since that would cause
issues on 32-bit systems where the VA region for kernel space mappings
is very small. So, before using the allocating memory, the object has to
be mapped using zs_map_object() to get a usable pointer and subsequently
unmapped using zs_unmap_object().

stat
====

With CONFIG_ZSMALLOC_STAT, we could see zsmalloc internal information via
``/sys/kernel/debug/zsmalloc/<user name>/classes``. The header is::

 # cat /sys/kernel/debug/zsmalloc/zram0/classes

 class size 10% 20% 30% 40% 50% 60% 70% 80% 90% 99% 100% obj_allocated obj_used pages_used pages_per_zspage freeable


class
	index
size
	object size zspage stores
10%, 20%, ..., 90%, 99%, 100%
	the number of zspages in each non-empty fullness group (see below)
obj_allocated
	the number of objects allocated
obj_used
	the number of objects allocated to the user
pages_used
	the number of pages allocated for the class
pages_per_zspage
	the number of 0-order pages to make a zspage
freeable
	the estimated number of pages compaction could free

Fullness groups
===============

There are twelve groups: empty, ten partial-use groups, and full. Let n be
the number of live objects and N the capacity of a zspage. Empty (n == 0)
and full (n == N) are handled separately. For partial pages, the group index
is ``(100 * n / N) / 10 + 1``, with integer division, matching Linux 6.6.

The ``10%`` column covers non-empty pages below 10% occupancy, ``20%``
covers 10% to below 20%, and so on. ``99%`` covers 90% to below 100%, while
``100%`` contains only full pages. A tiny nonzero occupancy never belongs
to the empty group. Empty pages awaiting deferred release are tracked
internally but omitted from the displayed fullness columns.

Allocation searches non-full groups from highest occupancy downwards.
Compaction selects sources from lowest occupancy upwards and destinations
from highest occupancy downwards, excluding empty and full groups. This
backport retains the existing per-class locking, migration implementation,
and maximum of four base pages per zspage.
