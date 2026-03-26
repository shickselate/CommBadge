# CMakeLists.txt for audio_service component.
# Uses ESP-IDF v6 component names for I2S.
# Links libm for sqrtf() used in RMS calculation.

idf_component_register(
    SRCS        "audio_service.c"
    INCLUDE_DIRS "."
    REQUIRES    esp_driver_i2s esp_driver_gpio freertos esp_common
)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)
