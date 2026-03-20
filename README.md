# Esp32MqttSeeder

An **ESP-IDF** firmware project for ESP32 (dual-core) that seeds three MQTT 5
topic types to a fully MQTT5-capable broker for connection and integration
testing.  The device also listens on **command topics** so the broker can
update runtime configuration without a firmware reflash.

---

## Features

* **MQTT 5** – content-type `application/json` set on every publish via MQTT5
  publish properties.
* **Client ID** – derived from the device MAC address (12 hex digits).
* **NTP time sync** – timestamps are ISO-8601 UTC via SNTP.
* **Dual-core** – all three publisher tasks are pinned to **Core 1**; the
  Wi-Fi/TCP stack runs on Core 0 as usual.
* **Runtime config via MQTT commands** – the broker can update publish intervals
  and other settings at runtime by publishing to the command topics (see
  [docs/mqtt-topics.md](docs/mqtt-topics.md)).
* **Clean Architecture** – `app_config` (config) → `network` (WiFi + MQTT5
  infrastructure) → `publishers` (use-case tasks) → `cmd_handler` (command
  subscriptions) → `main` (orchestration).
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
| Device ID | `DEV-001` | String wildcard for heartbeat & log topics, and the `<device_id>` segment in all command topics |
| Attribute Name | `temperature` | Heartbeat payload field |
| Device Status | `online` | Heartbeat payload field |
| Heartbeat Interval | `5000 ms` | How often to publish heartbeat |
| Counter ID | `1` | Numeric wildcard for counter topic |
| Counter Interval | `1000 ms` | How often to publish counter |
| Log Level | `info` | Log-level segment in log topic |
| Log Interval | `10000 ms` | How often to publish log message |

---

## MQTT Topics

Full reference documentation: **[docs/mqtt-topics.md](docs/mqtt-topics.md)**

### Uplink (Device → Broker)

| Topic | Default interval | Payload fields |
|---|---|---|
| `uplink/heartbeat/v1/<device_id>` | 5 000 ms | `serial_no`, `attribute_name`, `device_status`, `time_stamp`, `rssi` |
| `uplink/v3/di/<counter_id>` | 1 000 ms | `time_stamp`, `shoot_count`, `pulse_time` |
| `devices/<device_id>/log/<level>` | 10 000 ms | `message`, `time_stamp` |

### Downlink – Command Topics (Broker → Device)

The broker can reconfigure the device at runtime by publishing to:

| Topic | Payload |
|---|---|
| `cmd/<device_id>/config/heartbeat` | `{"interval_ms": 3000}` |
| `cmd/<device_id>/config/counter` | `{"interval_ms": 500, "counter_id": 2}` |
| `cmd/<device_id>/config/log` | `{"interval_ms": 5000, "level": "warning"}` |
| `cmd/<device_id>/config/device` | `{"attribute_name": "humidity", "device_status": "degraded"}` |
| `cmd/<device_id>/reboot` | _(payload ignored – reboots device)_ |

`<device_id>` is set at build time via `CONFIG_SEEDER_DEVICE_ID` (default: `DEV-001`).
See **[docs/mqtt-topics.md](docs/mqtt-topics.md)** for full payload schemas and examples.

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
│   └── main.c                  ← app_main (NVS → runtime-config → WiFi → NTP → MQTT5 → cmd → tasks)
└── components/
    ├── app_config/             ← compile-time config macros + runtime-mutable config
    ├── network/                ← WiFi manager + MQTT5 client manager (publish + subscribe)
    ├── cmd_handler/            ← MQTT command topic subscriptions & JSON dispatch
    └── publishers/             ← heartbeat, counter, log publisher tasks
```