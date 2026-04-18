"""Generate local MQTT TLS certificates for broker and client.

Creates a local CA, then issues:
- broker cert/key for localhost
- client cert/key for mutual TLS

Outputs are written to the repository certs directory by default.
"""

from __future__ import annotations

import argparse
import datetime as dt
from ipaddress import ip_address
from pathlib import Path
from typing import Any

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives.serialization import pkcs12
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

try:
    import yaml
except Exception:
    yaml = None


DEFAULT_CA_CERT_NAME = "mqtt_ca.crt"
DEFAULT_CA_KEY_NAME = "mqtt_ca.key"
DEFAULT_BROKER_CERT_NAME = "mqtt_broker.crt"
DEFAULT_BROKER_KEY_NAME = "mqtt_broker.key"
DEFAULT_CLIENT_CERT_NAME = "mqtt_client.crt"
DEFAULT_CLIENT_KEY_NAME = "mqtt_client.key"

LIST_FIELDS = {"eku", "broker_dns", "broker_ip", "cert_dns", "cert_ip"}
RESERVED_CONFIG_KEYS = {"jobs", "defaults"}


def _create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Generate local CA/broker/client TLS certificates")
    parser.add_argument(
        "--config",
        default=None,
        help="Path to YAML configuration file (supports defaults + jobs)",
    )
    parser.add_argument(
        "--certs-dir",
        default=str(Path(__file__).resolve().parents[2] / "certs"),
        help="Directory to write generated certificates",
    )
    parser.add_argument("--days", type=int, default=3650, help="Certificate validity in days")
    parser.add_argument(
        "--backup-existing",
        action="store_true",
        help="Rename existing output files with a timestamp before writing new ones",
    )
    parser.add_argument(
        "--profile",
        choices=("broker", "client", "custom"),
        default="broker",
        help="Generation profile: broker/client defaults or fully custom output",
    )

    parser.add_argument("--ca-cert-name", default=DEFAULT_CA_CERT_NAME, help="CA certificate filename")
    parser.add_argument("--ca-key-name", default=DEFAULT_CA_KEY_NAME, help="CA key filename")

    parser.add_argument("--cert-name", default=None, help="Leaf certificate filename for custom profile")
    parser.add_argument("--key-name", default=None, help="Leaf private key filename for custom profile")
    parser.add_argument("--cert-cn", default=None, help="Leaf certificate common name for custom profile")

    parser.add_argument(
        "--eku",
        action="append",
        default=[],
        choices=("server", "client"),
        help="Extended key usage for custom profile (repeatable)",
    )

    parser.add_argument(
        "--broker-cn",
        default="localhost",
        help="Common Name for broker certificate",
    )
    parser.add_argument(
        "--broker-dns",
        action="append",
        default=[],
        help="Additional DNS SAN entry for broker certificate (repeatable)",
    )
    parser.add_argument(
        "--broker-ip",
        action="append",
        default=[],
        help="Additional IP SAN entry for broker certificate (repeatable)",
    )
    parser.add_argument(
        "--cert-dns",
        action="append",
        default=[],
        help="Additional DNS SAN entry for custom profile certificate (repeatable)",
    )
    parser.add_argument(
        "--cert-ip",
        action="append",
        default=[],
        help="Additional IP SAN entry for custom profile certificate (repeatable)",
    )

    parser.add_argument(
        "--no-client-default-san",
        action="store_true",
        help="Do not auto-add mqtt-client SAN for client profile",
    )

    parser.add_argument(
        "--broker-pfx",
        action="store_true",
        help="Also export broker certificate and key as PKCS#12 (.pfx)",
    )
    parser.add_argument(
        "--broker-pfx-name",
        default="mqtt_broker.pfx",
        help="Broker PKCS#12 output filename when --broker-pfx is enabled",
    )
    parser.add_argument(
        "--broker-pfx-password",
        default="",
        help="Password for broker PKCS#12 output; empty means no encryption",
    )

    parser.add_argument(
        "--cert-pfx",
        action="store_true",
        help="Also export custom profile certificate and key as PKCS#12 (.pfx)",
    )
    parser.add_argument(
        "--cert-pfx-name",
        default="mqtt_cert.pfx",
        help="Custom profile PKCS#12 output filename when --cert-pfx is enabled",
    )
    parser.add_argument(
        "--cert-pfx-password",
        default="",
        help="Password for custom profile PKCS#12 output; empty means no encryption",
    )
    return parser


