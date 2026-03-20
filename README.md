# Esp32MqttSeeder

An **ESP-IDF** firmware project for ESP32 (dual-core) that seeds three MQTT 5
topic types to a fully MQTT5-capable broker for connection and integration
testing.  The device also listens on **command topics** so the broker can
update runtime configuration without a firmware reflash.

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
* **Runtime config via MQTT commands** – the broker can update publish intervals
  and other settings at runtime by publishing to the command topics below.
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

## MQTT Command Topics (Downlink)

The device subscribes to the following **command topics** at startup.  All
`<device_id>` segments use the value configured by `SEEDER_DEVICE_ID` at build
time (e.g. `DEV-001`).

### Update heartbeat publish interval

**Topic:** `cmd/<device_id>/config/heartbeat`

```json
{ "interval_ms": 3000 }
```

### Update counter publish interval and/or counter ID

**Topic:** `cmd/<device_id>/config/counter`

```json
{ "interval_ms": 500, "counter_id": 2 }
```

Either field can be omitted to leave the current value unchanged.

### Update log publish interval and/or log level

**Topic:** `cmd/<device_id>/config/log`

```json
{ "interval_ms": 5000, "level": "warning" }
```

`level` is reflected in the log publish topic: `devices/<device_id>/log/<level>`.

### Update heartbeat payload fields

**Topic:** `cmd/<device_id>/config/device`

```json
{ "attribute_name": "humidity", "device_status": "degraded" }
```

Either field can be omitted.

### Reboot the device

**Topic:** `cmd/<device_id>/reboot`

Payload is ignored.  The device reboots after a 1-second delay.

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