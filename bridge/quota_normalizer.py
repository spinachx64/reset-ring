"""Recognize quota windows by duration, never by primary/secondary position."""
from dataclasses import asdict, dataclass
import math


def number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


@dataclass(frozen=True)
class Window:
    used: float | None
    reset: int | None
    source: str = "api"

    @property
    def remaining(self):
        return None if self.used is None else max(0, min(100, 100 - self.used))

    def wire(self):
        return {"u": self.used, "r": self.reset, "s": self.source}

    @classmethod
    def restore(cls, value):
        if value is None:
            return None
        if not isinstance(value, dict):
            raise ValueError("invalid cached window")
        result = cls(**value)
        if result.used is not None and (not number(result.used) or not 0 <= result.used <= 100):
            raise ValueError("invalid cached usage")
        if result.reset is not None and (type(result.reset) is not int or not 0 < result.reset < 10**11):
            raise ValueError("invalid cached reset")
        if result.source not in {"api", "estimated", "stale", "unavailable"}:
            raise ValueError("invalid cached source")
        return result


def normalize(payload, *, wham=False):
    if not isinstance(payload, dict):
        raise ValueError("invalid quota response")
    if wham:
        bucket = payload.get("rate_limit")
        keys = ("primary_window", "secondary_window")
    else:
        buckets = payload.get("rateLimitsByLimitId")
        bucket = buckets.get("codex") if isinstance(buckets, dict) and "codex" in buckets else payload.get("rateLimits")
        keys = ("primary", "secondary")
    # A successful response may legitimately have no windows.
    if bucket is None:
        return {"h": None, "w": None}
    if not isinstance(bucket, dict):
        raise ValueError("invalid quota bucket")
    result = {"h": None, "w": None}
    for key in keys:
        item = bucket.get(key)
        if not isinstance(item, dict):
            continue
        duration = item.get("limit_window_seconds" if wham else "windowDurationMins")
        if not number(duration):
            continue
        minutes = duration / 60 if wham else duration
        mode = {300: "h", 10080: "w"}.get(minutes)
        if mode is None:
            continue
        used = item.get("used_percent" if wham else "usedPercent")
        reset = item.get("reset_at" if wham else "resetsAt")
        used = round(max(0, min(100, used)), 1) if number(used) else None
        reset = int(reset) if number(reset) and reset == int(reset) and 0 < reset < 10**11 else None
        result[mode] = Window(used, reset, "api" if used is not None else "unavailable")
    return result
