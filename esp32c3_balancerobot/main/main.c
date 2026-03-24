#include <string.h>
#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "lwip/apps/sntp.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_http_server.h"

// ================== CONFIG ==================
#define NVS_NAMESPACE "pid"
#define WIFI_SSID "Full House T6"
#define WIFI_PASSWORD "chuccanhavuive"
#define LED_PIN GPIO_NUM_2
#define MAX_RETRY 10

#define UART_PORT UART_NUM_0
#define UART_TX_PIN GPIO_NUM_21
#define UART_RX_PIN GPIO_NUM_20
#define UART_BAUD 115200
#define BUF_SIZE 256

static const char *TAG = "PID_WEBSERVER";

// ================== PROTOCOL STRUCTS ==================
#pragma pack(push, 1)
typedef struct __attribute__((packed))
{
    uint8_t header1; // 0xAA
    uint8_t header2; // 0xBB
    float angle;
    float rpm;
    float kp;
    float ki;
    float kd;
    float kx;
    uint8_t checksum;
    uint8_t footer; // 0xEE
} TelemetryPacket_t;

typedef struct
{
    uint8_t header1;
    uint8_t header2;
    float kp;
    float ki;
    float kd;
    float kx;
    uint8_t mode;
    uint8_t checksum;
    uint8_t footer;
} CommandPacket_t;
#pragma pack(pop)

// ================== GLOBAL VARIABLES ==================
typedef struct
{
    float kp;
    float ki;
    float kd;
    float kx;
} pid_params_t;

static pid_params_t pid_params = {.kp = 0.0f, .ki = 0.0f, .kd = 0.0f, .kx = 0.0f};

uint8_t CalcCRC8(uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1);
    }
    return crc;
}

volatile float global_angle = 0.0f;
volatile float global_rpm = 0.0f;
volatile float stm_kp = 0, stm_ki = 0, stm_kd = 0, stm_kx = 0;
// ================== WIFI EVENT GROUP ==================
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
static int s_retry_num = 0;

// ================== NVS FUNCTIONS ==================
static esp_err_t pid_load_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
        return err;

    size_t size = sizeof(float);
    if (nvs_get_blob(nvs_handle, "kp", &pid_params.kp, &size) == ESP_OK)
    {
        nvs_get_blob(nvs_handle, "ki", &pid_params.ki, &size);
        nvs_get_blob(nvs_handle, "kd", &pid_params.kd, &size);
        nvs_get_blob(nvs_handle, "kx", &pid_params.kx, &size);
        ESP_LOGI(TAG, "PID loaded → Kp:%.2f Ki:%.2f Kd:%.3f Kx:%.2f",
                 pid_params.kp, pid_params.ki, pid_params.kd, pid_params.kx);
    }
    nvs_close(nvs_handle);
    return err;
}

static esp_err_t pid_save_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
        return err;

    err = nvs_set_blob(nvs_handle, "kp", &pid_params.kp, sizeof(float));
    err |= nvs_set_blob(nvs_handle, "ki", &pid_params.ki, sizeof(float));
    err |= nvs_set_blob(nvs_handle, "kd", &pid_params.kd, sizeof(float));
    err |= nvs_set_blob(nvs_handle, "kx", &pid_params.kx, sizeof(float));

    if (err == ESP_OK)
        err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return err;
}

