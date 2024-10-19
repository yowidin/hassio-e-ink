import asyncio

from PIL import Image
import numpy as np
import lz4.block

from heihost.log import Log


class HostedImage:
    """
    A grayscale image split into blocks for easier delivery.
    Each block is also compressed with LZ4.
    """

    class Block:
        SIZE = 4096

        def __init__(self, uncompressed_size: int, data: bytes):
            self.uncompressed_size = uncompressed_size
            self.data = data
            self.size = len(self.data)

    def __init__(self, width: int, height: int, blocks: list['HostedImage.Block'], image: Image):
        self.width = width
        self.height = height
        self.blocks = blocks
        self.image = image

        original_size = sum(x.uncompressed_size for x in self.blocks)
        compressed_size = sum(x.size for x in self.blocks)
        Log.debug(f'Hosted image: {compressed_size} / {original_size} ({100 / original_size * compressed_size:0.2f}%)')

    @staticmethod
    async def from_image(image: Image):
        grayscale = await asyncio.to_thread(HostedImage._convert_to_4bit_grayscale, image)
        width, height, blocks = await asyncio.to_thread(HostedImage._split_into_blocks, grayscale)
        return HostedImage(width, height, blocks, grayscale)

    @staticmethod
    def _convert_to_4bit_grayscale(image: Image):
        raw = image.convert('L')
        np_image = np.array(raw)

        # Create a 4-bit (16 levels) grayscale palette
        palette = np.arange(0, 256, 256 // 16, dtype=np.uint8)

        # Map each pixel to the nearest palette color
        quantized = np.digitize(np_image, palette) - 1

        # Reshape the image to group consecutive pairs of 4-bit values along the width
        reshaped = quantized.reshape(quantized.shape[0], -1, 2)

        # The following table is adapted from the IT8951 v0.2.4.3 PDF, p 29:
        # Figure 7-17. Packed Pixel Data Transfer
        # 2bpp:
        # +------+------+------+------+------+------+------+------+
        # | Pn+7 | Pn+6 | Pn+5 | Pn+4 | Pn+3 | Pn+2 | Pn+1 |  Pn  |
        # +------+------+------+------+------+------+------+------+
        #  15                          7
        #
        # 3bpp:
        # +------+------+------+------+------+------+------+------+
        # | Pn+3 |  0   | Pn+2 |  0   | Pn+1 |  0   |  Pn  |  0   |
        # +------+------+------+------+------+------+------+------+
        #  15                          7
        #
        # 4bpp:
        # +------+------+------+------+
        # | Pn+3 | Pn+2 | Pn+1 |  Pn  |
        # +------+------+------+------+
        #  15            7
        #
        # 8bpp:
        # +------+------+
        # | Pn+1 |  Pn  |
        # +------+------+
        #  15     7
        #
        # We host 4bpp images, so we have to combine two 4-bit pixels into one byte, with the pixel N in the lower
        # nibble, and the pixel N+1 in the higher nibble.
        combined = (reshaped[:, :, 1] << 4) | reshaped[:, :, 0]

        # We also have to swap the byte pairs to better match the final data transfer to the display
        combined = combined.reshape(combined.shape[0], -1, 2)[:, :, ::-1].reshape(combined.shape)

        return Image.fromarray(combined.astype(np.uint8), mode='L')

    @staticmethod
    def _split_into_blocks(image: Image):
        raw = image.convert('L')
        width, height = raw.size
        pixel_data = np.array(raw)
        raw_data = pixel_data.flatten()

        remaining = len(raw_data)
        start = 0

        blocks = []
        while remaining != 0:
            size = min(HostedImage.Block.SIZE, remaining)
            block_data = raw_data[start:start + size]
            assert (len(block_data) == size)

            remaining -= size
            start += size
            compressed_data = lz4.block.compress(block_data, store_size=False)

            blocks.append(HostedImage.Block(len(block_data), compressed_data))

        return width, height, blocks
