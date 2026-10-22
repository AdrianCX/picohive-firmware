#!/usr/bin/env python3
"""Writes root_certificates_der.cpp: the root CAs the board trusts for outgoing
TLS, as DER in const arrays, so they stay in flash. Run by the build (see
libraries/pico_tls/CMakeLists.txt) from a list of domains.

The list is one domain per line (host or host:port), '#' starts a comment, and
"root <common name>" adds a root by name, for a host the build cannot reach.
For each domain the script handshakes with it, walks the chain it sends from
the server's certificate up, and takes the first issuer that is a root in this
machine's CA bundle - the root mbedtls will stop at on the board.

Each entry also records where the root's subject sits inside its DER: the board
matches a server's issuer against that slice byte for byte and parses only the
root that matches, for the handshake that needs it.

The network is only used when the list changes: the output records a hash of
the list, and is left alone while it matches. --force fetches again.

Usage:
    os/util/make_root_certificates.py --domains os/config/tls_domains.txt --out build/root_certificates_der.cpp
"""
import argparse
import base64
import hashlib
import os
import re
import subprocess
import sys

BUNDLES = ["/etc/ssl/certs/ca-certificates.crt", "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem"]
PEM = re.compile(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", re.S)
VERSION = "2"


def tlv(der, at):
    """(tag, start of the value, end of the whole element) of the element at at."""
    tag = der[at]
    size = der[at + 1]
    at += 2

    if size & 0x80:
        count = size & 0x7F
        size = int.from_bytes(der[at:at + count], "big")
        at += count

    return tag, at, at + size


def names(der):
    """(issuer, subject) as DER slices (offset, length), tag and length included - mbedtls' issuer_raw and subject_raw."""
    _, cert, _ = tlv(der, 0)
    _, at, _ = tlv(der, cert)           # tbsCertificate: walk its fields

    if der[at] == 0xA0:                 # [0] version
        at = tlv(der, at)[2]

    for _ in range(2):                  # serial, signature
        at = tlv(der, at)[2]

    issuer = (at, tlv(der, at)[2] - at)
    at = tlv(der, at)[2]
    at = tlv(der, at)[2]                # validity
    return issuer, (at, tlv(der, at)[2] - at)


def piece(der, slice_):
    return der[slice_[0]:slice_[0] + slice_[1]]


def common_name(der):
    name = piece(der, names(der)[1])
    # 2.5.4.3, commonName: the string after the last one is the CN.
    at = name.rfind(b"\x06\x03\x55\x04\x03")

    if at < 0:
        return "?"

    _, value, end = tlv(name, at + 5)
    return name[value:end].decode("utf-8", "replace")


def ders(text):
    return [base64.b64decode("".join(pem.strip().splitlines()[1:-1])) for pem in PEM.findall(text)]


def chain(host, port):
    """The certificates the server sends, its own first."""
    try:
        out = subprocess.run(["openssl", "s_client", "-connect", f"{host}:{port}", "-servername", host, "-showcerts"],
                             input=b"", capture_output=True, timeout=20).stdout.decode("utf-8", "replace")
    except (OSError, subprocess.TimeoutExpired) as failure:
        sys.exit(f"make_root_certificates: {host}:{port}: {failure}")

    found = ders(out)

    if not found:
        sys.exit(f"make_root_certificates: no certificates from {host}:{port} - is the network up? "
                 f"Remove it from the list, or name its root with 'root <common name>'.")

    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--domains", required=True, help="the list of domains, see above")
    parser.add_argument("--out", required=True, help="the .cpp to write")
    parser.add_argument("--bundle", help="PEM bundle of trusted roots (default: this machine's)")
    parser.add_argument("--force", action="store_true", help="fetch again even if the list did not change")
    args = parser.parse_args()

    bundle = args.bundle or next((path for path in BUNDLES if os.path.exists(path)), None)

    if bundle is None:
        sys.exit("make_root_certificates: no CA bundle found: pass --bundle")

    entries = []

    for line in open(args.domains, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()

        if line:
            entries.append(line)

    stamp = "// list: " + hashlib.sha256(("\n".join(entries) + "\n" + bundle + "\n" + VERSION).encode()).hexdigest()

    if not args.force and os.path.exists(args.out):
        with open(args.out, encoding="utf-8") as current:
            if stamp in current.read(4096):
                print(f"make_root_certificates: {os.path.basename(args.out)} up to date with {args.domains}")
                return

    roots = {}          # subject DER -> root DER, from the bundle

    for der in ders(open(bundle, encoding="utf-8").read()):
        roots[piece(der, names(der)[1])] = der

    chosen = {}         # root DER -> what needs it

    for entry in entries:
        if entry.startswith("root "):
            wanted = entry[5:].strip()
            match = [der for der in roots.values() if common_name(der) == wanted]

            if not match:
                sys.exit(f"make_root_certificates: no root named '{wanted}' in {bundle}")

            chosen.setdefault(match[0], []).append("named")
            continue

        host, _, port = entry.partition(":")
        found = None

        for der in chain(host, port or "443"):
            found = roots.get(piece(der, names(der)[0]))

            if found is not None:
                break

        if found is None:
            sys.exit(f"make_root_certificates: no root in {bundle} signs the chain {host} sends")

        chosen.setdefault(found, []).append(host)

    lines = [f"// Generated by os/util/make_root_certificates.py from {args.domains} - do not edit.",
             stamp,
             "// The root CAs outgoing TLS trusts, as DER, kept in flash.", "",
             '#include "root_certificates.h"', ""]

    for index, (der, users) in enumerate(chosen.items()):
        lines.append(f"// {common_name(der)}: {', '.join(users)}")
        lines.append(f"static const uint8_t ROOT_{index}[{len(der)}] = {{")

        for at in range(0, len(der), 16):
            lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in der[at:at + 16]) + ",")

        lines.append("};")
        lines.append("")

    lines.append("const RootCertificate ROOT_CERTIFICATES[] = {")

    for index, der in enumerate(chosen):
        offset, length = names(der)[1]
        lines.append(f'    {{ "{common_name(der)}", ROOT_{index}, sizeof(ROOT_{index}), {offset}, {length} }},')

    if not chosen:
        lines.append("    { NULL, NULL, 0, 0, 0 },")

    lines.append("};")
    lines.append("")
    lines.append(f"const uint32_t ROOT_CERTIFICATE_COUNT = {len(chosen)};")
    lines.append("")

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)

    with open(args.out, "w", encoding="utf-8") as out:
        out.write("\n".join(lines))

    total = sum(len(der) for der in chosen)
    print(f"make_root_certificates: {args.out}: {len(chosen)} roots, {total} bytes of DER")

    for der, users in chosen.items():
        print(f"    {common_name(der)}: {', '.join(users)}")


if __name__ == "__main__":
    main()
