# CMakeLists.txt for the main component.
# Lists every component whose public headers app_main.c uses directly.

idf_component_register(
    SRCS        "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES    button_service feedback_service state_machine audio_service
)
