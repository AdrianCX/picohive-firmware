#!/usr/bin/env python3

"""Checks a .uf2.encrypted the way the device will:

# ./verify_signed_binary.py firmware.uf2.encrypted [--out firmware.uf2]

"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ota_format


def main():
    here = os.path.dirname(os.path.abspath(__file__))

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("input", help="the .uf2.encrypted to check")
    parser.add_argument("--out", help="write the plain UF2 here")
    parser.add_argument("--keys", default=ota_format.default_key_version_dir(),
                        help="key version directory to check against")
    parser.add_argument("--key", help="public signing key, instead of the one in --keys")
    parser.add_argument("--decryption-key", help="private encryption key, instead of the one in --keys")
    args = parser.parse_args()

    args.key = args.key or os.path.join(args.keys, "ota_signing_key.pub.pem")
    args.decryption_key = args.decryption_key or os.path.join(args.keys, "ota_encryption_key.pem")

    with open(args.input, "rb") as f:
        container = ota_format.Container(f.read())

    print(container.describe())

    if not ota_format.verify(ota_format.load_public_key(args.key), container.signature, container.signed_bytes):
        sys.exit(f"{args.input}: bad signature, a device would refuse this")
    print("Signature Verified OK")

    if not os.path.exists(args.decryption_key):
        print(f"no {args.decryption_key}, tag and firmware not checked")
        return

    from cryptography.exceptions import InvalidTag

    secret = ota_format.recover_secret(ota_format.load_encryption_private_key(args.decryption_key),
                                       container.ephemeral)

    try:
        uf2 = ota_format.decrypt(secret[:ota_format.AES_KEY_LEN], secret[ota_format.AES_KEY_LEN:],
                                 container.tag, container.payload)
    except InvalidTag:
        sys.exit(f"{args.input}: bad tag, the ciphertext was altered")
    print("Tag Verified OK")

    if uf2[:4] != b"UF2\n":
        sys.exit(f"{args.input}: that is not a uf2 inside")
    print("Firmware Verified OK")

    if args.out:
        with open(args.out, "wb") as f:
            f.write(uf2)


if __name__ == "__main__":
    main()
