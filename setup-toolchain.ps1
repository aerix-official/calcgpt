param(
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'

Write-Host '=== CE C/C++ Toolchain auto-installer ===' -ForegroundColor Cyan
Write-Host ''
Write-Host 'This will:'
Write-Host '  1. Download the latest CE C/C++ Toolchain (~100 MB) from'
Write-Host '     https://github.com/CE-Programming/toolchain/releases'
Write-Host "  2. Extract it to $env:USERPROFILE\CEdev"
Write-Host '  3. Set the CEDEV environment variable for your user'
Write-Host '     (no admin rights required)'
Write-Host ''

if (-not $Yes) {
    $resp = Read-Host 'Continue? [y/N]'
    if ($resp -notmatch '^[yY]') {
        Write-Host 'Aborted.'
        exit 1
    }
}

$installParent = $env:USERPROFILE
$installDir = Join-Path $installParent 'CEdev'

if (Test-Path $installDir) {
    Write-Host ''
    Write-Host "An existing folder was found at: $installDir"
    if ($Yes) {
        # Check if it already looks like a working install; if so, skip.
        $existingMake = Join-Path $installDir 'bin\make.exe'
        if (Test-Path $existingMake) {
            Write-Host 'Existing install looks valid (bin\make.exe found). Skipping download.'
            [System.Environment]::SetEnvironmentVariable('CEDEV', $installDir, 'User')
            Write-Host "CEDEV -> $installDir"
            exit 0
        }
        Write-Host 'Existing folder is incomplete; replacing it...'
    } else {
        $resp = Read-Host 'Overwrite it? [y/N]'
        if ($resp -notmatch '^[yY]') {
            Write-Host 'Aborted.'
            exit 1
        }
    }
    Write-Host 'Removing existing folder...'
    Remove-Item $installDir -Recurse -Force
}

Write-Host ''
Write-Host 'Looking up the latest release on GitHub...'
$release = Invoke-RestMethod 'https://api.github.com/repos/CE-Programming/toolchain/releases/latest'

$asset = $release.assets |
    Where-Object { $_.name -match '(?i)win.*\.zip$' } |
    Select-Object -First 1
if (-not $asset) {
    throw 'No Windows .zip asset found on the latest release.'
}

$sizeMB = [math]::Round($asset.size / 1MB, 1)
Write-Host "Found: $($asset.name) ($sizeMB MB)"

$tmp = Join-Path $env:TEMP $asset.name
Write-Host "Downloading to $tmp..."
# Hiding the progress bar makes Invoke-WebRequest dramatically faster on
# Windows PowerShell 5.1.
$savedProgress = $ProgressPreference
$ProgressPreference = 'SilentlyContinue'
try {
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $tmp -UseBasicParsing
} finally {
    $ProgressPreference = $savedProgress
}

Write-Host "Extracting to $installParent..."
Expand-Archive -Path $tmp -DestinationPath $installParent -Force
Remove-Item $tmp -ErrorAction SilentlyContinue

# The zip might extract to a folder named CEdev, or something with a version
# suffix. Locate make.exe under bin\ and use its parent folder.
$makeExe = Get-ChildItem -Path $installParent -Recurse -Filter 'make.exe' -ErrorAction SilentlyContinue |
    Where-Object { $_.Directory.Name -eq 'bin' } |
    Select-Object -First 1
if (-not $makeExe) {
    throw 'Extracted, but no bin\make.exe found. The toolchain layout may have changed.'
}
$resolvedDir = $makeExe.Directory.Parent.FullName
if ($resolvedDir -ne $installDir) {
    Write-Host "Note: toolchain extracted to $resolvedDir (not $installDir)."
    $installDir = $resolvedDir
}

Write-Host "Setting CEDEV=$installDir (user scope)..."
[System.Environment]::SetEnvironmentVariable('CEDEV', $installDir, 'User')

Write-Host ''
Write-Host 'Done!' -ForegroundColor Green
Write-Host "CEDEV -> $installDir"
Write-Host ''
Write-Host 'Open a NEW terminal (so the env var is picked up), then run'
Write-Host 'build.bat in this folder to compile DEMO.8xp.'
