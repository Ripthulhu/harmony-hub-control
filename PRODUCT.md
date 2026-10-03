# Harmony Hub Control

<!-- impeccable:product-schema 1 -->

## Platform

web

## Users

Owners of rooted Harmony Hubs who want a phone or browser remote at home.
The first supported hardware is the verified Harmony Hub on firmware 4.15.600.

## Product Purpose

Keep the hub useful without a Logitech account or an always-on computer.
Daily use opens directly to the last-used device and its familiar controls.

## Operating Context

The web server and local services run on the hub. A desktop computer is used
for installation and USB recovery. Home Assistant and MQTT are optional.
The first deployment is trusted-LAN HTTP, not an internet-facing service.

## Capabilities and Constraints

The approved release includes IR, supported Bluetooth controls, editable local
activities, pairing, backups and reversible userspace updates. Preserve the
working native press/hold/release implementation and existing handheld setup.
Leave bootloader, kernel, radio firmware, identity and calibration untouched.
Use C and the installed Lua runtime, with small, bounded storage and memory.
Fresh setup is experimental until a separate blank-hub test passes. Only one
physical hub is available; development must not factory-reset it.

## Brand Commitments

Four destinations: Remote, Devices, Activities, Settings. Restrained light and
dark themes, system fonts and Lucide icons. Plain labels, no dashboard metrics
or setup panels mixed into the remote. The user explicitly approved this flow.

## Evidence on Hand

The installed LG C5 profile sends working IR. The owner confirmed both power
off and volume hold/release on the real television. Bluetooth and a completely
blank hub still need their own physical acceptance tests.

## Product Principles

- Saved controls work offline.
- Report transmission, never infer that an IR device responded.
- Pair controllers; reserve configuration and sensitive exports for owners.
- Preserve imported items that cannot be converted.
- Recovery must be possible without cloud services.

## Accessibility & Inclusion

Touch, mouse and keyboard controls, visible focus, large stable targets,
connection-loss release and reduced-motion support are required.
