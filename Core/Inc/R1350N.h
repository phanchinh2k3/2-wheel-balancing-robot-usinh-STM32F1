/*
 * R1350N.h
 *
 *  Created on: Feb 21, 2026
 *      Author: Phan Chinh
 */

#ifndef INC_R1350N_H_
#define INC_R1350N_H_

#include "main.h"
#include <stdbool.h>

#define R1350N_BUFFER_SIZE  64
#define R1350N_PACKET_SIZE 15

typedef struct {
	UART_HandleTypeDef *huart;
	    uint8_t rx_buffer[64];          // Buffer lớn hơn để circular DMA
	    float angle;                    // góc thô từ gyro
	    float rate;                     // tốc độ góc thô
	    float acc_x, acc_y, acc_z;      // gia tốc (g)
	    float filtered_angle;           // góc đã lọc (sẽ dùng cho PID)
	    float gyro_bias;                // bias của gyro (calib lúc khởi động)
	    bool is_data_ready;
} R1350N_t;

// Khởi tạo và bắt đầu nhận DMA
void R1350N_Init(R1350N_t *imu, UART_HandleTypeDef *huart);

// Giải mã dữ liệu (Gọi trong Callback UART DMA)
bool R1350N_Parse(R1350N_t *imu);

// Restart lại DMA nếu có lỗi xảy ra
void R1350N_Restart(R1350N_t *imu);

#endif /* INC_R1350N_H_ */
