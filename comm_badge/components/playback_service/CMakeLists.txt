# CMakeLists.txt for playback_service component.

idf_component_register(
    SRCS        "playback_service.c"
    INCLUDE_DIRS "."
    REQUIRES    esp_driver_i2s esp_driver_gpio fatfs esp_common freertos
)
