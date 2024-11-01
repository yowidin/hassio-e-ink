import argparse

import socket
import struct

import lz4.block

from PIL import Image


def download_and_save(host: str, port: int):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))

    # Get image request
    sock.send(struct.pack('<BBIIBI', 0x10, 1, 55, 0, 10, 3300000))

    # Receive the size of the image
    header_data = sock.recv(8)
    header = struct.unpack('<BBHHH', header_data)

    message_type = header[0]
    update_type = header[1]
    width = header[2]
    height = header[3]
    num_blocks = header[4]
    print(f't={message_type}, u={update_type}, w={width}, h={height}, n={num_blocks}')

    image_data = b''
    for i in range(num_blocks):
        block_header = sock.recv(5)
        block_header_data = struct.unpack('<BHH', block_header)
        message_type = block_header_data[0]
        if message_type != 0x12:
            raise ConnectionError(f"Bad block message {message_type}")

        uncompressed_size = block_header_data[1]
        compressed_size = block_header_data[2]
        print(f'Block #{i:03} r={uncompressed_size}, c={compressed_size}')

        chunk = sock.recv(compressed_size)
        if not chunk:
            raise ConnectionError("Socket connection broken")

        decompressed = lz4.block.decompress(chunk, uncompressed_size=uncompressed_size)
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
