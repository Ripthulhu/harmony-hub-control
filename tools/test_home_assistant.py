"""Run with the Home Assistant image/version being deployed; no physical hub."""
import asyncio
import importlib
import json
import os
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

from aiohttp import ClientSession, web
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError

from custom_components.harmony_owner.api import HarmonyOwnerApi, PairingRequired
from custom_components.harmony_owner import HarmonyCoordinator
from custom_components.harmony_owner.remote import HarmonyOwnerRemote
from custom_components.harmony_owner.config_flow import HarmonyOwnerConfigFlow


class ApiTests(unittest.IsolatedAsyncioTestCase):
    async def test_mqtt_discovery_schemas(self):
        path = os.environ.get("MQTT_DISCOVERY_JSON")
        if not path:
            self.skipTest("Set MQTT_DISCOVERY_JSON to the output of test_mqtt_protocol.lua")
        count = 0
        with tempfile.TemporaryDirectory() as directory:
            hass = HomeAssistant(directory)
            with patch("homeassistant.helpers.config_validation._async_get_hass_or_none", return_value=hass):
                for topic, raw in json.loads(Path(path).read_text()).items():
                    component = topic.split("/")[1]
                    schema = importlib.import_module("homeassistant.components.mqtt." + component).DISCOVERY_SCHEMA
                    schema(json.loads(raw))
                    count += 1
            await hass.async_stop()
        self.assertEqual(count, 10)

    async def asyncSetUp(self):
        self.calls, self.sent, self.heartbeats, self.cancels = [], 0, 0, 0
        self.role, self.op_state = "pending", "completed"
        self.delay = 0
        app = web.Application()
        app.router.add_route("*", "/api/v1/{tail:.*}", self.handle)
        self.runner = web.AppRunner(app)
        await self.runner.setup()
        site = web.TCPSite(self.runner, "127.0.0.1", 0)
        await site.start()
        self.port = site._server.sockets[0].getsockname()[1]
        self.session = ClientSession()
        self.api = HarmonyOwnerApi(self.session, "127.0.0.1", self.port)

    async def asyncTearDown(self):
        await self.session.close()
        await self.runner.cleanup()

    async def handle(self, request):
        path = request.match_info["tail"]
        body = await request.json() if request.method == "POST" else None
        self.calls.append((path, body))
        assert request.headers["Origin"] == f"http://127.0.0.1:{self.port}"
        if path == "controllers/request":
            response = web.json_response({"id": "test", "csrf": "csrf", "role": "pending"})
            response.set_cookie("harmony", "token", httponly=True)
            return response
        if path == "oversized":
            return web.Response(body=b"x" * 262145)
        if path == "redirect":
            return web.Response(status=302, headers={"Location": "/api/v1/commands/send"})
        if request.headers.get("Authorization") != "Bearer token":
            return web.json_response({"error": "Unauthorized"}, status=401)
        if request.method == "POST":
            assert request.headers["X-Harmony-CSRF"] == "csrf"
        if path == "session":
            return web.json_response({"role": self.role, "csrf": "csrf"})
        if path == "devices":
            return web.json_response({"deviceCount": 1, "devices": [{"id": "12", "name": "TV", "commands": [{"name": "VolumeUp"}]}]})
        if path == "configuration":
            return web.json_response({"activities": [{"id": "watch", "name": "Watch TV"}]})
        if path == "activities/state":
            return web.json_response({"activityId": "", "estimated": True})
        if path in ("commands/send", "activities/run"):
            self.sent += 1
            return web.json_response({"id": "operation-1", "state": "queued"}, status=202)
        if path == "operations":
            if body and body.get("action") == "cancel":
                self.cancels += 1
                return web.json_response({"id": "operation-1", "state": "cancelled"})
            if body:
                self.heartbeats += 1
            await asyncio.sleep(self.delay)
            return web.json_response({"id": "operation-1", "state": self.op_state,
                                      "result": {"error": "Simulated driver failure"}})
        raise AssertionError(path)

    async def pair(self):
        await self.api.pair(True)
        self.role = "owner"
        await self.api.check_pairing()

    async def test_pair_and_snapshot(self):
        await self.api.pair(True)
        self.assertEqual(self.api.token, "token")
        with self.assertRaises(PairingRequired):
            await self.api.check_pairing()
        self.role = "control"
        data = await self.api.snapshot()
        self.assertEqual(data["inventory"]["deviceCount"], 1)
        self.role = "unpaired"
        with self.assertRaises(PairingRequired):
            await self.api.snapshot()

    async def test_taps_repeats_and_failure(self):
        await self.pair()
        await self.api.send("12", "VolumeUp")
        await self.api.send("12", "VolumeUp")
        self.assertEqual(self.sent, 2)
        self.op_state = "failed"
        with self.assertRaisesRegex(HomeAssistantError, "Simulated driver failure"):
            await self.api.send("12", "VolumeUp")
        self.assertEqual(self.sent, 3)  # No retry after a failed operation.

    async def test_hold_release_and_cancellation(self):
        await self.pair()
        self.op_state = "running"
        await self.api.send("12", "VolumeUp", 0.35)
        self.assertGreaterEqual(self.heartbeats, 2)
        self.assertEqual(self.cancels, 1)
        task = asyncio.create_task(self.api.send("12", "VolumeUp", 5))
        await asyncio.sleep(0.1)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertEqual(self.cancels, 2)

    async def test_wait_timeout_and_bounds(self):
        await self.pair()
        self.op_state = "running"
        with self.assertRaises(TimeoutError):
            await self.api.wait_operation({"id": "operation-1"}, 0.05)
        self.assertEqual(self.cancels, 1)
        for path in ("oversized", "redirect"):
            with self.assertRaises(HomeAssistantError):
                await self.api.request(path)
        self.assertEqual(self.sent, 0)

    async def test_real_ha_entities(self):
        await self.pair()
        with tempfile.TemporaryDirectory() as directory:
            hass = HomeAssistant(directory)
            entry = SimpleNamespace(entry_id="test", data={"host": "127.0.0.1", "hub_id": "192.168.50.139"})
            coordinator = HarmonyCoordinator(hass, None, self.api)
            coordinator.async_set_updated_data(await self.api.snapshot())
            remote = HarmonyOwnerRemote(coordinator, entry)
            self.assertEqual(remote.unique_id, "192.168.50.139_owner_remote")
            self.assertIsNone(remote.is_on)  # Unknown must not be shown as powered off/on.
            await remote.async_send_command(["VolumeUp"], device="TV", num_repeats=2, delay_secs=0)
            self.assertEqual(self.sent, 2)
            for kwargs in ({"hold_secs": float("nan")}, {"num_repeats": 30}, {"device": "missing"}):
                with self.assertRaises(HomeAssistantError):
                    await remote.async_send_command(["VolumeUp"], **kwargs)
            with self.assertRaises(HomeAssistantError):
                await remote.async_turn_on()
            await coordinator.async_shutdown()
            await hass.async_stop()

    async def test_flow_preserves_identity_and_removes_password(self):
        await self.pair()
        flow = HarmonyOwnerConfigFlow()
        flow.api = self.api
        flow.pairing = "button"
        flow.connection = {"host": "127.0.0.1", "port": self.port}
        flow.existing = SimpleNamespace(data={"host": "192.168.50.139", "password": "retired"})
        with patch.object(flow, "async_update_reload_and_abort", return_value={"type": "abort"}) as update:
            await flow.async_step_pair({})
        data = update.call_args.kwargs["data"]
        self.assertEqual(data["hub_id"], "192.168.50.139")
        self.assertEqual(data["token"], "token")
        self.assertNotIn("password", data)
        self.assertEqual(update.call_args.kwargs["options"], {})

    async def test_config_flow_forms(self):
        from probatio import to_field_list
        from homeassistant.helpers.config_validation import custom_serializer
        with tempfile.TemporaryDirectory() as directory:
            hass = HomeAssistant(directory)
            flow = HarmonyOwnerConfigFlow()
            flow.hass, flow.context = hass, {"source": "user"}
            form = await flow.async_step_user()
            self.assertEqual(form["step_id"], "user")
            serialized = to_field_list(form["data_schema"], custom_serializer=custom_serializer)
            self.assertEqual({field["name"] for field in serialized}, {"host", "port", "pairing"})
            flow.api, flow.pairing = self.api, "button"
            form = await flow.async_step_pair()
            self.assertIn("90 seconds", form["description_placeholders"]["approval"])
            await hass.async_stop()


if __name__ == "__main__":
    unittest.main()
