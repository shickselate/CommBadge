# CMakeLists.txt for the main component (Milestone E3).

idf_component_register(
    SRCS        "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES    button_service audio_service playback_service storage_service state_machine
)
