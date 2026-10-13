# XiaoZhi Care DP-045D - Etapa 1
# SOLO LECTURA. No ejecuta esptool ni escribe en la ESP32.

param(
    [string]$DetectedCulture = ''
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$I18nPath = Join-Path $Root 'i18n.ps1'

if (-not (Test-Path -LiteralPath $I18nPath)) {
    throw 'Falta i18n.ps1.'
}

. $I18nPath
Select-XzLanguage $DetectedCulture

# DP-045D: control de errores
try {
$Local = [Environment]::GetFolderPath('LocalApplicationData')
if ([string]::IsNullOrWhiteSpace($Local)) {
    throw (T 'd45_local_missing')
}

$Base = Join-Path $Local 'XiaoZhi Care\Respaldos'
if (-not (Test-Path -LiteralPath $Base)) {
    throw ((T 'backup_root_missing') -f $Base)
}

$Copias = @(Get-ChildItem -LiteralPath $Base -Directory |
    Where-Object { $_.Name -match '^antes-xiaozhi-care-\d{8}-\d{6}$' } |
    Sort-Object Name -Descending)

if ($Copias.Count -eq 0) {
    throw (T 'backup_none')
}

Write-Host ''
Write-Host ('=== ' + (T 'd45_available') + ' ===') -ForegroundColor Cyan

for ($i = 0; $i -lt $Copias.Count; $i++) {
    Write-Host ('[{0}] {1}' -f ($i + 1), $Copias[$i].Name)
}

Write-Host ''
$Eleccion = Read-Host (T 'd45_choose')

if ([string]::IsNullOrWhiteSpace($Eleccion)) {
    Write-Host ('[' + (T 'warning_prefix') + '] ' + (T 'd45_cancelled')) -ForegroundColor Yellow
    exit 0
}

$Numero = 0

if (-not [int]::TryParse($Eleccion, [ref]$Numero)) {
    throw (T 'd45_invalid')
}

if ($Numero -lt 1 -or $Numero -gt $Copias.Count) {
    throw (T 'd45_out_of_range')
}

$Carpeta = $Copias[$Numero - 1].FullName
$Bin = Join-Path $Carpeta 'flash-completa-16MB.bin'
$Sha = Join-Path $Carpeta 'flash-completa-16MB.sha256.txt'

Write-Host ('=== ' + (T 'd45_verify_title') + ' ===') -ForegroundColor Cyan
Write-Host ((T 'backup_found') -f $Carpeta)

if (-not (Test-Path -LiteralPath $Bin)) {
    throw ((T 'backup_file_missing') -f $Bin)
}

if (-not (Test-Path -LiteralPath $Sha)) {
    throw (T 'sha_missing')
}

if ((Get-Item -LiteralPath $Bin).Length -ne 16777216) {
    throw ((T 'backup_size_bad') -f (Get-Item -LiteralPath $Bin).Length)
}

Write-Host ('[OK] ' + (T 'backup_size_ok')) -ForegroundColor Green

$Real = (Get-FileHash -LiteralPath $Bin -Algorithm SHA256).Hash
$Registro = Get-Content -LiteralPath $Sha -Raw
$Match = [regex]::Match($Registro, '(?i)\b[a-f0-9]{64}\b')

if (-not $Match.Success) {
    throw (T 'sha_format_bad')
}

if ($Real -ne $Match.Value.ToUpperInvariant()) {
    throw (T 'sha_mismatch')
}

Write-Host ('[OK] ' + ((T 'sha_ok') -f $Real)) -ForegroundColor Green
Write-Host ('[OK] ' + (T 'd45_done')) -ForegroundColor Green
} catch {
    Write-Host ''
    Write-Host ('[ERROR] ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
