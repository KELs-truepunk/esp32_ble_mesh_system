idf_component_register(SRCS "main.c" "ble_mesh_transport.c"
                       INCLUDE_DIRS "."
                       REQUIRES bt nvs_flash)