#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "main.h"
#include <stdint.h>

#pragma pack(push, 1)

typedef struct __attribute__((packed)) {
    uint8_t header1; // 0xAA
    uint8_t header2; // 0xBB
    float angle;
    float rpm;

    float kp;
    float ki;
    float kd;
    float kx;
    // ---------------------------

    uint8_t checksum;
    uint8_t footer;  // 0xEE
} TelemetryPacket_t;

// 2. Gói tin: ESP32 -> STM32 (Gửi hệ số PID từ Webserver)
typedef struct {
    uint8_t header1;    // Luôn là 0xAA
    uint8_t header2;    // Luôn là 0xCC (Khác header gói trên để phân biệt)
    float kp;
    float ki;
    float kd;
    float kx;
    uint8_t mode;       // 1: Chạy motor, 0: Dừng khẩn cấp
    uint8_t checksum;
    uint8_t footer;     // Luôn là 0xEE
} CommandPacket_t;

#pragma pack(pop)
void Send_Telemetry(float current_angle, float current_rpm,float kp,float ki,float kd, float kx);
uint8_t Parse_Packet(uint8_t *buf, uint16_t len, CommandPacket_t *cmd);
#endif
