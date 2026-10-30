import asyncio
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import AsyncMock, patch
from bridge.quota_normalizer import Window, normalize
from bridge.protocol import encode
from bridge.state import State
from bridge.quota_probe import probe_once
from bridge.quota_provider import CodexQuotaProvider, QuotaError


def window(duration=300, used=25, reset=1800000000):
    return {"windowDurationMins": duration, "usedPercent": used, "resetsAt": reset}


class NormalizeTests(unittest.TestCase):
    def test_zero_is_known_missing_is_unknown(self):
        result = normalize({"rateLimits": {"primary": window(used=0)}})
        self.assertEqual(result["h"].remaining, 100)
        self.assertIsNone(result["w"])
        result = normalize({"rateLimits": {"primary": {"windowDurationMins": 300}}})
        self.assertIsNone(result["h"].remaining)
        self.assertEqual(result["h"].source, "unavailable")

    def test_duration_and_codex_bucket_take_priority(self):
        result = normalize({"rateLimits": {"primary": window(used=99)},
                            "rateLimitsByLimitId": {"codex": {"primary": window(10080, 65), "secondary": window(300,25)}}})
        self.assertEqual(result["h"].remaining, 75)
        self.assertEqual(result["w"].remaining, 35)
        self.assertEqual(normalize({"rateLimits": {"primary": window(15)}}), {"h": None,"w": None})
        self.assertEqual(normalize({"rateLimitsByLimitId": {"codex": None}, "rateLimits": {"primary": window()}})["h"], None)

    def test_bad_fields_never_imply_full(self):
        for bad in (None, True, "25", float("nan"), float("inf")):
            result = normalize({"rateLimits": {"primary": window(used=bad)}})
            self.assertIsNone(result["h"].remaining)
        result = normalize({"rateLimits": {"primary": window(reset=None)}})
        self.assertEqual(result["h"].remaining,75)
        self.assertIsNone(result["h"].reset)

    def test_wham_uses_seconds_and_preserves_null(self):
        result=normalize({"rate_limit": {"secondary_window": {"limit_window_seconds": 18000, "used_percent": 25, "reset_at": 1800000000}}}, wham=True)
        self.assertEqual(result["h"].remaining,75)
        self.assertIsNone(result["w"])

    def test_wire_boundary_and_no_credentials(self):
        values={"h": Window(25.5,1800000000),"w": Window(65,1800900000)}
        packet=encode(1793327720130001, values, 1793327720130)
        self.assertLess(len(packet),256)
        self.assertTrue(packet.endswith(b"\n"))
        self.assertEqual(json.loads(packet)["h"]["u"],25.5)
        for seq in (True,0,2**53,1.5):
            with self.assertRaises(ValueError): encode(seq,values)
        with self.assertRaises(ValueError): encode(1,{"h": Window(float("nan"),1)})

    def test_cache_sequence_and_staleness(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/"cache.json"
            state=State(path)
            state.update({"h":Window(25,1800000000),"w":None})
            sequence=state.next_sequence()
            other=State(path)
            self.assertGreater(other.next_sequence(),sequence)
            self.assertEqual(other.windows(failed=True)["h"].source,"stale")
            other.data["updated_at"]=0
            self.assertEqual(other.windows()["h"].remaining,75)
            self.assertEqual(other.windows()["h"].source,"stale")
            self.assertEqual(path.stat().st_mode & 0o777,0o600)


class AsyncTests(unittest.IsolatedAsyncioTestCase):
    async def test_probe_persists_before_request_and_only_once(self):
        with tempfile.TemporaryDirectory() as tmp:
            state=State(Path(tmp)/"state.json")
            process=AsyncMock()
            process.returncode=0
            with patch("bridge.quota_probe.asyncio.create_subprocess_exec", AsyncMock(return_value=process)) as spawn:
                await probe_once("codex",state)
                await probe_once("codex",State(state.path))
                self.assertEqual(spawn.await_count,1)
                self.assertIsNotNone(state.data.get("estimate_reset"))

    async def test_no_probe_retry_after_failed_spawn(self):
        with tempfile.TemporaryDirectory() as tmp:
            state=State(Path(tmp)/"state.json")
            with patch("bridge.quota_probe.asyncio.create_subprocess_exec", AsyncMock(side_effect=OSError)) as spawn:
                with self.assertRaises(OSError): await probe_once("codex",state)
                await probe_once("codex",State(state.path))
                self.assertEqual(spawn.await_count,1)

    async def test_fallback_on_error_only(self):
        provider=CodexQuotaProvider()
        with patch.object(provider,"app_server",AsyncMock(return_value={"h":None,"w":None})), patch.object(provider,"wham") as fallback:
            await provider.fetch()
            fallback.assert_not_called()
        with patch.object(provider,"app_server",AsyncMock(side_effect=QuotaError("network_error"))), patch.object(provider,"wham",return_value={"h":None,"w":None}) as fallback:
            await provider.fetch()
            fallback.assert_called_once()
