"""Run a local MQTT broker with TLS/mTLS on localhost.

Uses amqtt and blocks until Ctrl+C.
"""

from __future__ import annotations

import argparse
import asyncio
import ssl
from pathlib import Path

from amqtt.broker import Broker


class LocalTlsBroker(Broker):
    require_client_cert = True
    min_tls_version = ssl.TLSVersion.TLSv1_2
    ciphers = None

    @staticmethod
    def _create_ssl_context(listener):
        context = Broker._create_ssl_context(listener)
        context.minimum_version = LocalTlsBroker.min_tls_version
        if LocalTlsBroker.ciphers:
            context.set_ciphers(LocalTlsBroker.ciphers)
        if LocalTlsBroker.require_client_cert:
            context.verify_mode = ssl.CERT_REQUIRED
        return context


def parse_tls_version(value: str) -> ssl.TLSVersion:
    mapping = {
        "1.2": ssl.TLSVersion.TLSv1_2,
        "1.3": ssl.TLSVersion.TLSv1_3,
    }
    try:
        return mapping[value]
    except KeyError as exc:
        raise argparse.ArgumentTypeError("--min-tls-version must be one of: 1.2, 1.3") from exc


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run local TLS MQTT broker")
    parser.add_argument("--host", default="127.0.0.1", help="Broker bind host")
    parser.add_argument("--port", type=int, default=8884, help="Broker bind port")
    parser.add_argument("--ca", default="certs/mqtt_ca.crt", help="CA certificate path")
    parser.add_argument("--cert", default="certs/mqtt_broker.crt", help="Broker certificate path")
    parser.add_argument("--key", default="certs/mqtt_broker.key", help="Broker private key path")
    parser.add_argument(
        "--allow-anonymous",
        action="store_true",
        help="Allow anonymous client connections",
    )
    parser.add_argument(
        "--no-client-cert-required",
        action="store_true",
        help="Disable strict mTLS requirement (client cert optional)",
    )
    parser.add_argument(
        "--password-file",
        default=None,
        help="Password file for username/password auth (passlib htpasswd-like format)",
    )
    parser.add_argument(
        "--min-tls-version",
        type=parse_tls_version,
        default=ssl.TLSVersion.TLSv1_2,
        help="Minimum TLS version allowed (1.2 or 1.3)",
    )
    parser.add_argument(
        "--ciphers",
        default=None,
        help="Optional OpenSSL cipher list for TLS 1.2 and below",
    )
    return parser.parse_args()


async def run(args: argparse.Namespace) -> None:
    ca = str(Path(args.ca).resolve())
    cert = str(Path(args.cert).resolve())
    key = str(Path(args.key).resolve())
    password_file = str(Path(args.password_file).resolve()) if args.password_file else None

    LocalTlsBroker.require_client_cert = not args.no_client_cert_required
    LocalTlsBroker.min_tls_version = args.min_tls_version
    LocalTlsBroker.ciphers = args.ciphers

    config = {
        "listeners": {
            "default": {
                "type": "tcp",
                "bind": f"{args.host}:{args.port}",
                "ssl": True,
                "cafile": ca,
                "certfile": cert,
                "keyfile": key,
            }
        },
        "sys_interval": 0,
        "auth": {
            "allow-anonymous": bool(args.allow_anonymous),
            **({"password-file": password_file} if password_file else {}),
        },
        "topic-check": {"enabled": False},
    }

    broker = LocalTlsBroker(config)
    await broker.start()
    mode = "mTLS required" if LocalTlsBroker.require_client_cert else "mTLS optional"
    print(f"Broker running on {args.host}:{args.port} ({mode})")

    stop_event = asyncio.Event()
    try:
        await stop_event.wait()
    finally:
        await broker.shutdown()


def main() -> int:
    args = parse_args()
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        print("Broker stopped.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
