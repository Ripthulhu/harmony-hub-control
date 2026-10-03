# Standalone implementation

This file records acceptance evidence, not a release claim.

## Required gates

- [x] Working installation and configuration backed up off-device (latest backup 2026-10-03 17:18, ZIP and hashes verified).
- [x] Pairing, revocation, origin checks and owner-only mutations tested in a disposable host instance.
- [x] Serialized configuration writes, journal recovery and conflict tests in a disposable host instance.
- [x] One control coordinator for browser, activities and MQTT (host tests; live IR tap, renewed hold and lost-controller release accepted by native engine).
- [ ] Cloud retries removed and native LAN bypasses restricted.
- [ ] Four-screen UI and device/activity editing tested.
- [x] Signature rejection and update rollback tested in a disposable environment.
- [x] Phone/desktop visual and keyboard checks (live Edge, 320/390/1280px widths; details below).
- [ ] Full internet-blocked tests and 24-hour offline soak.
- [ ] Separately scheduled blank-hub initialization and USB recovery test.

Until the last gate passes, do not advertise factory-reset setup as supported.
Keep U-Boot, kernel partitions, radio firmware and manufacturing data unchanged.

## Live development build, 2026-10-03

Installed the local runtime and restricted stock XMPP/HBus listeners to loopback.
The service health check passes. Native device, function, protocol, activity and
map hashes stayed unchanged through installation; the LG profile has 70 commands.
Core control remains independent of MQTT. Full blocked-internet acceptance is
still pending, so the combined cloud-independence gate above is not checked.

Fixed invalid XML in the packaged Lucide sprite, which made all icons disappear.
Fixed hidden navigation placing the claim form in a 180px sidebar column.
Added LG's `Vol_dn`, `Ch_next` and `Ch_prev` command aliases. The deployed sprite
parses as XML with 29 symbols; all UI icon references exist. Touch/keyboard hold
regressions pass. The owner authorized clearing this chat's saved denial for the
hub origin; live browser inspection now works. Other origin restrictions remain.

Observed the real Pair button on `/dev/input/event0`: EV_KEY code 30, values 1/0.
Installed physical browser approval in the existing web-service process, without
another daemon or GPIO writes. Physical approval grants full owner access, as
requested; approval through Settings remains control-only. Disposable integration
tests cover full ownership, control-to-owner upgrades, competing requests, expiry, cancellation, old/long/wrong
events, dropped input and revocation. The owner confirmed that a fresh private
browser opened the remote automatically after pressing Pair on the real hub.

Corrected an integration startup race: core-ready can appear before HBus listens.
Startup now waits for both, within the existing 60-second limit.

## Live UI inspection, 2026-10-03 18:17

Deployed only `app.js` and `app.css`, with verified hashes and no service restart.
The previous assets are backed up off-device in
`backups/standalone/20261003-181720-web-assets.zip` (outside the repository).
Native binaries, pairing roles, devices, commands and activities were not changed.

- Compact remote with separate On/Off labels, aligned volume/channel controls,
  and a reserved sending-status line that does not move a held button.
- Command drawer keeps its search and close controls visible while results scroll.
  Empty searches have a clear result; all command buttons retain hold handling.
- Imported device types remain selected instead of going blank. Brand/model fields
  have proper field groups; settings forms have a bounded reading width.
- Activity actions have visible command and delay labels; delay-only actions hide
  irrelevant command fields. Manually entered IR data enables Save when populated.
- Keyboard skip navigation preserves the current page. View changes reset focus
  and scroll; profile files are size-checked before reading.

Checked rendered layouts in Edge at 390x844, 320x740 and 1280x900, in light and
system-dark themes. No horizontal overflow; remote targets are at least 48x48px.
At 390x844 the entire remote fits above the bottom navigation. Smaller displays
scroll with bottom clearance. Checked command search and its empty state, device
type preservation, unsaved activity editing, manual NEC entry, and the keyboard
skip link. No browser errors or warnings. Restored the original system theme and
browser viewport. Screenshots are in `artifacts/ui-review-20261003` outside the repo.

`test_local_controls.mjs`, `test_assets.py` and read-only `test_hub.py` passed.
The live inventory remains one IR device with 70 commands. This UI pass did not
send IR, save test configuration, pair Bluetooth or exercise a live disconnect;
hold/cancel/lost-controller behavior retains the earlier native tests and current
host regressions. The editing gate remains open for full save/restore coverage.

Deployed SHA-256:

| Asset | SHA-256 |
| --- | --- |
| app.js | `4bdd77f356b38041cb493536c7aa3d009b8e2aaf052728b5b4f0bc9ee862bcaa` |
| app.css | `6e77612263310337cfb4b702b6f1ea416d3addc9bc796bf208348e7d0dc4d9bf` |

Still required: real Bluetooth and IR learning, native activity import coverage,
network-change rollback under failure, full offline tests and the 24-hour soak.
Fresh setup remains explicitly unverified.

## Repository cleanup, 2026-10-04

Removed the retired C-rendered dashboard, its form handlers, embedded JPEG and
unsigned updater. Profile parsers now live in `payload/www/profiles.js`; their
tests load that file directly. The current remote layout and controls are unchanged.

Stock resource reads and edits use cJSON instead of string scanning and splicing.
Edits retain unknown stock fields and reject files that exceed the resource limit.
IR and Bluetooth commands return results directly to the coordinator/API, without
an internal HTTP connection. Authenticated compatibility routes remain available.

Removed the unused PowerShell uploader and duplicate maintenance recovery paths.
The full build script uses `tools/build_payload.py` for the five shared helpers.
The database smoke test is read-only and fails when a sampled file cannot be parsed.

Passed disposable tests for pairing/access, configuration revisions, stock-field
preservation, exact IDs, malformed/oversized resources, Bluetooth profile storage,
signed updates, rollback, network recovery, MQTT and activities. Native IR/LTCP
tests and browser hold/release tests passed, including delayed queue replies and
failed renewals. Eleven offline profile fixtures and a sample of 347 commands from
ten public profiles passed. Asset, Python-upload and PowerShell tests passed.

All five MIPS helpers rebuilt. `codex_webui` is 339,236 bytes, down from 351,476;
the other four helpers are unchanged. The rebuilt binary and checksum are in the
repository. This cleanup has not been deployed to the live hub and does not close
the outstanding hardware, blank-hub or offline-soak tests above.
