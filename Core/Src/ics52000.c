#include "main.h"
#include "math.h"
#include "console.h"
#include <stdbool.h>
#include <stdio.h>

// Handles
#define TIM7_HANDLE htim7
#define SAI_HANDLE hsai_BlockA1

#define ICS_RAM_BUF ".ram_buf" // -> RAM
#define ICS_BUFFER_SAMPLES 512
#define ICS_BLOCK_SAMPLES (ICS_BUFFER_SAMPLES / 2)

// Magic bytes so that the nRF recognize new data
#define MAGIC_BYTES 0xAA

// Adjust raw input data
// These values are used for Decibel Full Scale calculations
#define AUDIO_SAMPLE_BITS 32
#define AUDIO_FULL_SCALE (1.0f * (1UL << (AUDIO_SAMPLE_BITS - 1)))
#define AUDIO_SAMPLE_SCALE (1.0f / AUDIO_FULL_SCALE)

#define MIC_COUNT 2
#define AUDIO_SAMPLES (ICS_BLOCK_SAMPLES / MIC_COUNT)

// DMA places raw data here
static uint32_t raw_data_buffer[ICS_BUFFER_SAMPLES] __attribute__((section(ICS_RAM_BUF), aligned(32)));

// This is the packet being sent over UART
typedef struct __attribute__((packed)) {
    uint8_t magic;           // Magic tells the receiver when new data have arrived
    float   dbfs[MIC_COUNT]; // dBFS values
} frame_t;

static frame_t frame __attribute__((section(ICS_RAM_BUF), aligned(32)));

// Flags to track startup sequence
volatile uint8_t _startup_stage;
static volatile bool _data_valid;

// Keeps track of which half of the ping-pong buffer to read from
static volatile bool _half_ready[2];

static void get_dbfs(uint32_t start, frame_t *frame)
{
	// Calculate dBFS values for each mic
	for (uint32_t mic_idx = 0; mic_idx < MIC_COUNT; ++mic_idx)
	{
		float sum_sq = 0.0f;
        for (uint32_t sample_idx = 0; sample_idx < AUDIO_SAMPLES; ++sample_idx)
		{
            float sample = (float)(int32_t)raw_data_buffer[start + sample_idx * MIC_COUNT + mic_idx] * AUDIO_SAMPLE_SCALE;
			sum_sq += sample * sample;
		}

		float rms = sqrtf(sum_sq / AUDIO_SAMPLES);

		// Sends data to serial monitor over USART2, only used for debugging
		float dbfs = 20.0f * log10f(rms + 1e-20f);
		printf("%.1f", dbfs);
		if (mic_idx + 1 < MIC_COUNT)
		{
			printf(",");
		}

		// Writes data to struct
		frame->dbfs[mic_idx] = 20.0f * log10f(rms + 1e-20f);
	}
	printf("\r\n");
}
/* --------- CALLBACKS --------- */

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef* hsai)
{
	// First half of ping-pong buffer free to read from
    if (hsai->Instance == SAI1_Block_A)
    	_half_ready[0] = true;
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef* hsai)
{
	// Second half of ping-pong buffer free to read from
    if (hsai->Instance == SAI1_Block_A)
    	_half_ready[1] = true;
}

// Use LED as confirmation that data is being transferred
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == UART4) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
    }
}

/* --------- STARTUP SEQUENCE --------- */

/* The ICS-52000 assigns itself a TDM slot by counting SCK edges while WS is
 * held low, so PB9 starts as a GPIO and only becomes the SAI frame-sync pin
 * once the microphones have finished enumerating. */

static void _ws_pin_as_gpio(void)
{
    GPIO_InitTypeDef g = { 0 };

    g.Pin   = GPIO_PIN_9;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    HAL_GPIO_Init(GPIOB, &g);

}

static void _ws_pin_as_sai(void)
{
    GPIO_InitTypeDef g = { 0 };

    g.Pin       = GPIO_PIN_9;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF13_SAI1;

    HAL_GPIO_Init(GPIOB, &g);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef* htim)
{
    if (htim->Instance != TIM7)
        return;

    if (_startup_stage == 0)
    {
    	// 30ms has elapsed since start, set WS active the wait to read data for at least 256ms as specified in the datasheet
        _ws_pin_as_sai();
        __HAL_TIM_SET_AUTORELOAD(&TIM7_HANDLE, 300 - 1);
    	__HAL_TIM_SET_COUNTER(&TIM7_HANDLE, 0);
        _startup_stage = 1;
    }
    else
    {
    	// 300ms has elapsed, data is now valid and ready to be read
        HAL_TIM_Base_Stop_IT(&TIM7_HANDLE);
        _data_valid = true;
    }
}

void ics52000_start(void)
{
	_startup_stage = 0;

	_ws_pin_as_gpio(); // Hold WS pin low as specified in the datasheet

	// Start timer with interrupt after 30ms, See HAL_TIM_PeriodElapsedCallback for interrupt
    __HAL_TIM_SET_AUTORELOAD(&TIM7_HANDLE, 30 - 1);
    __HAL_TIM_SET_COUNTER(&TIM7_HANDLE, 0);
    HAL_TIM_Base_Start_IT(&TIM7_HANDLE);

    if (HAL_SAI_Receive_DMA(&SAI_HANDLE, (uint8_t*)raw_data_buffer, ICS_BUFFER_SAMPLES) != HAL_OK)
        Error_Handler();
}


void ics52000_read(void)
{
	// Iterates over both halves in the ping-pong buffer
    for (uint32_t half = 0; half < 2; ++half)
    {
        if (!_half_ready[half]) continue;
        _half_ready[half] = false;

        if (!_data_valid) continue;

        if (huart4.gState != HAL_UART_STATE_READY) continue;   // previous TX still running

        // Assign values to the struct being sent over UART
        get_dbfs(half * ICS_BLOCK_SAMPLES, &frame);
        frame.magic = MAGIC_BYTES;

        HAL_UART_Transmit_DMA(&huart4, (uint8_t*)&frame, sizeof(frame)); // Send data
    }
}
