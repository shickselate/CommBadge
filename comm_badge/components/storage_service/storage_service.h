/*
 * storage_service.h — Public API for FAT filesystem on the "audio" flash partition.
 *
 * Mounts the wear-levelled FAT filesystem once at boot.  All other services
 * use the returned path strings to open files via standard stdio (fopen/fwrite).
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief  Mount the FAT filesystem on the "audio" flash partition at /audio.
 *         Safe to call only once; subsequent calls return ESP_ERR_INVALID_STATE.
 * @return ESP_OK on success.
 */
esp_err_t storage_service_init(void);

/**
 * @brief  Return the fixed path used for the current recording.
 *         The file is overwritten each time a new recording starts.
 */
const char *storage_get_recording_path(void);

/**
 * @brief  Return the number of free bytes on the audio partition.
 *         Returns 0 if the filesystem is not mounted.
 */
uint64_t storage_get_free_bytes(void);
