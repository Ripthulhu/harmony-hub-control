function Prompt-IfMissing([string]$Value, [string]$Label, [switch]$Required) {
    if ($Value) { return $Value }
    if ($NoPrompt) {
        if ($Required) { throw "$Label is required" }
        return ""
    }
    $v = Read-Host $Label
    if ($Required -and -not $v) { throw "$Label is required" }
    return $v
}

function Quote-ProcessArg([string]$Arg) {
    if ($null -eq $Arg) { return '""' }
    if ($Arg.Length -eq 0) { return '""' }
    if ($Arg -notmatch '[\s"]') { return $Arg }
    $out = '"'
    $slashes = 0
    foreach ($ch in $Arg.ToCharArray()) {
        if ($ch -eq '\') {
            $slashes += 1
            continue
        }
        if ($ch -eq '"') {
            $out += ('\' * (($slashes * 2) + 1)) + '"'
            $slashes = 0
            continue
        }
        if ($slashes) {
            $out += ('\' * $slashes)
            $slashes = 0
        }
        $out += $ch
    }
    if ($slashes) { $out += ('\' * ($slashes * 2)) }
    return $out + '"'
}

function Remote-Quote([string]$Value) {
    return "'" + $Value.Replace("'", ("'" + '"' + "'" + '"' + "'")) + "'"
}

function Get-SshArgs() {
    $args = @(
        "-p", [string]$Port,
        "-i", $KeyPath,
        "-o", "IdentitiesOnly=yes",
        "-o", "BatchMode=yes",
        "-o", "StrictHostKeyChecking=accept-new",
        "$SshUser@$HubHost"
    )
    return $args
}

function Invoke-Remote([string]$Command, [byte[]]$InputBytes = $null, [int]$TimeoutMs = 90000) {
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = "ssh"
    $allArgs = (Get-SshArgs) + @($Command)
    $psi.Arguments = ($allArgs | ForEach-Object { Quote-ProcessArg $_ }) -join " "
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $p = [System.Diagnostics.Process]::Start($psi)
    if ($InputBytes) {
        $p.StandardInput.BaseStream.Write($InputBytes, 0, $InputBytes.Length)
    }
    $p.StandardInput.BaseStream.Close()
    if (-not $p.WaitForExit($TimeoutMs)) {
        try { $p.Kill() } catch {}
        throw "ssh timed out while running: $Command"
    }
    $stdout = $p.StandardOutput.ReadToEnd()
    $stderr = $p.StandardError.ReadToEnd()
    if ($p.ExitCode -ne 0) {
        throw "ssh failed with exit $($p.ExitCode)`ncommand=$Command`nstdout=$stdout`nstderr=$stderr"
    }
    if ($stderr.Trim()) {
        Write-Host $stderr.Trim() -ForegroundColor DarkGray
    }
    return $stdout
}
