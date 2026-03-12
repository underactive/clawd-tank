#ifndef SIM_EVENTS_H
#define SIM_EVENTS_H

#include <stdbool.h>
#include <stdint.h>

/** Initialize event system from an inline event string (semicolon-separated). */
void sim_events_init_inline(const char *events_str);

/** Initialize event system from a JSON scenario file. */
void sim_events_init_scenario(const char *path);

/**
 * Process any events that are due at the given simulated time.
 * Fires events via ui_manager_handle_event().
 * @return true if an event was fired (caller should capture screenshot if --screenshot-on-event).
 */
bool sim_events_process(uint32_t current_time_ms);

/** Returns true when all events have been processed. */
bool sim_events_all_done(void);

/** Get the time of the last event (for --run-ms default calculation). */
uint32_t sim_events_get_end_time(void);

/** Get the suffix string for the most recently fired event (for screenshot naming). */
const char *sim_events_last_suffix(void);

#endif
