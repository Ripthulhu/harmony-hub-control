# Local API

Use `/api/v1`. JSON bodies are bounded to 512 KiB; most command bodies are
limited further. Responses describe hub acceptance, not IR reception.

## Pairing

`GET /api/v1/session` reports pairing state, `buttonAvailable` and the current
controller's `buttonPending` state. `POST /api/v1/controllers/request` with
`{"name":"Phone","button":true}` starts a 90-second physical pairing request.
A fresh short press/release of the hub's Pair button approves that one request.
Every physically approved browser becomes an owner with full access.
Only one physical request can wait at once; competing requests return 409.
Restarting the service clears the physical approval window.

As a fallback on an unclaimed hub,
`POST /api/v1/controllers/claim` accepts `{"name":"Phone","code":"<installer-code>"}`.
Requests and claims set an HttpOnly, SameSite=Strict controller cookie and return
a CSRF token. Without `button:true`, `controllers/request` needs an existing owner
and waits for approval in Settings. A pending browser can cancel its own request
through `controllers/cancel` using its cookie and CSRF token.
An existing control-only browser can POST `controllers/upgrade` with its cookie
and CSRF token, then press Pair to become an owner without changing its identity.
Cancelling or expiring this request leaves its existing control access unchanged.

Authenticated requests send the cookie or `Authorization: Bearer <cookie-value>`.
Mutations also send
`X-Harmony-CSRF: <token>` and a matching Origin. Host must be the numeric hub
address and port; DNS aliases and cross-origin requests are rejected.

## Endpoints

| Path | Method | Purpose |
| --- | --- | --- |
| session | GET | Controller role and pairing state |
| controllers | GET/POST | Owner list, approval and revocation |
| devices | GET/POST | Inventory; owner create/update/delete |
| bluetooth/devices | GET | Saved Bluetooth profiles |
| bluetooth/pair | POST | Owner pairing/status actions |
| commands/send | POST | Enqueue a saved IR tap/hold or Bluetooth key |
| commands/save, commands/import | POST | Owner command editing/import |
| commands/learn | POST | Owner IR capture |
| activities/run | POST | Enqueue local activity or estimated-state repair |
| activities/state | GET | Estimated local activity; empty ID means unknown |
| activities/native | GET | Preserved original native activities |
| operations?id=<id> | GET | Operation state/result |
| operations | POST | Cancel or renew an owned hold |
| configuration | GET/POST | Versioned local layouts and activities |
| setup | POST | Choose keep or restore; fresh setup remains disabled |
| backups/portable, backups/full | GET | Owner exports |
| backups/restore | POST | Owner portable configuration restore |
| integrations/mqtt | GET/POST | Owner integration settings and latest runtime status; no password in replies |
| network/wifi, network/confirm | POST | Owner network trial/confirmation |
| maintenance/reboot | POST | Owner normal reboot |
| updates/begin, updates/chunk, updates/apply | POST | Owner signed release activation |

## Commands And Holds

```json
{"deviceId":"<saved-id>","command":"VolumeUp","transport":"ir","mode":"hold"}
```

The response contains an operation ID and initial `queued` state.
Poll operations; terminal states are `completed`, `failed` and `cancelled`.
Renew a hold every 250 ms with `{"id":"<id>","action":"keepalive"}`.
Release with `action:"cancel"`. A lost lease stops after 1.2 seconds; every
hold stops after 30 seconds. Only its controller or the owner can cancel it.

Browser, activity and MQTT requests share one serialized command coordinator.
Activities may queue child operations; cancelling or revoking their controller
also stops the sequence. Bluetooth long holds are not advertised.

## Configuration And Backups

Owner configuration mutations include the last observed `revision`.
A stale revision returns 409 rather than overwriting another browser's work.
Malformed imports, unsupported schemas and low storage are rejected.

`POST devices` with `action:"create-profile"` creates an IR device with its
commands and remote layout in one transaction. Supply `revision`, `transport:"ir"`,
`name`, `manufacturer`, `model`, `type`, `source`, `payload` (the same line format
as `commands/import`) and `layout` (an array of `slot`, `label`, `command`).
Duplicate or invalid commands and assignments to missing commands reject the
whole setup. The response includes the new `deviceId`. This action never sends IR.

Configuration may contain `deviceSetup`, keyed by device ID. Each record stores
`source`, `status` (`untested`, `responded`, or `no-response`) and `testedCommand`.
New profiles start untested. A recorded response is the owner's confirmation of
that command only, not a guarantee about the whole profile. Command edits clear
the test record. Older devices without records have no recorded test result.

Browser restore changes portable device/activity resources only. Sensitive
exports contain Wi-Fi/MQTT settings, but restoring those credentials requires
desktop recovery. Full installer ZIP backups use `restore_backup.py`.

Wi-Fi changes retain the previous configuration for 90 seconds. Confirmation
requires the requested network to be connected; otherwise the old network is
restored. A reboot with an unconfirmed trial also restores it.

## Compatibility

The old inventory/device-command reads and `/api/ir-send` /
`/api/bt-saved-command` are authenticated adapters. Older unprotected write,
raw protocol and MD5 update routes are removed. New integrations should use v1.

Pairing is not encryption. This service remains trusted-LAN HTTP.
