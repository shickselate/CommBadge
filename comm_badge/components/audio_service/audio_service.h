/*
 * audio_service.h — Public API for INMP441 microphone capture (Milestone E2).
 *
 * Capture runs in a dedicated FreeRTOS task.  Every 2 seconds it prints
 * min/max/RMS statistics and an ASCII level meter to the serial monitor so
 * you can confirm the mic is alive without needing any extra tooling.
 *
 * Pin assignments are taken from config.h and passed to audio_service_init()
 * by app_main — this header stays pin-agnostic.
 */
#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdbool.h>

/**
 * @brief  Initialise the INMP441 I2S RX channel (I2S port 1).
 *
 * Must be called once at boot before start_capture().
 * Configures 16 kHz, 32-bit slots, mono left channel.
 *
 * @param sck_gpio   I2S bit-clock GPIO (SCK on the mic).
 * @param ws_gpio    I2S word-select GPIO (WS / LR clock on the mic).
 * @param sd_gpio    I2S serial-data GPIO (SD on the mic → DIN on ESP).
 * @return ESP_OK on success.
 */
esp_err_t audio_service_init(gpio_num_t sck_gpio,
                              gpio_num_t ws_gpio,
                              gpio_num_t sd_gpio);

/**
 * @brief  Start capturing audio.  Spawns the capture task if not running.
 * @return ESP_OK, or ESP_ERR_INVALID_STATE if already capturing.
 */
esp_err_t audio_service_start_capture(void);

/**
 * @brief  Stop capturing audio.  Blocks until the capture task has exited
 *         (max ~500 ms) so I2S is quiet before the caller continues.
 * @return ESP_OK.
 */
esp_err_t audio_service_stop_capture(void);

/** @brief  Returns true if a capture task is currently running. */
bool audio_service_is_capturing(void);