// ================== HTML PAGE ==================
static const char *html_page =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>PID Tuning</title>"
    "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"
    "<style>"
    "body{font-family:Arial;margin:20px;background:#f0f0f0} h1{color:#333;text-align:center}"
    ".container{max-width:800px;margin:0 auto;background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}"
    ".param{margin:15px 0;padding:15px;background:#f9f9f9;border-radius:5px} label{display:block;font-weight:bold;margin-bottom:8px;color:#555}"
    "input[type=number]{width:100%%;padding:10px;font-size:16px;border:2px solid #ddd;border-radius:5px;box-sizing:border-box}"
    "input[type=range]{width:100%%;height:30px} .value{display:inline-block;min-width:60px;font-size:18px;font-weight:bold;color:#007bff}"
    ".actual{font-size:14px; color:#28a745; margin-left:10px; font-weight:normal;}"
    "button{width:100%%;padding:15px;font-size:18px;background:#007bff;color:white;border:none;border-radius:5px;cursor:pointer;margin-top:10px}"
    "button:hover{background:#0056b3} .info{text-align:center;margin-top:10px;color:#666;font-size:14px}"
    ".chart-container{position:relative;height:350px;width:100%%;margin-top:20px;}"
    "</style>"
    "</head><body><div class='container'><h1>Dashboard PID Robot</h1>"
    
    "<div class='param'><label>Kp: <span class='value' id='kp_val'>%.1f</span> <span class='actual'>(Đang chạy trên xe: <b id='stm_kp'>...</b>)</span></label>"
    "<input type='range' id='kp' min='0' max='150' step='0.1' value='%.1f' oninput=\"updateSlider('kp',this.value)\">"
    "<input type='number' id='kp_num' step='0.1' min='0' max='150' value='%.1f' oninput=\"updateSlider('kp',this.value)\"></div>"
    
    "<div class='param'><label>Ki: <span class='value' id='ki_val'>%.1f</span> <span class='actual'>(Đang chạy trên xe: <b id='stm_ki'>...</b>)</span></label>"
    "<input type='range' id='ki' min='0' max='200' step='0.1' value='%.1f' oninput=\"updateSlider('ki',this.value)\">"
    "<input type='number' id='ki_num' step='0.1' min='0' max='200' value='%.1f' oninput=\"updateSlider('ki',this.value)\"></div>"
    
    "<div class='param'><label>Kd: <span class='value' id='kd_val'>%.3f</span> <span class='actual'>(Đang chạy trên xe: <b id='stm_kd'>...</b>)</span></label>"
    "<input type='range' id='kd' min='0' max='20' step='0.001' value='%.3f' oninput=\"updateSlider('kd',this.value)\">"
    "<input type='number' id='kd_num' step='0.001' min='0' max='20' value='%.3f' oninput=\"updateSlider('kd',this.value)\"></div>"
    
    "<div class='param'><label>Offset Góc (Kx): <span class='value' id='kx_val'>%.1f°</span> <span class='actual'>(Đang chạy trên xe: <b id='stm_kx'>...</b>)</span></label>"
    "<input type='range' id='kx' min='-30' max='30' step='0.1' value='%.1f' oninput=\"updateSlider('kx',this.value)\">"
    "<input type='number' id='kx_num' step='0.1' min='-30' max='30' value='%.1f' oninput=\"updateSlider('kx',this.value)\"></div>"
    
    "<button onclick='sendPID()'>Send PID Parameters</button><span id='status_msg' style='margin-left: 10px; font-weight: bold;'></span>"
    "<div class='info' id='status'>Ready to connect...</div>"
    "<div class='chart-container'><canvas id='pidChart'></canvas></div>"
    "</div>"
    "<script>"
    "function update(p, v) {"
    "    var parsed = parseFloat(v);"
    "    var txt = (p === 'kd') ? parsed.toFixed(3) : parsed.toFixed(1);"
    "    if(p === 'kx') txt += '°';"
    "    document.getElementById(p + '_val').innerText = txt;"
    "    document.getElementById(p + '_num').value = v;"
    "}"
    "function updateSlider(p, v) {"
    "    document.getElementById(p).value = v;"
    "    update(p, v);"
    "}"
    "function sendPID() {"
    "    let p = document.getElementById('kp').value;"
    "    let i = document.getElementById('ki').value;"
    "    let d = document.getElementById('kd').value;"
    "    let x = document.getElementById('kx').value;"
    "    let statusMsg = document.getElementById('status_msg');"
    "    statusMsg.innerText = 'Sending...';"
    "    statusMsg.style.color = 'blue';"
    "    fetch('/set_pid?p='+p+'&i='+i+'&d='+d+'&x='+x, { method: 'GET' })"
    "    .then(response => {"
    "        if (response.ok) {"
    "            statusMsg.innerText = 'Lệnh đã đi khỏi ESP32!';"
    "            statusMsg.style.color = 'green';"
    "            setTimeout(() => { statusMsg.innerText = ''; }, 3000);"
    "        } else {"
    "            statusMsg.innerText = 'Error: Send failed!';"
    "            statusMsg.style.color = 'red';"
    "        }"
    "    })"
    "    .catch(error => {"
    "        statusMsg.innerText = 'Unconnected!';"
    "        statusMsg.style.color = 'red';"
    "    });"
    "}"
    "try {"
    "    const ctx = document.getElementById('pidChart').getContext('2d');"
    "    var pidChart = new Chart(ctx, {"
    "        type: 'line',"
    "        data: {"
    "            labels: Array(50).fill(''),"
    "            datasets: ["
    "                {label: 'Tilt angle (PV)', borderColor: 'red', borderWidth: 2, pointRadius: 0, tension: 0, data: Array(50).fill(0)},"
    "                {label: 'Setpoint (Kx)', borderColor: 'rgba(0,0,0,0.5)', borderWidth: 1, borderDash: [5,5], pointRadius: 0, data: Array(50).fill(0)}"
    "            ]"
    "        },"
    "        options: {"
    "            responsive: true, maintainAspectRatio: false, animation: false,"
    "            scales: { y: { min: -20, max: 20, title: { display: true, text: 'Angle (deg)' } }, x: { display: false } }"
    "        }"
    "    });"
    "} catch (e) { console.error('Lỗi khởi tạo đồ thị:', e); }"
    
    "setInterval(() => {"
    "    fetch('/data')"
    "    .then(r => { if (!r.ok) throw new Error('HTTP Error'); return r.json(); })"
    "    .then(data => {"
    "        let statusDiv = document.getElementById('status');"
    "        statusDiv.innerText = 'Receiving data angle: ' + data.angle.toFixed(2) + '°';"
    "        statusDiv.style.color = 'green';"
    "        "
    "        /* Cập nhật thông số thực tế từ STM32 lên Web */"
    "        document.getElementById('stm_kp').innerText = data.kp.toFixed(1);"
    "        document.getElementById('stm_ki').innerText = data.ki.toFixed(1);"
    "        document.getElementById('stm_kd').innerText = data.kd.toFixed(3);"
    "        document.getElementById('stm_kx').innerText = data.kx.toFixed(1) + '°';"
    "        "
    "        if (typeof pidChart !== 'undefined') {"
    "            let kx_val = parseFloat(document.getElementById('kx').value);"
    "            pidChart.data.datasets[0].data.push(data.angle);"
    "            pidChart.data.datasets[0].data.shift();"
    "            pidChart.data.datasets[1].data.push(kx_val);"
    "            pidChart.data.datasets[1].data.shift();"
    "            pidChart.data.datasets[1].label = 'Setpoint (' + kx_val.toFixed(1) + ')';"
    "            pidChart.update();"
    "        }"
    "    })"
    "    .catch(e => {"
    "        let statusDiv = document.getElementById('status');"
    "        statusDiv.innerText = 'Mất kết nối lấy dữ liệu (/data)';"
    "        statusDiv.style.color = 'red';"
    "    });"
    "}, 100);"
    "</script></body></html>";
