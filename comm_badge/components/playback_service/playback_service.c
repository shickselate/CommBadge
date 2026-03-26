/*
 * playback_service.c — WAV file playback via MAX98357 I2S amplifier (port 0).
 *
 * Flow:
 *   1. playback_service_start() opens the WAV file and spawns a task.
 *   2. The task reads and validates the 44-byte WAV header.
 *   3. PCM data is read in chunks and written to the I2S TX channel.
 *   4. When the file ends (or stop is requested) the task posts
 *      EVT_PLAYBACK_DONE to the app event queue and exits.
 *
 * The I2S channel is reconfigured at each playback start to match the
 * sample rate stored in the WAV header, so recordings at different rates
 * will play back correctly.
 */

#include "playback_service.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static const char *TAG = "playback_service";

/* EVT_PLAYBACK_DONE value posted to the app event queue. */
#define EVT_PLAYBACK_DONE   2   /* matches the value in app_main event enum */

#define PLAYBACK_CHUNK_BYTES  2048   /* bytes per I2S write (1024 × 16-bit samples) */

/* WAV header layout — packed so we can fread it directly. */
typedef struct __attribute__((packed)) {
    char     riff[4];           /* "RIFF"                         */
    uint32_t file_size;         /* total file size - 8            */
    char     wave[4];           /* "WAVE"                         */
    char     fmt[4];            /* "fmt "                         */
    uint32_t fmt_size;          /* 16 for PCM                     */
    uint16_t audio_format;      /* 1 = PCM                        */
    uint16_t num_channels;      /* 1 = mono                       */
    uint32_t sample_rate;       /* e.g. 16000                     */
    uint32_t byte_rate;         /* sample_rate × channels × bps/8 */
    uint16_t block_align;       /* channels × bps/8               */
    uint16_t bits_per_sample;   /* 16                             */
    char     data[4];           /* "data"                         */
    uint32_t data_size;         /* PCM payload size in bytes      */
} wav_header_t;

/* Module state ---------------------------------------------------------- */
static i2s_chan_handle_t  s_tx_chan        = NULL;
static volatile bool      s_playing        = false;
static TaskHandle_t       s_play_task      = NULL;
static QueueHandle_t      s_app_queue      = NULL;
static gpio_num_t         s_bclk_gpio;
static gpio_num_t         s_lrclk_gpio;
static gpio_num_t         s_dout_gpio;

/* Filepath is copied here by playback_service_start(). */
static char s_filepath[128];

/* -------------------------------------------------------------------------
 * I2S channel reconfiguration
 * ---------------------------------------------------------------------- */

static esp_err_t configure_i2s(uint32_t sample_rate, uint16_t bits_per_sample)
{
    /* Disable → reconfigure → enable so sample rate changes take effect. */
    i2s_channel_disable(s_tx_chan);

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        (bits_per_sample == 16) ? I2S_DATA_BIT_WIDTH_16BIT
                                                : I2S_DATA_BIT_WIDTH_32BIT,
                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk  = I2S_GPIO_UNUSED,
            .bclk  = s_bclk_gpio,
            .ws    = s_lrclk_gpio,
            .dout  = s_dout_gpio,
            .din   = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    esp_err_t err = i2s_channel_reconfig_std_clock(s_tx_chan, &std_cfg.clk_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfig I2S clock: %s", esp_err_to_name(err));
        i2s_channel_enable(s_tx_chan);
        return err;
    }
    err = i2s_channel_reconfig_std_slot(s_tx_chan, &std_cfg.slot_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfig I2S slot: %s", esp_err_to_name(err));
        i2s_channel_enable(s_tx_chan);
        return err;
    }

    return i2s_channel_enable(s_tx_chan);
}

/* -------------------------------------------------------------------------
 * Playback task
 * ---------------------------------------------------------------------- */

