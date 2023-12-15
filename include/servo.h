#pragma once

#include <driver/ledc.h>

#define FREQUENCY 50
#define MAX_ANGLE 180
#define MIN_WIDTH_US 500
#define MAX_WIDTH_US 2500

namespace servo{
    void init(ledc_channel_t channel, gpio_num_t pin){
        ledc_timer_config_t ledc_timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = FREQUENCY,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

        ledc_channel_config_t ledc_ch = {
            .gpio_num = pin,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_ch));
    }

    void setAngle(ledc_channel_t channel, float angle){
        float angle_us = angle / MAX_ANGLE * (MAX_WIDTH_US - MIN_WIDTH_US) + MIN_WIDTH_US;
        ESP_ERROR_CHECK(ledc_set_duty(
            LEDC_LOW_SPEED_MODE,
            channel,
            (uint32_t) (angle_us * ((1 << LEDC_TIMER_10_BIT) - 1) * FREQUENCY / (1000000.0f))
        ));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, channel));
    }

    void turnOff(ledc_channel_t channel){
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, 0));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, channel));
    }
}