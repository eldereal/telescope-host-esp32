#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

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

#include "util.h"
#include "ssd1306.h"
#include "fonts.h"

#include "astro.h"
#include "mount_encoder.h"

#include "protocol.h"
#include "slew.h"
#include "mount.h"
#include "focuser.h"

const static char *TAG = "Telescope";

/* ------ configs ---------- */
#define WIFI_SSID   CONFIG_WIFI_SSID
#define WIFI_PASS   CONFIG_WIFI_PASS

#define UDP_PORT CONFIG_SERVER_PORT

#define DISPLAY_SCL (CONFIG_DISPLAY_SCL)
#define DISPLAY_SDA (CONFIG_DISPLAY_SDA)

#define CMD_PING 0
#define CMD_SET_TRACKING 1
#define CMD_SET_RA_SPEED 2
#define CMD_SET_DEC_SPEED 3
#define CMD_PULSE_GUIDING 4
#define CMD_SET_RA_GUIDE_SPEED 5
#define CMD_SET_DEC_GUIDE_SPEED 6
#define CMD_SYNC_TO_TARGET 7
#define CMD_SLEW_TO_TARGET 8
#define CMD_ABORT_SLEW 9
#define CMD_SET_SIDE_OF_PIER 10
#define CMD_SET_TIME_RATIO 101
#define CMD_FOCUSER_MOVE 201
#define CMD_FOCUSER_ABORT 202

#define PULSE_GUIDING_NONE 0
#define PULSE_GUIDING_DIR_WEST 4
#define PULSE_GUIDING_DIR_EAST 3
#define PULSE_GUIDING_DIR_NORTH 1
#define PULSE_GUIDING_DIR_SOUTH 2

typedef struct {
    char * title;
    char * line1;
    char * line2;
    char * line3;
    char line_font;
    char title_font;
} display_t;

void broadcastStatus();

bool displayEnabled = false;

display_t displayed;

void initDisplay() {
    displayed.title = malloc(32);
    displayed.line1 = malloc(32);
    displayed.line2 = malloc(32);
    displayed.line3 = malloc(32);
    bzero(displayed.title, 32);
    bzero(displayed.line1, 32);
    bzero(displayed.line2, 32);
    bzero(displayed.line3, 32);
}

void updateDisplay() {
    LOGI("DISPLAY", "\n    %s\n    %s\n    %s\n    %s", displayed.title, displayed.line1, displayed.line2, displayed.line3);
    if (!displayEnabled) return;
    ssd1306_clear(0);    
    ssd1306_select_font(0, displayed.title_font ? displayed.title_font - 1 : 1);
    if (displayed.title) ssd1306_draw_string(0, 1, 3, displayed.title, 1, 0);
    ssd1306_select_font(0, displayed.line_font ? displayed.line_font - 1 : 1);
    if (displayed.line1) ssd1306_draw_string(0, 1, 19, displayed.line1, 1, 0);
    if (displayed.line2) ssd1306_draw_string(0, 1, 35, displayed.line2, 1, 0);
    if (displayed.line3) ssd1306_draw_string(0, 1, 51, displayed.line3, 1, 0);
    // ssd1306_draw_rectangle(0, 0, 0, 128, 16, 1);
	// ssd1306_draw_rectangle(0, 0, 16, 128, 48, 1);
    ssd1306_refresh(0, true);
}

void updateDisplayTitle(const char* title) {
    strncpy(displayed.title, title, 32);
    updateDisplay();
}

void updateDisplayContent(const char* line1, const char* line2, const char* line3) {
    strncpy(displayed.line1, line1, 32);
    strncpy(displayed.line2, line2, 32);
    strncpy(displayed.line3, line3, 32);
    updateDisplay();
}

int8_t tracking = 0;
int8_t hardware_tracking = 0;
char pulseGuiding = 0;
int raSpeed = 0, decSpeed = 0, raGuideSpeed = 7500, decGuideSpeed = 7500;
uint8_t sideOfPier = 0;

