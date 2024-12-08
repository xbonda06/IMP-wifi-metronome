#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"

#define BUZZER_PIN 25

#define LEDC_TIMER        LEDC_TIMER_0
#define LEDC_MODE         LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL      LEDC_CHANNEL_0
#define LEDC_DUTY_RES     LEDC_TIMER_8_BIT
#define STRONG_BEAT_FREQUENCY 494
#define WEAK_BEAT_FREQUENCY 440


#define WIFI_SSID "Pixel"
#define WIFI_PASS "12131415"

static int volume = 128;
static int bpm = 120; // Default tempo (beats per minute)
static int time_signature = 4; // Default: 4/4 time signature

// Function to calculate delay in milliseconds based on bpm
static int calculate_delay() {
    if(bpm < 200)
        return (60000 / bpm) - 100;
    else
        return (60000 / bpm) / 2;
}

static int calculate_sound_time(){
    if(bpm < 200)
        return 100;
    else
        return (60000 / bpm) / 2;
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        printf("WIFI CONNECTING...\n");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        printf("WIFI CONNECTED\n");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        printf("WiFi lost connection\n");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        printf("IP Address: " IPSTR "\n", IP2STR(&event->ip_info.ip));
    }
}

void wifi_init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_initiation));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    printf("Connecting to Wi-Fi...\n");
    esp_wifi_connect();
}

void wifi_init_softap() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "Metronome_AP", // Название сети
            .ssid_len = strlen("Metronome_AP"),
            .password = "12345678", // Пароль
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    if (strlen("12345678") == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN; // Если пароль пустой, точка будет открытой
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    printf("Wi-Fi Access Point started. SSID: %s\n", "Metronome_AP");
}


static esp_err_t control_handler(httpd_req_t *req) {
    char* buf;
    size_t buf_len;

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[32];

            // Parse and validate volume
            if (httpd_query_key_value(buf, "volume", param, sizeof(param)) == ESP_OK) {
                int new_volume = atoi(param);
                if (new_volume >= 0 && new_volume <= 255) { // Validate volume range
                    volume = new_volume;
                } else {
                    printf("Invalid volume value: %d. Must be between 0 and 255.\n", new_volume);
                }
            }

            // Parse and validate BPM
            if (httpd_query_key_value(buf, "bpm", param, sizeof(param)) == ESP_OK) {
                int new_bpm = atoi(param);
                if (new_bpm >= 30 && new_bpm <= 300) { // Validate BPM range
                    bpm = new_bpm;
                } else {
                    printf("Invalid BPM value: %d. Must be between 30 and 300.\n", new_bpm);
                }
            }

            // Parse time signature
            if (httpd_query_key_value(buf, "time_signature", param, sizeof(param)) == ESP_OK) {
                time_signature = atoi(param); // No range validation for time_signature
            }
        }
        free(buf);
    }

    // HTML form for user control
    const char* html = "<!DOCTYPE html>"
                       "<html>"
                       "<head>"
                       "<title>Metronome Control</title>"
                       "<style>"
                       "body { font-family: Arial, sans-serif; text-align: center; background-color: #f4f4f9; }"
                       "h1 { color: #333; }"
                       "form { display: inline-block; text-align: left; margin: 20px; background: #fff; padding: 20px; border-radius: 10px; box-shadow: 0 0 10px rgba(0, 0, 0, 0.1); }"
                       "input, select { width: 80%%; padding: 10px; margin: 10px; border: 1px solid #ccc; border-radius: 5px; }"
                       "input[type=submit] { background-color: #007BFF; color: #fff; border: none; cursor: pointer; }"
                       "input[type=submit]:hover { background-color: #0056b3; }"
                       "img { max-width: 200px; margin: 20px auto; }"
                       "</style>"
                       "</head>"
                       "<body>"
                       "<h1>Metronome Control</h1>"
                       "<form action=\"/control\" method=\"get\">"
                       "Volume: <input type=\"number\" name=\"volume\" value=\"%d\" min=\"0\" max=\"255\"><br>"
                       "BPM: <input type=\"number\" name=\"bpm\" value=\"%d\" min=\"30\" max=\"300\"><br>"
                       "Time Signature: <select name=\"time_signature\">"
                       "<option value=\"4\" %s>4/4</option>"
                       "<option value=\"3\" %s>3/4</option>"
                       "<option value=\"2\" %s>2/4</option>"
                       "</select><br>"
                       "<input type=\"submit\" value=\"Update\">"
                       "</form>"
                       "</body>"
                       "</html>";

    char response[2048];
    snprintf(response, sizeof(response), html, volume, bpm,
             time_signature == 4 ? "selected" : "",
             time_signature == 3 ? "selected" : "",
             time_signature == 2 ? "selected" : "");

    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

void start_webserver() {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_start(&server, &config);

    httpd_uri_t uri_control = {
        .uri       = "/control",
        .method    = HTTP_GET,
        .handler   = control_handler,
        .user_ctx  = NULL
    };

    httpd_register_uri_handler(server, &uri_control);
}

void app_main(void) {
    wifi_init_softap(); // Настраиваем точку доступа
    start_webserver();  // Запускаем веб-сервер

    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = WEAK_BEAT_FREQUENCY,
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .channel    = LEDC_CHANNEL,
        .duty       = 0,
        .gpio_num   = BUZZER_PIN,
        .speed_mode = LEDC_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER
    };
    ledc_channel_config(&ledc_channel);

    while (1) {
        for (int i = 0; i < time_signature; i++) {
            if (i == 0) {
                ledc_set_freq(LEDC_MODE, LEDC_TIMER, STRONG_BEAT_FREQUENCY);
            } else {
                ledc_set_freq(LEDC_MODE, LEDC_TIMER, WEAK_BEAT_FREQUENCY);
            }

            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, volume);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

            vTaskDelay(pdMS_TO_TICKS(calculate_sound_time()));

            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

            vTaskDelay(pdMS_TO_TICKS(calculate_delay()));
        }
    }
}