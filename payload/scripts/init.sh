#!/bin/sh
PATH=/data/codex/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
LOG=/cache/codex-init.log
HUB_ID=$(cat /data/codex/hub_id 2>/dev/null)
case "$HUB_ID" in
  *[!0-9]*|"") HUB_ID="" ;;
esac

echo "$(date) codex init start" >> "$LOG"
if [ -z "$HUB_ID" ]; then
  echo "$(date) missing numeric /data/codex/hub_id; skipping HBus startup actions" >> "$LOG"
fi

if [ -x /data/codex/bin/dropbear ]; then
  echo '#!/bin/sh' > /usr/sbin/dropbear
  echo 'exec /data/codex/bin/dropbear -s -g -K 300 "$@"' >> /usr/sbin/dropbear
  chmod 755 /usr/sbin/dropbear
  if ! ps | grep '[d]ropbear' >/dev/null 2>&1; then
    /usr/sbin/dropbear
  fi
fi

if [ -x /data/codex/bin/codex_webui ]; then
  if ! ps | grep '[c]odex_webui 8080' >/dev/null 2>&1; then
    /data/codex/bin/codex_webui 8080 >> "$LOG" 2>&1 &
  fi
  if ! ps | grep '[c]odex_webui --coordinator' >/dev/null 2>&1; then
    /data/codex/bin/codex_webui --coordinator >> "$LOG" 2>&1 &
  fi
fi

if [ -x /data/codex/bin/codex_bthid_keyboard ]; then
  mkdir -p /cache/bin
  ln -sf /data/codex/bin/codex_bthid_keyboard /cache/bin/bthid_keyboard
  if ! ps | grep '[c]odex_bthid_keyboard' >/dev/null 2>&1; then
    /data/codex/bin/codex_bthid_keyboard >> "$LOG" 2>&1 &
  fi
fi

if [ -x /data/codex/recovery_ap.sh ]; then
  if [ ! -f /var/run/codex-recovery-monitor.pid ]; then
    /data/codex/recovery_ap.sh monitor >> "$LOG" 2>&1 &
    echo $! > /var/run/codex-recovery-monitor.pid
  fi
fi

(
  ready=0
  while [ "$ready" -lt 60 ]; do
    [ -f /tmp/harmony-operations/core-ready ] && grep -q ':1F98 00000000:0000 0A' /proc/net/tcp && break
    sleep 1
    ready=$(expr "$ready" + 1)
  done
  if [ "$ready" -ge 60 ]; then
    echo "$(date) local engine or HBus not ready after 60 seconds; integration startup skipped" >> "$LOG"
    exit 0
  fi
  if [ -n "$HUB_ID" ] && [ -x /data/codex/bin/codex_hbus ]; then
    /data/codex/bin/codex_hbus "$HUB_ID" "harmony.automation?discover" '{"gatewayType":"codexmqtt"}' >> "$LOG" 2>&1
  fi
) &

if [ -x /data/codex/maintenance.sh ]; then
  /data/codex/maintenance.sh watchdog >> "$LOG" 2>&1 &
fi

echo "$(date) codex init done" >> "$LOG"
