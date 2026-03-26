/*
 * button_service.c — GPIO button input with debounce and press-duration detection.
 *
 * Strategy:
 *   1. A GPIO ISR fires on any edge (press or release) and writes the pin
 *      number to a small internal queue.
 *   2. A dedicated FreeRTOS task reads that queue, waits 20 ms for the
 *      signal to settle (debounce), then reads the stable GPIO level.
 *   3. On a falling edge (press): records the timestamp.
 *      On a rising edge (release): calculates duration and posts
 *      EVT_BUTTON_SHORT or EVT_BUTTON_LONG to the caller's queue.
 */

#include "button_service.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "button_service";

/* Module-level state ---------------------------------------------------- */
static gpio_num_t    s_gpio_num;          /* GPIO we are monitoring        */
static QueueHandle_t s_event_queue;       /* Caller's queue for button evts */
static QueueHandle_t s_isr_queue;         /* Internal: edges from ISR      */
static int64_t       s_press_start_us;    /* Time of confirmed press (µs)  */

/* ISR — runs in IRAM so it is safe during flash cache operations. -------- */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    /* Just signal the task; do the real work outside interrupt context. */
    uint32_t pin = (uint32_t)(uintptr_t)arg;
    xQueueSendFromISR(s_isr_queue, &pin, NULL);
}

/* Debounce / detection task --------------------------------------------- */
static void button_task(void *arg)
{
    uint32_t pin;
    while (1) {
        /* Block until at least one GPIO edge arrives. */
        if (xQueueReceive(s_isr_queue, &pin, portMAX_DELAY)) {
            /* Wait 20 ms for the signal to settle. */
            vTaskDelay(pdMS_TO_TICKS(20));

            /* Drain any extra edges that bounced in during the wait. */
            while (xQueueReceive(s_isr_queue, &pin, 0)) { /* discard */ }

            /* Read the stable GPIO level. */
            int level = gpio_get_level(s_gpio_num);

            if (level == 0) {
                /* Falling edge confirmed — button is pressed. */
                s_press_start_us = esp_timer_get_time();
                ESP_LOGD(TAG, "Button pressed");
            } else {
                /* Rising edge confirmed — button was released. */
                if (s_press_start_us > 0) {
                    int64_t duration_ms =
                        (esp_timer_get_time() - s_press_start_us) / 1000;

                    /* 0 = short (EVT_BUTTON_SHORT), 1 = long (EVT_BUTTON_LONG)
                     * — values match sm_event_t in state_machine.h. */
                    int evt = (duration_ms >= 1000) ? 1 : 0;

                    ESP_LOGI(TAG, "%s press (%lld ms)",
                             (evt == 1) ? "Long" : "Short",
                             duration_ms);

                    /* Post to the caller's queue; drop if it is full. */
                    xQueueSend(s_event_queue, &evt, pdMS_TO_TICKS(50));
                    s_press_start_us = 0;
                }
            }
        }
    }
}

/* Public API ------------------------------------------------------------ */
esp_err_t button_service_init(gpio_num_t gpio_num, QueueHandle_t event_queue)
{
    s_gpio_num    = gpio_num;
    s_event_queue = event_queue;

    /* Small queue to buffer raw ISR notifications (depth = 10 is plenty). */
    s_isr_queue = xQueueCreate(10, sizeof(uint32_t));
    if (s_isr_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create internal ISR queue");
        return ESP_ERR_NO_MEM;
    }

    /* Configure the GPIO: input, pull-up enabled, interrupt on any edge. */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    /* Install the GPIO ISR service (safe to call even if already installed). */
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK(gpio_isr_handler_add(gpio_num, gpio_isr_handler,
                                         (void *)(uintptr_t)gpio_num));

    /* Spawn the debounce / detection task. */
    BaseType_t ret = xTaskCreate(button_task, "button_task",
                                 2048, NULL, 10, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Initialised on GPIO %d", gpio_num);
    return ESP_OK;
}