// ================== WIFI HANDLER ==================
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < MAX_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ================== WIFI INIT ==================
esp_err_t wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t any_id, got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &got_ip));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_FAIL;
}

// ================== HTTP HANDLERS ==================
static esp_err_t root_get_handler(httpd_req_t *req)
{
    char *buf = (char *)malloc(8192);
    if (!buf)
        return ESP_FAIL;
    snprintf(buf, 8192, html_page,
             pid_params.kp, pid_params.kp, pid_params.kp,
             pid_params.ki, pid_params.ki, pid_params.ki,
             pid_params.kd, pid_params.kd, pid_params.kd,
             pid_params.kx, pid_params.kx, pid_params.kx);
    httpd_resp_set_type(req, "text/html");
    esp_err_t ret = httpd_resp_send(req, buf, strlen(buf));
    free(buf);
    return ret;
}

static esp_err_t set_pid_post_handler(httpd_req_t *req)
{
    float kp = 0, ki = 0, kd = 0, kx = 0;

    // ... (Đoạn code parse req->method để lấy Kp, Ki, Kd bác giữ nguyên nhé) ...
    if (req->method == HTTP_GET)
    {
        /* parse query string p, i, d */
        char buf[128];
        if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK)
        {
            char val[16];
            if (httpd_query_key_value(buf, "p", val, sizeof(val)) == ESP_OK)
                kp = atof(val);
            if (httpd_query_key_value(buf, "i", val, sizeof(val)) == ESP_OK)
                ki = atof(val);
            if (httpd_query_key_value(buf, "d", val, sizeof(val)) == ESP_OK)
                kd = atof(val);
            if (httpd_query_key_value(buf, "x", val, sizeof(val)) == ESP_OK)
                kx = atof(val);
            /* kx handled via query now */
        }
    }
    else
    {
        char buf[256] = {0};
        int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (len <= 0)
            return ESP_FAIL;
        buf[len] = '\0';

        char *token = strtok(buf, "&");
        while (token)
        {
            if (strncmp(token, "kp=", 3) == 0)
                kp = atof(token + 3);
            else if (strncmp(token, "ki=", 3) == 0)
                ki = atof(token + 3);
            else if (strncmp(token, "kd=", 3) == 0)
                kd = atof(token + 3);
            else if (strncmp(token, "kx=", 3) == 0)
                kx = atof(token + 3);
            token = strtok(NULL, "&");
        }
    }

    // 1. GỬI UART XUỐNG STM32 NGAY LẬP TỨC (Ưu tiên số 1 - Nhanh như chớp)
    CommandPacket_t cmd;
    cmd.header1 = 0xAA;
    cmd.header2 = 0xCC;
    cmd.kp = kp;
    cmd.ki = ki;
    cmd.kd = kd;
    cmd.kx = kx;
    cmd.mode = 1;
    cmd.checksum = CalcCRC8((uint8_t *)&cmd, sizeof(CommandPacket_t) - 2);
    cmd.footer = 0xEE;
    uart_write_bytes(UART_PORT, (const char *)&cmd, sizeof(CommandPacket_t));

    // 2. TRẢ LỜI WEBSERVER NGAY (Để nút bấm trên Web không bị lag và đồ thị chạy tiếp)
    httpd_resp_send(req, "OK", 2);

    // 3. NHÁY LED CHỚP NHOÁNG (Không dùng vTaskDelay để tránh block server)
    gpio_set_level((gpio_num_t)LED_PIN, 1);

    // 4. LƯU BỘ NHỚ FLASH (Việc này cực chậm, vứt xuống cuối cùng, lúc này xe đã nhận lệnh rồi)
    pid_params.kp = kp;
    pid_params.ki = ki;
    pid_params.kd = kd;
    pid_params.kx = kx;
    pid_save_to_nvs();

    gpio_set_level((gpio_num_t)LED_PIN, 0);

    return ESP_OK;
}

