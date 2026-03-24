/*
 * R1350N.c
 *
 *  Created on: Feb 21, 2026
 *      Author: Phan Chinh
 */
#ifndef SRC_R1350N_C_
#define SRC_R1350N_C_


#include "R1350N.h"

void R1350N_Init(R1350N_t *imu, UART_HandleTypeDef *huart) {
    imu->huart = huart;
    imu->angle = 0.0f;
    imu->rate = 0.0f;
    imu->is_data_ready = false;

    // Bắt đầu nhận dữ liệu qua DMA (Chế độ Circular là tốt nhất)
    HAL_UART_Receive_DMA(imu->huart, imu->rx_buffer, R1350N_BUFFER_SIZE);
}

bool R1350N_Parse(R1350N_t *imu) {
    for (int i = 0; i <= R1350N_BUFFER_SIZE - 15; i++) {
        if (imu->rx_buffer[i] == 0xAA && imu->rx_buffer[i+1] == 0x00) {
            uint8_t *p = &imu->rx_buffer[i];

            // Checksum đúng chuẩn hãng
            uint8_t check_sum = 0;
            for (int j = 2; j <= 13; j++) {
                check_sum += p[j];
            }

            if (check_sum == p[14]) {
                // Parse đúng theo datasheet hãng
            	int16_t angle_raw = (int16_t)(p[3] | (p[4] << 8));
            	int16_t rate_raw  = (int16_t)(p[5] | (p[6] << 8));
                int16_t acc_x_raw = (int16_t)(p[7] | (p[8] << 8));
                int16_t acc_y_raw = (int16_t)(p[9] | (p[10] << 8));
                int16_t acc_z_raw = (int16_t)(p[11]| (p[12]<< 8));

                imu->rate   = rate_raw  / 100.0f;
                imu->angle  = angle_raw / 100.0f;
                imu->acc_x  = acc_x_raw / 1000.0f;
                imu->acc_y  = acc_y_raw / 1000.0f;
                imu->acc_z  = acc_z_raw / 1000.0f;

                imu->is_data_ready = true;
                HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);   // LED nháy rõ
                return true;
            }
        }
    }
    return false;
}

void R1350N_Restart(R1350N_t *imu) {
    HAL_UART_DMAStop(imu->huart);
    HAL_UART_Receive_DMA(imu->huart, imu->rx_buffer, R1350N_PACKET_SIZE);
}
#endif
