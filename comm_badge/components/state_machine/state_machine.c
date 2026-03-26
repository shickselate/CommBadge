/*
 * state_machine.c — CommBadge device state machine (E3).
 *
 * Every state transition is logged with ESP_LOGI.
 * Events that have no transition rule in the current state are ignored
 * with a debug log — they are not errors.
 */

#include "state_machine.h"
#include "esp_log.h"

static const char *TAG = "state_machine";

static sm_state_t s_state = STATE_BOOT;

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static const char *state_name(sm_state_t s)
{
    switch (s) {
        case STATE_BOOT:             return "BOOT";
        case STATE_IDLE:             return "IDLE";
        case STATE_RECORDING:        return "RECORDING";
        case STATE_PLAYING:          return "PLAYING";
        case STATE_SYNC_ADVERTISING: return "SYNC_ADVERTISING";
        case STATE_SLEEP:            return "SLEEP";
        case STATE_ERROR:            return "ERROR";
        default:                     return "UNKNOWN";
    }
}

static const char *event_name(sm_event_t e)
{
    switch (e) {
        case EVT_BUTTON_SHORT:   return "BUTTON_SHORT";
        case EVT_BUTTON_LONG:    return "BUTTON_LONG";
        case EVT_PLAYBACK_DONE:  return "PLAYBACK_DONE";
        case EVT_RECORDING_DONE: return "RECORDING_DONE";
        default:                 return "UNKNOWN";
    }
}

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

sm_state_t state_machine_process(sm_event_t evt)
{
    switch (s_state) {

        case STATE_IDLE:
            if (evt == EVT_BUTTON_SHORT) {
                transition_to(STATE_RECORDING);
            } else if (evt == EVT_BUTTON_LONG) {
                transition_to(STATE_SYNC_ADVERTISING);
            } else {
                ESP_LOGD(TAG, "IDLE: ignoring event %s", event_name(evt));
            }
            break;

        case STATE_RECORDING:
            if (evt == EVT_BUTTON_SHORT) {
                /* Short press ends recording and immediately starts playback. */
                transition_to(STATE_PLAYING);
            } else {
                ESP_LOGD(TAG, "RECORDING: ignoring event %s", event_name(evt));
            }
            break;

        case STATE_PLAYING:
            if (evt == EVT_BUTTON_SHORT || evt == EVT_PLAYBACK_DONE) {
                transition_to(STATE_IDLE);
            } else {
                ESP_LOGD(TAG, "PLAYING: ignoring event %s", event_name(evt));
            }
            break;

        case STATE_SYNC_ADVERTISING:
            if (evt == EVT_BUTTON_LONG) {
                transition_to(STATE_IDLE);
            } else {
                ESP_LOGD(TAG, "SYNC_ADV: ignoring event %s", event_name(evt));
            }
            break;

        case STATE_BOOT:
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
