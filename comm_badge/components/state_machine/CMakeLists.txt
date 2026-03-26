# CMakeLists.txt for state_machine component.
# No longer depends on button_service — events are now the unified sm_event_t.

idf_component_register(
    SRCS        "state_machine.c"
    INCLUDE_DIRS "."
    REQUIRES    esp_common
)
