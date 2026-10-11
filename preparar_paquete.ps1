# Canonical implementation: installer/windows/preparar_paquete.ps1.
# Forward every named/positional argument; keep old shortcuts working.
& (Join-Path $PSScriptRoot 'installer\windows\preparar_paquete.ps1') @args
exit $LASTEXITCODE
