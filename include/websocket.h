#pragma once

#include <atomic>
#include <driver/gpio.h>
#include <esp_http_client.h>
#include <esp_websocket_client.h>

#include "servo.h"
#include "utils.h"
#include "storage.h"
#include "battery.h"

#define DEFAULT_WEBSOCKET_URL "ws://leinne.net:33877/ws"

typedef enum{
    CONTINUITY,
    STRING,
    BINARY,
    QUIT = 0x08,
    PING,
    PONG
} websocket_opcode_t;

namespace ws{
    atomic<bool> connectServer = false;
    esp_websocket_client_handle_t webSocket = NULL;

    void sendWelcome(){
        auto device = storage::getDeviceId();
        uint8_t buffer[device.length() + 3] = {0x02, 0x01, battery::level}; //{device type, data type, battery level}
        for(uint8_t i = 0; i < device.length(); ++i){
            buffer[3 + i] = device[i];
        }
        esp_websocket_client_send_with_opcode(webSocket, WS_TRANSPORT_OPCODES_BINARY, buffer, device.length() + 3, portMAX_DELAY);
        printf("[WS] Send welcome message\n");
    }

    void sendSwitchState(ledc_channel_t channel, bool state){
        uint8_t buffer[3] = {
            0x02, // device type(0x01: checker, 0x02: switch bot)
            0x03, // data type (0x01: welcome 0x02: door state, 0x03: switch state)
            (uint8_t) ((channel << 6) | (state << 4) | (battery::level & 0b1111))
        };
        esp_websocket_client_send_with_opcode(webSocket, WS_TRANSPORT_OPCODES_BINARY, buffer, 3, portMAX_DELAY);
    }

    bool isConnected(){
        return esp_websocket_client_is_connected(webSocket);
    }

    static void eventHandler(void* object, esp_event_base_t base, int32_t eventId, void* eventData){
        esp_websocket_event_data_t* data = (esp_websocket_event_data_t*) eventData;
        if(eventId == WEBSOCKET_EVENT_CONNECTED){
            sendWelcome();
        }else if(eventId == WEBSOCKET_EVENT_DISCONNECTED || eventId == WEBSOCKET_EVENT_ERROR){
            if(connectServer){
                printf("[WS] Disconnected WebSocket\n");
            }
            connectServer = false;
        }else if(eventId == WEBSOCKET_EVENT_DATA){
            if(data->op_code == STRING && !connectServer){
                string device(data->data_ptr, data->data_len);
                if(storage::getDeviceId() == device){
                    connectServer = true;
                    printf("[WS] Connect successful.\n");
                }else{
                    printf("[WS] FAILED. device: %s, receive: %s, len: %d\n", storage::getDeviceId().c_str(), device.c_str(), data->data_len);
                }
            }
        }
    }

    void start(esp_event_handler_t handler){
        auto url = storage::getString("websocket_url");
        if(url.find_first_of("ws") == string::npos){
            storage::setString("websocket_url", url = DEFAULT_WEBSOCKET_URL);
        }

        esp_websocket_client_config_t websocket_cfg = {
            .uri = "ws://leinne.net:33877/ws",
            .keep_alive_enable = true,
            .reconnect_timeout_ms = 1000,
        };

        while(webSocket == NULL){
            webSocket = esp_websocket_client_init(&websocket_cfg);
        }
        esp_websocket_register_events(webSocket, WEBSOCKET_EVENT_ANY, handler, NULL);
        esp_websocket_register_events(webSocket, WEBSOCKET_EVENT_ANY, eventHandler, NULL);

        esp_err_t err = ESP_FAIL;
        while(err != ESP_OK){
            err = esp_websocket_client_start(webSocket);
        }
    }
}