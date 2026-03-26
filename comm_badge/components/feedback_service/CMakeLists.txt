# CMakeLists.txt for feedback_service component.
# Links the C math library for sinf().
idf_component_register(
    SRCS        "feedback_service.c"
    INCLUDE_DIRS "."
    REQUIRES    esp_driver_gpio esp_driver_i2s freertos esp_common
)
# sinf() lives in libm; tell the linker to include it.
target_link_libraries(${COMPONENT_LIB} PRIVATE m)