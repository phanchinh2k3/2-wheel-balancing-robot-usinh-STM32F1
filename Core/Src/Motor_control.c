/*
 * Motor_control.c
 *
 *  Created on: Feb 13, 2026
 *      Author: Phan Chinh
 */

#ifndef SRC_MOTOR_CONTROL_C_
#define SRC_MOTOR_CONTROL_C_

#include "Motor_control.h"

void Motor_Init(Motor_t *motor, GPIO_TypeDef *p1, uint16_t pin1, GPIO_TypeDef *p2, uint16_t pin2, TIM_HandleTypeDef *tim, uint32_t ch,int16_t min, int16_t max)
{
	motor->PORT_IN1 = p1;
	motor->PIN_IN1 = pin1;
	motor->PORT_IN2 = p2;
	motor->PIN_IN2 = pin2;
	motor->TIM_PWM = tim;
	motor->CHANNEL_PWM = ch;
	motor->Min_Output = min;
	motor->Max_Output = max;

	HAL_TIM_PWM_Start(motor->TIM_PWM, motor->CHANNEL_PWM);
	Motor_Stop(motor);
}
void Motor_Control(Motor_t *motor, int16_t speed)
{
	// 1. Áp dụng Min_Output (Deadzone compensation)
	if (speed > 0) {
	       speed += motor->Min_Output;
	    } else if (speed < 0) {
	       speed -= motor->Min_Output;
	    }
	// 2. Giới hạn tốc độ (Constrain)
	    if (speed > motor->Max_Output) speed = motor->Max_Output;
	    if (speed < -motor->Max_Output) speed = -motor->Max_Output;
	// 3. Điều khiển hướng (Direction)
	if (speed > 0)
	{
		HAL_GPIO_WritePin(motor->PORT_IN1, motor->PIN_IN1, GPIO_PIN_SET);
		HAL_GPIO_WritePin(motor->PORT_IN2, motor->PIN_IN2, GPIO_PIN_RESET);
	}
	else if (speed < 0)
	{
		HAL_GPIO_WritePin(motor->PORT_IN1, motor->PIN_IN1, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(motor->PORT_IN2, motor->PIN_IN2, GPIO_PIN_SET);
		speed = -speed;
	}
	else
	{
		Motor_Stop(motor);
		return;
	}
	__HAL_TIM_SET_COMPARE(motor->TIM_PWM, motor->CHANNEL_PWM, (uint32_t)speed);
}
void Motor_Stop(Motor_t *motor)
{
    HAL_GPIO_WritePin(motor->PORT_IN1, motor->PIN_IN1, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(motor->PORT_IN2, motor->PIN_IN2, GPIO_PIN_RESET);
    __HAL_TIM_SET_COMPARE(motor->TIM_PWM, motor->CHANNEL_PWM, 0);
}
#endif
