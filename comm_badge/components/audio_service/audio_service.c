/*
 * audio_service.c — INMP441 microphone capture with WAV file output (E3).
 *
 * INMP441 data format:
 *   The mic outputs 24-bit audio left-justified in a 32-bit I2S slot.
 *   Raw int32_t looks like: [D23..D0 | 0x00].  Shift right by 8 to get the
 *   signed 24-bit value.  We then truncate to 16-bit for the WAV file
 *   (drop the bottom 8 bits of the 24-bit value) — this is standard practice
 *   and the INMP441 noise floor is well above 16-bit resolution anyway.
 *
 *   The driver produces interleaved L/R words in mono mode; only every other
 *   sample (even index) carries real audio.  The odd samples are zero padding.
 *
 * WAV writing:
 *   - A placeholder 44-byte header is written at file open.
 *   - On stop, fseek(0) + rewrite with correct data_size and file_size.
 *
 * Level meter (retained from E2 for ongoing diagnosis):
 *   Logs min/max/RMS + ASCII bar every STATS_INTERVAL_MS milliseconds.
 */

#include "audio_service.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "audio_service";

/* Tuning constants ------------------------------------------------------ */
#define SAMPLE_RATE         16000
#define CAPTURE_CHUNK       512         /* 32-bit words read per I2S call   */
#define STATS_INTERVAL_MS   1000
#define METER_FULL_SCALE    10000
#define METER_WIDTH         10

/* WAV header struct — packed so fwrite() copies it directly. ------------ */
typedef struct __attribute__((packed)) {
    char     riff[4];           /* "RIFF"     */
    uint32_t file_size;         /* total - 8  */
    char     wave[4];           /* "WAVE"     */
    char     fmt[4];            /* "fmt "     */
    uint32_t fmt_size;          /* 16         */
    uint16_t audio_format;      /* 1 = PCM    */
    uint16_t num_channels;      /* 1          */
    uint32_t sample_rate;       /* 16000      */
    uint32_t byte_rate;         /* 32000      */
    uint16_t block_align;       /* 2          */
    uint16_t bits_per_sample;   /* 16         */
    char     data_tag[4];       /* "data"     */
    uint32_t data_size;         /* PCM bytes  */
} wav_header_t;

/* Module state ---------------------------------------------------------- */
static i2s_chan_handle_t  s_rx_chan        = NULL;
static volatile bool      s_capturing      = false;
static TaskHandle_t       s_capture_task   = NULL;
static bool               s_dumped         = false;
/* Filepath is copied here by start_capture(). */
static char               s_filepath[128];

/* -------------------------------------------------------------------------
 * WAV helpers
 * ---------------------------------------------------------------------- */

static void write_wav_header(FILE *f, uint32_t data_size)
{
    wav_header_t hdr = {
        .riff          = {'R','I','F','F'},
        .file_size     = data_size + sizeof(wav_header_t) - 8,
        .wave          = {'W','A','V','E'},
        .fmt           = {'f','m','t',' '},
        .fmt_size      = 16,
        .audio_format  = 1,
        .num_channels  = 1,
        .sample_rate   = SAMPLE_RATE,
        .byte_rate     = SAMPLE_RATE * 2,   /* 16-bit mono */
        .block_align   = 2,
        .bits_per_sample = 16,
        .data_tag      = {'d','a','t','a'},
        .data_size     = data_size,
    };
    fwrite(&hdr, 1, sizeof(hdr), f);
}

/* -------------------------------------------------------------------------
 * Level meter helper
 * ---------------------------------------------------------------------- */

