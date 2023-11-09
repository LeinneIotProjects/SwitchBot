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

#include "wifi.h"
#include "utils.h"
#include "servo.h"
#include "storage.h"

#define DEEP_SLEEP_DELAY 10000

using namespace std;

atomic<int64_t> lastUpdateTime(0);

void changeSwitchState(ledc_channel_t channel, bool state){
    switch(channel){
        case LEDC_CHANNEL_0:
            servo::setAngle(channel, storage::getInt(state ? "up_on" : "up_off", state ? 0 : 90));
            break;
        case LEDC_CHANNEL_1:
            servo::setAngle(channel, storage::getInt(state ? "down_on" : "down_off", state ? 90 : 0));
            break;
        default:
            return;
    }
    printf("[IR] 모터 상태를 변경했습니다. (channel: %d, state: %s)\n", channel, state ? "true" : "false");
}

/*void irTask(void* args){
    IRrecv irrecv(7, 1024, 15, true);
    irrecv.enableIRIn();

    decode_results data;
    for(;;){
        if(irrecv.decode(&data)){
            if(data.decode_type != NEC){
                if(data.decode_type != UNKNOWN){
                    printf("[IR] NEC 데이터가 아닙니다.\n");
                }
                continue;
            }

            if(data.address == 33877){ // TODO: check device type
                printf("[IR] 스위치봇 IR 신호가 아닙니다.\n");
                continue;
            }

            ledc_channel_t channel = LEDC_CHANNEL_MAX;
            switch(data.command >> 6){
                case 1:
                    channel = LEDC_CHANNEL_0;
                    break;
                case 2:
                    channel = LEDC_CHANNEL_1;
                    break;
                case 3:
                    channel = LEDC_CHANNEL_2;
                    break;
            }
            uint8_t state = (data.command >> 4) & 0b11;
            if(state > 1 || (data.command & 0b1111) > 0){
                printf("[IR] 신호가 올바르지 않습니다. channel: %d, state: %d, unused: %d\n", data.command >> 6, state, data.command & 0b1111);
                continue;
            }
            lastUpdateTime = millis();
            changeSwitchState(channel, state);
        }
    }
}*/

/*void touchTask(void* args){
    uint64_t sum1 = 0, sum2 = 0;
    uint64_t count1 = 0, count2 = 0;

    uint64_t time = millis();
    while(millis() - time < 500){
        ++count1;
        ++count2;
        sum1 += touchRead(GPIO_NUM_2);
        sum2 += touchRead(GPIO_NUM_3);
    }
    uint32_t result1 = sum1 * 100 / count1 / 100 + 500, result2 = sum2 * 100 / count2 / 100 + 500;
    printf("[calibration] gpio2: %lu, gpio3: %lu\n", result1, result2);

    for(;;){
        // TODO: touch on/off switch
    }
}*/

static void wifiHandler(void* arg, esp_event_base_t base, int32_t id, void* data){
    /*if(id == WIFI_EVENT_AP_START){
        web::start();
    }else if(web::stop()){
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    }*/
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
    //ws::start(webSocketHandler);

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
        
        /*time = millis();
        while(!ws::connectServer){
            if(!ws::isConnected() || millis() - time < 500){
                continue;
            }
            time = millis();
            ws::sendWelcome();
        }*/
    }
}

extern "C" void app_main(){
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 10,
        .light_sleep_enable = true
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    servo::init(LEDC_CHANNEL_0, GPIO_NUM_9);
    servo::init(LEDC_CHANNEL_1, GPIO_NUM_8);
    printf("wakeup cause: %d\n", esp_sleep_get_wakeup_cause());
    printf("error: %s\n", esp_err_to_name(esp_sleep_enable_wifi_wakeup()));

    //TaskHandle_t irHandle;
    //xTaskCreatePinnedToCore(irTask, "task", 10000, NULL, 1, &irHandle, 1);

    TaskHandle_t wifiHandle;
    xTaskCreatePinnedToCore(wifiTask, "wifi", 10000, NULL, 1, &wifiHandle, 0);

    //TaskHandle_t touchHandle;
    //xTaskCreatePinnedToCore(touchTask, "touch", 10000, NULL, 1, &touchHandle, 1);

    for(;;){
        /*if(millis() - lastUpdateTime > DEEP_SLEEP_DELAY){
            esp_sleep_enable_ext0_wakeup(GPIO_NUM_7, 0);
            touchSleepWakeUpEnable(GPIO_NUM_4, 40000);
            esp_deep_sleep_start();
        }*/
    }
}