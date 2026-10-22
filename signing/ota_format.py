"""The OTA container format

    .uf2.encrypted  [MAGIC][HEADER_SIZE][KEYLEN][EPHEMERAL_KEY]
                    [TAG][BUILD_ID][BOARD][SIGLEN][SIG][CIPHERTEXT]

Keep this in step with libraries/pico_simple_ota/ota_container.h, which parses
the same bytes on the device.
"""

import os
import struct
import sys

try:
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec, padding, rsa
    from cryptography.hazmat.primitives.kdf.hkdf import HKDF
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
except ImportError:
    sys.exit(
        "this needs the python cryptography package:\n"
        "  apt install python3-cryptography      (debian/ubuntu, and the build container)\n"
        "  dnf install python3-cryptography      (fedora)\n"
        "  pip install cryptography"
    )

ENCRYPTED_MAGIC = b"ENCRUF2\n"
MAGIC_LEN = 8

AES_KEY_LEN = 32
IV_LEN = 12
SECRET_LEN = AES_KEY_LEN + IV_LEN

# Must match OTA_KDF_SALT in libraries/pico_simple_ota/ota_container.h.
KDF_SALT = b"pico_os ota v1"
TAG_LEN = 16
BUILD_ID_LEN = 20
BOARD_LEN = 16

MAX_KEY_LEN = 133
MAX_SIG_LEN = 512


def default_key_dir():
    """keys/ next to the application's src/, the same place the build looks."""
    override = os.environ.get("PICO_OS_KEY_DIR")
    if override:
        return override

    here = os.path.dirname(os.path.abspath(__file__))

    # The outermost directory that still holds a CMakeLists.txt is the
    # application: this repository has one too when it is built on its own.
    root = None
    directory = here
    while True:
        parent = os.path.dirname(directory)
        if parent == directory:
            break
        if os.path.exists(os.path.join(parent, "CMakeLists.txt")):
            root = parent
        directory = parent

    return os.path.join(root or os.path.dirname(here), "keys")


def default_key_version_dir(version=0):
    return os.path.join(default_key_dir(), "versions", str(version))


def le32(value):
    return struct.pack("<I", value)


def read_le32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def load_private_key(path):
    with open(path, "rb") as f:
        key = serialization.load_pem_private_key(f.read(), password=None)
    if not isinstance(key, rsa.RSAPrivateKey):
        sys.exit(f"{path}: not an RSA private key")
    return key


def load_public_key(path):
    """The signing key's public half, which is RSA."""
    with open(path, "rb") as f:
        key = serialization.load_pem_public_key(f.read())
    if not isinstance(key, rsa.RSAPublicKey):
        sys.exit(f"{path}: not an RSA public key")
    return key


def load_encryption_public_key(path):
    """The device's encryption key, which is P-256."""
    with open(path, "rb") as f:
        key = serialization.load_pem_public_key(f.read())
    if not isinstance(key, ec.EllipticCurvePublicKey):
        sys.exit(f"{path}: not an EC public key, run create_keys.sh <version> --force")
    return key


def load_encryption_private_key(path):
    with open(path, "rb") as f:
        key = serialization.load_pem_private_key(f.read(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey):
        sys.exit(f"{path}: not an EC private key, run create_keys.sh <version> --force")
    return key


def sign(key, data):
    return key.sign(data, padding.PKCS1v15(), hashes.SHA256())


def verify(key, signature, data):
    """True when the signature is this key's over this data."""
    from cryptography.exceptions import InvalidSignature

    try:
        key.verify(signature, data, padding.PKCS1v15(), hashes.SHA256())
        return True
    except InvalidSignature:
        return False


def _derive(shared, ephemeral):
    """HKDF-SHA256 to [AES-256 key][IV], bound to this ephemeral key."""
    return HKDF(algorithm=hashes.SHA256(), length=SECRET_LEN, salt=KDF_SALT, info=ephemeral).derive(shared)


def make_secret(encryption_public_key):
    """A key and IV for one file. Returns (secret, ephemeral public key bytes)."""
    ephemeral = ec.generate_private_key(ec.SECP256R1())
    ephemeral_bytes = ephemeral.public_key().public_bytes(
        serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)

    shared = ephemeral.exchange(ec.ECDH(), encryption_public_key)
    return _derive(shared, ephemeral_bytes), ephemeral_bytes


def recover_secret(encryption_private_key, ephemeral_bytes):
    """The same secret, from the device's side of the exchange."""
    ephemeral = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), ephemeral_bytes)
    shared = encryption_private_key.exchange(ec.ECDH(), ephemeral)
    return _derive(shared, ephemeral_bytes)


def encrypt(key, iv, plaintext):
    """AES-256-GCM. Returns (ciphertext, tag)."""
    encryptor = Cipher(algorithms.AES(key), modes.GCM(iv)).encryptor()
    ciphertext = encryptor.update(plaintext) + encryptor.finalize()
    return ciphertext, encryptor.tag


def decrypt(key, iv, tag, ciphertext):
    """AES-256-GCM. Raises cryptography.exceptions.InvalidTag if it was altered."""
    decryptor = Cipher(algorithms.AES(key), modes.GCM(iv, tag)).decryptor()
    return decryptor.update(ciphertext) + decryptor.finalize()


