import argparse

import socket
import struct

import lz4.frame

from PIL import Image


def download_and_save(host: str, port: int):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))

    # Get image request
    sock.send(struct.pack('<H', 0x10))

    # Receive the size of the image
    header_data = sock.recv(8)
    header = struct.unpack('<HHHH', header_data)

    message_type = header[0]
    width = header[1]
    height = header[2]
    num_blocks = header[3]
    print(f't={message_type}, w={width}, h={height}, n={num_blocks}')

    image_data = b''
    for i in range(num_blocks):
        block_header = sock.recv(6)
        block_header_data = struct.unpack('<HHH', block_header)
        message_type = block_header_data [0]
        if message_type != 0x12:
            raise ConnectionError(f"Bad block message {message_type}")

        uncompressed_size = block_header_data[1]
        compressed_size = block_header_data[2]
        print(f'Block #{i:03} r={uncompressed_size}, c={compressed_size}')

        chunk = sock.recv(compressed_size)
        if not chunk:
            raise ConnectionError("Socket connection broken")

        decompressed = lz4.frame.decompress(chunk)
        if len(decompressed) != uncompressed_size:
            raise ConnectionError(f"Bad image data: {len(decompressed)} vs {uncompressed_size}")

        image_data += decompressed

    sock.close()

    return image_data, width, height


def main():
    parser = argparse.ArgumentParser('Dummy image server client')
    parser.add_argument('--host', type=str, required=True, help='Image server host')
    parser.add_argument('--port', type=int, required=True, help='Image server port')

    args = parser.parse_args()

    image_data, width, height = download_and_save(args.host, args.port)
    image = Image.frombytes('L', (width, height), image_data)
    image.save('image.png')


if __name__ == '__main__':
    main()