def parse_args() -> argparse.Namespace:
    pre_parser = argparse.ArgumentParser(add_help=False)
    pre_parser.add_argument("--config", default=None)
    pre_args, _ = pre_parser.parse_known_args()

    parser = _create_parser()
    config_data = _load_yaml_config(pre_args.config)

    default_overrides = _extract_default_overrides(config_data)
    _validate_allowed_fields(default_overrides, parser, context="config defaults")
    if default_overrides:
        parser.set_defaults(**default_overrides)

    args = parser.parse_args()
    return args


def _normalize_config_keys(value: Any) -> Any:
    if isinstance(value, dict):
        return {str(k).replace("-", "_"): _normalize_config_keys(v) for k, v in value.items()}
    if isinstance(value, list):
        return [_normalize_config_keys(v) for v in value]
    return value


def _load_yaml_config(config_path: str | None) -> dict[str, Any]:
    if not config_path:
        return {}

    if yaml is None:
        raise RuntimeError("PyYAML is required for --config. Install with: pip install pyyaml")

    path = Path(config_path)
    if not path.exists():
        raise FileNotFoundError(f"Config file not found: {path}")

    loaded = yaml.safe_load(path.read_text(encoding="utf-8"))
    if loaded is None:
        return {}
    if not isinstance(loaded, dict):
        raise ValueError("YAML config root must be a mapping/object")
    return _normalize_config_keys(loaded)


def _extract_default_overrides(config_data: dict[str, Any]) -> dict[str, Any]:
    if not config_data:
        return {}

    defaults: dict[str, Any] = {}

    defaults_block = config_data.get("defaults", {})
    if defaults_block:
        if not isinstance(defaults_block, dict):
            raise ValueError("config.defaults must be a mapping/object")
        defaults.update(defaults_block)

    for key, value in config_data.items():
        if key in RESERVED_CONFIG_KEYS:
            continue
        defaults[key] = value

    return defaults


def _validate_allowed_fields(values: dict[str, Any], parser: argparse.ArgumentParser, *, context: str) -> None:
    allowed = {action.dest for action in parser._actions}
    unknown = [key for key in values if key not in allowed]
    if unknown:
        unknown_sorted = ", ".join(sorted(unknown))
        raise ValueError(f"Unsupported field(s) in {context}: {unknown_sorted}")


def _as_list(value: Any, field_name: str) -> list[str]:
    if value is None:
        return []
    if isinstance(value, list):
        return [str(v) for v in value]
    if isinstance(value, str):
        return [value]
    raise ValueError(f"{field_name} must be a string or a list of strings")


def _namespace_to_dict(args: argparse.Namespace) -> dict[str, Any]:
    return {k: v for k, v in vars(args).items()}


def _merge_job_args(base_args: argparse.Namespace, job: dict[str, Any], parser: argparse.ArgumentParser) -> argparse.Namespace:
    _validate_allowed_fields(job, parser, context="config.jobs item")

    merged = _namespace_to_dict(base_args)
    for key, value in job.items():
        if key in LIST_FIELDS:
            merged[key] = _as_list(value, key)
        else:
            merged[key] = value
    return argparse.Namespace(**merged)


def _now() -> dt.datetime:
    return dt.datetime.now(dt.timezone.utc)


def _new_key() -> rsa.RSAPrivateKey:
    return rsa.generate_private_key(public_exponent=65537, key_size=2048)


def _write_key(path: Path, key: rsa.RSAPrivateKey) -> None:
    path.write_bytes(
        key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.TraditionalOpenSSL,
            encryption_algorithm=serialization.NoEncryption(),
        )
    )


def _write_cert(path: Path, cert: x509.Certificate) -> None:
    path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))


def _write_pfx(
    path: Path,
    *,
    cert: x509.Certificate,
    key: rsa.RSAPrivateKey,
    ca_cert: x509.Certificate,
    friendly_name: str,
    password: str,
) -> None:
    if password:
        encryption = serialization.BestAvailableEncryption(password.encode("utf-8"))
    else:
        encryption = serialization.NoEncryption()

    pfx_data = pkcs12.serialize_key_and_certificates(
        name=friendly_name.encode("utf-8"),
        key=key,
        cert=cert,
        cas=[ca_cert],
        encryption_algorithm=encryption,
    )
    path.write_bytes(pfx_data)


def _backup_if_needed(path: Path, enabled: bool) -> None:
    if not enabled or not path.exists():
        return
    stamp = dt.datetime.now().strftime("%Y%m%d%H%M%S")
    backup = path.with_name(f"{path.name}.bak-{stamp}")
    path.rename(backup)
    print(f"Backed up existing file: {backup}")


