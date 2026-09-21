idf_component_register(SRCS "ble_svc.c"
                       INCLUDE_DIRS "."
                       REQUIRES bt nvs_flash app_state)
