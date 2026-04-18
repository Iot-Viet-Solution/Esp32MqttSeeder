"""MQTT TLS smoke test using paho-mqtt.

Default behavior validates mutual TLS using certificates from the repository:
  - certs/mqtt_ca.crt
  - certs/mqtt_client.crt
  - certs/mqtt_client.key

Examples:
  - python test/mqtt/test_mqtt_tls.py
  - python test/mqtt/test_mqtt_tls.py --host 192.168.1.100 --port 8884
  - python test/mqtt/test_mqtt_tls.py --topic test/esp32/tls --timeout 12
  - python test/mqtt/test_mqtt_tls.py --no-client-cert
"""

import argparse
import hashlib
import os
import re
import ssl
import sys
import threading

try:
    import paho.mqtt.client as mqtt
except Exception:
    print("paho-mqtt not installed. Install with: pip install paho-mqtt")
    raise


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, "..", ".."))
CERTS_DIR = os.path.join(REPO_ROOT, "certs")

DEFAULT_HOST = "localhost"
DEFAULT_PORT = 8884
DEFAULT_TOPIC = "test/esp32/tls"

DEFAULT_CA_PATH = os.path.join(CERTS_DIR, "mqtt_ca.crt")
DEFAULT_CLIENT_CERT_PATH = os.path.join(CERTS_DIR, "mqtt_client.crt")
DEFAULT_CLIENT_KEY_PATH = os.path.join(CERTS_DIR, "mqtt_client.key")
DEFAULT_PAYLOAD = "hello from python tls test"


def parse_tls_version(value):
    mapping = {
        "1.2": ssl.TLSVersion.TLSv1_2,
        "1.3": ssl.TLSVersion.TLSv1_3,
    }
    if value not in mapping:
        raise argparse.ArgumentTypeError("--min-tls-version must be one of: 1.2, 1.3")
    return mapping[value]


def parse_args():
    parser = argparse.ArgumentParser(description="MQTT TLS smoke test")
    parser.add_argument("--host", default=os.getenv("MQTT_HOST", DEFAULT_HOST), help="MQTT broker host")
    parser.add_argument(
        "--port",
        type=int,
        default=int(os.getenv("MQTT_PORT", str(DEFAULT_PORT))),
        help="MQTT broker TLS port",
    )
    parser.add_argument("--topic", default=os.getenv("MQTT_TOPIC", DEFAULT_TOPIC), help="Test topic")
    parser.add_argument("--payload", default=os.getenv("MQTT_PAYLOAD", DEFAULT_PAYLOAD), help="Test payload")
    parser.add_argument(
        "--timeout",
        type=float,
        default=float(os.getenv("MQTT_TIMEOUT", "10")),
        help="Seconds to wait for test completion",
    )
    parser.add_argument("--ca", default=os.getenv("MQTT_CA", DEFAULT_CA_PATH), help="CA certificate path")
    parser.add_argument(
        "--cert",
        default=os.getenv("MQTT_CERT", DEFAULT_CLIENT_CERT_PATH),
        help="Client certificate path",
    )
    parser.add_argument(
        "--key",
        default=os.getenv("MQTT_KEY", DEFAULT_CLIENT_KEY_PATH),
        help="Client private key path",
    )
    parser.add_argument(
        "--no-client-cert",
        action="store_true",
        help="Disable client certificate authentication (server-auth only)",
    )
    parser.add_argument(
        "--insecure",
        action="store_true",
        help="Disable certificate hostname verification for temporary diagnostics",
    )
    parser.add_argument(
        "--no-verify",
        action="store_true",
        help="Disable TLS certificate verification completely (debug only)",
    )
    parser.add_argument(
        "--thumbprint",
        default=os.getenv("MQTT_THUMBPRINT"),
        help="Expected broker certificate thumbprint (hex, with or without colons)",
    )
    parser.add_argument(
        "--thumbprint-alg",
        choices=("sha256", "sha1"),
        default=os.getenv("MQTT_THUMBPRINT_ALG", "sha256"),
        help="Hash algorithm used for thumbprint verification",
    )
    parser.add_argument(
        "--thumbprint-file",
        default=os.getenv("MQTT_THUMBPRINT_FILE"),
        help="Certificate file (PEM/DER) used to derive expected thumbprint",
    )
    parser.add_argument(
        "--negative-wrong-ca",
        action="store_true",
        help="After a successful run, verify TLS enforcement by retrying with an intentionally wrong CA",
    )
    parser.add_argument(
        "--client-id",
        default=os.getenv("MQTT_CLIENT_ID", "mqtt-tls-smoke-client"),
        help="MQTT client ID",
    )
    parser.add_argument(
        "--username",
        default=os.getenv("MQTT_USERNAME"),
        help="MQTT username",
    )
    parser.add_argument(
        "--password",
        default=os.getenv("MQTT_PASSWORD"),
        help="MQTT password",
    )
    parser.add_argument(
        "--min-tls-version",
        type=parse_tls_version,
        default=parse_tls_version(os.getenv("MQTT_MIN_TLS_VERSION", "1.2")),
        help="Minimum TLS version to negotiate (1.2 or 1.3)",
    )
    parser.add_argument(
        "--ciphers",
        default=os.getenv("MQTT_CIPHERS"),
        help="Optional OpenSSL cipher list for TLS 1.2 and below",
    )
    return parser.parse_args()


