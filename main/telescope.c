#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

const static char *TAG = "Telescope";

/* ------ consts ---------- */
#define SIDEREAL_DAY_SECONDS 86164.1
#define DAY_SECONDS 86400.0
#define RA_CYCLE_MAX 30
#define RA_CYCLE_MIN 0.01
#define DEC_CYCLE_MAX 30
#define DEC_CYCLE_MIN 0.01
#define RA_SPEED_MAX 450000
#define RA_SPEED_MIN 150
#define DEC_SPEED_MAX 450000
#define DEC_SPEED_MIN 150

/* ------ utils ----------- */
#define SLEEP(ms) vTaskDelay(ms / portTICK_PERIOD_MS)
#define LEN(arr) (sizeof(arr) / sizeof(arr[0]))
#define DUTY_RES LEDC_TIMER_13_BIT
#define DUTY (((1 << DUTY_RES) - 1) / 2)
// #define LOGI(tag, format, ...) ESP_LOGI(tag, "[%lld] "format, esp_timer_get_time(), ##__VA_ARGS__)
// #define LOGE(tag, format, ...) ESP_LOGE(tag, "[%lld] "format, esp_timer_get_time(), ##__VA_ARGS__)
#define LOGI(tag, format, ...)
#define LOGE(tag, format, ...)
/* ------ configs ---------- */
#define WIFI_SSID   CONFIG_WIFI_SSID
#define WIFI_PASS   CONFIG_WIFI_PASS

#define UDP_PORT CONFIG_SERVER_PORT

#define DISPLAY_SCL (CONFIG_DISPLAY_SCL)
#define DISPLAY_SDA (CONFIG_DISPLAY_SDA)

#define GPIO_RA_EN  (CONFIG_GPIO_RA_EN)
#define GPIO_RA_DIR (CONFIG_GPIO_RA_DIR)
#define GPIO_RA_PUL (CONFIG_GPIO_RA_PUL)

#define RA_GEAR_RATIO (CONFIG_RA_GEAR_RATIO)
#define RA_RESOLUTION (CONFIG_RA_RESOLUTION)
#define RA_CYCLE_STEPS (CONFIG_RA_CYCLE_STEPS)

#define GPIO_DEC_EN  (CONFIG_GPIO_DEC_EN)
#define GPIO_DEC_DIR (CONFIG_GPIO_DEC_DIR)
#define GPIO_DEC_PUL (CONFIG_GPIO_DEC_PUL)

#define DEC_GEAR_RATIO (CONFIG_DEC_GEAR_RATIO)
#define DEC_RESOLUTION (CONFIG_DEC_RESOLUTION)
#define DEC_CYCLE_STEPS (CONFIG_DEC_CYCLE_STEPS)

/* ---------- FREQS ---------- */
#define RA_FREQ(cyclesPerSiderealDay) ((int)(RA_CYCLE_STEPS * RA_GEAR_RATIO * RA_RESOLUTION * cyclesPerSiderealDay / SIDEREAL_DAY_SECONDS))
#define DEC_FREQ(cyclesPerDay) ((int)(DEC_CYCLE_STEPS * DEC_GEAR_RATIO * DEC_RESOLUTION * cyclesPerDay / DAY_SECONDS))

#define CMD_PING 0
#define CMD_SET_TRACKING 1
#define CMD_SET_RA_SPEED 2
#define CMD_SET_DEC_SPEED 3
#define CMD_PULSE_GUIDING 4
#define CMD_SET_RA_GUIDE_SPEED 5
#define CMD_SET_DEC_GUIDE_SPEED 6

#define PULSE_GUIDING_NONE 0
#define PULSE_GUIDING_DIR_WEST 4
#define PULSE_GUIDING_DIR_EAST 3
#define PULSE_GUIDING_DIR_NORTH 1
#define PULSE_GUIDING_DIR_SOUTH 2

ledc_channel_config_t ra_pmw_channel = {
    .channel = LEDC_CHANNEL_0,
    .timer_sel = LEDC_TIMER_0,
    .duty = 0,
    .gpio_num = GPIO_RA_PUL,
    .speed_mode = LEDC_HIGH_SPEED_MODE,
};

ledc_timer_config_t ra_pmw_timer = {
    .freq_hz = RA_FREQ(1),
    .duty_resolution = DUTY_RES,
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .timer_num = LEDC_TIMER_0
};

ledc_channel_config_t dec_pmw_channel = {
    .channel = LEDC_CHANNEL_1,
    .timer_sel = LEDC_TIMER_1,
    .duty = 0,
    .gpio_num = GPIO_DEC_PUL,
    .speed_mode = LEDC_HIGH_SPEED_MODE,
};

