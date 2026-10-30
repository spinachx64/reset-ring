import json
import os
from pathlib import Path
import time
from dataclasses import asdict, replace
from .quota_normalizer import Window, number


class State:
    def __init__(self, path):
        self.path = Path(path).expanduser()
        self.path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        try:
            self.data = json.loads(self.path.read_text())
            if not isinstance(self.data, dict):
                raise ValueError("bad cache")
            if not isinstance(self.data.get("windows", {}), dict):
                raise ValueError("bad cached windows")
            if type(self.data.get("sequence", 0)) is not int or not 0 <= self.data.get("sequence", 0) < 2**53-1:
                raise ValueError("bad cached sequence")
            if not number(self.data.get("updated_at", 0)):
                raise ValueError("bad cached timestamp")
            for value in self.data.get("windows", {}).values():
                Window.restore(value)
        except (OSError, ValueError, TypeError):
            self.data = {}
        self.data["sequence"] = max(int(self.data.get("sequence", 0)), int(time.time() * 1000) * 1000)

    def save(self):
        tmp = self.path.with_suffix(".tmp")
        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as output:
            json.dump(self.data, output, allow_nan=False)
        os.replace(tmp, self.path)

    def next_sequence(self):
        self.data["sequence"] += 1
        self.save()
        return self.data["sequence"]

    def update(self, windows):
        self.data["windows"] = {key: asdict(win) if win else None for key, win in windows.items()}
        self.data["updated_at"] = time.time()
        self.save()

    def windows(self, failed=False):
        stale = failed or time.time() - self.data.get("updated_at", 0) > 900
        return {key: replace(win, source="stale") if stale and win and win.source == "api" else win
                for key, value in self.data.get("windows", {}).items() for win in [Window.restore(value)]}
