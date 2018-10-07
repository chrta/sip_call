
#include "driver/adc.h"
#include "driver/timer.h"
#include "lwip/ip4_addr.h"

#include "audio_client/codec/g711.h"
#include "audio_client/rtp.h"

#include <string.h>

// audio buffer
#define AUDIO_BUFFER_MAX 800
uint8_t audioBuffer[AUDIO_BUFFER_MAX];
uint8_t transmitBuffer[AUDIO_BUFFER_MAX];
uint32_t bufferPointer = 0;

// Notify to transfer audio
bool transmitNow = false;

// RTP Payload Types audio codec
int audio_codec = -1; // should be 0 or 8 (PCMU, PCMA; first given in SDP header SHOULD be used)

// IP and Port of RTP receiver
//IPAddress rtpAddress;
//uint16_t rtpPort;

// Timer handle for audio AD conversion
static intr_handle_t s_timer_handle;

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

static void timer_isr(void* arg)
{
    TIMERG0.int_clr_timers.t0 = 1;
    TIMERG0.hw_timer[0].config.alarm_en = 1;

    portENTER_CRITICAL_ISR(&timerMux); // says that we want to run critical code and don't want to be interrupted
    int adcVal = adc1_get_voltage(ADC1_CHANNEL_6); // reads the ADC

    uint8_t value = 0;
	if (audio_codec == 0)
		value = linear2ulaw(adcVal);
	else if (audio_codec == 8)
		value = linear2alaw(adcVal);

	audioBuffer[bufferPointer] = value; // stores the value
    bufferPointer++;

    if (bufferPointer == AUDIO_BUFFER_MAX) { // when the buffer is full
      bufferPointer = 0;
      memcpy(transmitBuffer, audioBuffer, AUDIO_BUFFER_MAX); // copy buffer into a second buffer
      transmitNow = true; // sets the value true so we know that we can transmit now
    }
    portEXIT_CRITICAL_ISR(&timerMux); // says that we have run our critical code
}

void init_timer(int timer_period_us)
{
    timer_config_t config = {
            .alarm_en = true,
            .counter_en = false,
            .intr_type = TIMER_INTR_LEVEL,
            .counter_dir = TIMER_COUNT_UP,
            .auto_reload = true,
            .divider = 80   /* 1 us per tick */
    };

    timer_init(TIMER_GROUP_0, TIMER_0, &config);
    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, timer_period_us);
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_register(TIMER_GROUP_0, TIMER_0, &timer_isr, NULL, 0, &s_timer_handle);
}

void  start_timer()
{
    timer_start(TIMER_GROUP_0, TIMER_0);
}

void  pause_timer()
{
    timer_pause(TIMER_GROUP_0, TIMER_0);
}

// Use port and ip to transfer RTP data
void audioTransmit()
{
	if (transmitNow) { // checks if the buffer is full
	/*       	    client.write((const uint8_t *)transmitBuffer, sizeof(transmitBuffer)); // sending the buffer to our server
	*/
	transmitNow = false;
	}
}
