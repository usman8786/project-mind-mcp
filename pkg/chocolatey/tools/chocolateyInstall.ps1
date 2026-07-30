$ErrorActionPreference = 'Stop'

$packageName = 'project-mind-mcp'
$version     = '0.8.1'
$url64       = "https://github.com/usman8786/project-mind-mcp/releases/download/v${version}/project-mind-mcp-windows-amd64.zip"
$checksum64  = 'a602ad090ed3f49d86c55472f73f27ad7055222806a82358f2e08513e027f00f'
$installDir  = Join-Path $env:ChocolateyBinRoot $packageName

Install-ChocolateyZipPackage `
  -PackageName   $packageName `
  -Url64bit      $url64 `
  -Checksum64    $checksum64 `
  -ChecksumType64 'sha256' `
  -UnzipLocation $installDir

# Shim the binary so it is on PATH
$binPath = Join-Path $installDir 'project-mind-mcp.exe'
Install-BinFile -Name 'project-mind-mcp' -Path $binPath

# Configure coding agents (non-fatal)
try {
  & $binPath install -y 2>&1 | Out-Null
} catch {
  Write-Warning "Agent configuration failed (non-fatal). Run manually: project-mind-mcp install"
}
