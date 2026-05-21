/**
 * ESP Dashboard Plus — ESP32-S3 Test Project
 *
 * Implements the full ESP-DashboardPlus example: all major card types,
 * card groups, real-time updates, console commands, and OTA.
 *
 * Hardware: ESP32-S3 (any devkit)
 * Build:    idf.py -p <PORT> build flash monitor
 * Open:     https://<device-ip>/ in browser (accept self-signed cert)
 *
 * WiFi: set WIFI_SSID / WIFI_PASSWORD below, or via menuconfig
 *       (Component config -> ESP Dashboard Plus Test).
 */

#include <cstring>
#include <cstdlib>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "mdns.h"
#include "esp_dashboard_plus.h"
#include "dashboard_html.h"

static const char* TAG = "dashboard_test";

// ─── WiFi credentials ────────────────────────────────────────────────────────
#ifndef WIFI_SSID
#define WIFI_SSID     "<SSID>"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "<PASSWORD>"
#endif

// ─── WiFi init ───────────────────────────────────────────────────────────────

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static void wifi_event_handler(void*, esp_event_base_t base,
                                int32_t id, void* data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting…");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(data);
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta()
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        wifi_event_handler, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        wifi_event_handler, nullptr, nullptr);

    wifi_config_t wifi_config = {};
    strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid),
            WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy(reinterpret_cast<char*>(wifi_config.sta.password),
            WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
}

// ─── Dashboard ───────────────────────────────────────────────────────────────

static ESPDashboardPlus dashboard("ESP32-S3 Dashboard");

