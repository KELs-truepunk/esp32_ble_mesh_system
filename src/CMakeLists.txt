idf_component_register(
    SRCS 
        "main.c"
        "ble_mesh_transport.c"
        "gap_handler.c"
        "wifi_app.c"
        "web_server.c"
    INCLUDE_DIRS 
        "."
        "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES 
        bt 
        nvs_flash 
        esp_wifi 
        esp_http_server
)

# Автоматическая генерация C-хедера из index.html
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/index.html")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/index.html" HEX_CONTENT HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " C_ARRAY "${HEX_CONTENT}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/index_html_autogen.h"
    "#pragma once\n"
    "static const char index_html[] = { ${C_ARRAY} 0x00 };\n"
)