static esp_err_t data_get_handler(httpd_req_t *req)
{
    char buf[128]; // Tăng lên 128 byte kẻo tràn bộ nhớ
    snprintf(buf, sizeof(buf), "{\"angle\":%.2f, \"rpm\":%.2f, \"kp\":%.2f, \"ki\":%.2f, \"kd\":%.3f, \"kx\":%.2f}",
             global_angle, global_rpm, stm_kp, stm_ki, stm_kd, stm_kx);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    return ESP_OK;
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ================== START SERVER ==================
static void start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler};
        httpd_register_uri_handler(server, &root);

        /* POST registration (legacy) */
        httpd_uri_t set_pid = {.uri = "/set_pid", .method = HTTP_POST, .handler = set_pid_post_handler};
        httpd_register_uri_handler(server, &set_pid);
        /* also allow GET requests used by updated JavaScript */
        httpd_uri_t set_pid_get = {.uri = "/set_pid", .method = HTTP_GET, .handler = set_pid_post_handler};
        httpd_register_uri_handler(server, &set_pid_get);

        httpd_uri_t get_data = {.uri = "/data", .method = HTTP_GET, .handler = data_get_handler};
        httpd_register_uri_handler(server, &get_data);

        httpd_uri_t favicon = {.uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_handler};
        httpd_register_uri_handler(server, &favicon);
        ESP_LOGI(TAG, "Web server started!");
    }
}

