/*
 * app_main.c — CommBadge entry point (Milestone E2).
 *
 * Boot sequence:
 *   1. Initialise feedback service (I2S port 0 / MAX98357 amplifier).
 *   2. Play a boot chirp to confirm audio hardware is working.
 *   3. Initialise audio service (I2S port 1 / INMP441 microphone).
 *   4. Create the button event queue.
 *   5. Initialise button service (GPIO + debounce task).
 *   6. Initialise state machine (BOOT → IDLE).
 *   7. Enter the main event loop.
 *
 * Main event loop:
 *   Blocks on the button queue.  Each received event is fed to the state
 *   machine; if the state changes, the appropriate chirp is played and
 *   microphone capture is started or stopped.
 */

#include "config.h"            /* Pin assignments — edit only this file    */
#include "button_service.h"
#include "feedback_service.h"
#include "audio_service.h"
#include "state_machine.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "=== CommBadge booting (Milestone E2) ===");

    /* ------------------------------------------------------------------ */
    /* 1.  Audio feedback service                                          */
    /* ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "Initialising feedback service...");
    ESP_ERROR_CHECK(feedback_service_init(
        CONFIG_I2S_BCLK_GPIO,
        CONFIG_I2S_LRCLK_GPIO,
        CONFIG_I2S_DOUT_GPIO
    ));

    /* ------------------------------------------------------------------ */
    /* 2.  Boot chirp — audible confirmation that the amp is alive         */
    /* ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "Playing boot chirp");
    ESP_ERROR_CHECK(feedback_play(CHIRP_RECORD_START));

    /* ------------------------------------------------------------------ */
    /* 3.  Audio capture service (INMP441 on I2S port 1)                  */
    /* ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "Initialising audio service...");
    ESP_ERROR_CHECK(audio_service_init(
        CONFIG_MIC_SCK_GPIO,
        CONFIG_MIC_WS_GPIO,
        CONFIG_MIC_SD_GPIO
    ));

    /* ------------------------------------------------------------------ */
    /* 4.  Button event queue                                              */
    /* ------------------------------------------------------------------ */
    QueueHandle_t button_queue = xQueueCreate(8, sizeof(button_event_t));
    if (button_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create button event queue — halting");
        return;
    }

    /* ------------------------------------------------------------------ */
    /* 5.  Button service                                                  */
    /* ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "Initialising button service on GPIO %d...", CONFIG_BUTTON_GPIO);
    ESP_ERROR_CHECK(button_service_init(CONFIG_BUTTON_GPIO, button_queue));

    /* ------------------------------------------------------------------ */
    /* 6.  State machine                                                   */
    /* ------------------------------------------------------------------ */
    state_machine_init();   /* Logs BOOT --> IDLE internally */
    ESP_LOGI(TAG, "=== CommBadge ready. Short-press = toggle record, Long-press = sync ===");

    /* ------------------------------------------------------------------ */
    /* 7.  Main event loop                                                 */
    /* ------------------------------------------------------------------ */
    button_event_t evt;
    while (1) {
        /* Block indefinitely until a button event arrives. */
        if (xQueueReceive(button_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* Snapshot the state BEFORE processing so we can detect a change. */
        sm_state_t prev = state_machine_get_state();
        sm_state_t next = state_machine_process(evt);

        if (next == prev) {
            /* No transition — nothing to do. */
            continue;
        }

        /* Play the chirp that matches the NEW state. */
        switch (next) {
            case STATE_RECORDING:
                ESP_LOGI(TAG, "→ Recording started");
                feedback_play(CHIRP_RECORD_START);
                audio_service_start_capture();
                break;

            case STATE_IDLE:
                if (prev == STATE_RECORDING) {
                    ESP_LOGI(TAG, "→ Recording stopped");
                    audio_service_stop_capture();
                    feedback_play(CHIRP_RECORD_STOP);
                } else {
                    /* Returning from SYNC_ADVERTISING. */
                    ESP_LOGI(TAG, "→ Sync advertising cancelled");
                    feedback_play(CHIRP_RECORD_STOP);
                }
                break;

            case STATE_SYNC_ADVERTISING:
                ESP_LOGI(TAG, "→ Sync advertising started");
                feedback_play(CHIRP_SYNC_START);
                break;

            default:
                ESP_LOGW(TAG, "Unhandled new state %d", (int)next);
                break;
        }
    }
}
