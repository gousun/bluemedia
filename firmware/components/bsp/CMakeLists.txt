idf_component_register(SRCS "led/led.c" "oled/oled.c" "oled/oled_gfx.c" "encoder/encoder.c"
                       INCLUDE_DIRS "led" "oled" "encoder"
                       REQUIRES "driver")
component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)