def ensure_file_exists(path, label):
    if not os.path.exists(path):
        print(f"ERROR: {label} not found: {path}")
        return False
    return True


def reason_code_to_int(reason_code):
    if isinstance(reason_code, int):
        return reason_code
    if hasattr(reason_code, "value"):
        return int(reason_code.value)
    return int(str(reason_code))


def normalize_thumbprint(value):
    cleaned = re.sub(r"[^0-9a-fA-F]", "", value or "")
    return cleaned.upper()


def format_thumbprint(value):
    return ":".join(value[i : i + 2] for i in range(0, len(value), 2))


def thumbprint_length_for_alg(alg):
    return 64 if alg == "sha256" else 40


def compute_cert_thumbprint(cert_bytes, alg):
    return hashlib.new(alg, cert_bytes).hexdigest().upper()


def cert_file_to_der(path):
    data = open(path, "rb").read()
    if b"-----BEGIN CERTIFICATE-----" in data:
        text = data.decode("utf-8")
        return ssl.PEM_cert_to_DER_cert(text)
    return data


def peer_thumbprint(client, alg):
    sock = client.socket()
    if sock is None:
        raise RuntimeError("TLS socket is not available")
    cert = sock.getpeercert(binary_form=True)
    if not cert:
        raise RuntimeError("Peer certificate is not available")
    return compute_cert_thumbprint(cert, alg)


