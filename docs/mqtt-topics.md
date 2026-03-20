# MQTT Topics Reference

This document describes all MQTT topics used by **Esp32MqttSeeder**.

- **Uplink topics** (device → broker): data published by the device.
- **Downlink / command topics** (broker → device): commands the broker can send
  to reconfigure the device at runtime without reflashing.

`<device_id>` in every topic is the value of `CONFIG_SEEDER_DEVICE_ID`, set at
build time (default: `DEV-001`).

---

## Uplink Topics (Device → Broker)

### Heartbeat

| Field | Value |
|---|---|
| **Topic** | `uplink/heartbeat/v1/<device_id>` |
| **QoS** | 1 |
| **Default interval** | 5 000 ms |
| **Content-Type** | `application/json` (MQTT5 property) |

**Payload example:**
```json
{
  "serial_no": "DEV-001",
  "attribute_name": "temperature",
  "device_status": "online",
  "time_stamp": "2024-01-15T08:30:00Z",
  "rssi": -62
}
```

| Field | Type | Description |
|---|---|---|
| `serial_no` | string | Device ID (compile-time, equals `<device_id>`) |
| `attribute_name` | string | Configurable via `cmd/.../config/device` |
| `device_status` | string | Configurable via `cmd/.../config/device` |
| `time_stamp` | string | ISO-8601 UTC timestamp |
| `rssi` | number | Wi-Fi received signal strength (dBm) |

---

### Counter

| Field | Value |
|---|---|
| **Topic** | `uplink/v3/di/<counter_id>` |
| **QoS** | 1 |
| **Default interval** | 1 000 ms |
| **Content-Type** | `application/json` (MQTT5 property) |

`<counter_id>` is a **numeric** identifier configured at build time (default:
`1`). It can be changed at runtime via `cmd/<device_id>/config/counter`.

**Payload example:**
```json
{
  "time_stamp": "2024-01-15T08:30:01Z",
  "shoot_count": 42,
  "pulse_time": 1000
}
```

| Field | Type | Description |
|---|---|---|
| `time_stamp` | string | ISO-8601 UTC timestamp |
| `shoot_count` | number | Monotonically increasing counter (resets on reboot) |
| `pulse_time` | number | Configured publish interval in milliseconds |

---

### Log

| Field | Value |
|---|---|
| **Topic** | `devices/<device_id>/log/<level>` |
| **QoS** | 1 |
| **Default interval** | 10 000 ms |
| **Content-Type** | `application/json` (MQTT5 property) |

`<level>` reflects the current log level (default: `info`). It updates
dynamically when changed via `cmd/<device_id>/config/log`.

**Payload example:**
```json
{
  "message": "Seeder is running normally",
  "time_stamp": "2024-01-15T08:30:10Z"
}
```

| Field | Type | Description |
|---|---|---|
| `message` | string | Rotating log message |
| `time_stamp` | string | ISO-8601 UTC timestamp |

---

## Downlink / Command Topics (Broker → Device)

All command topics are subscribed at startup and automatically re-subscribed
after every broker reconnect.  Changes take effect on the **next publish cycle**
of the respective publisher.

---

### `cmd/<device_id>/config/heartbeat`

Updates the **heartbeat publish interval**.

**Payload:**
```json
{ "interval_ms": 3000 }
```

| Field | Type | Required | Description |
|---|---|---|---|
| `interval_ms` | number | yes | New publish interval in milliseconds (must be > 0) |

**Example** (mosquitto_pub):
```bash
mosquitto_pub -t "cmd/DEV-001/config/heartbeat" \
  -m '{"interval_ms": 3000}'
```

---

### `cmd/<device_id>/config/counter`

Updates the **counter publish interval** and/or the **counter ID** (topic
segment).  Either field may be omitted.

**Payload:**
```json
{ "interval_ms": 500, "counter_id": 2 }
```

| Field | Type | Required | Description |
|---|---|---|---|
| `interval_ms` | number | no | New publish interval in milliseconds (must be > 0) |
| `counter_id` | number | no | New counter ID; changes the uplink topic to `uplink/v3/di/<new_id>` |

**Example:**
```bash
mosquitto_pub -t "cmd/DEV-001/config/counter" \
  -m '{"interval_ms": 500, "counter_id": 2}'
```

---

### `cmd/<device_id>/config/log`

Updates the **log publish interval** and/or the **log level** (topic segment).
Either field may be omitted.

**Payload:**
```json
{ "interval_ms": 5000, "level": "warning" }
```

| Field | Type | Required | Description |
|---|---|---|---|
| `interval_ms` | number | no | New publish interval in milliseconds (must be > 0) |
| `level` | string | no | New log level; changes the uplink topic to `devices/<device_id>/log/<level>` |

Valid `level` values: `verbose`, `debug`, `info`, `warning`, `error`, `fatal`, `none`

**Example:**
```bash
mosquitto_pub -t "cmd/DEV-001/config/log" \
  -m '{"interval_ms": 5000, "level": "warning"}'
```

---

### `cmd/<device_id>/config/device`

Updates the **heartbeat payload fields** (`attribute_name`, `device_status`).
Either field may be omitted.

**Payload:**
```json
{ "attribute_name": "humidity", "device_status": "degraded" }
```

| Field | Type | Required | Description |
|---|---|---|---|
| `attribute_name` | string | no | New value for the `attribute_name` field in heartbeat payloads |
| `device_status` | string | no | New value for the `device_status` field in heartbeat payloads |

**Example:**
```bash
mosquitto_pub -t "cmd/DEV-001/config/device" \
  -m '{"attribute_name": "humidity", "device_status": "degraded"}'
```

---

### `cmd/<device_id>/reboot`

Triggers an **immediate device reboot** (1-second delay before restart).
The payload is ignored.

**Example:**
```bash
mosquitto_pub -t "cmd/DEV-001/reboot" -m '{}'
```

---

## Topic Summary

| Direction | Topic | Purpose |
|---|---|---|
| ↑ Uplink | `uplink/heartbeat/v1/<device_id>` | Periodic heartbeat with status & RSSI |
| ↑ Uplink | `uplink/v3/di/<counter_id>` | Monotonic counter with pulse timing |
| ↑ Uplink | `devices/<device_id>/log/<level>` | Rotating log messages |
| ↓ Command | `cmd/<device_id>/config/heartbeat` | Update heartbeat interval |
| ↓ Command | `cmd/<device_id>/config/counter` | Update counter interval and/or ID |
| ↓ Command | `cmd/<device_id>/config/log` | Update log interval and/or level |
| ↓ Command | `cmd/<device_id>/config/device` | Update heartbeat payload fields |
| ↓ Command | `cmd/<device_id>/reboot` | Reboot the device |
