$ErrorActionPreference = 'Stop'

$packageName = 'project-mind-mcp'
$installDir  = Join-Path $env:ChocolateyBinRoot $packageName

Uninstall-BinFile -Name 'project-mind-mcp'

if (Test-Path $installDir) {
  Remove-Item $installDir -Recurse -Force
}
