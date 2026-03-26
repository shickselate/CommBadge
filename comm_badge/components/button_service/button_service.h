/*
 * button_service.h — Public API for button input handling.
 *
 * Detects short press (< 1000 ms) and long press (>= 1000 ms) on a
 * single GPIO and posts button_event_t values to a caller-supplied
 * FreeRTOS queue.  All GPIO and debounce logic is internal.
 */
#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Events posted to the caller's queue. */
typedef enum {
    EVT_BUTTON_SHORT,   /**< Press duration < 1000 ms */
    EVT_BUTTON_LONG,    /**< Press duration >= 1000 ms */
} button_event_t;

/**
 * @brief  Initialise the button service.
 *
 * Configures the GPIO as input with internal pull-up, installs a GPIO ISR,
 * and spawns a debounce task that posts events to @p event_queue.
 *
 * @param gpio_num    GPIO pin connected to the button (active-low).
 * @param event_queue FreeRTOS queue (item size == sizeof(button_event_t)).
 * @return ESP_OK on success, or an esp_err_t error code.
 */
esp_err_t button_service_init(gpio_num_t gpio_num, QueueHandle_t event_queue);
