/*
   Copyright 2020 Lennart Planz <lennart.planz@gmail.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include <freertos/queue.h>
#include <freertos/timers.h>

#include <boost/sml.hpp>

#include "driver/gpio.h"


template <gpio_num_t GPIO_PIN, bool ACTIVE_HIGH, int SWITCHING_DURATION_TIMEOUT_MSEC>
class ActuatorHandler
{
public:
	explicit ActuatorHandler()
	{
		gpio_config_t io_conf;
		
		io_conf.intr_type = GPIO_INTR_DISABLE;
		io_conf.mode = GPIO_MODE_OUTPUT;
		io_conf.pin_bit_mask = (1ULL << GPIO_PIN);;
		io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
		io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
		
		gpio_set_level(GPIO_PIN, !ACTIVE_HIGH);
		
		ESP_ERROR_CHECK(gpio_config(&io_conf));
		
		m_timer = xTimerCreate("Actuator Timer", SWITCHING_DURATION_TICKS, pdFALSE, NULL, int_callback);
		if(m_timer == NULL)
		{
			ESP_LOGE(TAG, "Cannot create FreeRTOS timer! Exit..");
			assert(m_timer);
		}
        
        ESP_LOGI(TAG, "Actuator created successfully.");
	}
	
	void trigger()
	{
		gpio_set_level(GPIO_PIN, ACTIVE_HIGH);
		ESP_LOGI(TAG, "Actuator triggered.");
        
		if(xTimerStart(m_timer, 0) != pdPASS)
		{
			ESP_LOGE(TAG, "Cannot start FreeRTOS timer!");
			ESP_LOGI(TAG, "No Problem, use normal delay instead of timer.");
			
			vTaskDelay(SWITCHING_DURATION_TICKS);
			int_action();
		}
	}
private:
	TimerHandle_t m_timer;
	
	static void int_callback( TimerHandle_t timer )
	{
		assert(timer);
		
		int_action();
	}
	
	static void int_action()
	{
		gpio_set_level(GPIO_PIN, !ACTIVE_HIGH);
	}
	
	static constexpr uint32_t SWITCHING_DURATION_TICKS = SWITCHING_DURATION_TIMEOUT_MSEC / portTICK_PERIOD_MS;
	const char* TAG = "actuator_handler";
};