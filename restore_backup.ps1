param(
    [Alias("Host")]
    [string]$HubHost,
    [string]$KeyPath,
    [int]$Port = 22,
    [string]$SshUser = "root",
    [string]$BackupDir,
    [switch]$NoPrompt
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "ssh_helpers.ps1")

$HubHost = Prompt-IfMissing $HubHost "Harmony hub IP address" -Required
$KeyPath = Prompt-IfMissing $KeyPath "SSH private key path for root login" -Required
$KeyPath = (Resolve-Path -LiteralPath $KeyPath).Path

if (-not $BackupDir) {
    $BackupDir = (Invoke-Remote "ls -dt /data/codex-backups/webui-handoff-* 2>/dev/null | sed -n '1p'" -TimeoutMs 30000).Trim()
}
if (-not $BackupDir) {
    throw "No /data/codex-backups/webui-handoff-* backup was found. Pass -BackupDir if you know the path."
}

Write-Host "Restoring from $BackupDir" -ForegroundColor Cyan

$restore = @"
B=$(Remote-Quote $BackupDir)
killall codex_webui 2>/dev/null || true
killall codex_portal 2>/dev/null || true
killall codex_dhcpd 2>/dev/null || true
restore_one() {
  backup="`$B/`$1"
  target="`$2"
  if [ -e "`$backup" ]; then
    cp -p "`$backup" "`$target"
    echo "restored `$target"
  else
    echo "missing backup for `$target"
  fi
}
restore_one _etc_init.d_rcS.local /etc/init.d/rcS.local
restore_one _opt_luaworks_tasks_connectserver_netservicestarter.lua /opt/luaworks/tasks/connectserver/netservicestarter.lua
restore_one _usr_sbin_dropbear /usr/sbin/dropbear
restore_one _usr_sbin_dropbearkey /usr/sbin/dropbearkey
restore_one _data_codex_hub_id /data/codex/hub_id
restore_one _data_codexmqtt_config.json /data/codexmqtt/config.json
chmod 755 /etc/init.d/rcS.local /usr/sbin/dropbear /usr/sbin/dropbearkey 2>/dev/null || true
chmod 600 /data/codexmqtt/config.json 2>/dev/null || true
/bin/busybox sync 2>/dev/null || true
echo "restore done; reboot the hub for boot-script changes to fully apply"
"@

$out = Invoke-Remote $restore -TimeoutMs 60000
Write-Host $out.Trim()
