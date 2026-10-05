# Home Assistant

There are two independent connections. Use Harmony Owner for `remote.send_command`
and long presses. Use MQTT for discovered command buttons and automations.
Neither connection is required for the hub's web remote.

## Harmony Owner

The integration is in `custom_components/harmony_owner`. It requires the current
hub API, including `/api/v1/activities/state`. Update the hub before the integration.

1. Back up the existing `harmony_owner` folder in Home Assistant's `custom_components`.
2. Replace that folder with the one from this repository, then restart Home Assistant.
3. Open **Settings > Devices & services > Harmony Owner**. An old installation asks
   you to authenticate again. Do not delete it: re-pairing preserves the remote entity.
4. Enter the hub address and port, normally `8080`. Choose the hub Pair button or
   approval from an existing owner. Follow the approval prompt, then submit.

The integration stores its own controller credentials. The old admin password is
no longer used. Revoke Home Assistant from **Settings > Controllers** on the hub.
Use **Reconfigure** in Home Assistant if the hub address changes.
Home Assistant must be able to reach the hub's HTTP port. If they are on separate
networks, permit that connection without opening the hub to the whole network.

Example tap:

```yaml
action: remote.send_command
target:
  entity_id: remote.harmony_hub_owner
data:
  device: "98869917"
  command: Vol_up
```

Add `hold_secs: 0.5` for a brief hold. `num_repeats` and `delay_secs` are also
supported. Command names are case-sensitive and must already exist on the hub.
The remote's `devices` attribute lists device IDs and saved commands.
Use a device name only when it identifies one saved device unambiguously.

Commands are serialized, and a failed operation raises an error rather than being
reported as sent successfully. Holds renew a short lease; cancellation sends a
release, and the hub releases independently if the connection disappears.
Limits are 10 seconds per hold, 20 repeats, and 100 commands per call.

The Activity selector contains local activities created in the hub UI. Remote
turn-on requires an `activity`; turn-off runs the current activity's exit steps.
It does not send power-off to every saved device. Power and input states remain
estimates. After a restart or failed sequence, activity state is unknown.

Device editing, learning and profile imports now belong in the hub UI. The old
`harmony_owner.create_device`, import, learning and other management services are
not registered by this rewrite. Replace old `harmony_owner.send_command` calls
with `remote.send_command`. No existing hub profiles are changed by installation.

## MQTT

Enter the broker's IPv4 address and credentials under **Settings > MQTT** on the
hub. Use the same broker as Home Assistant's MQTT integration. Saved commands do
not require Logitech or an internet connection.
The connection status appears above the broker settings; saving the form alone
does not mean the broker accepted the connection.

Discovery creates a hub with activity controls and diagnostics, plus a device
for each saved IR profile. Common command buttons are enabled; extra commands
can be enabled from the MQTT device's entity list. Buttons do not pretend to
know whether an IR-controlled device responded.

With the default topic prefix:

| Topic | Purpose |
| --- | --- |
| `harmony/hub/status` | Retained `online` or `offline` availability |
| `harmony/hub/state` | Inventory counts, hub details and estimated activity |
| `harmony/hub/command` | Non-retained JSON: `{"deviceId":"98869917","command":"Vol_up"}` |
| `harmony/hub/activity/set` | Local activity ID, displayed option, or `PowerOff` |
| `harmony/hub/result` | Queued operation ID, then its completed/failed/cancelled result |

Send commands at QoS 0 without retain. MQTT supports taps; use the paired API or
Harmony Owner for holds. Raw HBus commands are rejected. Discovery updates after
configuration changes, broker reconnects and Home Assistant restarts. Removed
commands have their own retained discovery cleared; unrelated devices are untouched.

Keep the MQTT `clientId` unchanged when upgrading to preserve existing entity IDs.
Give each hub its own client ID and base topic. Use broker accounts/ACLs: anyone
who can publish to the command topics can control that hub. HTTP pairing does
not secure MQTT. This release uses trusted-LAN HTTP and MQTT without TLS; do not
expose either service to the internet.

The owner-only `/api/v1/integrations/mqtt` response includes the latest connection
status and error when the Lua bridge has started. It never returns the broker password.

## Verification

Disposable tests cover Home Assistant 2026.9.4, a simulated HTTP hub, Lua 5.1
and Mosquitto 2. The rewrite was deployed on 2026-10-05. Existing entity identity,
MQTT discovery and all four saved profiles were preserved. The owner confirmed
a real LG C5 tap and half-second hold through Home Assistant, including release.
Physical-button pairing of this integration, other devices, full offline tests
and a 24-hour soak remain unverified.

Discovery follows Home Assistant's [MQTT button](https://www.home-assistant.io/integrations/button.mqtt/)
and [MQTT select](https://www.home-assistant.io/integrations/select.mqtt/) contracts.
