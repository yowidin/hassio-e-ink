from enum import Enum
from functools import wraps
from typing import List

import struct


class ByteOrder(Enum):
    Native = '='
    LittleEndian = '<'
    BigEndian = '>'
    Default = LittleEndian


class Format(Enum):
    i8 = 'b'
    u8 = 'B'
    i16 = 'h'
    u16 = 'H'
    i32 = 'i'
    u32 = 'I'
    i64 = 'q'
    u64 = 'Q'


def packed_int(fmt: Format):
    def decorator(cls):
        @wraps(cls)
        def wrapper(*args, **kwargs):
            is_signed = fmt.name[0] == 'i'
            num_bits = int(fmt.name[1:])
            if is_signed:
                min_value = -2 ** (num_bits - 1)
                max_value = (2 ** (num_bits - 1)) - 1
            else:
                min_value = 0
                max_value = (2 ** num_bits) - 1

            instance = cls.__new__(cls)

            def __init__(self, value):
                if not isinstance(value, int):
                    raise TypeError(f'Value must be of type {int.__name__}')

                if value < min_value:
                    raise ValueError(f'Value must be at least {min_value}')

                if value > max_value:
                    raise ValueError(f'Value must be at most {max_value}')

                self.value = value
                self.format = fmt

            instance.__init__ = __init__.__get__(instance)
            instance.__init__(*args, **kwargs)
            return instance

        return wrapper

    return decorator


@packed_int(Format.i8)
class I8:
    pass


@packed_int(Format.u8)
class U8:
    pass


@packed_int(Format.i16)
class I16:
    pass


@packed_int(Format.u16)
class U16:
    pass


@packed_int(Format.i32)
class I32:
    pass


@packed_int(Format.u32)
class U32:
    pass


@packed_int(Format.i64)
class I64:
    pass


@packed_int(Format.u64)
class U64:
    pass


def encode(ints: List[packed_int]):
    return encode_with_order(ByteOrder.Default, ints)


def encode_with_order(order: ByteOrder, ints: List[packed_int]):
    formats = []
    values = []
    for x in ints:
        formats.append(x.format.value)
        values.append(x.value)

    return struct.pack(f'{order.value}{"".join(formats)}', *values)