// ================== UART TASK (Nhận Telemetry từ STM32) ==================
static void uart_event_task(void *pvParameters)
{
    uint8_t byte;
    uint8_t buffer[sizeof(TelemetryPacket_t)];

    while (1)
    {
        // 1. Đi rình mò tìm cho bằng được Header 1 (0xAA)
        if (uart_read_bytes(UART_PORT, &byte, 1, portMAX_DELAY) == 1 && byte == 0xAA)
        {
            // 2. Thấy 0xAA rồi, ngó xem byte tiếp theo có phải 0xBB không
            if (uart_read_bytes(UART_PORT, &byte, 1, pdMS_TO_TICKS(10)) == 1 && byte == 0xBB)
            {
                buffer[0] = 0xAA;
                buffer[1] = 0xBB;

                // 3. Đúng chuẩn rồi, hốt nốt 10 bytes còn lại của gói tin
                int remaining = sizeof(TelemetryPacket_t) - 2;
                int len = uart_read_bytes(UART_PORT, buffer + 2, remaining, pdMS_TO_TICKS(50));

                if (len == remaining)
                {
                    TelemetryPacket_t *pkg = (TelemetryPacket_t *)buffer;
                    if (pkg->footer == 0xEE)
                    {
                        uint8_t crc = CalcCRC8(buffer, sizeof(TelemetryPacket_t) - 2);
                        if (crc == pkg->checksum)
                        {
                            // THÀNH CÔNG! Gán dữ liệu cho Web vẽ
                            global_angle = pkg->angle;
                            global_rpm = pkg->rpm;

                            // Lấy thông số thực tế từ STM32 đang chạy
                            stm_kp = pkg->kp;
                            stm_ki = pkg->ki;
                            stm_kd = pkg->kd;
                            stm_kx = pkg->kx;

                            // Bắn thẳng góc ra cổng USB (UART0) của ESP32
                            printf("%.2f\n", global_angle);
                        }
                        else
                        {
                            ESP_LOGW(TAG, "Lỗi CRC8 do nhiễu!");
                        }
                    }
                }
            }
        }
    }
}

// ================== APP MAIN ==================
void app_main(void)
{
    // 1. Cấu hình GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    // 2. KHỞI TẠO BỘ NHỚ FLASH TRƯỚC!
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 3. RỒI MỚI ĐƯỢC LOAD DỮ LIỆU TỪ FLASH LÊN RAM
    pid_load_from_nvs();

    if (wifi_init() == ESP_OK)
    {
        start_webserver();
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, "pool.ntp.org");
        sntp_init();
    }

    uart_config_t uart_config = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
    uart_param_config(UART_PORT, &uart_config);
    uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0);

    xTaskCreate(uart_event_task, "uart_task", 4096, NULL, 10, NULL);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
