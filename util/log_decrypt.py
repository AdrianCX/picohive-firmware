#!/usr/bin/env python3
"""Receive pico_logger UDP logs, decrypt the encrypted ones, print or store them.

Encrypted packet (see os/libraries/pico_logger/log_encryption.h):

    0   1   version (1)
    1  16   device id, the uuid in binary
   17   8   boot id, random per boot          | nonce
   25   4   sequence, big endian, per boot    |
   29   4   ip address                        | encrypted
   33   n   log line                          |
   ..  16   AES-GCM tag, bytes 0..28 authenticated too

Plain text lines (no key configured on the device) are passed through as they are.

Keys:
    --key HEX           one key for every device
    --keys FILE         a "<uuid> <hex key>" line per device, '#' starts a comment
    --genkey            print a new random AES-128 key and exit (--genkey 256 for AES-256)

Examples:
    python3 log_decrypt.py --genkey
    python3 log_decrypt.py --port 23500 --keys keys.txt
    python3 log_decrypt.py --port 23500 --key 00112233445566778899aabbccddeeff --out-dir ./logs

Needs the 'cryptography' package (pip install cryptography).
"""

import argparse
import ipaddress
import os
import secrets
import socket
import struct
import sys
import uuid as uuidlib
from datetime import datetime

try:
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    from cryptography.exceptions import InvalidTag
except ImportError:
    sys.exit("log_decrypt.py needs the 'cryptography' package: pip install cryptography")

VERSION = 1
HEADER_SIZE = 29
TAG_SIZE = 16


def parse_key(text):
    key = bytes.fromhex(text.strip())
    if len(key) not in (16, 32):
        raise ValueError("key must be 32 or 64 hex characters")
    return key


def device_name(device_id):
    """The uuid as text, or the id string for devices whose id isn't a uuid."""
    printable = device_id.rstrip(b"\0")
    if printable and all(32 <= c < 127 for c in printable) and len(printable) < 16:
        return printable.decode()
    return str(uuidlib.UUID(bytes=device_id))


def load_keys(path):
    keys = {}
    with open(path) as f:
        for number, line in enumerate(f, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) != 2:
                sys.exit("%s:%d: expected '<uuid> <hex key>'" % (path, number))
            keys[parts[0].lower()] = parse_key(parts[1])
    return keys


class Decoder:
    def __init__(self, keys, default_key):
        self.keys = keys
        self.default_key = default_key
        self.last_sequence = {}

    def key_for(self, name):
        return self.keys.get(name.lower(), self.default_key)

    def decode(self, data):
        """Returns (device, text) for one packet."""
        if len(data) < HEADER_SIZE + 4 + TAG_SIZE or data[0] != VERSION:
            return None, data.decode(errors="replace").rstrip("\n")

        header = data[:HEADER_SIZE]
        device = device_name(header[1:17])
        boot_id = header[17:25]
        sequence = struct.unpack(">I", header[25:29])[0]

        key = self.key_for(device)
        if key is None:
            return device, "<no key for %s>" % device

        try:
            plain = AESGCM(key).decrypt(header[17:29], data[HEADER_SIZE:], header)
        except InvalidTag:
            return device, "<does not decrypt, wrong key or damaged packet>"

        ip = str(ipaddress.IPv4Address(plain[:4]))
        text = plain[4:].decode(errors="replace")

        # Sequence restarts with every boot, gaps are lost packets, repeats are replays.
        note = ""
        last = self.last_sequence.get((device, boot_id))
        if last is not None:
            if sequence <= last:
                note = " <repeated, sequence %d after %d>" % (sequence, last)
            elif sequence != last + 1:
                note = " <%d lost>" % (sequence - last - 1)
        elif sequence != 0:
            note = " <joined at sequence %d>" % sequence
        self.last_sequence[(device, boot_id)] = max(sequence, last if last is not None else -1)

        return device, "%s %s %3d %s%s" % (device, ip, sequence % 1000, text, note)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=21000, help="UDP port to listen on (default 21000)")
    parser.add_argument("--bind", default="0.0.0.0", help="address to listen on")
    parser.add_argument("--key", help="hex key for every device")
    parser.add_argument("--keys", help="file with a '<uuid> <hex key>' line per device")
    parser.add_argument("--out-dir", help="also append each device's lines to <out-dir>/<device or ip>.txt")
    parser.add_argument("--genkey", nargs="?", const=128, type=int, choices=(128, 256), help="print a new key and exit")
    args = parser.parse_args()

    if args.genkey:
        print(secrets.token_hex(args.genkey // 8))
        return

    keys = load_keys(args.keys) if args.keys else {}
    default_key = parse_key(args.key) if args.key else None
    decoder = Decoder(keys, default_key)

    if args.out_dir:
        os.makedirs(args.out_dir, exist_ok=True)
    files = {}

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.bind, args.port))
    print("listening on %s:%d, %d device keys%s" % (args.bind, args.port, len(keys), ", default key" if default_key else ""))

    while True:
        data, addr = sock.recvfrom(4096)
        device, line = decoder.decode(data)
        stamped = datetime.now().strftime("[%Y-%m-%d %H:%M:%S] ") + line
        print(stamped, flush=True)

        if args.out_dir:
            name = device or addr[0]
            f = files.get(name)
            if f is None:
                f = files[name] = open(os.path.join(args.out_dir, name + ".txt"), "a")
            f.write(stamped + "\n")
            f.flush()


if __name__ == "__main__":
    main()