char my_ip[] = "255.255.255.255";
uint32_t my_ip_num;
char my_ip_port[] = "255.255.255.255:12345";
uint16_t my_ip_port_num;
char stepper_line1[] = "R.A. +00.0000 r/d";
char stepper_line2[] = "Dec  +00.0000 r/d";
char stepper_line3[] = "GUIDING/N  TRACKING/N";
display_t stepper_display = {
    .title = my_ip_port,
    .line1 = stepper_line1,
    .line2 = stepper_line2,
    .line3 = stepper_line3,
    .line_font = 1
};

void calcRaAndDecCycles(double *outRaCyclesPerSiderealDay, double *outDecCyclesPerDay) {
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
    *outRaCyclesPerSiderealDay = raCyclesPerSiderealDay;
    *outDecCyclesPerDay = decCyclesPerDay;
}

void updateDisplayStatus(){
    if (!is_slewing()) {
        double raCyclesPerSiderealDay;
        double decCyclesPerDay;
        calcRaAndDecCycles(&raCyclesPerSiderealDay, &decCyclesPerDay);
        char* guidingstr = "   ";
        switch (pulseGuiding) {
            case PULSE_GUIDING_DIR_NORTH:
                guidingstr = "G/N";
                break;
            case PULSE_GUIDING_DIR_SOUTH:
                guidingstr = "G/S";
                break;
            case PULSE_GUIDING_DIR_WEST:
                guidingstr = "G/W";
                break;
            case PULSE_GUIDING_DIR_EAST:
                guidingstr = "G/E";
                break;
        }

        char speedx[9];
        speedx[8] = 0;
        snprintf(speedx, 8, "x%1.4f", get_mount_time_ratio());

        sprintf(stepper_line1, "R.A. %+8.4f r/d", raCyclesPerSiderealDay);
        sprintf(stepper_line2, "Dec  %+8.4f r/d", decCyclesPerDay);
        char* trackingstr = "   ";
        if (tracking > 0) {
            trackingstr = "T/N";
        } else if (tracking < 0) {
            trackingstr = "T/W";
        }
        sprintf(stepper_line3, "%s   %s    %s",guidingstr, speedx, trackingstr);
    } else {
        sprintf(stepper_line3, "                     ");
        int progress = (int)(get_slew_progress() * 100.0);
        int timeToGo = get_slew_time_to_go_millis() / 1000;
        sprintf(stepper_line3, "Slew %d%% eta %02d:%02d", progress, timeToGo / 60, timeToGo % 60);
    }
    updateDisplayContent(stepper_display.line1, stepper_display.line2, stepper_display.line3);
}

void updateStepper() {
    double raCyclesPerSiderealDay;
    double decCyclesPerDay;
    calcRaAndDecCycles(&raCyclesPerSiderealDay, &decCyclesPerDay);

    set_ra_cycles_per_sidereal_day(raCyclesPerSiderealDay);
    set_dec_cycles_per_day(decCyclesPerDay);

    updateDisplayStatus();
}

void slewCallback(double raCyclesPerSiderealDay, double decCyclesPerDay) {
    raSpeed = raCyclesPerSiderealDay * 15000.0;
    decSpeed = decCyclesPerDay * 15000.0;
    updateStepper();
}

void sendAck(int sock, struct sockaddr_in *addr, socklen_t addrlen) {
    ack_t ackBuffer;
    set_ack_fields(&ackBuffer, 0);
    LOGI(TAG, "ack to %s:%d", inet_ntoa(addr->sin_addr), addr->sin_port);
    sendto(sock, ackBuffer.buffer, ACK_SIZE, 0, (struct sockaddr *) addr, addrlen);    
}

esp_timer_handle_t pulseGuidingTimer;
struct sockaddr_in lastPulseGuidingFrom;
socklen_t lastPulseGuidingFromLen;
int lastPulseGuidingSocket;