extern "C" void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init_sta();

    // Announce esp-dashboard.local via mDNS so the TLS certificate CN matches.
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("esp-dashboard"));
    mdns_instance_name_set("ESP Dashboard Plus");

    // Start HTTPS + WebSocket server; serve compressed HTML from flash.
    ESP_ERROR_CHECK(dashboard.begin(
        DASHBOARD_HTML_DATA, DASHBOARD_HTML_SIZE,
        /*maxCards=*/  24,
        /*enableOTA=*/ true,
        /*enableConsole=*/ true));

    dashboard.setTitle("ESP32-S3 Dashboard");
    dashboard.setSubtitle("ESP-DashboardPlus Test");
    dashboard.setVersionInfo("1.0.0", "Never");

    // ── Console commands ──────────────────────────────────────────────────
    dashboard.onCommand([](const char* cmd) {
        if (strcmp(cmd, "help") == 0) {
            dashboard.logInfo("Commands: help  status  reboot  mem");
        } else if (strcmp(cmd, "status") == 0) {
            dashboard.logInfo("System running");
        } else if (strcmp(cmd, "mem") == 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Free heap: %lu B",
                     static_cast<unsigned long>(esp_get_free_heap_size()));
            dashboard.logInfo(buf);
        } else if (strcmp(cmd, "reboot") == 0) {
            dashboard.logWarning("Rebooting in 2 s…");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        } else {
            char buf[80];
            snprintf(buf, sizeof(buf), "Unknown command: %s", cmd);
            dashboard.logWarning(buf);
        }
    });

    // ── Sensor cards ─────────────────────────────────────────────────────
    auto* temp = dashboard.addStatCard("temp", "Temperature", "23.5", "°C");
    temp->variant = CardVariant::PRIMARY;

    auto* hum = dashboard.addStatCard("humidity", "Humidity", "45", "%");
    hum->variant = CardVariant::INFO;

    auto* cpu = dashboard.addGaugeCard("cpu", "CPU Load", 0, 100, "%");
    cpu->data.gauge.warnThresh   = 60.0f;
    cpu->data.gauge.dangerThresh = 85.0f;

    auto* wifi_status = dashboard.addStatusCard("wifi", "WiFi", StatusIcon::WIFI);
    wifi_status->variant = CardVariant::SUCCESS;

    // ── Chart cards ───────────────────────────────────────────────────────
    // Multi-series line chart spanning 2 grid columns
    auto* env_chart = dashboard.addChartCard("env_chart", "Environment",
                                              ChartType::LINE, 30);
    env_chart->sizeX = 2;
    int tempIdx = dashboard.addChartSeries("env_chart", "Temperature", "primary");
    int humIdx  = dashboard.addChartSeries("env_chart", "Humidity",    "info");

    // CPU history as area chart
    dashboard.addChartCard("cpu_chart", "CPU History", ChartType::AREA, 30);
    int cpuIdx = dashboard.addChartSeries("cpu_chart", "CPU %", "warning");

    // ── Control cards ─────────────────────────────────────────────────────
    auto* led = dashboard.addToggleCard("led", "Onboard LED", "LED control", false);
    led->onToggle = [](bool val) {
        ESP_LOGI(TAG, "LED -> %s", val ? "ON" : "OFF");
        // gpio_set_level(GPIO_NUM_2, val ? 1 : 0);  // wire up your GPIO here
    };

    auto* brightness = dashboard.addSliderCard("brightness", "Brightness",
                                                0, 100, 5, "%");
    brightness->onSlider = [](int val) {
        ESP_LOGI(TAG, "Brightness -> %d%%", val);
    };

    auto* color = dashboard.addColorPickerCard("color", "LED Color", "#FF4500");
    color->onColor = [](const char* hex) {
        ESP_LOGI(TAG, "Color -> %s", hex);
    };

    // ── Config cards ──────────────────────────────────────────────────────
    auto* ssid_input = dashboard.addInputCard("ssid", "WiFi SSID", "Enter SSID...");
    ssid_input->onInput = [](const char* val) {
        ESP_LOGI(TAG, "SSID changed -> %s", val);
    };

    auto* interval_input = dashboard.addInputCard("interval", "Update Interval (s)", "2");
    interval_input->onInput = [](const char* val) {
        ESP_LOGI(TAG, "Interval -> %s s", val);
    };

    auto* wifi_mode = dashboard.addDropdownCard("wifi_mode", "WiFi Mode");
    wifi_mode->onDropdown = [](const char* val) {
        ESP_LOGI(TAG, "WiFi mode -> %s", val);
    };

    // ── Date / time cards ─────────────────────────────────────────────────
    dashboard.addDateCard("schedule", "Schedule", false);   // date only
    dashboard.addDateCard("alarm",    "Alarm",    true);    // date + time
    dashboard.addTimeCard("wake",     "Wake Time",  false); // HH:MM
    dashboard.addTimeCard("precise",  "Precise",    true);  // HH:MM:SS
    dashboard.addTimezoneCard("tz", "Timezone");
    dashboard.addLocationCard("loc", "Location");

    // ── Action / link cards ───────────────────────────────────────────────
    auto* restart_btn = dashboard.addActionButton(
        "restart", "System", "Restart Device",
        "Restart Device?", "The device will reboot immediately.",
        []() {
            ESP_LOGW(TAG, "Restart via dashboard");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        });
    restart_btn->variant = CardVariant::WARNING;

    auto* factory_btn = dashboard.addActionButton(
        "factory", "System", "Factory Reset",
        "Factory Reset?", "All settings will be erased.",
        []() {
            ESP_LOGW(TAG, "Factory reset via dashboard");
            nvs_flash_erase();
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        });
    factory_btn->variant = CardVariant::DANGER;

    dashboard.addLinkCard("docs",   "Documentation", "Open Docs",   "https://github.com/aaronbeckmann/ESP-DashboardPlus/tree/main/docs");
    dashboard.addLinkCard("github", "GitHub",        "View Source", "https://github.com/aaronbeckmann/ESP-DashboardPlus");

    // ── Groups ────────────────────────────────────────────────────────────
    dashboard.addGroup("sensors",  "Sensors");
    dashboard.addGroup("charts",   "Charts");
    dashboard.addGroup("controls", "Controls");
    dashboard.addGroup("config",   "Configuration");
    dashboard.addGroup("actions",  "Actions");

    dashboard.addCardToGroup("sensors",  "temp");
    dashboard.addCardToGroup("sensors",  "humidity");
    dashboard.addCardToGroup("sensors",  "cpu");
    dashboard.addCardToGroup("sensors",  "wifi");

    dashboard.addCardToGroup("charts",   "env_chart");
    dashboard.addCardToGroup("charts",   "cpu_chart");

    dashboard.addCardToGroup("controls", "led");
    dashboard.addCardToGroup("controls", "brightness");
    dashboard.addCardToGroup("controls", "color");

    dashboard.addCardToGroup("config",   "ssid");
    dashboard.addCardToGroup("config",   "interval");
    dashboard.addCardToGroup("config",   "wifi_mode");
    dashboard.addCardToGroup("config",   "schedule");
    dashboard.addCardToGroup("config",   "alarm");
    dashboard.addCardToGroup("config",   "wake");
    dashboard.addCardToGroup("config",   "precise");
    dashboard.addCardToGroup("config",   "tz");
    dashboard.addCardToGroup("config",   "loc");

    dashboard.addCardToGroup("actions",  "restart");
    dashboard.addCardToGroup("actions",  "factory");
    dashboard.addCardToGroup("actions",  "docs");
    dashboard.addCardToGroup("actions",  "github");

    dashboard.logInfo("Dashboard ready — open https://<device-ip>/ in your browser");

    // ── Simulated sensor values ───────────────────────────────────────────
    float temperature = 23.5f;
    float humidity    = 45.0f;
    int   cpuLoad     = 30;

    int64_t lastUpdate  = 0;
    int64_t lastLog     = 0;
    int     logLevelIdx = 0;
    const LogLevel logLevels[] = { LogLevel::DEBUG, LogLevel::INFO,
                                   LogLevel::WARNING, LogLevel::ERROR };

    while (true) {
        dashboard.loop();

        int64_t now = esp_timer_get_time() / 1000LL; // ms

        // Update sensor cards every 2 s
        if (now - lastUpdate > 2000) {
            lastUpdate = now;

            temperature += (static_cast<float>(rand() % 21) - 10.0f) / 10.0f;
            temperature  = temperature < 15.0f ? 15.0f :
                           temperature > 35.0f ? 35.0f : temperature;

            humidity += static_cast<float>((rand() % 11) - 5);
            humidity  = humidity < 30.0f ? 30.0f :
                        humidity > 70.0f ? 70.0f : humidity;

            cpuLoad += (rand() % 21) - 10;
            cpuLoad  = cpuLoad < 5  ?  5 :
                       cpuLoad > 95 ? 95 : cpuLoad;

            char buf[16];

            snprintf(buf, sizeof(buf), "%.1f", temperature);
            dashboard.updateStatCard("temp", buf);

            snprintf(buf, sizeof(buf), "%.0f", humidity);
            dashboard.updateStatCard("humidity", buf);

            dashboard.updateGaugeCard("cpu", static_cast<float>(cpuLoad));
            dashboard.updateChartCard("env_chart", tempIdx, temperature);
            dashboard.updateChartCard("env_chart", humIdx,  humidity);
            dashboard.updateChartCard("cpu_chart", cpuIdx,  static_cast<float>(cpuLoad));
        }

        // Rotate log messages every 5 s to exercise the console
        if (now - lastLog > 5000) {
            lastLog = now;

            char buf[64];
            snprintf(buf, sizeof(buf), "Uptime: %lld s | Heap: %lu B",
                     now / 1000LL,
                     static_cast<unsigned long>(esp_get_free_heap_size()));
            dashboard.log(logLevels[logLevelIdx % 4], buf);
            ++logLevelIdx;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
