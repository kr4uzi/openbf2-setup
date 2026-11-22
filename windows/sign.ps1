# Authenticode signing of the given files with SHA-256 and an RFC 3161 timestamp, for windows\build-release.ps1 -Sign
# and Inno Setup's SignTool hook (sign.cmd). One of two certificate sources:
#
#   Azure Artifact Signing: SETUP_SIGN_DLIB (Azure.CodeSigning.Dlib.dll of Microsoft.ArtifactSigning.Client, the
#     architecture of signtool) and SETUP_SIGN_METADATA (its metadata.json: Endpoint, CodeSigningAccountName,
#     CertificateProfileName). The Azure login comes from the environment (in CI: azure/login).
#   A certificate in the user's store: SETUP_SIGN_CERT_SHA1, its thumbprint (a cloud certificate such as Certum
#     SimplySign, a hardware token, or a local test).
#
# SETUP_SIGNTOOL is signtool.exe; without it, signtool must be on PATH. SETUP_SIGN_TIMESTAMP overrides the timestamp
# server (Microsoft's for Artifact Signing); SETUP_SIGN_TIMESTAMP=none signs without one (local tests only).
param([Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Files)
$ErrorActionPreference = 'Stop'
function Fail([string]$message) { [Console]::Error.WriteLine("sign: $message"); exit 1 }

$signtool = if ($env:SETUP_SIGNTOOL) { $env:SETUP_SIGNTOOL } else { (Get-Command signtool.exe -ErrorAction SilentlyContinue).Source }
if (-not $signtool -or -not (Test-Path $signtool -PathType Leaf)) { Fail 'signtool.exe not found (SETUP_SIGNTOOL)' }
$arguments = @('sign', '/fd', 'SHA256')
if ($env:SETUP_SIGN_DLIB) {
  if (-not (Test-Path $env:SETUP_SIGN_DLIB -PathType Leaf)) { Fail "SETUP_SIGN_DLIB does not exist: $env:SETUP_SIGN_DLIB" }
  if (-not $env:SETUP_SIGN_METADATA -or -not (Test-Path $env:SETUP_SIGN_METADATA -PathType Leaf)) { Fail 'SETUP_SIGN_METADATA (metadata.json) is missing' }
  $arguments += @('/dlib', $env:SETUP_SIGN_DLIB, '/dmdf', $env:SETUP_SIGN_METADATA)
  $timestamp = 'http://timestamp.acs.microsoft.com'
} elseif ($env:SETUP_SIGN_CERT_SHA1) {
  $arguments += @('/sha1', $env:SETUP_SIGN_CERT_SHA1)
  $timestamp = 'http://timestamp.digicert.com'
} else {
  Fail 'no certificate: set SETUP_SIGN_DLIB and SETUP_SIGN_METADATA, or SETUP_SIGN_CERT_SHA1'
}
if ($env:SETUP_SIGN_TIMESTAMP) { $timestamp = $env:SETUP_SIGN_TIMESTAMP }
if ($timestamp -ne 'none') { $arguments += @('/tr', $timestamp, '/td', 'SHA256') }

# an array even for one file: splatting a single string would pass its characters
$paths = @(foreach ($file in $Files) {
  if (-not (Test-Path $file -PathType Leaf)) { Fail "no such file: $file" }
  (Resolve-Path $file).Path
})
# the signing service and the timestamp server are remote: a failed attempt is retried twice
for ($attempt = 1; ; ++$attempt) {
  & $signtool @arguments @paths
  if ($LASTEXITCODE -eq 0) { break }
  if ($attempt -ge 3) { Fail "signtool exited with $LASTEXITCODE" }
  Start-Sleep -Seconds (10 * $attempt)
}
