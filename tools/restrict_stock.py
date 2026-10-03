"""Restrict the stock Lua 5.1 listeners without changing their engine logic."""
import struct


def loopback_transport(data: bytes, name: str) -> bytes:
    targets = {"hbushttpserverconnector.lua": b"0.0.0.0", "xmppserverconnector.lua": b"*"}
    if name not in targets or data[:12] != bytes.fromhex("1b4c75615100010404040804"):
        raise ValueError("Unsupported transport or Lua bytecode; no changes made")
    pos, strings = 12, []

    def count():
        nonlocal pos
        value = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        if value > 1_000_000:
            raise ValueError("Invalid bytecode count")
        return value

    def skip(size):
        nonlocal pos
        pos += size
        if pos > len(data):
            raise ValueError("Truncated bytecode")

    def string(constant=False):
        start = pos
        size = count()
        value = data[pos:pos + size]
        if size and (len(value) != size or value[-1] != 0):
            raise ValueError("Malformed Lua string")
        skip(size)
        if constant:
            strings.append((start, pos, value[:-1]))

    def function(depth=0):
        if depth > 32:
            raise ValueError("Bytecode nesting limit")
        string()
        skip(12)  # lines, upvalues, parameters, vararg, stack size
        skip(count() * 4)
        for _ in range(count()):
            kind = data[pos]
            skip(1)
            if kind == 1:
                skip(1)
            elif kind == 3:
                skip(8)
            elif kind == 4:
                string(True)
            elif kind != 0:
                raise ValueError("Unsupported constant type")
        for _ in range(count()):
            function(depth + 1)
        skip(count() * 4)
        for _ in range(count()):
            string()
            skip(8)
        for _ in range(count()):
            string()

    function()
    if pos != len(data):
        raise ValueError("Unexpected trailing bytecode")
    matches = [(start, end) for start, end, value in strings if value == targets[name]]
    if not matches and any(value == b"127.0.0.1" for _, _, value in strings):
        return data
    if len(matches) != 1:
        raise ValueError("Expected exactly one listener address; no changes made")
    start, end = matches[0]
    value = b"127.0.0.1\0"
    return data[:start] + struct.pack("<I", len(value)) + value + data[end:]
