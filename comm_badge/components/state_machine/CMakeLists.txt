# CMakeLists.txt for state_machine component.
# Depends on button_service (for button_event_t used in the public header).

idf_component_register(
    SRCS        "state_machine.c"
    INCLUDE_DIRS "."
    REQUIRES    button_service esp_common freertos
)
