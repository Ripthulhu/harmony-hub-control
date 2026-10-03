# Security

This build is for a trusted local network. The UI uses HTTP: pairing tokens,
commands and exported credentials are not encrypted in transit. Do not port
forward it or use it on an untrusted network.

## Access

The first browser claims ownership with the hub's Pair button or a single-use
installer code. Later browsers request physical approval or owner approval.
Physical pairing grants full owner access, including configuration, credentials
and updates. Approval in Settings grants control-only access. Existing control-only
browsers can request an upgrade, but must confirm it with a fresh physical press.
Physical pairing binds one waiting request to a fresh press/release, expires
after 90 seconds and ignores long holds, autorepeats and stale input events.
The button is observed without taking it away from the stock hardware services.
Press it only after starting your own request; physical pairing does not identify
an unknown waiting browser for you. Control-only controllers can send commands but cannot
change configuration, export credentials, pair new controllers or update
software. Owners can revoke them; active owned operations are cancelled.

Mutations require the controller cookie and CSRF token. Host and Origin must
match the hub's numeric address and port. Requests, workers, operation history
and logs are bounded. Stored pairing tokens are hashed.

MQTT and the Lua core use a root-only local integration key and the same
command coordinator. Keep SSH keys private. Dropbear disallows password login.
The installer restricts the original XMPP and HBus listeners to loopback,
while preserving local HAL and engine access. This is not a claim that every
stock service has been audited.

## Data And Recovery

Configuration saves use a global write lock, revision checks, atomic file
replacement and a recovery journal. Backups are made before installation.
The recovery hotspot is read-only; it does not accept Wi-Fi passwords. Use
USB recovery unless a protected hotspot has been independently verified.

Full desktop backups contain Wi-Fi credentials, stock preferences and host
keys. Portable UI exports exclude credentials. Sensitive UI exports include
Wi-Fi/MQTT settings and should be treated like passwords.

Browser updates verify Ed25519 signatures and SHA-512 file hashes before
activation. The private signing key never belongs on the hub. The installed
public key can only be changed through owner SSH. One previous release and a
separate known-good recovery helper are retained for rollback.

Never commit private keys, authorized_keys, credentials, personal backups,
firmware dumps or proprietary patched stock files. The installer reads and
patches the owned hub's original listener files locally.

## Unverified Gates

See STANDALONE_STATUS.md. Pairing is tested, but it does not compensate for
plaintext HTTP or replace a full security assessment. The blank-hub test,
offline soak and real-device failure/recovery tests remain required.
