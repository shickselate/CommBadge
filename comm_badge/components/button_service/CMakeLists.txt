# CMakeLists.txt for button_service component.

idf_component_register(
    SRCS        "button_service.c"
    INCLUDE_DIRS "."
    REQUIRES    esp_driver_gpio esp_timer freertos esp_common
)
