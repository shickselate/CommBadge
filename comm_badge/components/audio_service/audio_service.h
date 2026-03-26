/*
 * audio_service.h — Public API for INMP441 microphone capture (Milestone E3).
 *
 * Captures audio from the INMP441 on I2S port 1 and writes it as a 16-bit
 * mono WAV file to the path supplied by the caller (typically from
 * storage_get_recording_path()).
 *
 * The level meter and debug dump from E2 are retained for diagnosis.
 */
#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdbool.h>

/**
 * @brief  Initialise the INMP441 I2S RX channel (I2S port 1).
 *         Must be called once at boot before start_capture().
 */
esp_err_t audio_service_init(gpio_num_t sck_gpio,
                              gpio_num_t ws_gpio,
                              gpio_num_t sd_gpio);

/**
 * @brief  Start capturing audio and writing to @p filepath.
 *         The file is created (or overwritten) immediately.
 *         Spawns the capture task; returns immediately.
 *
 * @param filepath  Absolute VFS path for the output WAV file.
 * @return ESP_OK, or ESP_ERR_INVALID_STATE if already capturing.
 */
esp_err_t audio_service_start_capture(const char *filepath);

/**
 * @brief  Stop capturing.  Finalises the WAV header and closes the file.
 *         Blocks until the capture task has exited (max ~500 ms).
 * @return ESP_OK.
 */
esp_err_t audio_service_stop_capture(void);

/** @brief  Returns true if a capture task is currently running. */
bool audio_service_is_capturing(void);
