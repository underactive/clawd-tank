// firmware/main/notification.h
#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <stdbool.h>
#include <stdint.h>

#define NOTIF_MAX_COUNT    8
#define NOTIF_MAX_ID_LEN   48
#define NOTIF_MAX_PROJ_LEN 32
#define NOTIF_MAX_MSG_LEN  64

/* Default auto-dismiss window for notifications that include ttl_ms. The
 * daemon emits this value too; keep them in sync via the protocol changelog. */
#define NOTIF_DEFAULT_TTL_MS 120000U

typedef struct {
    char id[NOTIF_MAX_ID_LEN];
    char project[NOTIF_MAX_PROJ_LEN];
    char message[NOTIF_MAX_MSG_LEN];
    uint32_t seq;
    uint32_t created_tick;  /* lv_tick_get() at (re-)add time; 0 for tick-less callers */
    uint32_t ttl_ms;        /* 0 disables auto-dismiss */
    bool active;
} notification_t;

typedef struct {
    notification_t items[NOTIF_MAX_COUNT];
    int count;
    uint32_t next_seq;
} notification_store_t;

void notif_store_init(notification_store_t *store);

// Returns 0 on success, -1 on error. If ID exists, updates it.
// If full, drops oldest to make room.
// Equivalent to notif_store_add_with_ttl(..., 0, 0): no countdown recorded.
int notif_store_add(notification_store_t *store,
                    const char *id, const char *project, const char *message);

// Same as notif_store_add but also records the add tick and per-notification TTL.
// A re-fired add for an existing ID resets created_tick to now_tick, so the
// countdown restarts from the newest hook event.
int notif_store_add_with_ttl(notification_store_t *store,
                             const char *id, const char *project, const char *message,
                             uint32_t now_tick, uint32_t ttl_ms);

// Returns 0 if found and removed, -1 if not found (idempotent).
int notif_store_dismiss(notification_store_t *store, const char *id);

void notif_store_clear(notification_store_t *store);

// Dismisses every slot whose ttl_ms is non-zero and whose elapsed time
// (now_tick - created_tick) has reached or passed ttl_ms. Slots with
// ttl_ms == 0 are never expired. Returns the number of slots dismissed.
int notif_store_expire(notification_store_t *store, uint32_t now_tick);

int notif_store_count(const notification_store_t *store);

// Returns NULL if index out of range or slot inactive.
const notification_t *notif_store_get(const notification_store_t *store, int index);

#endif // NOTIFICATION_H
