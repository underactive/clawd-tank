// firmware/main/notification.c
#include "notification.h"
#include <string.h>

void notif_store_init(notification_store_t *store) {
    memset(store, 0, sizeof(*store));
}

// Find slot by ID. Returns index or -1.
static int find_by_id(const notification_store_t *store, const char *id) {
    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        if (store->items[i].active && strcmp(store->items[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

// Find first inactive slot. Returns index or -1.
static int find_free_slot(const notification_store_t *store) {
    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        if (!store->items[i].active) {
            return i;
        }
    }
    return -1;
}

// Find oldest active slot (lowest seq). Returns index or -1.
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

static void write_slot(notification_t *slot, const char *id,
                        const char *project, const char *message,
                        uint32_t seq, uint32_t now_tick, uint32_t ttl_ms) {
    strncpy(slot->id, id, NOTIF_MAX_ID_LEN - 1);
    slot->id[NOTIF_MAX_ID_LEN - 1] = '\0';
    strncpy(slot->project, project, NOTIF_MAX_PROJ_LEN - 1);
    slot->project[NOTIF_MAX_PROJ_LEN - 1] = '\0';
    strncpy(slot->message, message, NOTIF_MAX_MSG_LEN - 1);
    slot->message[NOTIF_MAX_MSG_LEN - 1] = '\0';
    slot->seq = seq;
    slot->created_tick = now_tick;
    slot->ttl_ms = ttl_ms;
    slot->active = true;
}

int notif_store_add_with_ttl(notification_store_t *store,
                             const char *id, const char *project, const char *message,
                             uint32_t now_tick, uint32_t ttl_ms) {
    uint32_t seq = store->next_seq++;

    // Update existing?
    int idx = find_by_id(store, id);
    if (idx >= 0) {
        write_slot(&store->items[idx], id, project, message, seq, now_tick, ttl_ms);
        return 0;
    }

    // Find free slot
    idx = find_free_slot(store);
    if (idx >= 0) {
        write_slot(&store->items[idx], id, project, message, seq, now_tick, ttl_ms);
        store->count++;
        return 0;
    }

    // Full — drop oldest
    idx = find_oldest(store);
    if (idx >= 0) {
        write_slot(&store->items[idx], id, project, message, seq, now_tick, ttl_ms);
        // count stays the same (replaced one)
        return 0;
    }

    return -1; // Should never happen
}

int notif_store_add(notification_store_t *store,
                    const char *id, const char *project, const char *message) {
    return notif_store_add_with_ttl(store, id, project, message, 0, 0);
}

int notif_store_expire(notification_store_t *store, uint32_t now_tick) {
    int dismissed = 0;
    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        notification_t *n = &store->items[i];
        if (!n->active || n->ttl_ms == 0) continue;
        // Unsigned subtraction handles tick wrap correctly: elapsed > 2^31
        // can only occur after ~24.8 days, far beyond any sane TTL.
        uint32_t elapsed = now_tick - n->created_tick;
        if (elapsed >= n->ttl_ms) {
            memset(n, 0, sizeof(*n));
            store->count--;
            dismissed++;
        }
    }
    return dismissed;
}

int notif_store_dismiss(notification_store_t *store, const char *id) {
    int idx = find_by_id(store, id);
    if (idx < 0) return -1;

    memset(&store->items[idx], 0, sizeof(notification_t));
    store->count--;
    return 0;
}

void notif_store_clear(notification_store_t *store) {
    notif_store_init(store);
}

int notif_store_count(const notification_store_t *store) {
    return store->count;
}

const notification_t *notif_store_get(const notification_store_t *store, int index) {
    if (index < 0 || index >= NOTIF_MAX_COUNT) return NULL;
    if (!store->items[index].active) return NULL;
    return &store->items[index];
}
