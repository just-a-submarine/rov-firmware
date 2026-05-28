set(srcs
        "main.c"
        "sketch.cpp"
        "net_wifi.cpp"
        "espnow_link.cpp"
        "web_server.cpp"
        "control.cpp"
        "gamepad.cpp")

set(requires
        "bluepad32"
        "bluepad32_arduino"
        "arduino"
        "btstack"
        "AsyncTCP"
        "ESPAsyncWebServer"
        "ArduinoJson"
        "esp_wifi")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "."
                    REQUIRES "${requires}")
