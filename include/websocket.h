#pragma once

#include <atomic>
#include <driver/gpio.h>
#include <esp_http_client.h>
#include <esp_websocket_client.h>

#include "servo.h"
#include "utils.h"
#include "storage.h"
#include "battery.h"

#define WEBSOCKET_URL "ws://leinne.net:8080/ws"

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

    void sendWelcome(bool upState, bool downState){
        auto device = storage::getDeviceId();
        uint8_t buffer[device.length() + 3] = {
            0x01, // protocol type (0x01: welcome, 0x02: door state, 0x03: switch state)
            0x02, // [data] device type((0x01: checker, 0x02: switch bot)
            (uint8_t) ((upState << 6) | (downState << 4) | (battery::level & 0b1111)) // [data] switch state, battery level
        };
        for(uint8_t i = 0; i < device.length(); ++i){
            buffer[3 + i] = device[i];
        }
        esp_websocket_client_send_with_opcode(webSocket, WS_TRANSPORT_OPCODES_BINARY, buffer, device.length() + 3, portMAX_DELAY);
        cout << "[Socket] 환영 메시지를 전송했습니다.\n";
    }

    void sendSwitchState(ledc_channel_t channel, bool state){
        uint8_t buffer[2] = {
            0x03, // protocol type (0x01: welcome 0x02: door state, 0x03: switch state)
            (uint8_t) ((channel << 6) | (state << 4) | (battery::level & 0b1111))
        };
        esp_websocket_client_send_with_opcode(webSocket, WS_TRANSPORT_OPCODES_BINARY, buffer, 2, portMAX_DELAY);
    }

    bool isConnected(){
        return esp_websocket_client_is_connected(webSocket);
    }

    static void eventHandler(void* object, esp_event_base_t base, int32_t eventId, void* eventData){
        esp_websocket_event_data_t* data = (esp_websocket_event_data_t*) eventData;
        if(eventId == WEBSOCKET_EVENT_DISCONNECTED){
            if(connectServer){
                cout << "[Socket] 연결이 끊어졌습니다.\n";
            }
            connectServer = false;
        }else if(eventId == WEBSOCKET_EVENT_ERROR){
            if(!wifi::connect){
                return;
            }

            cout << "[Socket] esp_tls_stack_err: " << data->error_handle.esp_tls_stack_err << ", status: " << data->error_handle.esp_ws_handshake_status_code << ", socket: " << data->error_handle.esp_transport_sock_errno << "\n";
            switch(data->error_handle.error_type){
                case WEBSOCKET_ERROR_TYPE_NONE:
                    cout << "[Socket] 에러 발생, type: NONE\n";
                    break;
                case WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT:
                    cout << "[Socket] 에러 발생, type: TCP_TRANSPORT\n";
                    break;
                case WEBSOCKET_ERROR_TYPE_PONG_TIMEOUT:
                    cout << "[Socket] 에러 발생, type: PONG_TIMEOUT\n";
                    break;
                case WEBSOCKET_ERROR_TYPE_HANDSHAKE:
                    cout << "[Socket] 에러 발생, type: TYPE_HANDSHAKE\n";
                    break;
            }
        }else if(eventId == WEBSOCKET_EVENT_DATA){
            if(data->op_code == STRING && !connectServer){
                string device(data->data_ptr, data->data_len);
                if(storage::getDeviceId() == device){
                    connectServer = true;
                    std::cout << "[Socket] 서버와 연결되었습니다.\n";
                }else{
                    std::cout << "[Socket] 서버 연결 실패. 기기명 불일치 [device: " << storage::getDeviceId() << ", receive: " << device << ", len: " << data->data_len << "]\n";
                }
            }
        }
    }

    void start(esp_event_handler_t handler){
        esp_websocket_client_config_t socketConfig = {
            .uri = WEBSOCKET_URL,
            .keep_alive_enable = true,
            .reconnect_timeout_ms = 1000,
        };

        while(webSocket == NULL){
            webSocket = esp_websocket_client_init(&socketConfig);
        }
        esp_websocket_register_events(webSocket, WEBSOCKET_EVENT_ANY, handler, NULL);
        esp_websocket_register_events(webSocket, WEBSOCKET_EVENT_ANY, eventHandler, NULL);

        esp_err_t err = ESP_FAIL;
        while(err != ESP_OK){
            err = esp_websocket_client_start(webSocket);
        }
    }
}