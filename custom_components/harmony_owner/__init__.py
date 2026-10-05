"""Harmony control without a Logitech account or a broker dependency."""

from datetime import timedelta
import logging

from homeassistant.const import Platform
from homeassistant.exceptions import ConfigEntryAuthFailed, HomeAssistantError
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api import HarmonyOwnerApi, PairingRequired
from .const import DOMAIN

PLATFORMS = [Platform.REMOTE, Platform.SELECT]


class HarmonyCoordinator(DataUpdateCoordinator):
    def __init__(self, hass, entry, api):
        super().__init__(hass, logging.getLogger(__name__), name=DOMAIN,
                         config_entry=entry, update_interval=timedelta(seconds=15))
        self.api = api

    async def _async_update_data(self):
        try:
            return await self.api.snapshot()
        except PairingRequired as err:
            raise ConfigEntryAuthFailed(str(err)) from err
        except HomeAssistantError as err:
            raise UpdateFailed(str(err)) from err


async def async_setup_entry(hass, entry):
    data = entry.data
    if not data.get("token"):
        raise ConfigEntryAuthFailed("The old admin login has been replaced. Pair Home Assistant with the hub.")
    api = HarmonyOwnerApi(async_get_clientsession(hass), data["host"], data["port"], data["token"], data["csrf"])
    coordinator = HarmonyCoordinator(hass, entry, api)
    await coordinator.async_config_entry_first_refresh()
    hass.data.setdefault(DOMAIN, {})[entry.entry_id] = coordinator
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass, entry):
    if await hass.config_entries.async_unload_platforms(entry, PLATFORMS):
        hass.data[DOMAIN].pop(entry.entry_id, None)
        return True
    return False
