/*
 * state_machine.h — Public API for the CommBadge state machine (E3).
 *
 * States:
 *   BOOT             Initial power-on; transitions to IDLE on init.
 *   IDLE             Waiting for user input.
 *   RECORDING        Capturing audio to a WAV file.
 *   PLAYING          Playing back the recorded WAV file.
 *   SYNC_ADVERTISING Broadcasting for peer synchronisation (future).
 *   SLEEP            Low-power idle (future).
 *   ERROR            Unrecoverable fault (future).
 *
 * Transitions:
 *   IDLE             + EVT_BUTTON_SHORT  → RECORDING
 *   RECORDING        + EVT_BUTTON_SHORT  → PLAYING    (stop record, start play)
 *   PLAYING          + EVT_BUTTON_SHORT  → IDLE       (stop playback early)
 *   PLAYING          + EVT_PLAYBACK_DONE → IDLE       (natural end)
 *   IDLE             + EVT_BUTTON_LONG   → SYNC_ADVERTISING
 *   SYNC_ADVERTISING + EVT_BUTTON_LONG   → IDLE
 */
#pragma once

#include "esp_err.h"

/* All possible device states. */
typedef enum {
    STATE_BOOT,
    STATE_IDLE,
    STATE_RECORDING,
    STATE_PLAYING,
    STATE_SYNC_ADVERTISING,
    STATE_SLEEP,
    STATE_ERROR,
} sm_state_t;

/* All events the state machine can consume.
 * Button events and system events share the same enum so app_main
 * can use a single queue of int for everything. */
typedef enum {
    EVT_BUTTON_SHORT    = 0,
    EVT_BUTTON_LONG     = 1,
    EVT_PLAYBACK_DONE   = 2,
    EVT_RECORDING_DONE  = 3,   /* reserved for future async stop */
} sm_event_t;

/**
 * @brief  Initialise the state machine (BOOT → IDLE).
 *         Call once after hardware init, before the event loop.
 */
void state_machine_init(void);

/**
 * @brief  Feed an event to the state machine.
 *         Transitions if a matching rule exists, logs every change.
 * @param evt  One of the sm_event_t values.
 * @return     The state after processing (may be unchanged).
 */
sm_state_t state_machine_process(sm_event_t evt);

/** @brief  Return the current state without triggering a transition. */
sm_state_t state_machine_get_state(void);
