import asyncio
import logging
import time
from .protocol import SERVICE, RX, TX, encode

log = logging.getLogger(__name__)


async def ble_loop(state, refresh, changed, device=None):
    from bleak import BleakClient, BleakScanner
    backoff = 1
    while True:
        try:
            target = await BleakScanner.find_device_by_filter(
                lambda dev, adv: dev.address == device if device else
                (adv.local_name == "Codex Watch" or SERVICE in [s.lower() for s in adv.service_uuids]),
                timeout=10)
            if target is None:
                log.info("waiting for Codex Watch")
                await asyncio.sleep(3)
                continue
            disconnected = asyncio.Event()
            async with BleakClient(target, disconnected_callback=lambda _: disconnected.set()) as client:
                loop = asyncio.get_running_loop()
                last_request = 0.0
                def notified(_, data):
                    nonlocal last_request
                    now = time.monotonic()
                    if b"R" in data and now - last_request >= 5:
                        last_request = now
                        loop.call_soon_threadsafe(refresh.set)
                await client.start_notify(TX, notified)
                log.info("BLE connected")
                backoff = 1
                refresh.set()
                while client.is_connected and not disconnected.is_set():
                    changed.clear()
                    data = encode(state.next_sequence(), state.windows())
                    # ATT minimum MTU=23. 20-byte acknowledged fragments work without long-write support.
                    for offset in range(0, len(data), 20):
                        await client.write_gatt_char(RX, data[offset:offset + 20], response=True)
                    waiters = [asyncio.create_task(e.wait()) for e in (changed, disconnected)]
                    try:
                        await asyncio.wait(waiters, timeout=60, return_when=asyncio.FIRST_COMPLETED)
                    finally:
                        for task in waiters:
                            task.cancel()
                        await asyncio.gather(*waiters, return_exceptions=True)
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            # CoreBluetooth exceptions may include identifiers; never log payloads or credentials.
            log.warning("BLE reconnect (%s)", type(exc).__name__)
        await asyncio.sleep(backoff)
        backoff = min(60, backoff * 2)
