# powershell -NoProfile -File tools/test_ssh_helpers.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
function Assert($Condition, $Message) { if (-not $Condition) { throw $Message } }
foreach ($file in Get-ChildItem $root -Filter "*.ps1") {
    $tokens = $null
    $errors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$tokens, [ref]$errors)
    Assert ($errors.Count -eq 0) "Syntax error in $file"
}
. (Join-Path $root "ssh_helpers.ps1")
$NoPrompt = $true
Assert ((Prompt-IfMissing "value" "label" -Required) -eq "value") "Prompt changed provided value"
Assert ((Prompt-IfMissing "" "optional") -eq "") "Optional prompt"
$failed = $false
try { Prompt-IfMissing "" "required" -Required } catch { $failed = $true }
Assert $failed "Required prompt did not fail"
Assert ((Quote-ProcessArg "a b") -eq '"a b"') "Space quoting"
Assert ((Quote-ProcessArg 'a"b') -eq '"a\"b"') "Quote escaping"
Assert ((Quote-ProcessArg "C:\a b\") -eq '"C:\a b\\"') "Trailing slash escaping"
$Port = 2222
$KeyPath = "C:\key with spaces"
$SshUser = "root"
$HubHost = "192.0.2.1"
Assert ((Get-SshArgs)[-1] -eq "root@192.0.2.1") "SSH target"
$quoted = "'" + "it's".Replace("'", ("'" + '"' + "'" + '"' + "'")) + "'"
Assert ((Remote-Quote "it's") -eq $quoted) "Remote single quote escaping"
$BackupDir = "/data/backup"
$tokens = $null
$errors = $null
$rollback = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $root "restore_backup.ps1"), [ref]$tokens, [ref]$errors)
$assignment = $rollback.Find({param($node) $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -eq '$restore'}, $true)
Invoke-Expression $assignment.Extent.Text
Assert ($restore.Contains('backup="$B/$1"')) "Rollback path expanded on Windows instead of the hub"

# Load only the upload functions, never the installer's top-level deployment.
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $root "install_webui.ps1"), [ref]$tokens, [ref]$errors)
$ast.FindAll({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -in @("Split-RemoteDir", "Upload-Bytes", "Upload-Text", "Upload-Data")}, $true) |
    ForEach-Object { Invoke-Expression $_.Extent.Text }
function Invoke-Remote($Command, [byte[]]$InputBytes, $TimeoutMs) {
    $script:captured = @{Command=$Command; Bytes=$InputBytes; Timeout=$TimeoutMs}
}
function Info($Message) { $script:info = $Message }
Upload-Text "text" "/data/config.json" "600"
Assert ($captured.Bytes.Length -eq 4 -and $captured.Timeout -ge 90000) "Text upload"
Assert ($info -match "md5=<hidden>") "Secret hash exposed"
Upload-Bytes (Join-Path $root "ssh_helpers.ps1") "/tmp/it's here" "755"
Assert ($captured.Command.Contains((Remote-Quote "/tmp/it's here"))) "Upload path quoting"
Assert ($captured.Command.Contains("&& mv ") -and $captured.Command.Contains("&& chmod 755 ")) "Atomic upload"
Assert ($captured.Bytes.Length -eq (Get-Item (Join-Path $root "ssh_helpers.ps1")).Length) "Binary upload"
Write-Host "PowerShell syntax, prompts, SSH quoting, and upload checks passed"
