idf_component_register(
    SRCS
        dashboard.c
        screen_main.c
    INCLUDE_DIRS
        include
    REQUIRES
        lvgl
        main
)
target_compile_definitions(${COMPONENT_TARGET} PRIVATE "FW_VERSION=\"${PROJECT_VER}\"")