static void make_meter(int32_t rms, char *buf, int buf_len)
{
    int bars = (int)((float)rms / (float)METER_FULL_SCALE * (float)METER_WIDTH);
    if (bars < 0)           bars = 0;
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
    /* Open the WAV file for writing. */
    FILE *wav_file = fopen(s_filepath, "wb");
    if (!wav_file) {
        ESP_LOGE(TAG, "Cannot open %s for writing", s_filepath);
        s_capture_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* Write a placeholder header; we'll fix the sizes on close. */
    write_wav_header(wav_file, 0);
    uint32_t pcm_bytes_written = 0;

    /* I2S read buffer: CAPTURE_CHUNK 32-bit words. */
    int32_t samples[CAPTURE_CHUNK];

    /* PCM write buffer */
    int16_t pcm_out[CAPTURE_CHUNK];

    /* Level meter accumulators. */
    int32_t    stat_min    = INT32_MAX;
    int32_t    stat_max    = INT32_MIN;
    int64_t    sum_sq      = 0;
    int        stat_n      = 0;
    TickType_t last_report = xTaskGetTickCount();

    ESP_LOGI(TAG, "Capture started → %s", s_filepath);

    while (s_capturing) {
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(s_rx_chan,
                                          samples, sizeof(samples),
                                          &bytes_read,
                                          pdMS_TO_TICKS(200));
        if (err == ESP_ERR_TIMEOUT) continue;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2s_channel_read: %s", esp_err_to_name(err));
            continue;
        }

        int n = (int)(bytes_read / sizeof(int32_t));

        /* Debug dump — first chunk only, retained for diagnosis. */
        if (!s_dumped) {
            s_dumped = true;
            ESP_LOGI(TAG, "--- RAW HEX DUMP (first 8 samples) ---");
            for (int i = 0; i < 8 && i < n; i++) {
                ESP_LOGI(TAG, "  raw[%d] = 0x%08lX  (%ld)",
                         i, (unsigned long)(uint32_t)samples[i], (long)samples[i]);
            }
            ESP_LOGI(TAG, "--- END DUMP ---");
        }

        int out_n = 0;
        for (int i = 0; i < n; i++) {
            int32_t raw = samples[i];
            /* Recover signed 24-bit value, apply 8x gain, clamp to int16 range. */
            int32_t s24 = ((int32_t)raw) >> 8;
            int16_t s16 = (int16_t)(s24 >> 8);
            pcm_out[out_n++] = s16;

            /* Stats use s24 so the meter reflects the real signal level. */
            if (s24 < stat_min) stat_min = s24;
            if (s24 > stat_max) stat_max = s24;
            int64_t sv = (int64_t)s24;
            sum_sq += sv * sv;
        }
        stat_n += out_n;

        /* Write 16-bit PCM to file. */
        size_t to_write = (size_t)(out_n) * sizeof(int16_t);
        size_t wrote    = fwrite(pcm_out, 1, to_write, wav_file);
        if (wrote != to_write) {
            ESP_LOGW(TAG, "Short write: %zu of %zu bytes", wrote, to_write);
        }
        pcm_bytes_written += (uint32_t)wrote;

        /* Level meter report. */
        TickType_t now = xTaskGetTickCount();
        if ((now - last_report) >= pdMS_TO_TICKS(STATS_INTERVAL_MS)) {
            int32_t rms = 0;
            if (stat_n > 0) {
                rms = (int32_t)sqrtf((float)(sum_sq / (int64_t)stat_n));
            }
            char meter[METER_WIDTH + 3];
            make_meter(rms, meter, sizeof(meter));
            ESP_LOGI(TAG, "mic  min=%8ld  max=%8ld  rms=%8ld  %s  (%lu bytes written)",
                     (long)stat_min, (long)stat_max, (long)rms, meter,
                     (unsigned long)pcm_bytes_written);

            stat_min    = INT32_MAX;
            stat_max    = INT32_MIN;
            sum_sq      = 0;
            stat_n      = 0;
            last_report = now;
        }
    }

    /* Finalise: seek back and rewrite WAV header with correct sizes. */
    fseek(wav_file, 0, SEEK_SET);
    write_wav_header(wav_file, pcm_bytes_written);
    fclose(wav_file);

    ESP_LOGI(TAG, "Capture stopped — %lu bytes PCM written to %s",
             (unsigned long)pcm_bytes_written, s_filepath);

    s_capture_task = NULL;
    vTaskDelete(NULL);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

esp_err_t audio_service_init(gpio_num_t sck_gpio,
                              gpio_num_t ws_gpio,
                              gpio_num_t sd_gpio)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1,
                                                             I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_rx_chan));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                     I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk  = I2S_GPIO_UNUSED,
            .bclk  = sck_gpio,
            .ws    = ws_gpio,
            .dout  = I2S_GPIO_UNUSED,
            .din   = sd_gpio,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_chan));

    ESP_LOGI(TAG, "Initialised — SCK=%d  WS=%d  SD=%d  @ %d Hz 32-bit mono",
             sck_gpio, ws_gpio, sd_gpio, SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t audio_service_start_capture(const char *filepath)
{
    if (s_capturing) {
        ESP_LOGW(TAG, "Already capturing");
        return ESP_ERR_INVALID_STATE;
    }

    strncpy(s_filepath, filepath, sizeof(s_filepath) - 1);
    s_filepath[sizeof(s_filepath) - 1] = '\0';
    s_dumped    = false;
    s_capturing = true;

    BaseType_t ret = xTaskCreate(capture_task, "audio_cap",
                                  6144, NULL, 5, &s_capture_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create capture task");
        s_capturing = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t audio_service_stop_capture(void)
{
    if (!s_capturing) return ESP_OK;

    s_capturing = false;

    /* Wait up to 500 ms for the task to finalise the WAV and exit. */
    int retries = 50;
    while (s_capture_task != NULL && retries-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_capture_task != NULL) {
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