def _build_ca(days: int) -> tuple[rsa.RSAPrivateKey, x509.Certificate]:
    ca_key = _new_key()
    ca_public_key = ca_key.public_key()
    ca_ski = x509.SubjectKeyIdentifier.from_public_key(ca_public_key)
    subject = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME, "VN"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Local MQTT Test"),
        x509.NameAttribute(NameOID.COMMON_NAME, "Local MQTT Test CA"),
    ])
    now = _now()
    ca_cert = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(subject)
        .public_key(ca_public_key)
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - dt.timedelta(minutes=5))
        .not_valid_after(now + dt.timedelta(days=days))
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        .add_extension(ca_ski, critical=False)
        .add_extension(x509.AuthorityKeyIdentifier.from_issuer_subject_key_identifier(ca_ski), critical=False)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True,
                key_encipherment=False,
                key_cert_sign=True,
                key_agreement=False,
                content_commitment=False,
                data_encipherment=False,
                encipher_only=False,
                decipher_only=False,
                crl_sign=True,
            ),
            critical=True,
        )
        .sign(private_key=ca_key, algorithm=hashes.SHA256())
    )
    return ca_key, ca_cert


def _build_leaf(
    *,
    common_name: str,
    days: int,
    ca_key: rsa.RSAPrivateKey,
    ca_cert: x509.Certificate,
    san_dns: list[str] | None,
    san_ips: list[str] | None,
    server_auth: bool,
    client_auth: bool,
) -> tuple[rsa.RSAPrivateKey, x509.Certificate]:
    key = _new_key()
    public_key = key.public_key()
    ca_ski_ext = ca_cert.extensions.get_extension_for_class(x509.SubjectKeyIdentifier).value
    subject = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME, "VN"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Local MQTT Test"),
        x509.NameAttribute(NameOID.COMMON_NAME, common_name),
    ])

    now = _now()
    builder = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(ca_cert.subject)
        .public_key(public_key)
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - dt.timedelta(minutes=5))
        .not_valid_after(now + dt.timedelta(days=days))
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .add_extension(x509.SubjectKeyIdentifier.from_public_key(public_key), critical=False)
        .add_extension(x509.AuthorityKeyIdentifier.from_issuer_subject_key_identifier(ca_ski_ext), critical=False)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True,
                key_encipherment=True,
                key_cert_sign=False,
                key_agreement=False,
                content_commitment=False,
                data_encipherment=False,
                encipher_only=False,
                decipher_only=False,
                crl_sign=False,
            ),
            critical=True,
        )
    )

    usages = []
    if server_auth:
        usages.append(ExtendedKeyUsageOID.SERVER_AUTH)
    if client_auth:
        usages.append(ExtendedKeyUsageOID.CLIENT_AUTH)
    builder = builder.add_extension(x509.ExtendedKeyUsage(usages), critical=False)

    san_entries = []
    for name in san_dns or []:
        san_entries.append(x509.DNSName(name))
    for ip_text in san_ips or []:
        san_entries.append(x509.IPAddress(ip_address(ip_text)))
    if san_entries:
        builder = builder.add_extension(x509.SubjectAlternativeName(san_entries), critical=False)

    cert = builder.sign(private_key=ca_key, algorithm=hashes.SHA256())
    return key, cert


def _dedupe(values: list[str]) -> list[str]:
    return list(dict.fromkeys(values))


def _require_custom_field(value: str | None, flag: str) -> str:
    if not value:
        raise ValueError(f"{flag} is required when --profile custom is used")
    return value


