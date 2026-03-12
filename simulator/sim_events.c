#include "sim_events.h"
#include "ble_service.h"
#include "ui_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_EVENTS 64
#define MAX_NOTIF_IDS 16

typedef struct {
    uint32_t time_ms;       /* absolute simulated time */
    ble_evt_t evt;          /* event to fire */
    bool is_wait;           /* true = just a time advance, no event */
    char suffix[32];        /* for screenshot naming */
} sim_event_t;

static sim_event_t s_events[MAX_EVENTS];
static int s_event_count = 0;
static int s_event_cursor = 0;  /* next event to process */
static char s_last_suffix[32] = "";

/* Track injected notification IDs for dismiss-by-index */
static char s_notif_ids[MAX_NOTIF_IDS][NOTIF_MAX_ID_LEN];
static int s_notif_id_count = 0;
static int s_next_id = 1;

static void add_event(uint32_t time_ms, const ble_evt_t *evt, const char *suffix)
{
    if (s_event_count >= MAX_EVENTS) return;
    sim_event_t *e = &s_events[s_event_count++];
    e->time_ms = time_ms;
    e->is_wait = false;
    if (evt) {
        e->evt = *evt;
    }
    if (suffix) {
        snprintf(e->suffix, sizeof(e->suffix), "%s", suffix);
    } else {
        e->suffix[0] = '\0';
    }
}

/* Skip whitespace */
static const char *skip_ws(const char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* Parse a quoted string, returns pointer past closing quote */
static const char *parse_quoted(const char *s, char *out, int out_sz)
{
    s = skip_ws(s);
    if (*s != '"') {
        /* Unquoted — read until space or semicolon */
        int i = 0;
        while (*s && *s != ';' && *s != ' ' && i < out_sz - 1) {
            out[i++] = *s++;
        }
        out[i] = '\0';
        return s;
    }
    s++; /* skip opening quote */
    int i = 0;
    while (*s && *s != '"' && i < out_sz - 1) {
        out[i++] = *s++;
    }
    out[i] = '\0';
    if (*s == '"') s++; /* skip closing quote */
    return s;
}

void sim_events_init_inline(const char *events_str)
{
    if (!events_str) return;

    uint32_t current_time = 0;
    const char *p = events_str;

    while (*p) {
        p = skip_ws(p);
        if (!*p) break;

        if (strncmp(p, "connect", 7) == 0 && (!p[7] || p[7] == ';' || isspace((unsigned char)p[7]))) {
            ble_evt_t evt = { .type = BLE_EVT_CONNECTED };
            add_event(current_time, &evt, "connect");
            p += 7;
        }
        else if (strncmp(p, "disconnect", 10) == 0 && (!p[10] || p[10] == ';' || isspace((unsigned char)p[10]))) {
            ble_evt_t evt = { .type = BLE_EVT_DISCONNECTED };
            add_event(current_time, &evt, "disconnect");
            p += 10;
        }
        else if (strncmp(p, "clear", 5) == 0 && (!p[5] || p[5] == ';' || isspace((unsigned char)p[5]))) {
            ble_evt_t evt = { .type = BLE_EVT_NOTIF_CLEAR };
            add_event(current_time, &evt, "clear");
            p += 5;
        }
        else if (strncmp(p, "notify", 6) == 0) {
            p += 6;
            ble_evt_t evt = { .type = BLE_EVT_NOTIF_ADD };
            /* Generate a unique ID */
            snprintf(evt.id, sizeof(evt.id), "sim_%d", s_next_id);
            /* Track ID for dismiss-by-index */
            if (s_notif_id_count < MAX_NOTIF_IDS) {
                strncpy(s_notif_ids[s_notif_id_count], evt.id, NOTIF_MAX_ID_LEN - 1);
                s_notif_id_count++;
            }
            s_next_id++;
            p = parse_quoted(p, evt.project, sizeof(evt.project));
            p = parse_quoted(p, evt.message, sizeof(evt.message));
            add_event(current_time, &evt, "notify");
        }
        else if (strncmp(p, "dismiss", 7) == 0) {
            p += 7;
            p = skip_ws(p);
            int index = atoi(p);
            while (*p && *p != ';' && !isspace((unsigned char)*p)) p++;

            ble_evt_t evt = { .type = BLE_EVT_NOTIF_DISMISS };
            if (index >= 0 && index < s_notif_id_count) {
                strncpy(evt.id, s_notif_ids[index], sizeof(evt.id) - 1);
            }
            add_event(current_time, &evt, "dismiss");
        }
        else if (strncmp(p, "wait", 4) == 0) {
            p += 4;
            p = skip_ws(p);
            uint32_t ms = (uint32_t)atoi(p);
            while (*p && *p != ';' && !isspace((unsigned char)*p)) p++;
            current_time += ms;
        }
        else {
            /* Unknown token — skip to next semicolon */
            while (*p && *p != ';') p++;
        }

        /* Skip to next command */
        p = skip_ws(p);
        if (*p == ';') p++;
    }
}

void sim_events_init_scenario(const char *path)
{
    /* TODO: Task 8 — JSON scenario parsing with cJSON */
    (void)path;
    fprintf(stderr, "Scenario files not yet implemented\n");
}

bool sim_events_process(uint32_t current_time_ms)
{
    bool fired = false;

    while (s_event_cursor < s_event_count &&
           s_events[s_event_cursor].time_ms <= current_time_ms) {

        sim_event_t *e = &s_events[s_event_cursor];
        s_event_cursor++;

        if (!e->is_wait) {
            ui_manager_handle_event(&e->evt);
            snprintf(s_last_suffix, sizeof(s_last_suffix), "%s", e->suffix);
            fired = true;
        }
    }

    return fired;
}

bool sim_events_all_done(void)
{
    return s_event_cursor >= s_event_count;
}

uint32_t sim_events_get_end_time(void)
{
    if (s_event_count == 0) return 0;
    return s_events[s_event_count - 1].time_ms;
}

const char *sim_events_last_suffix(void)
{
    return s_last_suffix;
}
