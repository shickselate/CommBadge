/*
 * audio_service.c — INMP441 microphone capture via ESP-IDF v6 I2S driver.
 *
 * INMP441 data format note:
 *   The mic outputs 24-bit audio left-justified inside a 32-bit I2S slot.
 *   That means each raw int32_t sample looks like: [D23..D0 | 0x00].
 *   Shift right by 8 bits to recover the signed 24-bit value before any
 *   arithmetic (the CPU sign-extends the shift so negative values stay
 *   negative — no masking needed).
 *
 * Every 2 seconds the capture task logs:
 *   min, max, RMS (all in 24-bit headroom scale), and an ASCII bar meter.
 */

#include "audio_service.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdint.h>
#include <limits.h>

static const char *TAG = "audio_service";

/* Tuning constants ------------------------------------------------------ */
#define SAMPLE_RATE         16000       /* Hz — INMP441 supports 16 kHz    */
#define CAPTURE_CHUNK       512         /* Samples read per I2S call        */
#define STATS_INTERVAL_MS   1000        /* How often to log statistics      */

/* The RMS level that fills the ASCII meter to 100 %.
 * 24-bit full-scale is 8 388 608.  Normal speech at arm's length is
 * roughly 0.5–5 % FS, so 100 000 (~1.2 % FS) gives a readable display
 * without clipping for normal use.  Adjust if your room is very quiet. */
#define METER_FULL_SCALE    10000
#define METER_WIDTH         10          /* Number of bar characters         */

/* Module state ---------------------------------------------------------- */
static i2s_chan_handle_t  s_rx_chan          = NULL;
static volatile bool      s_capturing        = false;
static TaskHandle_t       s_capture_task     = NULL;
static bool dumped = false;

/* -------------------------------------------------------------------------
 * Statistics / display helpers
 * ---------------------------------------------------------------------- */

/** Build a null-terminated ASCII bar string like "[####      ]". */
static void make_meter(int32_t rms, char *buf, int buf_len)
{
    /* buf must be at least METER_WIDTH + 3 bytes ('[' + bars + ']' + '\0'). */
    int bars = (int)((float)rms / (float)METER_FULL_SCALE * (float)METER_WIDTH);
    if (bars < 0)          bars = 0;
    if (bars > METER_WIDTH) bars = METER_WIDTH;

    int idx = 0;
    buf[idx++] = '[';
    for (int i = 0; i < METER_WIDTH && idx < buf_len - 2; i++) {
        buf[idx++] = (i < bars) ? '#' : ' ';
    }
    buf[idx++] = ']';
    buf[idx]   = '\0';
}

/* -------------------------------------------------------------------------
 * Capture task
 * ---------------------------------------------------------------------- */

