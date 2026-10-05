"""The existing remote entity, now backed by operation IDs and hold leases."""

import asyncio
import math
from homeassistant.components.remote import RemoteEntity, RemoteEntityFeature
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN, DEFAULT_NAME


def device_info(entry, api):
    return {"identifiers": {(DOMAIN, entry.data.get("hub_id", entry.data["host"]))},
            "name": DEFAULT_NAME, "manufacturer": "Logitech", "model": "Harmony Hub",
            "configuration_url": api.base_url}


async def async_setup_entry(hass, entry, async_add_entities):
    async_add_entities([HarmonyOwnerRemote(hass.data[DOMAIN][entry.entry_id], entry)])


class HarmonyOwnerRemote(CoordinatorEntity, RemoteEntity):
    _attr_has_entity_name = True
    _attr_name = None
    _attr_supported_features = RemoteEntityFeature.ACTIVITY

    def __init__(self, coordinator, entry):
        super().__init__(coordinator)
        self._attr_unique_id = f"{entry.data.get('hub_id', entry.data['host'])}_owner_remote"
        self._attr_device_info = device_info(entry, coordinator.api)

    @property
    def activity_list(self):
        return [a["name"] for a in self.coordinator.data["config"].get("activities", [])]

    @property
    def current_activity(self):
        current = self.coordinator.data["activity"].get("activityId")
        return next((a["name"] for a in self.coordinator.data["config"].get("activities", []) if a["id"] == current), None)

    @property
    def is_on(self):
        current = self.coordinator.data["activity"].get("activityId")
        return None if not current else current not in ("-1", "power-off")

    @property
    def extra_state_attributes(self):
        inventory = self.coordinator.data["inventory"]
        return {"state_estimated": True, "device_count": inventory.get("deviceCount", 0),
                "devices": [{**{k: d.get(k) for k in ("id", "name", "manufacturer", "model")},
                             "commands": [c["name"] for c in d.get("commands", [])]}
                            for d in inventory.get("devices", [])]}

    def resolve(self, command, device):
        if not device and ":" in command:
            device, command = command.split(":", 1)
        matches = [(d["id"], command) for d in self.coordinator.data["inventory"].get("devices", [])
                   if (not device or str(d["id"]) == str(device) or d["name"] == device)
                   and any(c["name"] == command for c in d.get("commands", []))]
        if len(matches) != 1:
            raise HomeAssistantError("Choose a saved device and an unambiguous command from the hub")
        return matches[0]

    async def async_send_command(self, command, **kwargs):
        commands = [command] if isinstance(command, str) else list(command)
        repeats = kwargs.get("num_repeats", 1)
        delay, hold = float(kwargs.get("delay_secs", 0.4)), float(kwargs.get("hold_secs", 0))
        if not isinstance(repeats, int) or not 1 <= repeats <= 20 or not 1 <= len(commands) * repeats <= 100:
            raise HomeAssistantError("Send between 1 and 100 commands, with at most 20 repeats")
        if not math.isfinite(delay) or not math.isfinite(hold) or not 0 <= delay <= 10 or not 0 <= hold <= 10:
            raise HomeAssistantError("Hold and delay must be between 0 and 10 seconds")
        resolved = [self.resolve(str(c), kwargs.get("device") or kwargs.get("device_id")) for c in commands]
        async with self.coordinator.api.lock:
            first = True
            for _ in range(repeats):
                for device, name in resolved:
                    if not first:
                        await asyncio.sleep(delay)
                    await self.coordinator.api.send(str(device), name, hold)
                    first = False

    async def async_turn_on(self, **kwargs):
        name = kwargs.get("activity")
        matches = [a for a in self.coordinator.data["config"].get("activities", []) if name in (a["id"], a["name"])]
        if len(matches) != 1:
            raise HomeAssistantError("Choose a local activity; individual device power is not known")
        await self.coordinator.api.activity(matches[0]["id"])
        await self.coordinator.async_request_refresh()

    async def async_turn_off(self, **kwargs):
        await self.coordinator.api.activity("-1")
        await self.coordinator.async_request_refresh()
