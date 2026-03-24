#include "protocol.h"
#include "PID.h"
#include "encoder.h"
#include "R1350N.h"

extern PIDController pid_balance;
extern R1350N_t imu;
extern Encoder_t enc_left;
extern UART_HandleTypeDef huart1;

TelemetryPacket_t tx_packet;

// ==============================================================
// Hàm tính mã CRC8
// ==============================================================
uint8_t CalcCRC8(uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    for(uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(uint8_t j = 0; j < 8; j++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1);
        }
    }
    return crc;
}

/* * Hàm này được gọi mỗi 50ms trong vòng lặp while(1) của main.c
 * Nhiệm vụ: Lấy góc, lấy vận tốc và gửi lên ESP32 để vẽ đồ thị
 */
// Thêm 2 tham số (angle và rpm) vào hàm
void Send_Telemetry(float current_angle, float current_rpm,float kp,float ki,float kd, float kx) {
    // 1. Nhét dữ liệu thực tế vào gói tin
    tx_packet.header1 = 0xAA;
    tx_packet.header2 = 0xBB;
    tx_packet.angle   = current_angle;
    tx_packet.rpm     = current_rpm;
    // Nhét thông số PID hiện tại vào
    tx_packet.kp = kp;
    tx_packet.ki = ki;
    tx_packet.kd = kd;
    tx_packet.kx = kx;
    // 2. Tính lại CRC sau khi đã có dữ liệu mới
    tx_packet.checksum = CalcCRC8((uint8_t*)&tx_packet, sizeof(TelemetryPacket_t) - 2);
    tx_packet.footer = 0xEE;

    // 3. Bắn qua UART sang ESP32
    HAL_UART_Transmit(&huart1, (uint8_t*)&tx_packet, sizeof(TelemetryPacket_t), 10);
}

 /* Nhiệm vụ: Kiểm tra gói tin ESP32 gửi xuống có sạch không, nếu sạch thì nạp vào lệnh
 */
uint8_t Parse_Packet(uint8_t *buf, uint16_t len, CommandPacket_t *cmd) {
    // Nếu rác còn ngắn hơn cả 1 gói tin thì bỏ qua luôn
    if (len < sizeof(CommandPacket_t)) return 0;

    // Quét dọc bộ đệm để tìm đúng gói tin
    for (uint16_t i = 0; i <= len - sizeof(CommandPacket_t); i++) {
        // Tìm đúng 2 Header của ESP32 gửi xuống (0xAA và 0xCC)
        if (buf[i] == 0xAA && buf[i+1] == 0xCC) {
            CommandPacket_t *temp = (CommandPacket_t*)(&buf[i]);

            // Tìm thấy Header rồi, check tiếp Footer xem có bị móp méo không
            if (temp->footer == 0xEE) {
                // Kiểm tra CRC8
                uint8_t calculated_crc = CalcCRC8(&buf[i], sizeof(CommandPacket_t) - 2);
                if (calculated_crc == temp->checksum) {
                    *cmd = *temp; // Data sạch 100%, nạp vào xe!
                    return 1;
                }
            }
        }
    }
    return 0; // Tìm toét mắt không thấy gói nào
}
