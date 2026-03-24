/*
 * encoder.c
 *
 *  Created on: Feb 14, 2026
 *      Author: Phan Chinh
 *
 */
#ifndef SRC_ENCODER_C_
#define SRC_ENCODER_C_

#include "encoder.h"

void Encoder_Init(Encoder_t *enc, TIM_HandleTypeDef *timer, uint16_t ppr, float ratio) {
    enc->htim = timer;
    enc->ppr = ppr;
    enc->gear_ratio = ratio;
    enc->total_counter = 0;
    enc->last_counter_value = 0;
    enc->speed_rpm = 0;

    HAL_TIM_Encoder_Start(enc->htim, TIM_CHANNEL_ALL);
    __HAL_TIM_SET_COUNTER(enc->htim, 0);
}

void Encoder_Update(Encoder_t *enc, float delta_t) {
    uint32_t current_cnt = __HAL_TIM_GET_COUNTER(enc->htim);

    int16_t delta = (int16_t)(current_cnt - enc->last_counter_value);

    enc->total_counter += delta;

    float pulses_per_rev = enc->ppr * enc->gear_ratio * 4.0f;
    if (delta_t > 0) {
        enc->speed_rpm = ((float)delta / pulses_per_rev) / delta_t * 60.0f;
    }

    enc->last_counter_value = current_cnt;
}

float Encoder_Get_Distance_Revs(Encoder_t *enc) {
    float pulses_per_rev = enc->ppr * enc->gear_ratio * 4.0f;
    return (float)enc->total_counter / pulses_per_rev;
}

void Encoder_Reset(Encoder_t *enc) {
    enc->last_counter_value = 0;
    enc->speed_rpm = 0;
    __HAL_TIM_SET_COUNTER(enc->htim, 0);
}
#endif
