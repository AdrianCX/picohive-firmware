#!/usr/bin/env python3

"""Wraps a .uf2 into a .uf2.encrypted used for OTA"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ota_format


def main():
    here = os.path.dirname(os.path.abspath(__file__))

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("input", help="the .uf2 to package")
    parser.add_argument("output", nargs="?", help="default: <input>.encrypted")
    parser.add_argument("--board", help="what the device must be (pico_w, pico2_w, ...); "
                                        "guessed from a build/<board>/ path when not given")
    parser.add_argument("--elf", help="ELF to take the build id from, usually the one next to the .uf2")
    parser.add_argument("--build-id", default="", help="build id as hex, instead of --elf")
    parser.add_argument("--keys", default=ota_format.default_key_version_dir(),
                        help="key version directory to package with")
    parser.add_argument("--key", help="public encryption key, instead of the one in --keys")
    parser.add_argument("--signing-key", help="private signing key, instead of the one in --keys")
    args = parser.parse_args()

    output = args.output or args.input + ".encrypted"

    args.key = args.key or os.path.join(args.keys, "ota_encryption_key.pub.pem")
    args.signing_key = args.signing_key or os.path.join(args.keys, "ota_signing_key.pem")

    board = args.board or ota_format.guess_board(args.input)
    if not board:
        sys.exit("pass --board (pico_w, pico2_w, ...), a device refuses firmware built for another one")

    for key in (args.key, args.signing_key):
        if not os.path.exists(key):
            sys.exit(f"no key at {key}, run create_keys.sh")

    build_id = args.build_id
    if not build_id and args.elf:
        build_id = ota_format.elf_build_id(args.elf)

    with open(args.input, "rb") as f:
        uf2 = f.read()

    encrypted = ota_format.pack_encrypted(uf2,
                                          ota_format.load_private_key(args.signing_key),
                                          ota_format.load_encryption_public_key(args.key),
                                          board,
                                          build_id)

    with open(output + ".tmp", "wb") as f:
        f.write(encrypted)
    os.replace(output + ".tmp", output)

    print(f"Encrypted {args.input} -> {output} ({board}, build {build_id or 'unknown'})")


if __name__ == "__main__":
    main()
