#!/bin/sh
PATH=/data/codex/bin:/usr/sbin:/usr/bin:/sbin:/bin
ROOT=/data/codex/local
case "$1" in
  recover-network)
    if [ -f "$ROOT/wifi.pending" ] && [ -f "$ROOT/wifi.previous" ]; then
      cp "$ROOT/wifi.previous" /etc/wpa_supplicant.conf && chmod 600 /etc/wpa_supplicant.conf && rm -f "$ROOT/wifi.pending"
    fi
    ;;
  wifi-try)
    sleep 2
    wpa_cli -i ath0 reconfigure >/dev/null 2>&1
    sleep 90
    if [ -f "$ROOT/wifi.pending" ]; then
      cp "$ROOT/wifi.previous" /etc/wpa_supplicant.conf && chmod 600 /etc/wpa_supplicant.conf
      /cache/harmony-recovery --sync
      wpa_cli -i ath0 reconfigure >/dev/null 2>&1
      rm -f "$ROOT/wifi.pending"
    fi
    ;;
  watchdog)
    exec 9>/tmp/harmony-maintenance.lock
    # The native coordinator has its own singleton lock. This PID is only for
    # the small log/recovery monitor, which never changes hardware partitions.
    if [ -f /tmp/harmony-maintenance.pid ]; then
      previous=$(cat /tmp/harmony-maintenance.pid)
      kill -0 "$previous" 2>/dev/null && exit 0
    fi
    echo $$ >/tmp/harmony-maintenance.pid
    while true; do
      /data/codex/bin/codex_webui --trim-logs
      if ! ps | grep '[c]odex_webui --coordinator' >/dev/null 2>&1; then
        /data/codex/bin/codex_webui --coordinator >/cache/codex-coordinator.log 2>&1 &
      fi
      if ! ps | grep '[c]odex_webui 8080' >/dev/null 2>&1; then
        /data/codex/bin/codex_webui 8080 >>/cache/codex-init.log 2>&1 &
      fi
      sleep 30
    done
    ;;
  update-check)
    [ -f "$ROOT/update.pending" ] || exit 0
    tries=0
    while [ "$tries" -lt 60 ]; do
      if /data/codex/bin/codex_webui --update-health; then
        rm -f "$ROOT/update.pending"
        echo 'Release passed startup health checks' > "$ROOT/update.result"
        exit 0
      fi
      sleep 1; tries=$(expr "$tries" + 1)
    done
    # This known-good binary is kept outside the signed activation file set.
    /cache/harmony-recovery --rollback && /sbin/reboot
    ;;
  *) exit 2 ;;
esac
