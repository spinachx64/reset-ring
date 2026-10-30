"""Run with python -m bridge.main. --once reads quotas without BLE or inference."""
import argparse
import fcntl
import asyncio
import json
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
import random
import shutil
import time
from .ble_bridge import ble_loop
from .quota_provider import CodexQuotaProvider, QuotaError
from .quota_probe import probe_once, estimate
from .state import State
from .protocol import encode

log = logging.getLogger(__name__)


def arguments():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--codex", default=shutil.which("codex") or "codex")
    p.add_argument("--provider", choices=("auto", "app-server", "wham"), default="auto")
    p.add_argument("--interval", type=float, default=300)
    p.add_argument("--state", default="~/Library/Application Support/CodexStopWatch/state.json")
    p.add_argument("--device", help="optional macOS BLE device UUID")
    p.add_argument("--once", action="store_true")
    p.add_argument("--enable-probe", action="store_true", help="authorize ONE real Codex request when 5H is absent")
    p.add_argument("--log-file")
    args = p.parse_args()
    if args.interval < 5:
        p.error("--interval must be >= 5")
    return args


async def run(args):
    state_path = Path(args.state).expanduser()
    state_path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    lock_file = state_path.with_suffix(".lock").open("a")
    try:
        fcntl.flock(lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise SystemExit("Bridge already running with this state file") from None
    state = State(args.state)
    provider = CodexQuotaProvider(args.codex, args.provider)
    if args.once:
        windows = await provider.fetch()
        print(encode(state.next_sequence(), windows).decode(), end="")
        return
    refresh, changed = asyncio.Event(), asyncio.Event()
    async def poll():
        failures = 0
        last_fetch = 0.0
        retry_at = 0.0
        while True:
            refresh.clear()
            await asyncio.sleep(max(0, 5 - (time.monotonic() - last_fetch), retry_at - time.monotonic()))
            last_fetch = time.monotonic()
            try:
                windows = await provider.fetch()
                if (windows["h"] is None or windows["h"].reset is None) and args.enable_probe:
                    try:
                        await probe_once(args.codex, state)
                    except (OSError, TimeoutError):
                        log.warning("opt-in probe failed; no implicit retry")
                    if state.data.get("estimate_reset") and not state.data.get("probe_requeried"):
                        state.data["probe_requeried"] = True
                        state.save()
                        windows = await provider.fetch()
                    if windows["h"] is None or windows["h"].reset is None:
                        windows["h"] = estimate(state) or windows["h"]
                state.update(windows)
                failures = 0
                delay = args.interval
                log.info("quota updated via %s; 5H=%s weekly=%s", provider.last_source,
                         windows["h"].source if windows["h"] else "missing_window",
                         windows["w"].source if windows["w"] else "missing_window")
            except (QuotaError, OSError, ValueError) as exc:
                failures += 1
                log.warning("quota read: %s", exc.status if isinstance(exc, QuotaError) else "network_error")
                # Mark cached data as stale immediately, retain original successful timestamp.
                cached = state.windows(failed=True)
                state.data["windows"] = {key: {"used": win.used, "reset": win.reset, "source": win.source} if win else None
                                         for key, win in cached.items()}
                state.save()
                delay = min(900, 5 * 2**min(failures, 8)) * random.uniform(0.9, 1.1)
            retry_at = time.monotonic() + delay if failures else 0.0
            changed.set()
            try:
                await asyncio.wait_for(refresh.wait(), timeout=delay)
            except TimeoutError:
                pass
    async with asyncio.TaskGroup() as group:
        group.create_task(poll())
        group.create_task(ble_loop(state, refresh, changed, args.device))


def main():
    args = arguments()
    handlers = [logging.StreamHandler()]
    if args.log_file:
        path = Path(args.log_file).expanduser()
        path.parent.mkdir(parents=True, exist_ok=True)
        handlers.append(RotatingFileHandler(path, maxBytes=2_000_000, backupCount=3))
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s", handlers=handlers)
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass
    except QuotaError as exc:
        log.error("quota read: %s", exc.status)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
