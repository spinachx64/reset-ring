"""Explicit opt-in, persisted once-only inference request. Never retried implicitly."""
import asyncio
import time
from .quota_normalizer import Window


async def probe_once(codex, state):
    if state.data.get("probe_attempted"):
        return
    state.data["probe_attempted"] = int(time.time())
    state.save()  # Persist before spawning, including crash/failure cases.
    proc = await asyncio.create_subprocess_exec(
        codex, "exec", "--skip-git-repo-check", "只回复 OK",
        stdin=asyncio.subprocess.DEVNULL, stdout=asyncio.subprocess.DEVNULL,
        stderr=asyncio.subprocess.DEVNULL)
    try:
        await asyncio.wait_for(proc.wait(), 120)
        if proc.returncode == 0:
            state.data["estimate_reset"] = int(time.time()) + 5 * 3600
            state.save()
    except BaseException:
        if proc.returncode is None:
            proc.kill()
            await proc.wait()
        raise


def estimate(state):
    reset = state.data.get("estimate_reset")
    return Window(None, reset, "estimated") if type(reset) is int else None
