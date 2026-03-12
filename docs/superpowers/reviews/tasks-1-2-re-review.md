# Re-Review: notification.c/h + Tests

**Date:** 2026-03-11
**Reviewer:** code-reviewer agent
**Original issues:** 2 critical, 3 medium, 4 low
**Fixes confirmed:** All original issues addressed

---

## Verification of Fixes

### ✅ Critical #1 — `find_oldest` eviction bug: FIXED

```c
static int find_oldest(const notification_store_t *store) {
    int oldest = -1;
    uint32_t min_seq = UINT32_MAX;
    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        if (store->items[i].active && store->items[i].seq < min_seq) {
            min_seq = store->items[i].seq;
            oldest = i;
        }
    }
    return oldest;
}
```

Min-seq scan is correct. `seq` is assigned via `store->next_seq++` in `notif_store_add` before any path, so every notification (new or updated) gets a monotonically increasing sequence number. The regression test (`test_overflow_after_dismiss_evicts_by_seq`) correctly exercises the exact scenario from the original bug report.

### ✅ Medium #6 — Redundant `active = false` before memset: FIXED

`notif_store_dismiss` now goes straight to `memset`. Clean.

### ✅ New tests: Comprehensive

11 tests vs 6 previously. The five new tests cover:
- Regression for the eviction bug (exact scenario: dismiss s0, refill slot0 with s8, overflow must evict s1 not s8)
- Free-slot reuse after dismiss (no spurious eviction)
- String boundary behavior (exact-fit, over-length ID, over-length message — all correctly truncated and null-terminated)
- Empty-string ID (valid lifecycle: add, update, dismiss)
- Count consistency through mixed add/dismiss cycles

All previously flagged test gaps are now covered.

---

## One Design Observation (Not a Bug)

When updating an existing notification (ID already in store), `write_slot` assigns a fresh `seq`:

```c
uint32_t seq = store->next_seq++;
int idx = find_by_id(store, id);
if (idx >= 0) {
    write_slot(&store->items[idx], id, project, message, seq);  // seq refreshed
    return 0;
}
```

This means an updated notification is treated as "newer" for eviction purposes. This is a reasonable policy (you just refreshed it; you don't want it evicted immediately), and the spec says "update-by-ID" without specifying eviction ordering for updated entries. Just worth knowing — if the intent is to preserve original insertion order through updates, `seq` should not be updated on the update path.

---

## Assessment: ✅ Approved

All original issues resolved. Test coverage is now thorough. The eviction logic is correct and well-tested.
