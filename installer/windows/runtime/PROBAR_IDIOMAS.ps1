[CmdletBinding()]
param([string]$DetectedCulture = '')
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $Root 'i18n.ps1')
Select-XzLanguage $DetectedCulture

Write-Host ''
Write-Host ('=' * 68) -ForegroundColor DarkCyan
Write-Host ('  ' + (T 'installer_title')) -ForegroundColor Cyan
Write-Host ('=' * 68) -ForegroundColor DarkCyan
Write-Host ''
Write-Host (T 'assisted_install')
Write-Host ''
Write-Host (T 'compatibility') -ForegroundColor White
Write-Host (T 'compat_base')
Write-Host ('[' + (T 'warning_prefix') + '] ' + (T 'warn_preserve_nvs')) -ForegroundColor Yellow
Write-Host ''
Write-Host (T 'title_verify_package')
Write-Host (T 'title_detect_board')
Write-Host (T 'title_backup')
Write-Host (T 'title_ready')
Write-Host (T 'title_installed') -ForegroundColor Green
Write-Host ''
Write-Host ('Language code: ' + $script:XzLang + ' | Windows UI: ' + $script:XzCulture) -ForegroundColor DarkGray
Write-Host ''
Write-Host 'OK - No ESP32/COM/Flash operation was performed.' -ForegroundColor Green
