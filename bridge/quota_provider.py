"""Read-only app-server client; internal WHAM endpoint is a compatibility fallback."""
import asyncio
import json
import os
from pathlib import Path
import urllib.error
import urllib.request
from .quota_normalizer import normalize


class QuotaError(Exception):
    def __init__(self, status):
        self.status = status
        super().__init__(status)  # Never expose subprocess output or server bodies.


def classify(message):
    text = str(message).lower()
    if any(s in text for s in ("401", "403", "auth", "login", "sign in")):
        return "no_auth"
    return "rate_limited" if any(s in text for s in ("429", "rate limit")) else "network_error"


class CodexQuotaProvider:
    def __init__(self, codex="codex", provider="auto", timeout=25):
        self.codex, self.provider, self.timeout = codex, provider, timeout
        self.last_source = None

    async def app_server(self):
        proc = await asyncio.create_subprocess_exec(
            self.codex, "app-server", stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL,
            limit=2**20)
        async def request(ident, method, params):
            proc.stdin.write((json.dumps({"id": ident, "method": method, "params": params}) + "\n").encode())
            await proc.stdin.drain()
            while line := await proc.stdout.readline():
                msg = json.loads(line)
                if msg.get("id") == ident:
                    if "error" in msg:
                        raise QuotaError(classify(json.dumps(msg["error"])))
                    return msg["result"]
            raise QuotaError("network_error")
        try:
            async with asyncio.timeout(self.timeout):
                await request(1, "initialize", {"clientInfo": {"name": "codex_stopwatch", "version": "1.0.0"}})
                proc.stdin.write(b'{"method":"initialized"}\n')
                await proc.stdin.drain()
                return normalize(await request(2, "account/rateLimits/read", {}))
        finally:
            if proc.returncode is None:
                try:
                    proc.terminate()
                except ProcessLookupError:
                    pass
            try:
                await asyncio.wait_for(proc.wait(), 3)
            except TimeoutError:
                proc.kill()
                await proc.wait()

    def wham(self):
        home = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex"))
        try:
            auth = json.loads((home / "auth.json").read_text())
            tokens = auth.get("tokens") or {}
            token = tokens.get("access_token")
            if not token:
                raise QuotaError("no_auth")
            headers = {"Authorization": f"Bearer {token}", "User-Agent": "CodexStopWatch/1.0"}
            if tokens.get("account_id"):
                headers["ChatGPT-Account-Id"] = tokens["account_id"]
            req = urllib.request.Request("https://chatgpt.com/backend-api/wham/usage", headers=headers)
            with urllib.request.urlopen(req, timeout=self.timeout) as response:
                return normalize(json.load(response), wham=True)
        except (OSError, ValueError) as exc:
            if isinstance(exc, urllib.error.HTTPError):
                raise QuotaError(classify(exc.code)) from None
            raise QuotaError("no_auth" if isinstance(exc, FileNotFoundError) else "network_error") from None

    async def fetch(self):
        if self.provider != "wham":
            try:
                result = await self.app_server()
                self.last_source = "app-server"
                return result
            except (OSError, ValueError, KeyError, TimeoutError, QuotaError) as exc:
                if self.provider == "app-server":
                    raise QuotaError(exc.status if isinstance(exc, QuotaError) else "network_error") from None
        result = await asyncio.to_thread(self.wham)
        self.last_source = "wham"
        return result
