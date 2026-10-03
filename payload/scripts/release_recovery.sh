#!/bin/sh
PATH=/usr/sbin:/usr/bin:/sbin:/bin
ROOT=/data/codex/local

# Kept outside the replaceable release: recovery must survive a broken init.
if [ -f "$ROOT/wifi.pending" ] && [ -f "$ROOT/wifi.previous" ]; then
  cp "$ROOT/wifi.previous" /etc/wpa_supplicant.conf && chmod 600 /etc/wpa_supplicant.conf && rm -f "$ROOT/wifi.pending"
  /cache/harmony-recovery --sync
fi
[ "$1" = update-check ] || exit 0
[ -f "$ROOT/update.pending" ] || exit 0
tries=0
while [ "$tries" -lt 120 ]; do
  if /cache/harmony-recovery --update-health; then
    rm -f "$ROOT/update.pending"
    echo 'Release passed startup health checks' > "$ROOT/update.result"
    /cache/harmony-recovery --sync
    exit 0
  fi
  sleep 1; tries=$(expr "$tries" + 1)
done
/cache/harmony-recovery --rollback && /sbin/reboot
