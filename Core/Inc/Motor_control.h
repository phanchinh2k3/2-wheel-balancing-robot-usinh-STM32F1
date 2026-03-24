/*
 * Motor_control.h
 *
 *  Created on: Feb 13, 2026
 *      Author: Phan Chinh
 */

#ifndef INC_MOTOR_CONTROL_H_
#define INC_MOTOR_CONTROL_H_

#include "main.h"

typedef struct {
	GPIO_TypeDef *PORT_IN1;
	uint16_t PIN_IN1;
	GPIO_TypeDef *PORT_IN2;
	uint16_t PIN_IN2;
	TIM_HandleTypeDef *TIM_PWM;
	uint32_t CHANNEL_PWM;
	int16_t Min_Output;
	int16_t Max_Output;
}Motor_t;

void Motor_Init(Motor_t *motor, GPIO_TypeDef *p1, uint16_t pin1, GPIO_TypeDef *p2, uint16_t pin2, TIM_HandleTypeDef *tim, uint32_t ch, int16_t Min_Output, int16_t Max_Output);
void Motor_Control(Motor_t *motor, int16_t speed);
void Motor_Stop(Motor_t *motor);
#endif /* INC_MOTOR_CONTROL_H_ */
