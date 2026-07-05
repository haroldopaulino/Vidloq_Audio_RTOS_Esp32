#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "driver/i2s.h"
#include "config.h"

#define I2S_PORT I2S_NUM_0

static const char *TAG = "heltec_audio";
static EventGroupHandle_t wifi_events;
static const int WIFI_CONNECTED_BIT = BIT0;
static uint32_t seq_no = 0;
static uint32_t sent_frames = 0;
static uint32_t reconnects = 0;
static uint32_t zero_frames = 0;
static uint32_t wifi_disconnects = 0;
static uint32_t minute_frames = 0;
static uint32_t minute_bytes = 0;
static uint32_t minute_reconnects = 0;
static uint32_t minute_zero = 0;
static uint32_t minute_peak = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi STA started, connecting to SSID=%s", WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_disconnects++;
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Wi-Fi connected. IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void) {
    if (strlen(WIFI_PASSWORD) == 0 || strcmp(WIFI_PASSWORD, "CHANGE_ME") == 0) {
        ESP_LOGE(TAG, "WIFI_PASSWORD is not configured in main/config.h. Edit it, then rebuild and flash.");
        return;
    }

    wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void i2s_setup(void) {
    i2s_driver_uninstall(I2S_PORT);
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD
    };
    ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pin_config));
    ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_PORT));
    ESP_LOGI(TAG, "Audio ready on I2S0 BCLK=%d WS=%d SD=%d rate=%d TCP 20ms auto-channel audible PCM 95 percent volume", I2S_BCLK, I2S_WS, I2S_SD, SAMPLE_RATE);
}

static int connect_audio_socket(void) {
    char port_text[8];
    snprintf(port_text, sizeof(port_text), "%d", SERVER_PORT);

    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = NULL;
    int gai = getaddrinfo(SERVER_HOST, port_text, &hints, &result);
    if (gai != 0 || result == NULL) {
        ESP_LOGW(TAG, "DNS lookup failed for %s", SERVER_HOST);
        return -1;
    }

    int sock = -1;
    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) continue;

        int yes = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            freeaddrinfo(result);
            return sock;
        }

        close(sock);
        sock = -1;
    }

    freeaddrinfo(result);
    return -1;
}

static int send_all(int sock, const uint8_t *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int ret = send(sock, data + sent, len - sent, 0);
        if (ret <= 0) return -1;
        sent += ret;
    }
    return 0;
}

static int32_t clamp16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return v;
}

static void audio_task(void *arg) {
    int32_t *raw = malloc(SAMPLES_PER_FRAME * 2 * sizeof(int32_t));
    int16_t *pcm = malloc(SAMPLES_PER_FRAME * sizeof(int16_t));
    uint8_t header[24];
    int sock = -1;
    while (1) {
        if (wifi_events == NULL) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        xEventGroupWaitBits(wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
        if (sock < 0) {
            sock = connect_audio_socket();
            if (sock < 0) {
                reconnects++;
                minute_reconnects++;
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
        }
        size_t bytes_read = 0;
        esp_err_t err = i2s_read(I2S_PORT, raw, SAMPLES_PER_FRAME * 2 * sizeof(int32_t), &bytes_read, pdMS_TO_TICKS(1000));
        if (err != ESP_OK || bytes_read == 0) {
            i2s_setup();
            continue;
        }
        int samples = bytes_read / (2 * sizeof(int32_t));
        int left_peak = 0;
        int right_peak = 0;
        int nonzero_left = 0;
        int nonzero_right = 0;
        for (int i = 0; i < samples; i++) {
            int32_t left = raw[(i * 2)] >> MIC_SHIFT_BITS;
            int32_t right = raw[(i * 2) + 1] >> MIC_SHIFT_BITS;
            int abs_l = left < 0 ? -left : left;
            int abs_r = right < 0 ? -right : right;
            if (abs_l > left_peak) left_peak = abs_l;
            if (abs_r > right_peak) right_peak = abs_r;
            if (left != 0) nonzero_left++;
            if (right != 0) nonzero_right++;
        }
        int selected_right = 1;
#if FORCE_AUDIO_CHANNEL == 2
        selected_right = 0;
#elif FORCE_AUDIO_CHANNEL == 1
        selected_right = 1;
#else
        selected_right = right_peak >= left_peak;
#endif
        int peak = selected_right ? right_peak : left_peak;
        int nonzero = selected_right ? nonzero_right : nonzero_left;
        for (int i = 0; i < samples; i++) {
            int32_t chosen = selected_right ? raw[(i * 2) + 1] : raw[(i * 2)];
            int32_t sample = chosen >> MIC_SHIFT_BITS;
            sample = (sample * GAIN_NUM) / GAIN_DEN;
            sample = clamp16(sample);
            pcm[i] = (int16_t)sample;
        }
        if (nonzero == 0) {
            zero_frames++;
            minute_zero++;
            if (zero_frames % 10 == 0) i2s_setup();
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }
        uint32_t pcm_len = samples * sizeof(int16_t);
        memcpy(header, "AUD2", 4);
        uint32_t seq = seq_no++;
        uint16_t sr = SAMPLE_RATE, ch = 1, bits = 16;
        memcpy(header + 4, &seq, 4);
        memcpy(header + 8, &sr, 2);
        memcpy(header + 10, &ch, 2);
        memcpy(header + 12, &bits, 2);
        memcpy(header + 14, &samples, 4);
        memcpy(header + 18, &pcm_len, 4);
        header[22] = 0;
        header[23] = 0;
        if (send_all(sock, header, sizeof(header)) != 0 || send_all(sock, (uint8_t *)pcm, pcm_len) != 0) {
            close(sock);
            sock = -1;
            reconnects++;
            minute_reconnects++;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        sent_frames++;
        minute_frames++;
        minute_bytes += pcm_len;
        if ((uint32_t)peak > minute_peak) minute_peak = peak;
    }
}

static void status_task(void *arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        uint32_t frames = minute_frames;
        uint32_t bytes = minute_bytes;
        uint32_t reconnect_delta = minute_reconnects;
        uint32_t zero_delta = minute_zero;
        uint32_t peak = minute_peak;
        minute_frames = 0;
        minute_bytes = 0;
        minute_reconnects = 0;
        minute_zero = 0;
        minute_peak = 0;
        ESP_LOGI(TAG, "minute report audio_frames=%lu audio_bytes=%lu audio_seconds=%.2f peak=%lu reconnects_this_minute=%lu zero_this_minute=%lu total_frames=%lu total_reconnects=%lu wifi_disconnects=%lu heap=%lu",
            (unsigned long)frames,
            (unsigned long)bytes,
            (double)frames * ((double)SAMPLES_PER_FRAME / (double)SAMPLE_RATE),
            (unsigned long)peak,
            (unsigned long)reconnect_delta,
            (unsigned long)zero_delta,
            (unsigned long)sent_frames,
            (unsigned long)reconnects,
            (unsigned long)wifi_disconnects,
            (unsigned long)esp_get_free_heap_size());
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "HELTEC WIRELESS STICK LITE AUDIO-ONLY RTOS V2.7 TCP AUTO-CHANNEL AUDIBLE PCM");
    ESP_LOGI(TAG, "Server configured as %s:%d", SERVER_HOST, SERVER_PORT);
    ESP_LOGI(TAG, "============================================================");
    i2s_setup();
    wifi_init();
    xTaskCreatePinnedToCore(audio_task, "audio_task", 8192, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(status_task, "status_task", 4096, NULL, 2, NULL, 0);
}
