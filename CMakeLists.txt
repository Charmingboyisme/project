set(SOURCES_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/sle_server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sle_server_adv.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server_main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ssd1306.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ssd1306_fonts.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mqtt_demo.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cjson_demo.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wifi_connect.c
)

set(SOURCES "${SOURCES}" ${SOURCES_LIST} PARENT_SCOPE)

