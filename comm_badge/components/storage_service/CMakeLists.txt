# CMakeLists.txt for storage_service component.

idf_component_register(
    SRCS        "storage_service.c"
    INCLUDE_DIRS "."
    REQUIRES    wear_levelling fatfs esp_partition esp_common
)
