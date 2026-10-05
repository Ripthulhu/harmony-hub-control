from homeassistant.components.select import SelectEntity
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .remote import device_info


async def async_setup_entry(hass, entry, async_add_entities):
    async_add_entities([HarmonyActivity(hass.data[DOMAIN][entry.entry_id], entry)])


class HarmonyActivity(CoordinatorEntity, SelectEntity):
    _attr_has_entity_name = True
    _attr_name = "Activity"
    _attr_icon = "mdi:remote-tv"

    def __init__(self, coordinator, entry):
        super().__init__(coordinator)
        self._attr_unique_id = f"{entry.data.get('hub_id', entry.data['host'])}_owner_activity"
        self._attr_device_info = device_info(entry, coordinator.api)

    def activities(self):
        return {f"{a['name']} ({a['id']})": a["id"] for a in self.coordinator.data["config"].get("activities", [])}

    @property
    def options(self):
        return ["Power off", *self.activities()]

    @property
    def current_option(self):
        current = self.coordinator.data["activity"].get("activityId")
        if current in ("-1", "power-off"):
            return "Power off"
        return next((name for name, id_ in self.activities().items() if id_ == current), None)

    async def async_select_option(self, option):
        await self.coordinator.api.activity("-1" if option == "Power off" else self.activities()[option])
        await self.coordinator.async_request_refresh()
