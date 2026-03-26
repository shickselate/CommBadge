/*
 * feedback_service.c — Sine-wave tone generation via the ESP-IDF v5 I2S driver.
 *
 * How it works:
 *   - The I2S peripheral is configured in standard Philips mode, 44100 Hz,
 *     16-bit mono, driving a MAX98357 I2S amplifier.
 *   - A dedicated FreeRTOS task waits on a chirp queue and generates audio
 *     sample-by-sample using a phase-accumulator sine oscillator.
 *   - Tones are written to I2S in CHUNK_SAMPLES-sized blocks to keep the
 *     stack usage low while still giving smooth audio.
 *   - A short (10 ms) linear fade-in and fade-out is applied to every segment
 *     to prevent audible clicks.
 */

#include "feedback_service.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "feedback_service";

/* Audio parameters ------------------------------------------------------- */
#define SAMPLE_RATE     44100
#define AMPLITUDE       20000   /* Peak amplitude out of 32767 (~61 % FS) */
#define CHUNK_SAMPLES   512     /* Samples per I2S write call              */
#define FADE_MS         10      /* Fade-in / fade-out length in ms         */

/* Module state ----------------------------------------------------------- */
static i2s_chan_handle_t s_tx_chan   = NULL;
static QueueHandle_t     s_chirp_q  = NULL;

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/**
 * Write a frequency sweep (or constant tone when start_hz == end_hz) to I2S.
 * Samples are generated in CHUNK_SAMPLES blocks to limit stack usage.
 *
 * @param start_hz   Starting frequency in Hz.
 * @param end_hz     Ending frequency in Hz (== start_hz for a pure tone).
 * @param duration_ms Duration of the segment in milliseconds.
 */
static void write_sweep(float start_hz, float end_hz, int duration_ms)
{
    int   total_samples = (SAMPLE_RATE * duration_ms) / 1000;
    int   fade_samples  = (SAMPLE_RATE * FADE_MS)     / 1000;
    float phase         = 0.0f;
    const float two_pi  = 2.0f * (float)M_PI;

    int16_t chunk[CHUNK_SAMPLES];

    for (int offset = 0; offset < total_samples; offset += CHUNK_SAMPLES) {
        int n = CHUNK_SAMPLES;
        if (offset + n > total_samples) {
            n = total_samples - offset;
        }

        for (int j = 0; j < n; j++) {
            int   i    = offset + j;
            /* Linear interpolation of frequency across the segment. */
            float t    = (total_samples > 1) ? (float)i / (float)(total_samples - 1) : 0.0f;
            float freq = start_hz + (end_hz - start_hz) * t;

            /* Advance the phase accumulator. */
            phase += two_pi * freq / (float)SAMPLE_RATE;
            /* Wrap to [0, 2π] to avoid floating-point drift. */
            if (phase >= two_pi) phase -= two_pi;

            /* Linear amplitude envelope to avoid clicks. */
            float env = 1.0f;
            if (i < fade_samples) {
                env = (float)i / (float)fade_samples;
            } else if (i > total_samples - fade_samples) {
                env = (float)(total_samples - i) / (float)fade_samples;
            }

            chunk[j] = (int16_t)(sinf(phase) * env * (float)AMPLITUDE);
        }

        size_t bytes_written = 0;
        i2s_channel_write(s_tx_chan,
                          chunk, (size_t)(n * (int)sizeof(int16_t)),
                          &bytes_written,
                          pdMS_TO_TICKS(500));
    }
}

/** Convenience wrapper: constant tone. */
static inline void write_tone(float freq_hz, int duration_ms)
{
    write_sweep(freq_hz, freq_hz, duration_ms);
}

/** Write digital silence (zeros) for @p duration_ms milliseconds. */
static void write_silence(int duration_ms)
{
    int     total_samples = (SAMPLE_RATE * duration_ms) / 1000;
    int16_t chunk[CHUNK_SAMPLES];
    memset(chunk, 0, sizeof(chunk));

    for (int offset = 0; offset < total_samples; offset += CHUNK_SAMPLES) {
        int n = CHUNK_SAMPLES;
        if (offset + n > total_samples) n = total_samples - offset;

        size_t bytes_written = 0;
        i2s_channel_write(s_tx_chan,
                          chunk, (size_t)(n * (int)sizeof(int16_t)),
                          &bytes_written,
                          pdMS_TO_TICKS(500));
    }
}