ledc_timer_config_t dec_pmw_timer = {
    .freq_hz = DEC_FREQ(1),
    .duty_resolution = DUTY_RES,
    .speed_mode = LEDC_HIGH_SPEED_MODE,
    .timer_num = LEDC_TIMER_1
};

int8_t tracking = 0;
char pulseGuiding = 0;
int raSpeed = 0, decSpeed = 0, raGuideSpeed = 7500, decGuideSpeed = 7500;

void updateStepper() {
    double raCyclesPerSiderealDay = raSpeed / 15000.0;
    double decCyclesPerDay = decSpeed / 15000.0;

    switch (pulseGuiding) {
        case PULSE_GUIDING_DIR_NORTH:
            decCyclesPerDay += decGuideSpeed / 15000.0;
            break;
        case PULSE_GUIDING_DIR_SOUTH:
            decCyclesPerDay -= decGuideSpeed / 15000.0;
            break;
        case PULSE_GUIDING_DIR_WEST:
            raCyclesPerSiderealDay += raGuideSpeed / 15000.0;
            break;
        case PULSE_GUIDING_DIR_EAST:
            raCyclesPerSiderealDay -= raGuideSpeed / 15000.0;
            break;
    }

    if (tracking) {
        raCyclesPerSiderealDay += 1;
    }

    if (raCyclesPerSiderealDay < 0) {
        raCyclesPerSiderealDay = -raCyclesPerSiderealDay;
        gpio_set_level(GPIO_RA_DIR, 0);
    } else {
        gpio_set_level(GPIO_RA_DIR, 1);
    }

    int rafreq = RA_FREQ(raCyclesPerSiderealDay);
    if (raCyclesPerSiderealDay < RA_CYCLE_MIN || rafreq == 0) {
        LOGI(TAG, "RA Stop");
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, ra_pmw_channel.channel, 0);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, ra_pmw_channel.channel);
        gpio_set_level(GPIO_RA_EN, 1);
    } else {
        if (raCyclesPerSiderealDay > RA_CYCLE_MAX) raCyclesPerSiderealDay = RA_CYCLE_MAX;        
        LOGI(TAG, "RA Freq: %d", rafreq);
        ledc_set_freq(LEDC_HIGH_SPEED_MODE, ra_pmw_timer.timer_num, rafreq);
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, ra_pmw_channel.channel, DUTY);        
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, ra_pmw_channel.channel);
        gpio_set_level(GPIO_RA_EN, 0);
    }

    if (decCyclesPerDay < 0) {
        decCyclesPerDay = -decCyclesPerDay;
        gpio_set_level(GPIO_DEC_DIR, 0);
    } else {
        gpio_set_level(GPIO_DEC_DIR, 1);
    }


    int decfreq = DEC_FREQ(decCyclesPerDay);
    if (decCyclesPerDay < DEC_CYCLE_MIN || decfreq == 0) {
        LOGI(TAG, "DEC Stop");
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, dec_pmw_channel.channel, 0);
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, dec_pmw_channel.channel);
        gpio_set_level(GPIO_DEC_EN, 1);
    } else {
        if (decCyclesPerDay > DEC_CYCLE_MAX) decCyclesPerDay = DEC_CYCLE_MAX;        
        LOGI(TAG, "DEC Freq: %d", decfreq);
        ledc_set_freq(LEDC_HIGH_SPEED_MODE, dec_pmw_timer.timer_num, decfreq);
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, dec_pmw_channel.channel, DUTY);        
        ledc_update_duty(LEDC_HIGH_SPEED_MODE, dec_pmw_channel.channel);
        gpio_set_level(GPIO_DEC_EN, 0);
    }
}

char ackBuf[18];
int8_t* ackTracking = (int8_t*)ackBuf;
char* ackPulseGuiding = ackBuf + 1;
int* ackRaSpeed = (int*)(ackBuf + 2);
int* ackDecSpeed = (int*)(ackBuf + 6);
int* ackRaGuideSpeed = (int*)(ackBuf + 10);
int* ackDecGuideSpeed = (int*)(ackBuf + 14);

void sendAck(int sock, struct sockaddr_in *addr, socklen_t addrlen) {    
    *ackTracking = tracking;
    *ackPulseGuiding = pulseGuiding;
    *ackRaSpeed = ntohl(raSpeed);
    *ackDecSpeed = ntohl(decSpeed);
    *ackRaGuideSpeed = ntohl(raGuideSpeed);
    *ackDecGuideSpeed = ntohl(decGuideSpeed);
    LOGI(TAG, "ack to %s:%d", inet_ntoa(addr->sin_addr), addr->sin_port);
    sendto(sock, ackBuf, LEN(ackBuf), 0, (struct sockaddr *) addr, addrlen);    
}

