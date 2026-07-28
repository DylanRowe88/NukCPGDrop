idf_component_register(SRCS "es8311.c" "i2s_audio.c" "prompts.c"
    INCLUDE_DIRS "include"
    REQUIRES driver i2c_shared)