def elf_build_id(path):
    """The GNU build id the linker put in a little endian ELF, 32 or 64 bit, as hex ("" if absent)."""
    try:
        with open(path, "rb") as f:
            elf = f.read()
    except OSError as error:
        print(f"warning: {error}, leaving the build id empty", file=sys.stderr)
        return ""

    if elf[:4] != b"\x7fELF" or elf[5] != 1 or elf[4] not in (1, 2):
        print(f"warning: {path} is not a little endian ELF, leaving the build id empty", file=sys.stderr)
        return ""

    if elf[4] == 1:
        # 32 bit: section header fields are 4 bytes.
        e_shoff = read_le32(elf, 0x20)
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", elf, 0x2E)
        offset_of = lambda header: read_le32(elf, header + 0x10)
    else:
        # 64 bit: 8 byte offsets.
        e_shoff = struct.unpack_from("<Q", elf, 0x28)[0]
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", elf, 0x3A)
        offset_of = lambda header: struct.unpack_from("<Q", elf, header + 0x18)[0]

    names_offset = offset_of(e_shoff + e_shstrndx * e_shentsize)

    for i in range(e_shnum):
        header = e_shoff + i * e_shentsize
        name_offset = names_offset + read_le32(elf, header)
        name = elf[name_offset:elf.index(b"\0", name_offset)]

        if name != b".note.gnu.build-id":
            continue

        offset = offset_of(header)
        namesz, descsz, _type = struct.unpack_from("<III", elf, offset)
        desc = offset + 12 + ((namesz + 3) & ~3)
        return elf[desc:desc + descsz].hex()

    print(f"warning: no build id in {path}", file=sys.stderr)
    return ""


def guess_board(path):
    """build/pico2_w/washingmachine.uf2 -> pico2_w"""
    parts = os.path.abspath(path).split(os.sep)
    if "build" in parts:
        index = parts.index("build")
        if index + 1 < len(parts) - 1:
            return parts[index + 1]
    return None


def pack_encrypted(uf2, signing_key, encryption_key, board, build_id):
    """build_id is hex, board a short name like "pico2_w"."""
    board = board.encode()
    if len(board) >= BOARD_LEN:
        sys.exit(f"board name '{board.decode()}' does not fit in {BOARD_LEN} bytes")

    # Cut rather than push the fields that follow out of place; a build id is a
    # sha1, so this is about a caller passing something else.
    build_id = bytes.fromhex(build_id)[:BUILD_ID_LEN]

    secret, ephemeral = make_secret(encryption_key)
    ciphertext, tag = encrypt(secret[:AES_KEY_LEN], secret[AES_KEY_LEN:], uf2)

    signature_len = signing_key.key_size // 8
    header_size = (MAGIC_LEN + 4 + 4 + len(ephemeral) + TAG_LEN + BUILD_ID_LEN +
                   BOARD_LEN + 4 + signature_len)

    prefix = (ENCRYPTED_MAGIC + le32(header_size) + le32(len(ephemeral)) + ephemeral +
              tag + build_id.ljust(BUILD_ID_LEN, b"\0") + board.ljust(BOARD_LEN, b"\0"))

    signature = sign(signing_key, prefix + ciphertext)
    if len(signature) != signature_len:
        sys.exit(f"signature is {len(signature)} bytes, the header says {signature_len}")

    return prefix + le32(len(signature)) + signature + ciphertext


class Container:
    """A packaged file taken apart, as the device parses it."""

    def __init__(self, data):
        self.data = data
        if data[:MAGIC_LEN] != ENCRYPTED_MAGIC:
            sys.exit("not a .uf2.encrypted (bad magic)")

        self.header_size = read_le32(data, MAGIC_LEN)
        self.key_len = read_le32(data, 12)

        tag_offset = 16 + self.key_len
        build_id_offset = tag_offset + TAG_LEN
        board_offset = build_id_offset + BUILD_ID_LEN
        signature_len_offset = board_offset + BOARD_LEN
        signature_offset = signature_len_offset + 4

        if self.key_len > MAX_KEY_LEN or signature_offset + 1 > self.header_size:
            sys.exit(f"malformed header: {self.key_len} byte ephemeral key in a {self.header_size} byte header")

        self.signature_len = read_le32(data, signature_len_offset)
        if signature_offset + self.signature_len != self.header_size:
            sys.exit(f"header says {self.header_size} bytes, fields add up to "
                     f"{signature_offset + self.signature_len}")

        self.ephemeral = data[16:16 + self.key_len]
        self.tag = data[tag_offset:tag_offset + TAG_LEN]
        self.build_id = data[build_id_offset:build_id_offset + BUILD_ID_LEN].hex()
        self.board = data[board_offset:board_offset + BOARD_LEN].rstrip(b"\0").decode(errors="replace")
        self.signature = data[signature_offset:signature_offset + self.signature_len]
        self.payload = data[self.header_size:]
        self.signed_bytes = data[:signature_len_offset] + self.payload

    def describe(self):
        return (f"encrypted uf2 for {self.board}, build {self.build_id}, "
                f"{self.key_len} byte ephemeral key, {self.signature_len} byte signature")