void pulseGuidingFinished(void* args) {
    pulseGuiding = PULSE_GUIDING_NONE;
    updateStepper();
    LOGI(TAG, "pulseGuide finished");
    broadcastStatus();
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
            if (is_slewing()) return 0;
            if (hardware_tracking) return 0;
            int8_t* newTracking = (int8_t*)(buf + 1);
            tracking = *newTracking;
            updateStepper();
            LOGI(TAG, "setTracking: %s", tracking ? (tracking > 0 ? "YES/N" : "YES/S") : "NO");
        } break;
        case CMD_SET_RA_SPEED: {
            if (len != 5) return 0;
            if (is_slewing()) return 0;
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
            if (is_slewing()) return 0;
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
            if (is_slewing()) return 0;
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
        case CMD_SYNC_TO_TARGET: {
            if (len != 9) return 0;
            if (is_slewing()) return 0;
            int* raMillisPtr = (int*)(buf + 1);
            int* decMillisPtr = (int*)(buf + 5);
            int raMillis = ntohl(*raMillisPtr);
            int decMillis = ntohl(*decMillisPtr);
            set_angles(raMillis, decMillis);
            LOGI(TAG, "syncTo: %d, %d", raMillis, decMillis);
        }break;
        case CMD_SLEW_TO_TARGET: {
            if (is_slewing()) return 0;
            if (pulseGuiding) return 0;
            int* raMillisPtr = (int*)(buf + 1);
            int* decMillisPtr = (int*)(buf + 5);
            int raMillis = ntohl(*raMillisPtr);
            int decMillis = ntohl(*decMillisPtr);
            slew_to_coordinates(raMillis, decMillis);
            LOGI(TAG, "slewTo: %d, %d", raMillis, decMillis);
        }break;
        case CMD_ABORT_SLEW: {
            if (!is_slewing()) return 0;
            abort_slew();
            LOGI(TAG, "abortSlew");
        }break;
        case CMD_SET_SIDE_OF_PIER: {
            if (len != 2) return 0;
            if (is_slewing()) return 0;
            int8_t* newSideOfPier = (int8_t*)(buf + 1);
            sideOfPier = *newSideOfPier;
            int32_t ra = get_ra_angle_millis();
            int32_t dec = get_dec_angle_millis();
            set_angles(ra, dec);
            LOGI(TAG, "setSideOfPier: %s", sideOfPier ? "BeyondThePole/West" : "Normal/East");
        }break;
        case CMD_SET_TIME_RATIO: {
            if (len != 5) return 0;
            int32_t* newTimeRatioPtr = (int32_t*)(buf + 1);            
            double timeRatio = (double)(ntohl(*newTimeRatioPtr)) / 1000000.0;
            set_mount_time_ratio_persist(timeRatio);
            LOGI(TAG, "setTimeRatio: %f", timeRatio);
            updateDisplayStatus();
        }break;
        case CMD_FOCUSER_MOVE: {
            if (len != 5) return 0;
            int32_t* stepsPtr = (int32_t*)(buf + 1);
            int32_t steps = ntohl(*stepsPtr);
            focuser_move(steps);
        }break;
        case CMD_FOCUSER_ABORT: {
            if (len != 1) return 0;
            focuser_abort_move();
        }break;
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

// static EventGroupHandle_t wifi_started_event;
// static EventGroupHandle_t wifi_stopped_event;

#define brdcPorts (CONFIG_SERVER_BROADCAST_PORT_LENGTH)
int brdcFd = -1;
struct sockaddr_in theirAddr[brdcPorts];

void autoDiscoverPrepare() {
    if (brdcFd == -1) {
        brdcFd = socket(PF_INET, SOCK_DGRAM, 0);
            if (brdcFd == -1) {
            LOGE(TAG, "autoDiscover socket fail: %d", errno);
            SLEEP(1000);
            esp_restart();
        }
        int optval = 1;//这个值一定要设置，否则可能导致sendto()失败  
        setsockopt(brdcFd, SOL_SOCKET, SO_BROADCAST | SO_REUSEADDR, &optval, sizeof(int));
        for (int i = 0; i < brdcPorts; i ++) {
            memset(&theirAddr[i], 0, sizeof(struct sockaddr_in));  
            theirAddr[i].sin_family = AF_INET;
            theirAddr[i].sin_addr.s_addr = inet_addr("255.255.255.255");  
            theirAddr[i].sin_port = htons(CONFIG_SERVER_BROADCAST_PORT_START + i);                  
        }
    }
}

void broadcastStatus() {
    broadcast_t data;
    set_broadcast_fields(&data, 
        my_ip_num,
        UDP_PORT,
        get_ra_angle_millis(),
        get_dec_angle_millis(),
        is_slewing(),
        tracking,
        raSpeed,
        decSpeed,
        sideOfPier,
        focuser_get_max_steps(),
        focuser_get_movement_nanos_per_step(),
        focuser_get_is_moving()
    );
    
    for (int i = 0; i < brdcPorts; i ++) {
        // LOGI(TAG, "Auto discover broadcast to port %d", ntohs(theirAddr[i].sin_port));
        sendto(brdcFd, data.buffer, BROADCAST_SIZE, 0, (struct sockaddr *)&(theirAddr[i]), sizeof(struct sockaddr));
    }
}

void autoDiscoverLoop(void* p) {
    LOGI(TAG, "Auto Discover Prepare");
    autoDiscoverPrepare();
    while(1) {
        SLEEP(1000);
        broadcastStatus();        
    }
    LOGI(TAG, "Auto Discover Stopped");
    vTaskDelete(NULL);
}

void track_button_init() {
    gpio_config_t conf;
    conf.intr_type = GPIO_INTR_DISABLE;
    conf.mode = GPIO_MODE_INPUT;
    conf.pin_bit_mask = 1 << CONFIG_GPIO_TRACK_PIN;
    conf.pull_up_en = GPIO_PULLUP_ENABLE;
    conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&conf);
}

