/*
 * playback_service.h — Public API for WAV file playback via MAX98357 (I2S port 0).
 *
 * Reads a WAV file, validates the header, and streams 16-bit PCM samples to
 * the I2S amplifier in a dedicated FreeRTOS task.  When playback finishes
 * (or is stopped early) an EVT_PLAYBACK_DONE event is posted to the
 * app-level event queue so the state machine can advance.
 */
#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>

/**
 * @brief  Initialise the I2S TX channel on port 0 for the MAX98357 amplifier.
 *
 * The sample rate is configured at playback time from the WAV header, so
 * this call just sets up the channel ready for use.
 *
 * @param bclk_gpio  Bit-clock GPIO.
 * @param lrclk_gpio Word-select (LR clock) GPIO.
 * @param dout_gpio  Data-out GPIO → DIN on the amp.
 * @param app_event_queue  Queue to post EVT_PLAYBACK_DONE to when done.
 * @return ESP_OK on success.
 */
esp_err_t playback_service_init(gpio_num_t bclk_gpio,
                                 gpio_num_t lrclk_gpio,
                                 gpio_num_t dout_gpio,
                                 QueueHandle_t app_event_queue);

/**
 * @brief  Start playback of a WAV file.  Returns immediately; audio runs async.
 * @param filepath  Absolute path to a 16-bit PCM WAV file.
 * @return ESP_OK if the playback task was started.
 */
esp_err_t playback_service_start(const char *filepath);

/**
 * @brief  Stop playback early.  Blocks until the task exits (max ~200 ms).
 */
esp_err_t playback_service_stop(void);

/** @brief  True if a playback task is currently running. */
bool playback_service_is_playing(void);
