import argparse
import sys
from hashlib import sha256
import os.path

def main():
    parser = argparse.ArgumentParser()

    parser.add_argument("--offset", help="Offset to start calculation", default = '0')
    parser.add_argument("--image", help="Image to be modified", required=True)
    parser.add_argument("--size", help="Number of bytes to use", required=True)
    args = parser.parse_args()

    size = int(args.size)
    offset = int(args.offset)
    ec_checksum = os.path.join(os.path.dirname(args.image), "ec_hash.bin")

    with open(args.image, 'rb') as f:
        image = bytearray(f.read())
        checksum = sha256(image[offset:offset + size])
        print(checksum.hexdigest())

    image[offset + size:offset + size + 32] = checksum.digest()

    with open(ec_checksum, 'wb') as f:
        f.write(image)

if __name__ == '__main__':
    sys.exit(main())