static bool TRACK_BTN_PRESSED = 0;

void track_button_loop(void* p) {
    bool oldval = gpio_get_level(CONFIG_GPIO_TRACK_PIN);
    if (oldval == TRACK_BTN_PRESSED) {
        hardware_tracking = true;
        tracking = true;
        updateStepper();
    }
    while(1) {
        SLEEP(100);
        bool val = gpio_get_level(CONFIG_GPIO_TRACK_PIN);
        if (val != oldval) {
            oldval = val;
            if (oldval == TRACK_BTN_PRESSED) {
                hardware_tracking = true;
                tracking = true;
                updateStepper();
            } else {
                hardware_tracking = false;
                tracking = false;
                updateStepper();
            }
        }
    }
}

// static void wait_wifi(void *p)
// {
//     while (1) {
//         LOGI(TAG, "Waiting for AP connection...");
//         int ticks = 0;
//         EventBits_t bits = xEventGroupWaitBits(wifi_started_event, BIT0, false, true, 1000 / portTICK_PERIOD_MS);
//         while(!bits) {
//             switch (ticks)
//             {
//             case 0:
//                 updateDisplayTitle("Searching WiFi");
//                 break;
//             case 1:
//                 updateDisplayTitle("SSID: "WIFI_SSID);
//                 break;
//             case 2:
//                 updateDisplayTitle("PASS: "WIFI_PASS);
//                 break;
//             }
//             ticks = (ticks + 1) % 3;            
//             bits = xEventGroupWaitBits(wifi_started_event, BIT0, false, true, 1000 / portTICK_PERIOD_MS);
//         }
//         LOGI(TAG, "Connected to AP");        
//     }
//     vTaskDelete(NULL);
// }

