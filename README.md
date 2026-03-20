# Esp32MqttSeeder

An **ESP-IDF** firmware project for ESP32 (dual-core) that seeds three MQTT 5
topic types to a fully MQTT5-capable broker for connection and integration
testing.

---

## Features

| Topic pattern | Wildcard(s) | Payload fields |
|---|---|---|
| `uplink/heartbeat/v1/<device_id>` | string (configurable) | `serial_no`, `attribute_name`, `device_status`, `time_stamp` (ISO-8601 UTC), `rssi` |
| `uplink/v3/di/<counter_id>` | number (configurable) | `time_stamp`, `shoot_count`, `pulse_time` |
| `devices/<device_id>/log/<level>` | string + log-level | `message`, `time_stamp` |

* **MQTT 5** – content-type `application/json` set on every publish via MQTT5
  publish properties.
* **Client ID** – derived from the device MAC address (12 hex digits).
* **NTP time sync** – timestamps are ISO-8601 UTC via SNTP.
* **Dual-core** – all three publisher tasks are pinned to **Core 1**; the
  Wi-Fi/TCP stack runs on Core 0 as usual.
* **Clean Architecture** – `app_config` (config) → `network` (WiFi + MQTT5
  infrastructure) → `publishers` (use-case tasks) → `main` (orchestration).
* **Safe JSON serialisation** – stack-allocated fixed-size buffers with
  `snprintf`; no dynamic allocation, no heap fragmentation.

---

## Requirements

* [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/)
* Target: `esp32` (any dual-core variant)

---

## Configuration

Run `idf.py menuconfig` and navigate to **Esp32 MQTT Seeder Configuration**:

| Setting | Default | Description |
|---|---|---|
| WiFi SSID | `MyWiFi` | Access-point SSID |
| WiFi Password | `password` | Leave empty for open networks |
| WiFi Max Retry | `5` | Reconnect attempts before giving up |
| MQTT Broker URI | `mqtt://192.168.1.100:1883` | Full MQTT5 broker URI |
| MQTT Username | _(empty)_ | Broker username (optional) |
| MQTT Password | _(empty)_ | Broker password (optional) |
| MQTT Keepalive | `60 s` | Keepalive interval |
| Device ID | `DEV-001` | String wildcard for heartbeat & log topics |
| Attribute Name | `temperature` | Heartbeat payload field |
| Device Status | `online` | Heartbeat payload field |
| Heartbeat Interval | `5000 ms` | How often to publish heartbeat |
| Counter ID | `1` | Numeric wildcard for counter topic |
| Counter Interval | `1000 ms` | How often to publish counter |
| Log Level | `info` | Log-level segment in log topic |
| Log Interval | `10000 ms` | How often to publish log message |

---

## Build & Flash

```bash
# Set up ESP-IDF environment (once per shell session)
. $IDF_PATH/export.sh

# Configure (optional – defaults work out of the box)
idf.py menuconfig

# Build
idf.py build

# Flash & monitor (replace PORT with your serial port)
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## Project Structure

```
.
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild       ← all tuneable parameters
│   └── main.c                  ← app_main (NVS → WiFi → NTP → MQTT5 → tasks)
└── components/
    ├── app_config/             ← config macros from Kconfig (header-only)
    ├── network/                ← WiFi manager + MQTT5 client manager
    └── publishers/             ← heartbeat, counter, log publisher tasks
```