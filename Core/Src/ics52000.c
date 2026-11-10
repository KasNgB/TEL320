#include "main.h"
#include "math.h"
#include "console.h"
#include <stdbool.h>
#include <stdio.h>

#define ICS_RAM_BUF ".ram_buf" // -> RAM
#define TIM7_HANDLE htim7
#define SAI_HANDLE hsai_BlockA1
#define ICS_BUFFER_SAMPLES 512
#define ICS_BLOCK_SAMPLES (ICS_BUFFER_SAMPLES / 2)
#define MAGIC_BYTES 0xAA
#define SEND_EVERY 4

// Adjust raw input data
#define AUDIO_SAMPLE_BITS 32
#define AUDIO_FULL_SCALE (1.0f * (1UL << (AUDIO_SAMPLE_BITS - 1)))
#define AUDIO_SAMPLE_SCALE (1.0f / AUDIO_FULL_SCALE)

static uint32_t raw_data_buffer[ICS_BUFFER_SAMPLES] __attribute__((section(ICS_RAM_BUF), aligned(32)));

typedef struct __attribute__((packed)) {
    uint8_t magic;
    float   dbfs;
} frame_t;

static float dbfs_value __attribute__((section(ICS_RAM_BUF), aligned(32)));
static frame_t frame __attribute__((section(ICS_RAM_BUF), aligned(32)));

uint8_t _interrupt_fires;
volatile uint8_t _startup_stage;
static volatile bool _data_valid;
static char tx_buf[24];
static uint32_t count;


static volatile bool _half_ready[2];

static void get_dbfs(uint32_t sample_idx_start)
{
	float sum_sq = 0.0f;
	float peak = 0.0f;
	for (uint32_t sample_idx = sample_idx_start; sample_idx < sample_idx_start + ICS_BLOCK_SAMPLES; ++sample_idx)
	{
		float sample = (float)(int32_t)raw_data_buffer[sample_idx] * AUDIO_SAMPLE_SCALE;
		sum_sq += sample * sample;

		float abs = fabsf(sample);
		if (abs > peak)
			peak = abs;
	}

	float rms = sqrtf(sum_sq / ICS_BLOCK_SAMPLES);
	float dbfs = 20.0f * log10f(rms + 1e-20f);
	dbfs_value = dbfs;
}
/* --------- CALLBACKS --------- */

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef* hsai)
{
    if (hsai->Instance == SAI1_Block_A)
    	_half_ready[0] = true;
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef* hsai)
{
    if (hsai->Instance == SAI1_Block_A)
    	_half_ready[1] = true;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == UART4) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
    }
}

/* --------- STARTUP SEQUENCE --------- */

/* The ICS-52000 assigns itself a TDM slot by counting SCK edges while WS is
 * held low, so PE4 starts as a GPIO and only becomes the SAI frame-sync pin
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
        _ws_pin_as_sai();
        __HAL_TIM_SET_AUTORELOAD(&TIM7_HANDLE, 300 - 1);
    	__HAL_TIM_SET_COUNTER(&TIM7_HANDLE, 0);
        _startup_stage = 1;
    }
    else
    {
        HAL_TIM_Base_Stop_IT(&TIM7_HANDLE);
        _data_valid = true;
    }
}

void ics52000_start(void)
{
	_startup_stage = 0;

	_ws_pin_as_gpio();

    __HAL_TIM_SET_AUTORELOAD(&TIM7_HANDLE, 30 - 1);
    __HAL_TIM_SET_COUNTER(&TIM7_HANDLE, 0);
    HAL_TIM_Base_Start_IT(&TIM7_HANDLE);

    if (HAL_SAI_Receive_DMA(&SAI_HANDLE, (uint8_t*)raw_data_buffer, 512) != HAL_OK)
        Error_Handler();
}


void ics52000_read(void)
{
    for (uint32_t half = 0; half < 2; ++half)
    {
        if (!_half_ready[half]) continue;
        _half_ready[half] = false;

        if (_data_valid && ++count >= SEND_EVERY)
        {
        	count   = 0;
    		get_dbfs(half * ICS_BLOCK_SAMPLES);
    		if (huart4.gState == HAL_UART_STATE_READY)
    		{
    			// Uncomment to send data over serial
				// int len = snprintf(tx_buf, sizeof(tx_buf), "%.1f\r\n", dbfs_value);
				// HAL_UART_Transmit_DMA(&huart2, (uint8_t*)tx_buf, len);

    		    frame.magic = MAGIC_BYTES;
    		    frame.dbfs     = dbfs_value;
				HAL_UART_Transmit_DMA(&huart4, (uint8_t*)&frame, sizeof(frame));
    		}
        }
    }
}
