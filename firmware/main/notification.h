// firmware/main/notification.h
#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <stdbool.h>

#define NOTIF_MAX_COUNT    8
#define NOTIF_MAX_ID_LEN   48
#define NOTIF_MAX_PROJ_LEN 32
#define NOTIF_MAX_MSG_LEN  64

typedef struct {
    char id[NOTIF_MAX_ID_LEN];
    char project[NOTIF_MAX_PROJ_LEN];
    char message[NOTIF_MAX_MSG_LEN];
    bool active;
} notification_t;

typedef struct {
    notification_t items[NOTIF_MAX_COUNT];
    int count;
} notification_store_t;

void notif_store_init(notification_store_t *store);

// Returns 0 on success, -1 on error. If ID exists, updates it.
// If full, drops oldest to make room.
int notif_store_add(notification_store_t *store,
                    const char *id, const char *project, const char *message);

// Returns 0 if found and removed, -1 if not found (idempotent).
int notif_store_dismiss(notification_store_t *store, const char *id);

void notif_store_clear(notification_store_t *store);

int notif_store_count(const notification_store_t *store);

// Returns NULL if index out of range or slot inactive.
const notification_t *notif_store_get(const notification_store_t *store, int index);

#endif // NOTIFICATION_H