int disconnect_ticks = 0;

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        LOGI(TAG, "WiFi event SYSTEM_EVENT_STA_START");
        updateDisplayTitle("Searching WiFi");
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        sprintf(my_ip, "%s", inet_ntoa(event->event_info.got_ip.ip_info.ip));
        my_ip_num = ntohl(event->event_info.got_ip.ip_info.ip.addr);
        sprintf(my_ip_port, "%s:%d", my_ip, UDP_PORT);        
        updateDisplayTitle(my_ip_port);
        // xEventGroupSetBits(wifi_started_event, BIT0);
        disconnect_ticks = 0;
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        LOGI(TAG, "WiFi event SYSTEM_EVENT_STA_DISCONNECTED");
        esp_wifi_connect();
        switch (disconnect_ticks)
        {
        case 0:
            updateDisplayTitle("Searching WiFi");
            break;
        case 1:
            updateDisplayTitle("SSID: "WIFI_SSID);
            break;
        case 2:
            updateDisplayTitle("PASS: "WIFI_PASS);
            break;
        }
        disconnect_ticks = (disconnect_ticks + 1) % 3;  
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void wifi_conn_init(void)
{
    tcpip_adapter_init();
    // wifi_started_event = xEventGroupCreate();
    // wifi_stopped_event = xEventGroupCreate();
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
    LOGI("BOOT", "App main");
    LOGI("BOOT", "esp_timer_init");
    ESP_ERROR_CHECK_ALLOW_INVALID_STATE(esp_timer_init());
    LOGI("BOOT", "nvs_flash_init");
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    LOGI("BOOT", "init_mount");
    init_mount();
    LOGI("BOOT", "init_mount_encoder");
    init_mount_encoder();
    LOGI("BOOT", "init_slew");
    init_slew(slewCallback);
    LOGI("BOOT", "focuser_init");
    focuser_init();
    LOGI("BOOT", "ssd1306_init");
    if (ssd1306_init(0, CONFIG_DISPLAY_SCL, CONFIG_DISPLAY_SDA)) {
        LOGI(TAG, "Display inited");
        displayEnabled = true;
    } else {
        LOGE(TAG, "Cannot init display, try again in 1 sec");
        SLEEP(1000);
        if (ssd1306_init(0, CONFIG_DISPLAY_SCL, CONFIG_DISPLAY_SDA)) {
            LOGI(TAG, "Display inited");
            displayEnabled = true;
        } else {
            LOGE(TAG, "Cannot init display");
            displayEnabled = false;
        }
    }
    LOGI("BOOT", "initDisplay");
    initDisplay();

    esp_timer_create_args_t args = {
        .dispatch_method = ESP_TIMER_TASK,
        .callback = pulseGuidingFinished
    };
    if (esp_timer_create(&args, &pulseGuidingTimer) != ESP_OK) {
        LOGI(TAG, "Failed to create pulse guiding timers");
        SLEEP(1000);
        esp_restart();
    }

    LOGI("BOOT", "wifi_conn_init");
    wifi_conn_init();
    track_button_init();
    // LOGI("BOOT", "xTaskCreate wait_wifi");
    // xTaskCreate(wait_wifi, TAG, 4096, NULL, 5, NULL);
    xTaskCreate(autoDiscoverLoop, "autoDiscoverLoop", 4096, NULL, 5, NULL);
    xTaskCreate(udp_server, "udp_server", 4096, NULL, 5, NULL);
    xTaskCreate(track_button_loop, "track_button_loop", 4096, NULL, 5, NULL);
}

uint8_t getSideOfPier() {
    return sideOfPier;
}

/* 90  - 21600000 */
/* 180 - 43200000 */
/* 270 - 64800000 */
/* 360 - 86400000 */
int32_t decMillis2decMecMillis(int32_t decMillis) {
    if (sideOfPier) {
        return 43200000 - decMillis;
    } else {
        return decMillis;
    }
}

int32_t decMecMillis2decMillis(int32_t decMecMillis, uint8_t* parseSideOfPier) {
    while (decMecMillis < 0) {
        decMecMillis += 86400000;
    }
    while (decMecMillis >= 86400000) {
        decMecMillis -= 86400000;
    }/* 0 - 360 */
    if (decMecMillis < 21600000) {
        if (parseSideOfPier) {
            *parseSideOfPier = 0;
        }    
        return decMecMillis;
    }
    if (decMecMillis > 64800000) {
        if (parseSideOfPier) {
            *parseSideOfPier = 0;
        }
        return decMecMillis - 86400000;
    }
    /* 90 - 270 */
    if (parseSideOfPier) {
        *parseSideOfPier = 1;
    }
    return 43200000 - decMecMillis;
}

void setSideOfPierWithDecMecMillis(int32_t decMecMillis) {
    decMecMillis2decMillis(decMecMillis, &sideOfPier);
}