static void playback_task(void *arg)
{
    FILE *f = fopen(s_filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", s_filepath);
        goto done;
    }

    /* Read and validate WAV header. */
    wav_header_t hdr;
    if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        ESP_LOGE(TAG, "Failed to read WAV header");
        fclose(f);
        goto done;
    }
    if (memcmp(hdr.riff, "RIFF", 4) != 0 || memcmp(hdr.wave, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "Not a valid WAV file");
        fclose(f);
        goto done;
    }
    if (hdr.audio_format != 1) {
        ESP_LOGE(TAG, "Only PCM WAV supported (format=%d)", hdr.audio_format);
        fclose(f);
        goto done;
    }

    ESP_LOGI(TAG, "Playing: %lu Hz, %u-bit, %u ch, %lu bytes PCM",
             (unsigned long)hdr.sample_rate,
             hdr.bits_per_sample,
             hdr.num_channels,
             (unsigned long)hdr.data_size);

    /* Reconfigure I2S to match the file. */
    if (configure_i2s(hdr.sample_rate, hdr.bits_per_sample) != ESP_OK) {
        fclose(f);
        goto done;
    }

    /* Stream PCM data in chunks. */
    static uint8_t pcm_buf[PLAYBACK_CHUNK_BYTES];
    uint32_t remaining = hdr.data_size;

    while (s_playing && remaining > 0) {
        size_t to_read = (remaining < PLAYBACK_CHUNK_BYTES)
                         ? remaining : PLAYBACK_CHUNK_BYTES;
        size_t got = fread(pcm_buf, 1, to_read, f);
        if (got == 0) break;

        size_t written = 0;
        i2s_channel_write(s_tx_chan, pcm_buf, got, &written, pdMS_TO_TICKS(500));
        remaining -= (uint32_t)got;
    }

    fclose(f);
    ESP_LOGI(TAG, "Playback complete");

done:
    /* Drain the I2S DMA buffer with a short silence to avoid a click. */
    {
        static uint8_t silence[256];
        memset(silence, 0, sizeof(silence));
        size_t w = 0;
        i2s_channel_write(s_tx_chan, silence, sizeof(silence), &w, pdMS_TO_TICKS(100));
    }

    /* Notify the app that playback has finished. */
    int evt = EVT_PLAYBACK_DONE;
    if (s_app_queue) {
        xQueueSend(s_app_queue, &evt, 0);
    }

    s_playing    = false;
    s_play_task  = NULL;
    vTaskDelete(NULL);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

esp_err_t playback_service_init(gpio_num_t bclk_gpio,
                                 gpio_num_t lrclk_gpio,
                                 gpio_num_t dout_gpio,
                                 QueueHandle_t app_event_queue)
{
    s_bclk_gpio  = bclk_gpio;
    s_lrclk_gpio = lrclk_gpio;
    s_dout_gpio  = dout_gpio;
    s_app_queue  = app_event_queue;

    /* Create the TX channel at a default rate; it will be reconfigured before
     * each playback to match the WAV file's actual sample rate. */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,
                                                             I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
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

    ESP_LOGI(TAG, "Initialised — BCLK=%d  LRCLK=%d  DOUT=%d",
             bclk_gpio, lrclk_gpio, dout_gpio);
    return ESP_OK;
}

esp_err_t playback_service_start(const char *filepath)
{
    if (s_playing) {
        ESP_LOGW(TAG, "Already playing");
        return ESP_ERR_INVALID_STATE;
    }

    strncpy(s_filepath, filepath, sizeof(s_filepath) - 1);
    s_filepath[sizeof(s_filepath) - 1] = '\0';
    s_playing = true;

    BaseType_t ret = xTaskCreate(playback_task, "playback",
                                  4096, NULL, 5, &s_play_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create playback task");
        s_playing = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t playback_service_stop(void)
{
    if (!s_playing) return ESP_OK;

    s_playing = false;

    /* Wait up to 200 ms for the task to exit naturally. */
    int retries = 20;
    while (s_play_task != NULL && retries-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_play_task != NULL) {
        vTaskDelete(s_play_task);
        s_play_task = NULL;
    }
    return ESP_OK;
}

bool playback_service_is_playing(void)
{
    return s_playing;
}