esp_timer_handle_t pulseGuidingTimer;
struct sockaddr_in lastPulseGuidingFrom;
socklen_t lastPulseGuidingFromLen;
int lastPulseGuidingSocket;

void pulseGuidingFinished(void* args) {
    pulseGuiding = PULSE_GUIDING_NONE;
    updateStepper();
    LOGI(TAG, "pulseGuide finished");
    sendAck(lastPulseGuidingSocket, &lastPulseGuidingFrom, lastPulseGuidingFromLen);
}

const char* getPulseDirDescr(int dir){
    switch (dir) {
        case PULSE_GUIDING_DIR_WEST:
        return "west";
        case PULSE_GUIDING_DIR_EAST:
        return "east";
        case PULSE_GUIDING_DIR_NORTH:
        return "north";
        case PULSE_GUIDING_DIR_SOUTH:
        return "south";
        default:
        return "unknown";
    }
}

int parse_command(char* buf, unsigned int len, int fromSocket, struct sockaddr_in* from, socklen_t fromlen) {
    char* cmd = buf;
    switch(*cmd) {
        case CMD_PING: {
            if (len != 1) return 0;
            LOGI(TAG, "ping");
        } break;
        case CMD_SET_TRACKING: {
            if (len != 2) return 0;
            int8_t* newTracking = (int8_t*)(buf + 1);
            tracking = *newTracking;
            updateStepper();
            LOGI(TAG, "setTracking: %s", tracking ? (tracking > 0 ? "YES/N" : "YES/S") : "NO");
        } break;
        case CMD_SET_RA_SPEED: {
            if (len != 5) return 0;
            int* newRaSpeed = (int*)(buf + 1);
            raSpeed = ntohl(*newRaSpeed);
            if (raSpeed > RA_SPEED_MAX) raSpeed = RA_SPEED_MAX;
            else if (raSpeed > RA_SPEED_MIN);
            else if (raSpeed > -RA_SPEED_MIN) raSpeed = 0;
            else if (raSpeed > -RA_SPEED_MAX);
            else raSpeed = -RA_SPEED_MAX;
            updateStepper();
            LOGI(TAG, "setRaSpeed: %f", raSpeed / 1000.0);
        } break;
        case CMD_SET_DEC_SPEED: {
            if (len != 5) return 0;
            int* newDecSpeed = (int*)(buf + 1);
            decSpeed = ntohl(*newDecSpeed);
            if (decSpeed > DEC_SPEED_MAX) decSpeed = DEC_SPEED_MAX;
            else if (decSpeed > DEC_SPEED_MIN);
            else if (decSpeed > -DEC_SPEED_MIN) decSpeed = 0;
            else if (decSpeed > -DEC_SPEED_MAX);
            else decSpeed = -DEC_SPEED_MAX;
            updateStepper();
            LOGI(TAG, "setDecSpeed: %f", decSpeed / 1000.0);
        } break;
        case CMD_PULSE_GUIDING: {
            if (len != 4) return 0;
            if (pulseGuiding) return 0;
            char* dir = (char*)(buf + 1);
            short* pulseLengthN = (short*)(buf + 2);
            short pulseLength = htons(*pulseLengthN);
            pulseGuiding = *dir;
            updateStepper();
            lastPulseGuidingFromLen = fromlen;
            memcpy(&lastPulseGuidingFrom, from, fromlen);
            lastPulseGuidingSocket = fromSocket;
            esp_timer_start_once(pulseGuidingTimer, pulseLength * 1000);
            LOGI(TAG, "pulseGuide: %s in %dms", getPulseDirDescr(*dir), pulseLength);
        } break;
        case CMD_SET_RA_GUIDE_SPEED: {
            if (len != 5) return 0;
            int* newRaGuideSpeed = (int*)(buf + 1);
            raGuideSpeed = ntohl(*newRaGuideSpeed);
            if (raGuideSpeed > RA_SPEED_MAX) raGuideSpeed = RA_SPEED_MAX;
            else if (raGuideSpeed > RA_SPEED_MIN);
            else if (raGuideSpeed > -RA_SPEED_MIN) raGuideSpeed = 0;
            else if (raGuideSpeed > -RA_SPEED_MAX);
            else raGuideSpeed = -RA_SPEED_MAX;
            updateStepper();
            LOGI(TAG, "setRaGuideSpeed: %f", raSpeed / 1000.0);
        } break;
        case CMD_SET_DEC_GUIDE_SPEED: {
            if (len != 5) return 0;
            int* newDecGuideSpeed = (int*)(buf + 1);
            decGuideSpeed = ntohl(*newDecGuideSpeed);
            if (decGuideSpeed > DEC_SPEED_MAX) decGuideSpeed = DEC_SPEED_MAX;
            else if (decGuideSpeed > DEC_SPEED_MIN);
            else if (decGuideSpeed > -DEC_SPEED_MIN) decGuideSpeed = 0;
            else if (decGuideSpeed > -DEC_SPEED_MAX);
            else decGuideSpeed = -DEC_SPEED_MAX;
            updateStepper();
            LOGI(TAG, "setDecGuideSpeed: %f", decGuideSpeed / 1000.0);
        } break;
        default:
        LOGI(TAG, "Unknown command: %d", *buf);
        return 0;
        break;
    }
    return 1;
}



