import argparse
import asyncio
import struct

from dataclasses import dataclass
from enum import Enum

from typing import Callable, Awaitable, Dict

from heihost.log import Log
from heihost.image_capture import CaptureConfig, ImageCapture
from heihost.hosted_image import HostedImage
from heihost.encoding import encode, decode, U8, U16, U32


class Message:
    class Type(Enum):
        # fg_valid: u8, runtime_to_empty: u32, runtime_to_full: u32, charge_percentage: u8, voltage: u32
        GetImageRequest = 0x10

        # width: u16, height: u16, num_blocks: u16
        ImageHeaderResponse = 0x11

        # original_size: u16, compressed_size: u16, compressed_data: u8 * compressed_size
        ImageBlockResponse = 0x12

        # No payload
        ServerError = 0x50

        @staticmethod
        def from_int(value):
            for e in Message.Type:
                if e.value == value:
                    return e
            raise RuntimeError(f'Invalid message type: {value}')

    def __init__(self, message_type: 'Message.Type', *values):
        self.message_type = message_type
        self.values = list(values)

    async def write(self, writer: asyncio.StreamWriter):
        writer.write(encode([U8(self.message_type.value)] + self.values))


@dataclass
class GetImageRequest(Message):
    fuel_gauge_valid: bool  # u8
    runtime_to_empty: int  # u32
    runtime_to_full: int  # u32
    charge_percentage: int  # u8
    voltage: int  # u32

    @staticmethod
    async def read(reader: asyncio.StreamReader, timeout) -> 'GetImageRequest':
        # The rest of the fields we still have to read
        remaining_bytes = 1 + 4 + 4 + 1 + 4
        payload_bytes = await asyncio.wait_for(reader.readexactly(remaining_bytes), timeout)
        payload = decode(payload_bytes, [U8, U32, U32, U8, U32])
        return GetImageRequest(payload[0] != 0, payload[1], payload[2], payload[3], payload[4])


class ImageHeaderMessage(Message):
    """ | Message Type | Width | Height | Num image blocks | """

    def __init__(self, image: HostedImage):
        super().__init__(Message.Type.ImageHeaderResponse, U16(image.width), U16(image.height), U16(len(image.blocks)))


class ImageBlockMessage(Message):
    """ | Message Type | Uncompressed Size | Compressed Size | Data | """

    def __init__(self, block: HostedImage.Block):
        super().__init__(Message.Type.ImageBlockResponse, U16(block.uncompressed_size), U16(block.size))
        self.data = block.data

    async def write(self, writer: asyncio.StreamWriter):
        await super().write(writer)
        writer.write(self.data)


class ServerErrorMessage(Message):
    """ | Message Type | """

    def __init__(self):
        super().__init__(Message.Type.ServerError)


class Server:
    @dataclass
    class Config:
        port: int
        client_timeout: int

        @staticmethod
        def add_arguments(parser: argparse.ArgumentParser):
            parser.add_argument('--port', '-p', type=int, default=8765, help='Server listen port')
            parser.add_argument('--client-timeout', '-t', type=int, default=15, help='Client timeout in seconds')

        @staticmethod
        def from_args(args) -> 'Server.Config':
            return Server.Config(args.port, args.client_timeout)

    def __init__(self, server_config: 'Server.Config', capture_config: CaptureConfig):
        self.image_capture = ImageCapture(capture_config)
        self.server_config = server_config
        self.server = None

    @property
    def timeout(self):
        return self.server_config.client_timeout

    @staticmethod
    def make():
        parser = argparse.ArgumentParser('Home Assistant Image Server')

        Log.add_args(parser)
        CaptureConfig.add_arguments(parser)
        Server.Config.add_arguments(parser)

        args = parser.parse_args()
        Log.setup(args)

        capture_config = CaptureConfig.from_args(args)
        server_config = Server.Config.from_args(args)

        return Server(server_config, capture_config)

    async def run(self):
        asyncio.create_task(self.image_capture.run())

        self.server = await asyncio.start_server(self._handle_client, None, self.server_config.port)
        for socket in self.server.sockets:
            Log.info(f'Serving on {socket.getsockname()}')

        async with self.server:
            await self.server.serve_forever()

    async def _handle_client(self, reader, writer):
        addr = writer.get_extra_info('peername')
        Log.info(f"New connection from {addr}")

        # NOTE: Multi-byte values are encoded as little endian
        try:
            while True:
                try:
                    # Read message type
                    message_type_bytes = await asyncio.wait_for(reader.readexactly(1), timeout=self.timeout)
                    raw_type = struct.unpack('<B', message_type_bytes)[0]
                    message_type = Message.Type.from_int(raw_type)

                    handlers: Dict[
                        Message.Type, Callable[[asyncio.StreamReader, asyncio.StreamWriter], Awaitable[None]]] = \
                        {
                            Message.Type.GetImageRequest: self._handle_get_image,
                        }

                    if message_type not in handlers:
                        Log.warning(f"Unknown message type: {message_type}")
                        break

                    await handlers[message_type](reader, writer)
                except asyncio.TimeoutError:
                    Log.warning(f"Timeout while reading from {addr}")
                    break
                except struct.error as e:
                    Log.error(f"Struct unpacking error: {e}")
                    break
        except asyncio.IncompleteReadError:
            Log.info(f"Client {addr} disconnected")
        except ConnectionResetError:
            Log.warning(f"Connection reset by {addr}")
        except Exception as e:
            Log.error(f"Unexpected error handling client {addr}: {e}")
        finally:
            try:
                writer.close()
                await writer.wait_closed()
            except Exception as e:
                Log.error(f"Error closing connection to {addr}: {e}")
            Log.info(f"Connection from {addr} closed")

    async def _send_server_error(self, writer):
        await ServerErrorMessage().write(writer)
        await asyncio.wait_for(writer.drain(), timeout=self.server_config.client_timeout)

    async def _handle_get_image(self, _, writer):
        Log.debug("Get image request")
    async def _handle_get_image(self, reader, writer):
        request = await GetImageRequest.read(reader, self.timeout)

        Log.debug(f"Get image request: {request}")

        # TODO: Post fuel gauge values into MQTT

        # TODO: Decide on refresh type and post it as a parameter

        screenshot = self.image_capture.latest_screenshot
        if screenshot is None:
            Log.error('No screenshot available')
            return await self._send_server_error(writer)

        image = await HostedImage.from_image(self.image_capture.latest_screenshot)
        await ImageHeaderMessage(image).write(writer)

        for block in image.blocks:
            await ImageBlockMessage(block).write(writer)

        await asyncio.wait_for(writer.drain(), timeout=self.server_config.client_timeout)


async def main():
    try:
        server = Server.make()
        await server.run()
    except Exception as e:
        Log.error(f'Server error: {e}')


def sync_main():
    asyncio.run(main())


if __name__ == '__main__':
    sync_main()
