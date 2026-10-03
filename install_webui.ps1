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
. (Join-Path $PSScriptRoot "ssh_helpers.ps1")
function Info($Text) { Write-Host "  $Text" }

function Split-RemoteDir([string]$Path) {
    $i = $Path.LastIndexOf("/")
    if ($i -le 0) { return "/" }
    return $Path.Substring(0, $i)
}

function Upload-Bytes([string]$LocalPath, [string]$RemotePath, [string]$Mode) {
    Upload-Data ([System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $LocalPath))) $RemotePath $Mode
}

function Upload-Text([string]$Text, [string]$RemotePath, [string]$Mode) {
    Upload-Data ([Text.Encoding]::UTF8.GetBytes($Text)) $RemotePath $Mode
}

function Upload-Data([byte[]]$Bytes, [string]$RemotePath, [string]$Mode) {
    $dir = Split-RemoteDir $RemotePath
    $tmp = "$RemotePath.tmp-handoff-$PID"
    $cmd = "mkdir -p $(Remote-Quote $dir) && cat > $(Remote-Quote $tmp) && mv $(Remote-Quote $tmp) $(Remote-Quote $RemotePath) && chmod $Mode $(Remote-Quote $RemotePath)"
    Invoke-Remote $cmd $Bytes ([Math]::Max(90000, 45000 + [int]($Bytes.Length / 12000))) | Out-Null
    if ($RemotePath -match "pass|authorized_keys|config\.json") {
        $md5 = "<hidden>"
    } else {
        $md5 = [System.BitConverter]::ToString([System.Security.Cryptography.MD5]::Create().ComputeHash($Bytes)).Replace("-", "").ToLowerInvariant()
    }
    Info "$RemotePath bytes=$($Bytes.Length) md5=$md5"
}


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
