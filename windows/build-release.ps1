# Non-interactive release build for CI: the Windows application with both client packages embedded, the tests and
# the installer. No downloads, no installs. Run it in an MSVC x64 developer environment (cl, cmake, ninja, cargo).
#
#   windows\build-release.ps1 CLIENT_X64_DIR CLIENT_X86_DIR OUTPUT.exe [BUILD_DIR] [-Sign]
#
# CLIENT_X64_DIR and CLIENT_X86_DIR are the unpacked openbf2-windows-x64-client and openbf2-windows-x86-client
# packages of the engine's build. OUTPUT.exe must not exist; OUTPUT.exe.sha256 is written beside it. BUILD_DIR
# defaults to openbf2-setup-build beside OUTPUT.exe and may be reused between runs.
# SETUP_BOOST_ROOT, SETUP_QT_ROOT, SETUP_SDL3_ROOT, ZLIB_LIBRARY, ZLIB_INCLUDE_DIR, SETUP_DIRECTX_REDIST and
# SETUP_ISCC locate the toolchain (ci/fetch-dependencies.sh prints them). The owner's media and key tests stay off.
# -Sign signs, with windows\sign.ps1 and its environment, every program and DLL without a valid signature: first in
# the client folders (in place, before the build copies them), then in the application folder, then the installer
# and its uninstaller. Files already signed (Microsoft's runtime and DirectX setup) stay as they are. Every program
# and DLL of the application folder and the installer must then carry a valid signature.
param(
  [Parameter(Mandatory = $true)][string]$ClientX64,
  [Parameter(Mandatory = $true)][string]$ClientX86,
  [Parameter(Mandatory = $true)][string]$Output,
  [string]$BuildDir,
  [switch]$Sign
)
$ErrorActionPreference = 'Stop'
function Fail([string]$message) { throw "build-release: $message" }
function Run([string]$program, [string[]]$arguments) {
  & $program @arguments
  if ($LASTEXITCODE -ne 0) { Fail "$program exited with $LASTEXITCODE" }
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
function Programs([string]$folder) {
  Get-ChildItem $folder -Recurse -File | Where-Object { $_.Extension -in '.exe', '.dll', '.pyd' } | ForEach-Object { $_.FullName }
}
function Unsigned([string[]]$files) { $files | Where-Object { (Get-AuthenticodeSignature $_).Status -ne 'Valid' } }
function SignFiles([string[]]$files) {
  $files = @(Unsigned $files)
  if ($files.Count -eq 0) { return }
  Write-Host "signing $($files.Count) file(s)"
  $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'sign.ps1')) + $files
  Run powershell $arguments
}
$Output = [IO.Path]::GetFullPath($Output)
if (Test-Path $Output) { Fail "output already exists: $Output" }
New-Item -ItemType Directory -Force (Split-Path $Output) | Out-Null
if (-not $BuildDir) { $BuildDir = Join-Path (Split-Path $Output) 'openbf2-setup-build' }
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$clients = @{}
foreach ($pair in @(@('X64', $ClientX64), @('X86', $ClientX86))) {
  $folder = (Resolve-Path $pair[1]).Path
  foreach ($file in 'BF2.exe', 'dice_py.dll', 'RendDX9.dll', 'SwiffPlayer.dll', 'BF2Audio.dll') {
    if (-not (Test-Path (Join-Path $folder $file) -PathType Leaf)) { Fail "missing client file: $folder\$file" }
  }
  $clients[$pair[0]] = $folder
}
foreach ($tool in 'cl', 'cmake', 'ninja', 'cargo') {
  if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Fail "$tool not found (run in an MSVC x64 developer environment)" }
}
foreach ($variable in 'SETUP_BOOST_ROOT', 'SETUP_QT_ROOT', 'SETUP_SDL3_ROOT', 'ZLIB_LIBRARY', 'ZLIB_INCLUDE_DIR', 'SETUP_DIRECTX_REDIST', 'SETUP_ISCC') {
  if (-not [Environment]::GetEnvironmentVariable($variable)) { Fail "$variable is not set (ci/fetch-dependencies.sh prints it)" }
}
if ($Sign) {
  if (-not (($env:SETUP_SIGN_DLIB -and $env:SETUP_SIGN_METADATA) -or $env:SETUP_SIGN_CERT_SHA1)) { Fail '-Sign needs SETUP_SIGN_DLIB and SETUP_SIGN_METADATA, or SETUP_SIGN_CERT_SHA1 (windows\sign.ps1)' }
  Write-Host '== sign the clients'
  foreach ($folder in $clients.Values) { SignFiles (Programs $folder) }
}
$scratch = Join-Path $BuildDir 'scratch'
New-Item -ItemType Directory -Force $scratch | Out-Null

Write-Host "== configure ($BuildDir)"
$options = @('-S', $repo, '-B', $BuildDir, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DSETUP_SCRATCH=$scratch",
  "-DSETUP_CLIENT_X64=$($clients.X64)", "-DSETUP_CLIENT_X86=$($clients.X86)", "-DSETUP_SIGN=$(if ($Sign) { 'ON' } else { 'OFF' })")
foreach ($variable in 'SETUP_BOOST_ROOT', 'SETUP_QT_ROOT', 'SETUP_SDL3_ROOT', 'ZLIB_LIBRARY', 'ZLIB_INCLUDE_DIR', 'SETUP_DIRECTX_REDIST', 'SETUP_ISCC') {
  $options += "-D$variable=$([Environment]::GetEnvironmentVariable($variable))"
}
Run cmake $options
Write-Host '== build'
Run cmake @('--build', $BuildDir)
Write-Host '== test'
foreach ($variable in 'SETUP_TEST_ISO', 'BF2_TEST_KEY', 'BF2SF_TEST_KEY') { Remove-Item "Env:$variable" -ErrorAction SilentlyContinue }
Run ctest @('--test-dir', $BuildDir, '--output-on-failure')
Write-Host '== installer'
$app = Join-Path $BuildDir 'app'
foreach ($file in 'OpenBF2.exe', 'Qt6Core.dll', 'Qt6Network.dll', 'msvcp140.dll', 'vcruntime140.dll', 'platforms\qwindows.dll', 'tls\qschannelbackend.dll',
    'setup\content-150.tsv', 'directx\DXSETUP.exe', 'x64\BF2.exe', 'x86\BF2.exe') {
  if (-not (Test-Path (Join-Path $app $file) -PathType Leaf)) { Fail "the application folder lacks $file" }
}
if ($Sign) {
  Write-Host '== sign the application'
  SignFiles (Programs $app)
}
Run cmake @('--build', $BuildDir, '--target', 'installer')
$built = Join-Path $BuildDir 'installer\openbf2-windows-setup.exe'
if (-not (Test-Path $built)) { Fail "the installer was not built: $built" }
if ($Sign) {
  # the installer build copies the client folders again: what it packed must still be signed
  $unsigned = @(Unsigned (@(Programs $app) + $built))
  if ($unsigned.Count -gt 0) { Fail "not validly signed: $($unsigned -join ', ')" }
  Write-Host "signature: $((Get-AuthenticodeSignature $built).SignerCertificate.Subject)"
}
Move-Item $built $Output
$hash = (Get-FileHash $Output -Algorithm SHA256).Hash.ToLower()
[IO.File]::WriteAllText("$Output.sha256", "$hash  $(Split-Path $Output -Leaf)`n")
Write-Host "$hash  $Output"
Write-Host "build-release: done ($Output)"