/* -------------------------------------------------------------------------
 * Chirp patterns
 * ---------------------------------------------------------------------- */

/** Classic Trek communicator: ascending sweep then a short hold. */
static void play_record_start(void)
{
    write_sweep(800.0f, 1800.0f, 400);   /* Ascending 800 → 1800 Hz / 400 ms */
    write_tone(1200.0f, 100);            /* Hold at 1200 Hz / 100 ms          */
}

/** Two short descending blips. */
static void play_record_stop(void)
{
    write_tone(1200.0f, 120);
    write_silence(40);
    write_tone(900.0f, 120);
}

/** Single long rising tone: sync / advertising mode. */
static void play_sync_start(void)
{
    write_sweep(600.0f, 1400.0f, 600);
}

/** Three short blips at a low pitch: error indicator. */
static void play_error(void)
{
    for (int i = 0; i < 3; i++) {
        write_tone(400.0f, 80);
        if (i < 2) write_silence(80);   /* Gap after first two blips only */
    }
}

/* -------------------------------------------------------------------------
 * Chirp task
 * ---------------------------------------------------------------------- */

static void chirp_task(void *arg)
{
    feedback_chirp_t chirp;
    while (1) {
        if (xQueueReceive(s_chirp_q, &chirp, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Playing chirp %d", (int)chirp);
            switch (chirp) {
                case CHIRP_RECORD_START: play_record_start(); break;
                case CHIRP_RECORD_STOP:  play_record_stop();  break;
                case CHIRP_SYNC_START:   play_sync_start();   break;
                case CHIRP_ERROR:        play_error();         break;
                default:
                    ESP_LOGW(TAG, "Unknown chirp type %d", (int)chirp);
                    break;
            }
            /* A short silence between chirps prevents I2S underrun clicks. */
            write_silence(20);
        }
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

esp_err_t feedback_service_init(gpio_num_t bclk_gpio,
                                 gpio_num_t lrclk_gpio,
                                 gpio_num_t dout_gpio)
{
    /* --- Create the I2S TX channel (transmit only, no RX) -------------- */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO,
                                                            I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;   /* Fill DMA buffer with zeros on underrun */
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL));

    /* --- Configure standard Philips I2S, 44100 Hz, 16-bit mono --------- */
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        /* Philips/I2S standard: MSB one BCLK after WS edge.
         * Mono — data goes to the left channel; MAX98357 default is L-ch. */
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                         I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk  = I2S_GPIO_UNUSED,
            .bclk  = bclk_gpio,
            .ws    = lrclk_gpio,
            .dout  = dout_gpio,
            .din   = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_chan));

    /* --- FreeRTOS chirp queue and task --------------------------------- */
    s_chirp_q = xQueueCreate(4, sizeof(feedback_chirp_t));
    if (s_chirp_q == NULL) {
        ESP_LOGE(TAG, "Failed to create chirp queue");
        return ESP_ERR_NO_MEM;
    }

    /* Give the chirp task a generous stack: sine math + 512-sample buffer. */
    BaseType_t ret = xTaskCreate(chirp_task, "chirp_task",
                                 8192, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create chirp task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Initialised — BCLK=%d  LRCLK=%d  DOUT=%d  @ %d Hz 16-bit mono",
             bclk_gpio, lrclk_gpio, dout_gpio, SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t feedback_play(feedback_chirp_t chirp)
{
    if (s_chirp_q == NULL) {
        ESP_LOGE(TAG, "feedback_service not initialised");
        return ESP_ERR_INVALID_STATE;
    }
    /* Non-blocking send; if the queue is full the chirp is dropped. */
    if (xQueueSend(s_chirp_q, &chirp, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Chirp queue full — dropping chirp %d", (int)chirp);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
