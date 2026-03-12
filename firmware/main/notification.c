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

// Find oldest active slot (lowest index). Returns index or -1.
static int find_oldest(const notification_store_t *store) {
    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        if (store->items[i].active) {
            return i;
        }
    }
    return -1;
}

static void write_slot(notification_t *slot, const char *id,
                        const char *project, const char *message) {
    strncpy(slot->id, id, NOTIF_MAX_ID_LEN - 1);
    slot->id[NOTIF_MAX_ID_LEN - 1] = '\0';
    strncpy(slot->project, project, NOTIF_MAX_PROJ_LEN - 1);
    slot->project[NOTIF_MAX_PROJ_LEN - 1] = '\0';
    strncpy(slot->message, message, NOTIF_MAX_MSG_LEN - 1);
    slot->message[NOTIF_MAX_MSG_LEN - 1] = '\0';
    slot->active = true;
}

int notif_store_add(notification_store_t *store,
                    const char *id, const char *project, const char *message) {
    // Update existing?
    int idx = find_by_id(store, id);
    if (idx >= 0) {
        write_slot(&store->items[idx], id, project, message);
        return 0;
    }

    // Find free slot
    idx = find_free_slot(store);
    if (idx >= 0) {
        write_slot(&store->items[idx], id, project, message);
        store->count++;
        return 0;
    }

    // Full — drop oldest
    idx = find_oldest(store);
    if (idx >= 0) {
        write_slot(&store->items[idx], id, project, message);
        // count stays the same (replaced one)
        return 0;
    }

    return -1; // Should never happen
}

int notif_store_dismiss(notification_store_t *store, const char *id) {
    int idx = find_by_id(store, id);
    if (idx < 0) return -1;

    store->items[idx].active = false;
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
