param(
    [Alias("Host")]
    [string]$HubHost,
    [string]$KeyPath,
    [int]$Port = 22,
    [string]$SshUser = "root",
    [string]$HubId,
    [string]$MqttBroker = "",
    [int]$MqttPort = 1883,
    [string]$MqttUser = "",
    [string]$MqttPassword = "",
    [string]$MqttBaseTopic = "harmony/hub",
    [string]$MqttDiscoveryPrefix = "homeassistant",
    [string]$MqttClientId = "harmony-local-mqtt",
    [switch]$MqttDisabled,
    [switch]$SkipCloudSuppression,
    [switch]$NoApplyCloudRestart,
    [string]$BackupDir,
    [string]$ReleasePublicKey,
    [switch]$BackupOnly,
    [switch]$NoPrompt
)

$ErrorActionPreference = "Stop"
# Both platforms use the same backup, pairing and signed-update installation.
$python = Get-Command py -ErrorAction SilentlyContinue
$prefix = @()
if ($python) { $prefix = @("-3") }
else { $python = Get-Command python -ErrorAction SilentlyContinue }
if (-not $python) { throw "Python 3 is required. Install it, then run this installer again." }
$arguments = @((Join-Path $PSScriptRoot "install_webui.py"))
$values = @{
    "hub-host"=$HubHost; "key-path"=$KeyPath; "port"=$Port; "ssh-user"=$SshUser; "hub-id"=$HubId;
    "mqtt-broker"=$MqttBroker; "mqtt-port"=$MqttPort; "mqtt-user"=$MqttUser; "mqtt-password"=$MqttPassword;
    "mqtt-base-topic"=$MqttBaseTopic; "mqtt-discovery-prefix"=$MqttDiscoveryPrefix;
    "mqtt-client-id"=$MqttClientId; "backup-dir"=$BackupDir; "release-public-key"=$ReleasePublicKey
}
foreach ($name in $values.Keys) {
    if ($null -ne $values[$name] -and "$($values[$name])" -ne "") { $arguments += @("--$name", "$($values[$name])") }
}
foreach ($flag in @(
    @($MqttDisabled,"mqtt-disabled"), @($SkipCloudSuppression,"skip-cloud-suppression"),
    @($NoApplyCloudRestart,"no-apply-cloud-restart"), @($BackupOnly,"backup-only"), @($NoPrompt,"no-prompt")
)) {
    if ($flag[0]) { $arguments += "--$($flag[1])" }
}
& $python.Source @prefix @arguments
exit $LASTEXITCODE