void udp_server(void *pvParameter) {

    LOGI(TAG, "Server Started");

    //bind loop
    while (1) {
        struct sockaddr_in saddr = { 0 };
        int sock = -1;
        int err = 0;
        
        sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            LOGE(TAG, "Failed to create socket. Error %d", errno);
            LOGE(TAG, "Retry After 1 second");
            SLEEP(1000);
            continue;
        }

        saddr.sin_family = PF_INET;
        saddr.sin_port = htons(UDP_PORT);
        saddr.sin_addr.s_addr = htonl(INADDR_ANY);
        err = bind(sock, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in));
        if (err < 0) {
            LOGE(TAG, "Failed to bind socket. Error %d", errno);
            LOGE(TAG, "Retry After 1 second");
            SLEEP(1000);
            continue;
        }

        LOGI(TAG, "Server started at %d", UDP_PORT);

        updateStepper();

        //recv loop
        while (1) {
            char buf[129];
            struct sockaddr_in from;
            socklen_t fromlen;
            int count = recvfrom(sock, buf, 128, 0, (struct sockaddr *) &from, &fromlen);
            if (count <= 0) {
                continue;
            }
            parse_command(buf, count, sock, &from, fromlen);
            sendAck(sock, &from, fromlen);
        }
    }
}

static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;
esp_err_t err;
bool connected = false;

void stepper_gpio_init(){
    gpio_pad_select_gpio(GPIO_RA_DIR);
    gpio_set_direction(GPIO_RA_DIR, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_RA_DIR, 1);

    gpio_pad_select_gpio(GPIO_RA_EN);
    gpio_set_direction(GPIO_RA_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_RA_EN, 1);

    gpio_pad_select_gpio(GPIO_DEC_DIR);
    gpio_set_direction(GPIO_DEC_DIR, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_DEC_DIR, 1);

    gpio_pad_select_gpio(GPIO_DEC_EN);
    gpio_set_direction(GPIO_DEC_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_DEC_EN, 1);
}

static void wait_wifi(void *p)
{
    while (1) {
        LOGI(TAG, "Waiting for AP connection...");
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, 1000 / portTICK_PERIOD_MS);
        while(!bits) {
            bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, 1000 / portTICK_PERIOD_MS);
        }
        LOGI(TAG, "Connected to AP");

        connected = true;

        SLEEP(1000);

        esp_timer_create_args_t args = {
            .dispatch_method = ESP_TIMER_TASK,
            .callback = pulseGuidingFinished
        };
        if (esp_timer_create(&args, &pulseGuidingTimer) != ESP_OK) {
            LOGI(TAG, "Failed to create pulse guiding timers");
            continue;
        }
        
        ledc_channel_config(&ra_pmw_channel);
        ledc_timer_config(&ra_pmw_timer);

        ledc_channel_config(&dec_pmw_channel);
        ledc_timer_config(&dec_pmw_timer);

        udp_server(NULL);
        
    }

    vTaskDelete(NULL);
}

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        if (!connected) {
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        } else {
            esp_restart();        
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void wifi_conn_init(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(wifi_event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

void app_main(void)
{
    LOGI("BOOT", "stepper_gpio_init");
    stepper_gpio_init();
    LOGI("BOOT", "esp_timer_init");
    esp_timer_init();
    LOGI("BOOT", "nvs_flash_init");
    ESP_ERROR_CHECK(nvs_flash_init());
    LOGI("BOOT", "wifi_conn_init");
    wifi_conn_init();
    LOGI("BOOT", "xTaskCreate wait_wifi");
    xTaskCreate(wait_wifi, TAG, 4096, NULL, 5, NULL);
}