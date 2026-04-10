# MQTT TLS / Certificate Setup Guide

This guide explains how to enable TLS encryption and X.509 certificate
authentication for the MQTT connection in **Esp32MqttSeeder**.

Certificates are **optional** – the device works with plain `mqtt://` without
any certificate configuration.

---

## Overview of Options

| Kconfig option | Purpose |
|---|---|
| `SEEDER_MQTT_TLS_ENABLED` | Enable TLS; broker URI must use `mqtts://` |
| `SEEDER_MQTT_CA_CERT` | Verify broker with a CA certificate |
| `SEEDER_MQTT_CLIENT_CERT` | Authenticate device to broker (mutual TLS) |

All three options are found in **menuconfig** under:

```
Esp32 MQTT Seeder Configuration
  └── MQTT Broker Settings
        └── TLS / Certificate Settings
```

---

## Certificate Files

When TLS features are enabled, the firmware embeds the certificate(s) at
**build time** from the project-root `certs/` directory.

| File | Used when |
|---|---|
| `certs/mqtt_ca.crt` | `SEEDER_MQTT_CA_CERT` is enabled |
| `certs/mqtt_client.crt` | `SEEDER_MQTT_CLIENT_CERT` is enabled |
| `certs/mqtt_client.key` | `SEEDER_MQTT_CLIENT_CERT` is enabled |

All files must be **PEM-encoded**.  The `certs/` directory is listed in
`.gitignore` so that private keys are never accidentally committed to source
control.

---

## Step-by-Step Setup

### 1. Obtain or generate certificates

#### Self-signed CA + broker + client certificates (for testing)

```bash
# 1. Create a CA key and self-signed CA certificate
openssl genrsa -out ca.key 2048
openssl req -new -x509 -days 3650 -key ca.key -out ca.crt \
    -subj "/CN=MyMqttCA"

# 2. Create a broker key and certificate signed by the CA
openssl genrsa -out broker.key 2048
openssl req -new -key broker.key -out broker.csr \
    -subj "/CN=<broker-hostname-or-IP>"
openssl x509 -req -days 3650 -in broker.csr \
    -CA ca.crt -CAkey ca.key -CAcreateserial -out broker.crt

# 3. (Optional) Create a client key and certificate signed by the CA
openssl genrsa -out mqtt_client.key 2048
openssl req -new -key mqtt_client.key -out mqtt_client.csr \
    -subj "/CN=esp32-seeder"
openssl x509 -req -days 3650 -in mqtt_client.csr \
    -CA ca.crt -CAkey ca.key -CAcreateserial -out mqtt_client.crt
```

> Replace `<broker-hostname-or-IP>` with the actual hostname or IP address of
> your MQTT broker.  The value must match the broker's TLS certificate CN or
> Subject Alternative Name.

#### Using certificates from a Certificate Authority (production)

Obtain `ca.crt`, `mqtt_client.crt`, and `mqtt_client.key` from your PKI /
CA provider and skip the self-signing steps above.

---

### 2. Place certificates in the `certs/` directory

Copy the required PEM files into the `certs/` folder at the project root:

```
Esp32MqttSeeder/
├── certs/
│   ├── mqtt_ca.crt        ← CA cert used to verify the broker
│   ├── mqtt_client.crt    ← client certificate (mutual TLS only)
│   └── mqtt_client.key    ← client private key  (mutual TLS only)
```

---

### 3. Configure the broker URI

Open **menuconfig** and update the broker URI to use the `mqtts://` scheme
and the correct TLS port (default **8883**):

```
Esp32 MQTT Seeder Configuration
  └── MQTT Broker Settings
        └── MQTT Broker URI  →  mqtts://broker.example.com:8883
```

---

### 4. Enable TLS and certificate options

Still in **menuconfig**, navigate to:

```
Esp32 MQTT Seeder Configuration
  └── MQTT Broker Settings
        └── TLS / Certificate Settings
```

| Scenario | Options to enable |
|---|---|
| TLS encryption only (no cert verification) | `SEEDER_MQTT_TLS_ENABLED` |
| TLS + broker verification | `SEEDER_MQTT_TLS_ENABLED` + `SEEDER_MQTT_CA_CERT` |
| TLS + broker verification + mutual TLS | All three options |

> **Warning:** Enabling TLS without `SEEDER_MQTT_CA_CERT` skips broker
> certificate verification.  This protects against passive eavesdropping but
> not against man-in-the-middle attacks.  Always enable `SEEDER_MQTT_CA_CERT`
> in production.

---

### 5. Build and flash

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

The certificate files are embedded directly into the firmware image during the
`build` step.  No separate flashing step is required.

---

## Broker Configuration Reference

### Mosquitto (`mosquitto.conf`)

```
listener 8883
cafile   /etc/mosquitto/certs/ca.crt
certfile /etc/mosquitto/certs/broker.crt
keyfile  /etc/mosquitto/certs/broker.key

# Require client certificates (mutual TLS)
require_certificate true
```

Restart Mosquitto after editing:

```bash
sudo systemctl restart mosquitto
```

### EMQX

In the EMQX Dashboard, go to **Cluster → Listeners → Add Listener**:
- **Type**: SSL
- **Port**: 8883
- Upload `ca.crt`, `broker.crt`, `broker.key`
- Enable **Peer Certificate Verify** for mutual TLS

---

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `MQTT_ERROR_TYPE_TCP_TRANSPORT` on connect | Wrong URI scheme or port; TLS not configured on broker |
| `esp_tls_last_esp_err` non-zero in error log | Certificate validation failure; check CA cert and broker hostname |
| `Connection refused` | Broker requires client cert (`require_certificate true`) but `SEEDER_MQTT_CLIENT_CERT` is not enabled |
| Build error: file not found | Certificate file missing from `certs/` – check filenames exactly |

Enable verbose logging in menuconfig (`CONFIG_LOG_DEFAULT_LEVEL_VERBOSE`) to
see detailed TLS handshake errors from the `esp-tls` component.

---

## Security Notes

- **Never commit private keys** to source control.  The `certs/` directory is
  git-ignored for this reason.
- Rotate certificates before they expire.  The firmware must be rebuilt and
  reflashed after a certificate rotation.
- Use a dedicated client certificate per device in production so that
  individual devices can be revoked without affecting others.
