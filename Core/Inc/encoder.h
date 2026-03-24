/*
 * encoder.h
 *
 *  Created on: Feb 14, 2026
 *      Author: Phan Chinh
 */

#ifndef INC_ENCODER_H_
#define INC_ENCODER_H_

#include "main.h"
typedef struct {
	TIM_HandleTypeDef *htim;
	int32_t total_counter;
	float speed_rpm;
	float gear_ratio;
	uint16_t ppr;
	int32_t last_counter_value;
}Encoder_t;
void Encoder_Init(Encoder_t *enc, TIM_HandleTypeDef *timer, uint16_t ppr, float ratio);
void Encoder_Update(Encoder_t *enc, float delta_t);
float Encoder_Get_Distance_Revs(Encoder_t *enc);
void Encoder_Reset(Encoder_t *enc);
#endif /* INC_ENCODER_H_ */
