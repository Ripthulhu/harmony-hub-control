"""Pair using the hub button or approval from an existing owner."""

import voluptuous as vol
from homeassistant import config_entries
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .api import HarmonyOwnerApi, PairingRequired
from .const import DEFAULT_NAME, DEFAULT_PORT, DOMAIN


class HarmonyOwnerConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    def __init__(self):
        self.api = None
        self.connection = {}
        self.existing = None

    async def async_step_reauth(self, entry_data):
        self.existing = self._get_reauth_entry()
        return await self.async_step_user()

    async def async_step_reconfigure(self, user_input=None):
        self.existing = self._get_reconfigure_entry()
        return await self.async_step_user(user_input)

    async def async_step_user(self, user_input=None):
        errors = {}
        defaults = {**self.existing.data, **self.existing.options} if self.existing else {}
        if user_input is not None:
            try:
                api = HarmonyOwnerApi(async_get_clientsession(self.hass), user_input["host"].strip(), user_input["port"])
                await api.request("session")
                if not self.existing:
                    await self.async_set_unique_id(f"{user_input['host'].strip()}:{user_input['port']}")
                    self._abort_if_unique_id_configured()
                await api.pair(user_input["pairing"] == "button")
            except (HomeAssistantError, ValueError):
                errors["base"] = "cannot_connect"
            else:
                self.api = api
                self.connection = {"host": user_input["host"].strip(), "port": user_input["port"]}
                self.pairing = user_input["pairing"]
                return await self.async_step_pair()
        return self.async_show_form(step_id="user", errors=errors, data_schema=vol.Schema({
            vol.Required("host", default=defaults.get("host", "")): str,
            vol.Required("port", default=defaults.get("port", DEFAULT_PORT)): vol.All(vol.Coerce(int), vol.Range(min=1, max=65535)),
            vol.Required("pairing", default="button"): vol.In({"button": "Hub Pair button", "approval": "Approve in the hub UI"}),
        }))

    async def async_step_pair(self, user_input=None):
        errors = {}
        if user_input is not None:
            try:
                await self.api.check_pairing()
                await self.api.snapshot()
            except PairingRequired:
                errors["base"] = "not_paired"
            except HomeAssistantError:
                errors["base"] = "cannot_connect"
            else:
                data = {**self.connection, "token": self.api.token, "csrf": self.api.csrf,
                        "hub_id": self.existing.data.get("hub_id", self.existing.data["host"]) if self.existing else self.connection["host"]}
                if self.existing:
                    # Preserve the old identity/entity IDs, not the retired password or importer options.
                    return self.async_update_reload_and_abort(self.existing, data=data, options={})
                return self.async_create_entry(title=DEFAULT_NAME, data=data)
        return self.async_show_form(step_id="pair", errors=errors, data_schema=vol.Schema({}),
            description_placeholders={"hub_url": self.api.base_url,
                "approval": "Briefly press and release Pair on the hub (within 90 seconds)." if self.pairing == "button"
                else "Open Settings > Controllers on the hub and approve Home Assistant."})
