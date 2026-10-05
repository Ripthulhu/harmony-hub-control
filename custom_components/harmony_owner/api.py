"""Paired local API client. Never retry a command submission."""

from __future__ import annotations

import asyncio
import json
import re
from ipaddress import IPv4Address

from aiohttp import ClientError, ClientSession, ClientTimeout
from homeassistant.exceptions import HomeAssistantError


class PairingRequired(HomeAssistantError):
    """The controller has not been approved or has been revoked."""


class CannotConnect(HomeAssistantError):
    """The hub did not return a usable response."""


class HarmonyOwnerApi:
    def __init__(self, session: ClientSession, host: str, port: int = 8080,
                 token: str = "", csrf: str = "") -> None:
        IPv4Address(host)
        if not 1 <= port <= 65535:
            raise ValueError("Enter the hub IPv4 address and a valid port")
        self.session = session
        self.base_url = f"http://{host}:{port}"
        self.token, self.csrf = token, csrf
        self.lock = asyncio.Lock()

    async def request(self, path: str, body: dict | None = None) -> dict:
        headers = {"Origin": self.base_url}
        if self.token:
            headers.update({"Authorization": f"Bearer {self.token}", "X-Harmony-CSRF": self.csrf})
        try:
            async with self.session.request(
                "POST" if body is not None else "GET", self.base_url + "/api/v1/" + path,
                json=body, headers=headers, timeout=ClientTimeout(total=10), allow_redirects=False,
            ) as response:
                if response.status in (401, 403):
                    raise PairingRequired("Pair Home Assistant with the hub again")
                raw = bytearray()
                async for chunk in response.content.iter_chunked(16384):
                    raw.extend(chunk)
                    if len(raw) > 262144:
                        raise CannotConnect("Hub response is too large")
                try:
                    result = json.loads(raw)
                except (ValueError, UnicodeError) as err:
                    raise CannotConnect("Hub returned invalid JSON") from err
                if not isinstance(result, dict):
                    raise CannotConnect("Hub returned an invalid response")
                if response.status >= 300 or result.get("ok") is False:
                    raise HomeAssistantError(result.get("error") or f"Hub returned HTTP {response.status}")
                if path == "controllers/request":
                    cookie = response.cookies.get("harmony")
                    if cookie is None or not result.get("csrf"):
                        raise CannotConnect("Hub did not issue pairing credentials")
                    self.token, self.csrf = cookie.value, result["csrf"]
                return result
        except (ClientError, TimeoutError) as err:
            raise CannotConnect("Cannot reach the Harmony hub") from err

    async def pair(self, button: bool) -> None:
        await self.request("controllers/request", {"name": "Home Assistant", "button": button})

    async def check_pairing(self) -> dict:
        session = await self.request("session")
        if session.get("role") not in ("owner", "control"):
            raise PairingRequired("Approve Home Assistant on the hub")
        self.csrf = session["csrf"]
        return session

    async def snapshot(self) -> dict:
        await self.check_pairing()
        return {"inventory": await self.request("devices"),
                "config": await self.request("configuration"),
                "activity": await self.request("activities/state")}

    async def wait_operation(self, operation: dict, timeout: float = 30) -> dict:
        op_id = operation.get("id", "")
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,96}", op_id):
            raise CannotConnect("Hub did not return an operation ID")
        try:
            async with asyncio.timeout(timeout):
                while operation.get("state") not in ("completed", "failed", "cancelled"):
                    await asyncio.sleep(0.15)
                    operation = await self.request(f"operations?id={op_id}")
                if operation["state"] != "completed":
                    raise HomeAssistantError(operation.get("result", {}).get("error") or operation["state"])
                return operation
        except (TimeoutError, asyncio.CancelledError):
            await self.cancel(op_id)
            raise

    async def cancel(self, op_id: str) -> None:
        try:
            await self.request("operations", {"id": op_id, "action": "cancel"})
        except HomeAssistantError:
            # If the connection is gone, the hub's hold lease expires independently.
            pass

    async def send(self, device: str, command: str, hold: float = 0) -> None:
        operation = await self.request("commands/send", {
            "deviceId": device, "command": command, "transport": "ir",
            "mode": "hold" if hold else "tap",
        })
        if not hold:
            await self.wait_operation(operation)
            return
        op_id = operation["id"]
        try:
            # Start timing only after the coordinator owns the press, not while queued.
            async with asyncio.timeout(15 + hold):
                deadline = None
                while True:
                    operation = await self.request("operations", {"id": op_id, "action": "keepalive"})
                    if operation["state"] in ("failed", "cancelled", "completed"):
                        raise HomeAssistantError("Hold ended before release")
                    now = asyncio.get_running_loop().time()
                    if operation["state"] == "running" and deadline is None:
                        deadline = now + hold
                    if deadline is not None and now >= deadline:
                        break
                    await asyncio.sleep(0.3)
        finally:
            await self.cancel(op_id)

    async def activity(self, activity_id: str) -> None:
        async with self.lock:
            operation = await self.request("activities/run", {"activityId": activity_id})
            await self.wait_operation(operation, 180)
