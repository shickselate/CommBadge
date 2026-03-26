/*
 * feedback_service.h — Public API for I2S audio feedback via MAX98357 amplifier.
 *
 * Provides Star Trek communicator–style chirp patterns generated as sine-wave
 * PCM samples and streamed to the amp over I2S.  All chirps run in a dedicated
 * FreeRTOS task so callers are never blocked.
 */
#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

/* Available chirp patterns. */
typedef enum {
    CHIRP_RECORD_START, /**< Ascending sweep 800 → 1800 Hz / 400 ms + 1200 Hz hold / 100 ms */
    CHIRP_RECORD_STOP,  /**< Two descending blips: 1200 Hz then 900 Hz, 120 ms each          */
    CHIRP_SYNC_START,   /**< Single long rising tone 600 → 1400 Hz / 600 ms                  */
    CHIRP_ERROR,        /**< Three short 400 Hz blips, 80 ms each with 80 ms gaps             */
} feedback_chirp_t;

/**
 * @brief  Initialise the feedback service and the ESP-IDF v5 I2S driver.
 *
 * @param bclk_gpio   I2S bit-clock GPIO.
 * @param lrclk_gpio  I2S word-select (LR clock) GPIO.
 * @param dout_gpio   I2S data-out GPIO — connects to DIN on the MAX98357.
 * @return ESP_OK on success.
 */
esp_err_t feedback_service_init(gpio_num_t bclk_gpio,
                                 gpio_num_t lrclk_gpio,
                                 gpio_num_t dout_gpio);

/**
 * @brief  Queue a chirp for playback.  Returns immediately; audio plays async.
 *
 * If a chirp is already playing the new one is queued behind it.
 * The queue depth is 4; oldest items are dropped if it overflows.
 *
 * @param chirp  Which pattern to play.
 * @return ESP_OK if the chirp was accepted into the queue.
 */
esp_err_t feedback_play(feedback_chirp_t chirp);
