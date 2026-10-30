"""v1 NUS wire format. q is persisted and remains exactly representable in JSON doubles."""
import json
import time

SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
LINE_MAX = 256


def encode(sequence, windows, now_ms=None):
    if type(sequence) is not int or not 0 < sequence <= 2**53 - 1:
        raise ValueError("invalid sequence")
    packet = {"v": 1, "q": sequence, "t": int(time.time() * 1000) if now_ms is None else now_ms}
    for key in ("h", "w"):
        win = windows.get(key)
        packet[key] = win.wire() if win else None
    data = (json.dumps(packet, separators=(",", ":"), allow_nan=False) + "\n").encode()
    if len(data) >= LINE_MAX:
        raise ValueError("packet exceeds firmware line limit")
    return data