def _generate_single(
    args: argparse.Namespace,
    *,
    ca_cache: dict[tuple[str, str], tuple[rsa.RSAPrivateKey, x509.Certificate]] | None = None,
) -> None:
    certs_dir = Path(args.certs_dir).resolve()
    certs_dir.mkdir(parents=True, exist_ok=True)

    paths = {
        "ca_cert": certs_dir / args.ca_cert_name,
        "ca_key": certs_dir / args.ca_key_name,
    }

    if args.profile == "broker":
        paths["leaf_cert"] = certs_dir / DEFAULT_BROKER_CERT_NAME
        paths["leaf_key"] = certs_dir / DEFAULT_BROKER_KEY_NAME
        if args.broker_pfx:
            paths["leaf_pfx"] = certs_dir / args.broker_pfx_name
    elif args.profile == "client":
        paths["leaf_cert"] = certs_dir / DEFAULT_CLIENT_CERT_NAME
        paths["leaf_key"] = certs_dir / DEFAULT_CLIENT_KEY_NAME
    else:
        cert_name = _require_custom_field(args.cert_name, "--cert-name")
        key_name = _require_custom_field(args.key_name, "--key-name")
        _require_custom_field(args.cert_cn, "--cert-cn")
        if not args.eku:
            raise ValueError("--eku is required at least once when --profile custom is used")

        paths["leaf_cert"] = certs_dir / cert_name
        paths["leaf_key"] = certs_dir / key_name
        if args.cert_pfx:
            paths["leaf_pfx"] = certs_dir / args.cert_pfx_name

    ca_key_ref = (str(paths["ca_cert"]), str(paths["ca_key"]))
    use_cached_ca = ca_cache is not None and ca_key_ref in ca_cache

    paths_to_backup = dict(paths)
    if use_cached_ca:
        # Keep previously generated CA files for this run; only rotate leaf outputs.
        del paths_to_backup["ca_cert"]
        del paths_to_backup["ca_key"]

    for path in paths_to_backup.values():
        _backup_if_needed(path, args.backup_existing)

    if use_cached_ca:
        ca_key, ca_cert = ca_cache[ca_key_ref]
    else:
        ca_key, ca_cert = _build_ca(days=args.days)
        if ca_cache is not None:
            ca_cache[ca_key_ref] = (ca_key, ca_cert)

    if args.profile == "broker":
        leaf_cn = args.broker_cn
        leaf_dns = _dedupe(["localhost", *_as_list(args.broker_dns, "broker_dns")])
        leaf_ips = _dedupe(["127.0.0.1", *_as_list(args.broker_ip, "broker_ip")])
        leaf_server_auth = True
        leaf_client_auth = False
    elif args.profile == "client":
        leaf_cn = "mqtt-client"
        base_dns = [] if args.no_client_default_san else ["mqtt-client"]
        leaf_dns = _dedupe([*base_dns, *_as_list(args.cert_dns, "cert_dns")])
        leaf_ips = _dedupe(_as_list(args.cert_ip, "cert_ip"))
        leaf_server_auth = False
        leaf_client_auth = True
    else:
        leaf_cn = args.cert_cn
        leaf_dns = _dedupe(_as_list(args.cert_dns, "cert_dns"))
        leaf_ips = _dedupe(_as_list(args.cert_ip, "cert_ip"))
        leaf_server_auth = "server" in _as_list(args.eku, "eku")
        leaf_client_auth = "client" in _as_list(args.eku, "eku")

    leaf_key, leaf_cert = _build_leaf(
        common_name=leaf_cn,
        days=args.days,
        ca_key=ca_key,
        ca_cert=ca_cert,
        san_dns=leaf_dns,
        san_ips=leaf_ips,
        server_auth=leaf_server_auth,
        client_auth=leaf_client_auth,
    )

    if not use_cached_ca:
        _write_cert(paths["ca_cert"], ca_cert)
        _write_key(paths["ca_key"], ca_key)
    _write_cert(paths["leaf_cert"], leaf_cert)
    _write_key(paths["leaf_key"], leaf_key)

    if "leaf_pfx" in paths:
        if args.profile == "broker":
            friendly_name = "mqtt-broker"
            pfx_password = args.broker_pfx_password
        else:
            friendly_name = leaf_cn
            pfx_password = args.cert_pfx_password

        _write_pfx(
            paths["leaf_pfx"],
            cert=leaf_cert,
            key=leaf_key,
            ca_cert=ca_cert,
            friendly_name=friendly_name,
            password=pfx_password,
        )

    print("Generated certificates:")
    print(f"- {paths['ca_cert']}")
    print(f"- {paths['ca_key']}")
    print(f"- {paths['leaf_cert']}")
    print(f"- {paths['leaf_key']}")
    if "leaf_pfx" in paths:
        print(f"- {paths['leaf_pfx']}")


def main() -> int:
    args = parse_args()
    parser = _create_parser()
    config_data = _load_yaml_config(args.config)
    jobs = config_data.get("jobs", [])

    if jobs:
        if not isinstance(jobs, list):
            raise ValueError("config.jobs must be a list")

        ca_cache: dict[tuple[str, str], tuple[rsa.RSAPrivateKey, x509.Certificate]] = {}

        for idx, job in enumerate(jobs, start=1):
            if not isinstance(job, dict):
                raise ValueError("Each item in config.jobs must be a mapping/object")
            normalized_job = _normalize_config_keys(job)
            job_args = _merge_job_args(args, normalized_job, parser)
            print(f"Running job {idx}/{len(jobs)} (profile={job_args.profile})")
            _generate_single(job_args, ca_cache=ca_cache)
    else:
        _generate_single(args)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
