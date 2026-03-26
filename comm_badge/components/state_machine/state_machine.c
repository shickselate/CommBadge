/*
 * state_machine.c — CommBadge device state machine.
 *
 * Every state transition is logged with ESP_LOGI so you can follow the
 * device lifecycle on the serial monitor without a debugger.
 *
 * Only the transitions listed in state_machine.h are implemented.
 * Unexpected events in a given state are silently ignored — the state
 * is returned unchanged and a debug message is emitted.
 */

#include "state_machine.h"
#include "button_service.h"
#include "esp_log.h"

static const char *TAG = "state_machine";

/* Current state — starts at BOOT; state_machine_init() moves it to IDLE. */
static sm_state_t s_state = STATE_BOOT;

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

/** Return a human-readable name for logging. */
static const char *state_name(sm_state_t state)
{
    switch (state) {
        case STATE_BOOT:             return "BOOT";
        case STATE_IDLE:             return "IDLE";
        case STATE_RECORDING:        return "RECORDING";
        case STATE_SYNC_ADVERTISING: return "SYNC_ADVERTISING";
        default:                     return "UNKNOWN";
    }
}

/** Perform the transition and log it. */
static void transition_to(sm_state_t next)
{
    ESP_LOGI(TAG, "State: %s --> %s", state_name(s_state), state_name(next));
    s_state = next;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void state_machine_init(void)
{
    transition_to(STATE_IDLE);
}

sm_state_t state_machine_process(button_event_t evt)
{
    switch (s_state) {

        case STATE_IDLE:
            if (evt == EVT_BUTTON_SHORT) {
                transition_to(STATE_RECORDING);
            } else if (evt == EVT_BUTTON_LONG) {
                transition_to(STATE_SYNC_ADVERTISING);
            }
            break;

        case STATE_RECORDING:
            if (evt == EVT_BUTTON_SHORT) {
                transition_to(STATE_IDLE);
            } else {
                /* Long press has no defined action while recording. */
                ESP_LOGD(TAG, "Long press ignored in RECORDING state");
            }
            break;

        case STATE_SYNC_ADVERTISING:
            if (evt == EVT_BUTTON_LONG) {
                transition_to(STATE_IDLE);
            } else {
                /* Short press has no defined action while advertising. */
                ESP_LOGD(TAG, "Short press ignored in SYNC_ADVERTISING state");
            }
            break;

        case STATE_BOOT:
            /* Should not receive events before init(); log and ignore. */
            ESP_LOGW(TAG, "Event received in BOOT state — call state_machine_init() first");
            break;

        default:
            ESP_LOGE(TAG, "Unknown state %d", (int)s_state);
            break;
    }

    return s_state;
}

sm_state_t state_machine_get_state(void)
{
    return s_state;
}