def run_single_test(args, ca_path, expect_failure=False):
    use_thumbprint = bool(args.thumbprint)

    print(f"Broker: {args.host}:{args.port}")
    print(f"Topic: {args.topic}")
    if use_thumbprint:
        print(f"Thumbprint verify: {args.thumbprint_alg} {format_thumbprint(args.thumbprint)}")
    else:
        print(f"CA file: {os.path.normpath(ca_path)}")

    ok = True
    if not use_thumbprint:
        ok = ensure_file_exists(ca_path, "CA certificate")
    use_client_cert = not args.no_client_cert

    if use_client_cert:
        print(f"Client cert: {os.path.normpath(args.cert)}")
        print(f"Client key: {os.path.normpath(args.key)}")
        ok = ensure_file_exists(args.cert, "Client certificate") and ok
        ok = ensure_file_exists(args.key, "Client key") and ok
    else:
        print("Client cert auth: disabled")

    if not ok:
        return 2

    state = {
        "connected": False,
        "subscribed": False,
        "published": False,
        "message_received": False,
        "error": None,
    }
    done_event = threading.Event()

    def on_connect(client, userdata, flags, reason_code, properties=None):
        rc = reason_code_to_int(reason_code)
        print(f"Connected with result code: {rc}")
        if rc != 0:
            state["error"] = f"Connect failed with code {rc}"
            done_event.set()
            return

        if use_thumbprint:
            try:
                actual_thumbprint = peer_thumbprint(client, args.thumbprint_alg)
            except Exception as ex:
                state["error"] = f"Thumbprint check failed: {ex}"
                done_event.set()
                client.disconnect()
                return

            print(f"Peer thumbprint: {args.thumbprint_alg} {format_thumbprint(actual_thumbprint)}")
            if actual_thumbprint != args.thumbprint:
                state["error"] = (
                    "Thumbprint mismatch: "
                    f"expected={format_thumbprint(args.thumbprint)} "
                    f"actual={format_thumbprint(actual_thumbprint)}"
                )
                done_event.set()
                client.disconnect()
                return

        state["connected"] = True
        result, _ = client.subscribe(args.topic)
        state["subscribed"] = result == mqtt.MQTT_ERR_SUCCESS
        print(f"Subscribe result: {result}")

    def on_subscribe(client, userdata, mid, granted_qos, properties=None):
        print(f"Subscribed mid={mid} qos={granted_qos}")
        info = client.publish(args.topic, payload=args.payload, qos=0)
        state["published"] = info.rc == mqtt.MQTT_ERR_SUCCESS
        print(f"Publish queued rc={info.rc} mid={info.mid}")

    def on_message(client, userdata, msg):
        payload = msg.payload.decode(errors="replace")
        print(f"Received message: topic={msg.topic} payload={payload}")
        state["message_received"] = True
        done_event.set()

    def on_disconnect(client, userdata, disconnect_flags, reason_code, properties=None):
        rc = reason_code_to_int(reason_code)
        print(f"Disconnected with result code: {rc}")
        if not done_event.is_set() and rc != 0 and state["error"] is None:
            state["error"] = f"Unexpected disconnect with code {rc}"
            done_event.set()

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=args.client_id)

    if args.username:
        client.username_pw_set(args.username, args.password)

    if args.no_verify or use_thumbprint:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        context.minimum_version = args.min_tls_version
        if args.ciphers:
            context.set_ciphers(args.ciphers)
        if use_client_cert:
            context.load_cert_chain(certfile=args.cert, keyfile=args.key)
        client.tls_set_context(context)
    else:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.minimum_version = args.min_tls_version
        if args.ciphers:
            context.set_ciphers(args.ciphers)
        context.load_verify_locations(cafile=ca_path)
        context.check_hostname = not args.insecure
        context.verify_mode = ssl.CERT_REQUIRED
        if use_client_cert:
            context.load_cert_chain(certfile=args.cert, keyfile=args.key)
        client.tls_set_context(context)

    client.on_connect = on_connect
    client.on_subscribe = on_subscribe
    client.on_message = on_message
    client.on_disconnect = on_disconnect

    try:
        client.connect(args.host, args.port, keepalive=60)
    except Exception as ex:
        if expect_failure:
            print(f"Expected failure observed with wrong CA: {ex}")
            return 0

        print(f"ERROR: Failed to connect to {args.host}:{args.port} -> {ex}")
        if not args.insecure and not args.no_verify:
            print("Hint: for quick diagnosis only, re-run with --no-verify.")
        return 3

    client.loop_start()
    completed = done_event.wait(timeout=args.timeout)
    client.loop_stop()
    client.disconnect()

    if state["error"]:
        if expect_failure:
            print(f"Expected failure observed with wrong CA: {state['error']}")
            return 0
        print(f"ERROR: {state['error']}")
        return 4

    if not completed:
        if expect_failure:
            print("Expected failure observed with wrong CA: timed out without successful loopback.")
            return 0
        print(f"ERROR: Timed out after {args.timeout}s waiting for loopback message.")
        return 5

    if not (state["connected"] and state["subscribed"] and state["published"] and state["message_received"]):
        if expect_failure:
            print("Expected failure observed with wrong CA: success checkpoints were incomplete.")
            return 0
        print("ERROR: TLS test did not reach all success checkpoints.")
        print(
            "State: "
            f"connected={state['connected']} "
            f"subscribed={state['subscribed']} "
            f"published={state['published']} "
            f"message_received={state['message_received']}"
        )
        return 6

    if expect_failure:
        print("ERROR: Negative test unexpectedly succeeded with wrong CA.")
        return 7

    print("SUCCESS: MQTT TLS smoke test passed.")
    return 0


def main():
    args = parse_args()

    if args.thumbprint_file:
        if not ensure_file_exists(args.thumbprint_file, "Thumbprint source certificate"):
            return 2
        try:
            der = cert_file_to_der(args.thumbprint_file)
            args.thumbprint = compute_cert_thumbprint(der, args.thumbprint_alg)
        except Exception as ex:
            print(f"ERROR: Failed to derive thumbprint from {args.thumbprint_file}: {ex}")
            return 2

    if args.thumbprint:
        args.thumbprint = normalize_thumbprint(args.thumbprint)
        expected_len = thumbprint_length_for_alg(args.thumbprint_alg)
        if len(args.thumbprint) != expected_len:
            print(
                f"ERROR: Invalid {args.thumbprint_alg} thumbprint length. "
                f"Expected {expected_len} hex chars, got {len(args.thumbprint)}"
            )
            return 2

    primary_code = run_single_test(args, args.ca)
    if primary_code != 0:
        return primary_code

    if args.negative_wrong_ca:
        if args.thumbprint:
            print("Skipping --negative-wrong-ca because thumbprint mode does not use CA trust.")
            return 0

        print("Running negative TLS check with intentionally wrong CA...")
        wrong_ca = args.cert
        if not os.path.exists(wrong_ca):
            wrong_ca = args.key

        negative_code = run_single_test(args, wrong_ca, expect_failure=True)
        if negative_code != 0:
            return negative_code

        print("SUCCESS: Negative TLS check passed (connection was rejected as expected).")

    return 0


if __name__ == "__main__":
    sys.exit(main())

