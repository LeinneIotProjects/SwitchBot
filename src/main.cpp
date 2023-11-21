#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_pm.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_sleep.h>
#include <nvs_flash.h>
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <driver/touch_sensor.h>
#include <atomic>
#include <iostream>
#include <utility>

#define SERVO_ANGLE_360 1
#define DEEP_SLEEP_DELAY 10000

#include "web.h"
#include "wifi.h"
#include "utils.h"
#include "servo.h"
#include "storage.h"
#include "battery.h"
#include "websocket.h"

using namespace std;

RTC_DATA_ATTR atomic<bool> upSwitchState = false;
RTC_DATA_ATTR atomic<uint64_t> upSwitchUpdateTime = 0;

RTC_DATA_ATTR atomic<bool> downSwitchState = false;
RTC_DATA_ATTR atomic<uint64_t> downSwitchUpdateTime = 0;

void changeSwitchState(ledc_channel_t channel, bool state){
    switch(channel){
        case LEDC_CHANNEL_0:
            if(state == upSwitchState){
                return;                
            }
            upSwitchState = state;
            upSwitchUpdateTime = millis();
            #if SERVO_ANGLE_360
            servo::setAngle(channel, state ? 0 : 180);
            #else
            servo::setAngle(channel, state ? 45 : 110);
            #endif
            break;
        case LEDC_CHANNEL_1:
            if(state == downSwitchState){
                return;                
            }
            downSwitchState = state;
            downSwitchUpdateTime = millis();
            #if SERVO_ANGLE_360
            servo::setAngle(channel, state ? 180 : 0);
            #else
            servo::setAngle(channel, state ? 110 : 45);
            #endif
            break;
        default:
            return;
    }
    ws::sendSwitchState(channel, state); // TODO: check thread-safe
    cout << "[Switch] 전등 상태를 변경했습니다. (switch: " << (channel ? "down" : "up") << ", state: " << (state ? "on" : "off") << ")\n";
}

void touchTask(void* args){
    uint64_t sum1 = 0, sum2 = 0;
    uint64_t count1 = 0, count2 = 0;

    uint64_t time = millis();
    while(millis() - time <= 300){
        ++count1;
        ++count2;
        sum1 += touchRead(GPIO_NUM_2);
        sum2 += touchRead(GPIO_NUM_3);
    }
    uint32_t thresholdUp = sum1 / 100 / count1 * 100 + 100, thresholdDown = sum2 / 100 / count2 * 100 + 100;
    cout << "[calibration] touch1: " << thresholdUp << ", touch2: " << thresholdDown << "\n";

    pair<bool, bool> touch(false, false);
    for(;;){
        if(touchRead(GPIO_NUM_2) > thresholdUp + 2500){
            if(!touch.first && millis() - upSwitchUpdateTime >= 500){
                touch.first = true;
                changeSwitchState(LEDC_CHANNEL_0, !upSwitchState);
            }
        }else{
            touch.first = false;
        }

        if(touchRead(GPIO_NUM_3) > thresholdDown + 2500){
            if(!touch.second && millis() - downSwitchUpdateTime >= 1000){
                touch.second = true;
                changeSwitchState(LEDC_CHANNEL_1, !downSwitchState);
            }
        }else{
            touch.second = false;
        }
    }
}

static void webSocketHandler(void* object, esp_event_base_t base, int32_t eventId, void* eventData){
    esp_websocket_event_data_t* data = (esp_websocket_event_data_t*) eventData;
    if(eventId == WEBSOCKET_EVENT_CONNECTED){
        ws::sendWelcome(upSwitchState, downSwitchState);
    }else if(eventId == WEBSOCKET_EVENT_DATA && data->op_code == BINARY){
        if(data->data_len != 1){
            return;
        }
        changeSwitchState((ledc_channel_t) (data->data_ptr[0] >> 4), data->data_ptr[0] & 0b1111);
    }
}

static void wifiHandler(void* arg, esp_event_base_t base, int32_t id, void* data){
    if(id == WIFI_EVENT_AP_START){
        web::start();
    }else if(web::stop()){
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    }
}

static void wifiTask(void* args){
    esp_err_t err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND){
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    
    storage::begin();
    wifi::begin();
    ws::start(webSocketHandler);

    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiHandler, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_START, &wifiHandler, NULL);

    for(;;){
        int64_t time = millis();
        while(!wifi::connect){
            if(
                wifi::getMode() != WIFI_MODE_APSTA &&
                millis() - time >= 6 * 1000
            ){
                wifi::setApMode();
            }
            continue;
        }
        
        time = millis();
        while(!ws::connectServer){
            if(!ws::isConnected() || millis() - time < 500){
                continue;
            }
            time = millis();
            ws::sendWelcome(upSwitchState, downSwitchState);
        }
    }
}

IRAM_ATTR void stopMotor(){
    servo::setAngle(LEDC_CHANNEL_0, 90);
}

extern "C" void app_main(){
    servo::init(LEDC_CHANNEL_0, GPIO_NUM_8);
    servo::init(LEDC_CHANNEL_1, GPIO_NUM_9);

    esp_pm_config_t pm_config = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 10,
        .light_sleep_enable = true
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    TaskHandle_t wifiHandle;
    xTaskCreatePinnedToCore(wifiTask, "wifi", 10000, NULL, 1, &wifiHandle, 0);

    TaskHandle_t touchHandle;
    TaskHandle_t batteryHandle;
    xTaskCreatePinnedToCore(touchTask, "touch", 10000, NULL, 1, &touchHandle, 1);
    xTaskCreatePinnedToCore(battery::calculate, "battery", 10000, NULL, 1, &batteryHandle, 1);

    //bool state = 0; // TEST CODE
    //uint64_t time = millis(); // TEST CODE
    for(;;){
        if(millis() - MAX(upSwitchUpdateTime, downSwitchUpdateTime) > DEEP_SLEEP_DELAY){
            // TODO: !!!!!!!FIND WAKEUP METHOD!!!!!!!!

            /*touchSleepWakeUpEnable(GPIO_NUM_4, 40000);
            esp_deep_sleep_start();*/
        }

        // TEST CODE
        /*if(millis() - time < 2000){
            continue;
        }
        time = millis();
        //servo::setAngle(LEDC_CHANNEL_0, (state = !state) ? 0 : 180);
        changeSwitchState(LEDC_CHANNEL_0, state = !state);
        changeSwitchState(LEDC_CHANNEL_1, state);*/
    }
}