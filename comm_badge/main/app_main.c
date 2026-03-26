/*
 * app_main.c — CommBadge entry point (Milestone E3).
 *
 * Boot sequence:
 *   1. storage_service  — mount FAT filesystem on the "audio" flash partition.
 *   2. audio_service    — init INMP441 on I2S port 1.
 *   3. playback_service — init MAX98357 on I2S port 0.
 *   4. button_service   — init GPIO with debounce task.
 *   5. state_machine    — BOOT → IDLE.
 *   6. Main event loop.
 *
 * Event loop:
 *   A single integer queue carries both button events and system events
 *   (EVT_PLAYBACK_DONE from playback_service).  Every event is mapped to
 *   sm_event_t and fed to the state machine.  The app then acts on the
 *   resulting state transition.
 *
 * State → action mapping:
 *   → STATE_RECORDING : audio_service_start_capture()
 *   → STATE_PLAYING   : audio_service_stop_capture() + playback_service_start()
 *   → STATE_IDLE      : playback_service_stop() (if was PLAYING, stops early)
 *   → STATE_SYNC_ADV  : (placeholder — no action yet)
 */

#include "config.h"
#include "button_service.h"
#include "audio_service.h"
#include "playback_service.h"
#include "storage_service.h"
#include "state_machine.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "app_main";

/* Single app-wide event queue.  Carries sm_event_t values as int.
 * button_service posts 0 (short) / 1 (long).
 * playback_service posts EVT_PLAYBACK_DONE (2).
 * All values match sm_event_t in state_machine.h. */
static QueueHandle_t s_app_queue;

void app_main(void)
{
    ESP_LOGI(TAG, "=== CommBadge booting (Milestone E3) ===");

    /* ------------------------------------------------------------------ */
    /* 1.  Storage — mount FAT on "audio" partition                        */
    /* ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "Mounting storage...");
    ESP_ERROR_CHECK(storage_service_init());
    ESP_LOGI(TAG, "Free space: %llu bytes", storage_get_free_bytes());

    /* ------------------------------------------------------------------ */
    /* 2.  Audio capture service (INMP441 on I2S port 1)                  */
    /* ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "Initialising audio service...");
    ESP_ERROR_CHECK(audio_service_init(
        CONFIG_MIC_SCK_GPIO,
        CONFIG_MIC_WS_GPIO,
        CONFIG_MIC_SD_GPIO
    ));

    /* ------------------------------------------------------------------ */
    /* 3.  App event queue — shared by button, playback, and future events */
    /* ------------------------------------------------------------------ */
    s_app_queue = xQueueCreate(16, sizeof(int));
    if (s_app_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create app event queue — halting");
        return;
    }

    /* ------------------------------------------------------------------ */
    /* 4.  Playback service (MAX98357 on I2S port 0)                      */
    /* ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "Initialising playback service...");
    ESP_ERROR_CHECK(playback_service_init(
        CONFIG_I2S_BCLK_GPIO,
        CONFIG_I2S_LRCLK_GPIO,
        CONFIG_I2S_DOUT_GPIO,
        s_app_queue
    ));

    /* ------------------------------------------------------------------ */
    /* 5.  Button service                                                  */
    /* ------------------------------------------------------------------ */
    ESP_LOGI(TAG, "Initialising button service on GPIO %d...", CONFIG_BUTTON_GPIO);
    /* button_service posts button_event_t values (0 = SHORT, 1 = LONG)
     * which match EVT_BUTTON_SHORT / EVT_BUTTON_LONG in sm_event_t. */
    ESP_ERROR_CHECK(button_service_init(CONFIG_BUTTON_GPIO, s_app_queue));

    /* ------------------------------------------------------------------ */
    /* 6.  State machine                                                   */
    /* ------------------------------------------------------------------ */
    state_machine_init();
    ESP_LOGI(TAG, "=== CommBadge ready. Short-press = record/stop, Long-press = sync ===");

    /* ------------------------------------------------------------------ */
    /* 7.  Main event loop                                                 */
    /* ------------------------------------------------------------------ */
    int raw_evt;
    while (1) {
        if (xQueueReceive(s_app_queue, &raw_evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        sm_event_t evt  = (sm_event_t)raw_evt;
        sm_state_t prev = state_machine_get_state();
        sm_state_t next = state_machine_process(evt);

        if (next == prev) continue;   /* No transition — nothing to do. */

        /* Act on the new state. */
        switch (next) {

            case STATE_RECORDING:
                ESP_LOGI(TAG, "→ Starting capture to %s",
                         storage_get_recording_path());
                audio_service_start_capture(storage_get_recording_path());
                break;

            case STATE_PLAYING:
                /* Stop the mic first (finalises the WAV header), then play. */
                ESP_LOGI(TAG, "→ Stopping capture, starting playback");
                audio_service_stop_capture();
                playback_service_start(storage_get_recording_path());
                break;

            case STATE_IDLE:
                /* Could be arriving from PLAYING (button interrupt) or
                 * SYNC_ADVERTISING (long press cancel).  Stop whatever is running. */
                if (prev == STATE_PLAYING) {
                    ESP_LOGI(TAG, "→ Playback interrupted");
                    playback_service_stop();
                } else {
                    ESP_LOGI(TAG, "→ Returned to IDLE from %d", (int)prev);
                }
                break;

            case STATE_SYNC_ADVERTISING:
                ESP_LOGI(TAG, "→ Sync advertising (placeholder)");
                break;

            default:
                ESP_LOGW(TAG, "Unhandled new state %d", (int)next);
                break;
        }
    }
}
