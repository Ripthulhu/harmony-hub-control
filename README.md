# Harmony Hub Control

A local remote that runs on the Harmony Hub. Open `http://<hub-ip>:8080/` from
your phone or computer. No always-on computer or Logitech account is needed for
the installed local controls.

This is a development build for Harmony Hub firmware **4.15.600**. It keeps
the native IR, Bluetooth and handheld services. It does not replace U-Boot,
the kernel, radio firmware or manufacturing data.

## Install

You need Python 3, OpenSSH and an owner SSH key. For Wi-Fi provisioning or
initial rooting, keep [harmony-hub-root](https://github.com/Ripthulhu/harmony-hub-root)
beside this checkout, or pass its location with `--root-tool`.

On Windows, open **Install_Harmony_Control.cmd**. On Linux or macOS:

```sh
python3 setup.py
```

The desktop flow offers USB Wi-Fi provisioning, discovers the hub, uses the
existing LAN root tool if necessary, verifies a sensitive backup on your
computer, installs the local runtime and opens the UI. Enter a browser name,
choose **Pair with hub button**, then briefly press and release Pair on the
back of the hub. Physical pairing grants full access. The installer code is an
alternative for the first browser; owner approval in Settings can grant other
browsers remote-control-only access. A control-only browser can request full
access in Settings and confirm with a fresh Pair-button press.

For a hub that already has owner SSH access:

```sh
python3 install_webui.py --hub-host <hub-ip> --key-path <owner-key> --no-prompt
```

The numeric Hub ID is read from the existing root-tool handoff or the hub.
Use `--hub-id <id>` only when you know the real value. Existing devices,
handheld configurations, owner access and MQTT settings are preserved.
A normal reboot activates the new service and listener restrictions.

## Everyday Use

- **Remote:** last-used device or activity, supported buttons and searchable extra commands.
- **Devices:** add IR profiles, import files, learn commands or pair a supported Bluetooth profile.
- **Activities:** choose power/input actions, delays and remote button assignments.
- **Settings:** approve controllers, export backups, change Wi-Fi and configure optional MQTT/Home Assistant.

For Home Assistant remote commands, long presses and MQTT discovery, see
[Home Assistant setup](docs/HOME_ASSISTANT.md).

A tap sends one press. Holding an IR button uses native press/hold/release.
Releasing, leaving the page or losing the connection stops it. IR state is an
estimate; a successful send does not prove that the TV responded.

To add an IR device, enter its brand and model, find or import a profile, then
review its remote buttons. Nothing is saved until the final step. Choose
**Save and test** to send a command yourself, or **Save without testing** to
leave testing for later. A matching profile name is not proof of compatibility.

Choose **Keep existing setup** on first use. Supported local devices can be
edited; original native activity configurations are preserved, not silently
converted. Generic Bluetooth mice and offline handheld remapping are outside
this release.

## Recovery And Updates

The installer creates a verified ZIP backup before changing the hub. Keep it
private: it contains credentials and keys. Validate a backup without changing
anything:

```sh
python3 restore_backup.py <backup.zip> --hub-host <hub-ip> --key-path <owner-key>
```

Add `--apply --reboot` to restore its files through owner SSH. USB network
recovery and stock reset/firmware flashing remain in the existing desktop
tool. A stock factory reset may remove this installation.

Browser updates require an Ed25519-signed bundle and an owner-supplied trusted
public key. Unsigned releases are rejected. See [Build notes](docs/BUILD.md).

## Limits Before Release

Fresh setup after a stock factory reset is **not verified** and is disabled in
the UI. Missing-resource initialization is opt-in for a separately scheduled
blank-hub test. Do not reset your only hub to try it.

Phone/desktop visual checks, physical Bluetooth and learning tests, full
internet-blocked tests and a 24-hour offline soak remain release gates.
See [Acceptance status](docs/STANDALONE_STATUS.md) for the evidence.

The UI uses trusted-LAN **HTTP**, not HTTPS. Pairing does not encrypt traffic.
Do not expose it to the internet. Owner SSH is key-only; stock XMPP and HBus
listeners are restricted to loopback. See [Security](docs/SECURITY.md) and
[API](docs/API.md).

## Development

Edit the separate HTML/CSS/JavaScript in `payload/www/`, Lua in
`payload/core/` and `payload/mqtt/`, and native code in `payload/source/`.
There are no runtime CDNs, external fonts, Node/Python services or database
server on the hub. Backups, keys, firmware dumps and proprietary stock files
must stay outside the repository.
