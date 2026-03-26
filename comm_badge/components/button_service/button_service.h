/*
 * button_service.h — Public API for button input handling.
 *
 * Detects short press (< 1000 ms) and long press (>= 1000 ms) on a
 * single GPIO and posts int values to a caller-supplied FreeRTOS queue.
 *
 * Posted values:
 *   0 — short press  (matches EVT_BUTTON_SHORT in sm_event_t)
 *   1 — long press   (matches EVT_BUTTON_LONG  in sm_event_t)
 *
 * The queue item size must be sizeof(int).
 */
#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * @brief  Initialise the button service.
 *
 * @param gpio_num    GPIO pin connected to the button (active-low).
 * @param event_queue FreeRTOS queue with item size == sizeof(int).
 * @return ESP_OK on success.
 */
esp_err_t button_service_init(gpio_num_t gpio_num, QueueHandle_t event_queue);
