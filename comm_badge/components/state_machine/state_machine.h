/*
 * state_machine.h — Public API for the CommBadge state machine.
 *
 * States:
 *   BOOT            Initial power-on state; transitions to IDLE on init.
 *   IDLE            Waiting for user input.
 *   RECORDING       Actively recording audio (future milestone).
 *   SYNC_ADVERTISING Broadcasting for peer synchronisation (future milestone).
 *
 * Transitions (all driven by button events):
 *   IDLE             + SHORT → RECORDING
 *   RECORDING        + SHORT → IDLE
 *   IDLE             + LONG  → SYNC_ADVERTISING
 *   SYNC_ADVERTISING + LONG  → IDLE
 */
#pragma once

#include "esp_err.h"
#include "button_service.h"   /* button_event_t */

/* All possible device states. */
typedef enum {
    STATE_BOOT,
    STATE_IDLE,
    STATE_RECORDING,
    STATE_SYNC_ADVERTISING,
} sm_state_t;

/**
 * @brief  Initialise the state machine and transition from BOOT → IDLE.
 *         Call once after hardware init, before the main event loop.
 */
void state_machine_init(void);

/**
 * @brief  Feed a button event to the state machine.
 *
 * Evaluates the current state's transition table, moves to the next state
 * if a matching transition exists, and logs the change with ESP_LOGI.
 *
 * @param evt  The button event to process.
 * @return     The state after processing (may be unchanged).
 */
sm_state_t state_machine_process(button_event_t evt);

/**
 * @brief  Return the current state without triggering any transition.
 */
sm_state_t state_machine_get_state(void);
