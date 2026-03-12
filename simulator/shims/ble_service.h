#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include "notification.h"

typedef enum {
    BLE_EVT_NOTIF_ADD,
    BLE_EVT_NOTIF_DISMISS,
    BLE_EVT_NOTIF_CLEAR,
    BLE_EVT_CONNECTED,
    BLE_EVT_DISCONNECTED,
} ble_evt_type_t;

typedef struct {
    ble_evt_type_t type;
    char id[NOTIF_MAX_ID_LEN];
    char project[NOTIF_MAX_PROJ_LEN];
    char message[NOTIF_MAX_MSG_LEN];
} ble_evt_t;

/* Stub — simulator does not init real BLE */
static inline void ble_service_init(void *q) { (void)q; }

#endif
