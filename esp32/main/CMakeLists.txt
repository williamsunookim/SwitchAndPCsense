# The Pro Controller 2 protocol and the UART framing are shared with the Pico
# firmware and covered by its host tests (firmware/tests).
set(shared ${CMAKE_CURRENT_LIST_DIR}/../../firmware/src)
idf_component_register(
    SRCS main.cpp
         ${shared}/switch2_pro_protocol.cpp
         ${shared}/switch2_link.cpp
         ${shared}/aes128.cpp
    INCLUDE_DIRS . ${shared}
    REQUIRES bt nvs_flash esp_driver_uart esp_driver_gpio esp_timer)
# switch_rumble.h uses defaulted operator== (C++20).
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++20>)
# Optional pin override for boards other than ESP32-S3-DevKitC-1.
if(DEFINED LINK_TX_PIN)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LINK_TX_PIN=${LINK_TX_PIN})
endif()
if(DEFINED LINK_RX_PIN)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LINK_RX_PIN=${LINK_RX_PIN})
endif()
