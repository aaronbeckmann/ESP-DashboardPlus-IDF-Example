# ESP-DashboardPlus IDF Example

This is an example application for [ESP-DashboardPlus](https://github.com/aaronbeckmann/ESP-DashboardPlus) using the ESP-IDF framework. It demonstrates all available card types, card groups, real-time sensor updates, console commands, and OTA firmware updates.

All you need to do is clone the [ESP-DashboardPlus-IDF](https://github.com/aaronbeckmann/ESP-DashboardPlus-IDF) library into the same parent directory as this repository, then set `WIFI_SSID` and `WIFI_PASSWORD` in `main/main.cpp` to match your WiFi credentials and flash it to your ESP32.

```
parent-folder/
├── ESP-DashboardPlus-IDF/         ← library (clone this too)
└── ESP-DashboardPlus-IDF-Example/ ← this repo
```

```bash
idf.py -p <PORT> build flash monitor
```

Once connected, open `http://<device-ip>/` in your browser (or `http://esp-dashboard.local/` if mDNS works on your network).

## 📖 Documentation

Full documentation is available at: **[https://aaronbeckmann.github.io/ESP-DashboardPlus](https://aaronbeckmann.github.io/ESP-DashboardPlus)**