static void capture_task(void *arg)
{
    /* Stack-allocated read buffer.  512 × 4 = 2 048 bytes — fits easily. */
    int32_t  samples[CAPTURE_CHUNK];

    /* Running statistics reset at each reporting interval. */
    int32_t  stat_min  = INT32_MAX;
    int32_t  stat_max  = INT32_MIN;
    int64_t  sum_sq    = 0;
    int      stat_n    = 0;
    TickType_t last_report = xTaskGetTickCount();

    ESP_LOGI(TAG, "Capture started — reporting every %d ms", STATS_INTERVAL_MS);

    while (s_capturing) {
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(s_rx_chan,
                                          samples, sizeof(samples),
                                          &bytes_read,
                                          pdMS_TO_TICKS(200));

        if (err == ESP_ERR_TIMEOUT) {
            /* No data yet — loop again so we can check s_capturing. */
            continue;
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2s_channel_read error: %s", esp_err_to_name(err));
            continue;
        }

        int n = (int)(bytes_read / sizeof(int32_t));

        // TEMPORARY DEBUG — remove after diagnosis
        if (!dumped) {
            dumped = true;
            ESP_LOGI(TAG, "--- RAW HEX DUMP (first 8 samples) ---");
            for (int i = 0; i < 8 && i < n; i++) {
                ESP_LOGI(TAG, "  raw[%d] = 0x%08lX  (%ld)", i, (uint32_t)samples[i], samples[i]);
            }
            ESP_LOGI(TAG, "--- END DUMP ---");
        }

        for (int i = 0; i < n; i += 2) {
            /* Even samples are real audio (right channel), odd samples are zero. */
            /* The INMP441 L/R pin selects right channel despite being wired to GND. */
            /* Cast to unsigned first then to signed to handle the full 32-bit range. */
            int32_t raw = samples[i];
            if (raw == 0) continue;  /* skip genuine zeros */
            /* Data is in upper 24 bits — shift down by 8 */
            int32_t s = raw >> 8;
            if (s < stat_min) stat_min = s;
            if (s > stat_max) stat_max = s;
            int64_t sv = (int64_t)s;
            sum_sq += sv * sv;
        }
        stat_n += n / 2;

        /* Report every STATS_INTERVAL_MS milliseconds. */
        TickType_t now = xTaskGetTickCount();
        if ((now - last_report) >= pdMS_TO_TICKS(STATS_INTERVAL_MS)) {
            int32_t rms = 0;
            if (stat_n > 0) {
                rms = (int32_t)sqrtf((float)(sum_sq / (int64_t)stat_n));
            }

            char meter[METER_WIDTH + 3];
            make_meter(rms, meter, sizeof(meter));

            ESP_LOGI(TAG, "mic  min=%8ld  max=%8ld  rms=%8ld  %s",
                     (long)stat_min, (long)stat_max, (long)rms, meter);

            /* Reset accumulators. */
            stat_min    = INT32_MAX;
            stat_max    = INT32_MIN;
            sum_sq      = 0;
            stat_n      = 0;
            last_report = now;
        }
    }

    ESP_LOGI(TAG, "Capture stopped");
    s_capture_task = NULL;  /* Signal to stop_capture() that we have exited. */
    vTaskDelete(NULL);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

esp_err_t audio_service_init(gpio_num_t sck_gpio,
                              gpio_num_t ws_gpio,
                              gpio_num_t sd_gpio)
{
    /* --- Create the I2S RX channel on port 1 (port 0 = MAX98357 output). - */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1,
                                                             I2S_ROLE_MASTER);
    /* auto_clear is TX-only; for RX it has no effect but leave at default. */
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_rx_chan));

    /* --- Standard Philips I2S, 16 kHz, 32-bit slots, mono left channel. -- */
    /* The INMP441 requires 32-bit slot width even though it only outputs
     * 24 bits of real data.  The bottom 8 bits of each sample are always 0. */
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                     I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk  = I2S_GPIO_UNUSED,
            .bclk  = sck_gpio,
            .ws    = ws_gpio,
            .dout  = I2S_GPIO_UNUSED,   /* RX only — no output from this port */
            .din   = sd_gpio,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_chan, &std_cfg));

    /* Enable the channel so the hardware is ready; capture task will read. */
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_chan));

    ESP_LOGI(TAG, "Initialised — SCK=%d  WS=%d  SD=%d  @ %d Hz 32-bit mono",
             sck_gpio, ws_gpio, sd_gpio, SAMPLE_RATE);
    return ESP_OK;
}


esp_err_t audio_service_start_capture(void)
{
    if (s_capturing) {
        ESP_LOGW(TAG, "Already capturing");
        return ESP_ERR_INVALID_STATE;
    }

    dumped = false;

    s_capturing = true;


    BaseType_t ret = xTaskCreate(capture_task, "audio_cap",
                                 4096, NULL, 5, &s_capture_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create capture task");
        s_capturing = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t audio_service_stop_capture(void)
{
    if (!s_capturing) {
        return ESP_OK;
    }

    /* Signal the task to exit at its next loop iteration. */
    s_capturing = false;

    /* Wait up to 500 ms for the task to clean up and null its own handle. */
    int retries = 50;
    while (s_capture_task != NULL && retries-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (s_capture_task != NULL) {
        /* Timed out — force-delete as a last resort. */
        ESP_LOGW(TAG, "Capture task did not exit cleanly; force-deleting");
        vTaskDelete(s_capture_task);
        s_capture_task = NULL;
    }

    return ESP_OK;
}

bool audio_service_is_capturing(void)
{
    return s_capturing;
}
