[CmdletBinding()]
param(
    [string]$BuildDir = '',
    [string]$CareVersion = '0.3.2-alpha',
    [string]$Milestone = 'DP-045-experimental',
    [string]$SupportedBoardSku = 'bread-compact-wifi-lcd',
    [string[]]$BaseXiaoZhiVersions = @('2.5.0'),
    [bool]$EnableGraphics = $true,
    [string]$Python = 'python',
    [string]$OutputRoot = '',
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $Root '..\..')).Path

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RepoRoot 'build'
}

$RuntimeDir = Join-Path $Root 'runtime'
. (Join-Path $RuntimeDir 'activity.ps1')
$DocsDir = Join-Path $Root 'docs'
$OutRoot = Join-Path $Root 'SALIDA'
if (-not [string]::IsNullOrWhiteSpace($OutputRoot)) { $OutRoot = [IO.Path]::GetFullPath($OutputRoot) }
function Write-Ok([string]$Text) { Write-Host ('[OK]    ' + $Text) -ForegroundColor Green }
function Write-Warn([string]$Text) { Write-Host ('[AVISO] ' + $Text) -ForegroundColor Yellow }
function Fail([string]$Text, [int]$Code = 2) { Write-Host ('[ERROR] ' + $Text) -ForegroundColor Red; exit $Code }
function Sha([string]$Path) { return (Get-XzFileHash $Path 'Comprobando SHA-256').ToLowerInvariant() }
function Read-U32LE([byte[]]$Bytes, [int]$Offset) { return [BitConverter]::ToUInt32($Bytes, $Offset) }
function AsciiZ([byte[]]$Bytes, [int]$Offset, [int]$Length) {
    $end = $Offset; $lim = [Math]::Min($Bytes.Length, $Offset + $Length)
    while ($end -lt $lim -and $Bytes[$end] -ne 0) { $end++ }
    if ($end -le $Offset) { return '' }
    return [Text.Encoding]::ASCII.GetString($Bytes, $Offset, $end - $Offset)
}
function Contains-ExactAsciiZ([byte[]]$Bytes, [string]$Text) {
    [byte[]]$needle = [Text.Encoding]::ASCII.GetBytes($Text + [char]0)
    for ($i=0; $i -le $Bytes.Length-$needle.Length; $i++) {
        $ok=$true
        for ($j=0; $j -lt $needle.Length; $j++) { if ($Bytes[$i+$j] -ne $needle[$j]) { $ok=$false; break } }
        if ($ok) { return $true }
    }
    return $false
}
function App-Info([string]$Path) {
    [byte[]]$b=[IO.File]::ReadAllBytes($Path)
    if ($b.Length -lt 160 -or $b[0] -ne 0xE9) { return $null }
    $desc=32
    if ($b[$desc] -ne 0x32 -or $b[$desc+1] -ne 0x54 -or $b[$desc+2] -ne 0xCD -or $b[$desc+3] -ne 0xAB) { return $null }
    return [pscustomobject]@{
        Version = AsciiZ $b ($desc+16) 32
        Project = AsciiZ $b ($desc+48) 32
        Bytes = $b
    }
}
function Partition-Entries([string]$Path) {
    [byte[]]$b=[IO.File]::ReadAllBytes($Path); $out=@()
    for($o=0; $o+32 -le $b.Length -and $o -lt 0xC00; $o+=32) {
        if($b[$o] -eq 0xFF -and $b[$o+1] -eq 0xFF){break}
        if($b[$o] -ne 0xAA -or $b[$o+1] -ne 0x50){continue}
        $out += [pscustomobject]@{
            label=AsciiZ $b ($o+12) 16
            offset=[BitConverter]::ToUInt32($b,$o+4)
            size=[BitConverter]::ToUInt32($b,$o+8)
        }
    }
    return $out
}
function P([object[]]$e,[string]$l,[uint32]$o,[uint32]$s){
    $x=$e|Where-Object{$_.label -eq $l}|Select-Object -First 1
    return ($null -ne $x -and [uint32]$x.offset -eq $o -and [uint32]$x.size -eq $s)
}

Clear-Host
Write-Host '====================================================================' -ForegroundColor DarkCyan
Write-Host '  XIAOZHI CARE - CREAR INSTALADOR FINAL PARA USUARIO' -ForegroundColor Cyan
Write-Host '====================================================================' -ForegroundColor DarkCyan
Write-Host ''
Write-Host ('Build origen: ' + $BuildDir)
Write-Host ('Care: ' + $CareVersion + ' / ' + $Milestone)
Write-Host ('Perfil: ' + $SupportedBoardSku)
Write-Host ''

$files = [ordered]@{
    'bootloader.bin' = (Join-Path $BuildDir 'bootloader\bootloader.bin')
    'partition-table.bin' = (Join-Path $BuildDir 'partition_table\partition-table.bin')
    'ota_data_initial.bin' = (Join-Path $BuildDir 'ota_data_initial.bin')
    'xiaozhi.bin' = (Join-Path $BuildDir 'xiaozhi.bin')
    'generated_assets.bin' = (Join-Path $BuildDir 'generated_assets.bin')
}
foreach($k in $files.Keys){ if(-not(Test-Path -LiteralPath $files[$k])){Fail ('Falta ' + $files[$k]) 10} }
Write-Ok 'Estan presentes los cinco binarios necesarios.'


# V1.8 preflight: validate multilingual runtime before creating a distributable ZIP.
$i18nPath = Join-Path $RuntimeDir 'i18n.ps1'
if(-not(Test-Path -LiteralPath $i18nPath)){Fail 'Falta runtime\\i18n.ps1.' 18}
$parseTokens = $null
$parseErrors = $null
[System.Management.Automation.Language.Parser]::ParseFile($i18nPath, [ref]$parseTokens, [ref]$parseErrors) | Out-Null
if($parseErrors -and $parseErrors.Count -gt 0){
    $detail = ($parseErrors | ForEach-Object { $_.Message }) -join ' | '
    Fail ('i18n.ps1 contiene un error de sintaxis: ' + $detail) 19
}
Write-Ok 'Archivo multidioma i18n.ps1 validado.'
foreach ($scriptPath in @(
    (Join-Path $RuntimeDir 'installer.ps1'),
    (Join-Path $RuntimeDir 'graphics.ps1'),
    (Join-Path $RuntimeDir 'restaurar.ps1'),
    (Join-Path $RuntimeDir 'restore-core.ps1'))) {
    if (-not (Test-Path -LiteralPath $scriptPath)) { Fail ('Falta ' + $scriptPath) 18 }
    $parseErrors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$parseTokens, [ref]$parseErrors) | Out-Null
    if (@($parseErrors).Count -gt 0) { Fail ('Error de sintaxis en ' + $scriptPath) 19 }
}

Get-ChildItem -LiteralPath $RuntimeDir -Filter '*.bat' -File | ForEach-Object {
    [byte[]]$batBytes = [IO.File]::ReadAllBytes($_.FullName)
    if($batBytes.Length -ge 3 -and $batBytes[0] -eq 0xEF -and $batBytes[1] -eq 0xBB -and $batBytes[2] -eq 0xBF){
        Fail ('El BAT ' + $_.Name + ' contiene UTF-8 BOM y no es compatible con cmd.exe.') 20
    }
}
Write-Ok 'Archivos BAT del runtime validados sin UTF-8 BOM.'

$app=App-Info $files['xiaozhi.bin']
if($null -eq $app){Fail 'xiaozhi.bin no tiene un encabezado ESP-IDF valido.' 11}
if($app.Project -ne 'xiaozhi'){Fail ('Project name inesperado: ' + $app.Project) 12}
if($BaseXiaoZhiVersions -notcontains $app.Version){Fail ('Version de app inesperada: ' + $app.Version) 13}
if(-not(Contains-ExactAsciiZ $app.Bytes $SupportedBoardSku)){Fail ('xiaozhi.bin no contiene el perfil exacto ' + $SupportedBoardSku) 14}
Write-Ok ('Aplicacion validada: xiaozhi ' + $app.Version + ' / ' + $SupportedBoardSku)

if((Get-Item $files['xiaozhi.bin']).Length -gt 0x3F0000){Fail 'xiaozhi.bin excede la particion OTA de 0x3F0000.' 15}
if((Get-Item $files['generated_assets.bin']).Length -gt 0x600000){Fail 'generated_assets.bin excede la particion assets de XiaoZhi Care (6 MB).' 16}

$parts=@(Partition-Entries $files['partition-table.bin'])
$careLayout =
    (P $parts 'nvs'       0x00009000 0x00004000) -and
    (P $parts 'otadata'   0x0000D000 0x00002000) -and
    (P $parts 'phy_init'  0x0000F000 0x00001000) -and
    (P $parts 'ota_0'     0x00020000 0x003F0000) -and
    (P $parts 'ota_1'     0x00410000 0x003F0000) -and
    (P $parts 'assets'    0x00800000 0x00600000) -and
    (P $parts 'care_data' 0x00E00000 0x00100000) -and
    (P $parts 'voice'     0x00F00000 0x00100000)
if(-not $careLayout){Fail 'La partition-table.bin no coincide con el layout validado de XiaoZhi Care.' 17}
Write-Ok 'Tabla XiaoZhi Care validada.'

if ($EnableGraphics) {
    if ($SupportedBoardSku -ne 'bread-compact-wifi-lcd') { Fail 'DP-045 requiere bread-compact-wifi-lcd.' 21 }
    if (-not (Contains-ExactAsciiZ $app.Bytes 'care_graphic_theme_v1')) { Fail 'Recompilar: el firmware no incluye soporte de tema DP-045.' 22 }
    $buildConfig = Join-Path $BuildDir 'config\sdkconfig.json'
    if (-not (Test-Path -LiteralPath $buildConfig)) { Fail 'Falta configuracion del build para verificar pantalla.' 23 }
    $config = Get-Content -LiteralPath $buildConfig -Raw | ConvertFrom-Json
    if ($config.IDF_TARGET -ne 'esp32s3' -or $config.ESPTOOLPY_FLASHSIZE -ne '16MB' -or
        -not $config.SPIRAM_MODE_OCT -or -not $config.LV_FONT_FMT_TXT_LARGE -or -not $config.BOARD_TYPE_BREAD_COMPACT_WIFI_LCD -or
        (-not $config.LCD_ST7789_240X240 -and -not $config.LCD_ST7789_240X240_7PIN)) {
        Fail 'El build no corresponde a ESP32-S3 ST7789 240x240.' 23
    }
    $pinsPath = Join-Path $Root 'v2\graphic_sources.json'
    $pins = Get-Content -LiteralPath $pinsPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ((Sha (Join-Path $RepoRoot 'assets.bin')) -cne $pins.source_sha256) { Fail 'El origen grafico cambio; revisar antes de empaquetar.' 24 }
}
if ($ValidateOnly) { Write-Ok 'Prevalidacion sin generar ZIP ni acceder al dispositivo.'; exit 0 }

$stamp=Get-Date -Format 'yyyyMMdd-HHmmss-fff'
if ($CareVersion -notmatch '^[A-Za-z0-9._-]+$' -or $SupportedBoardSku -notmatch '^[A-Za-z0-9_-]+$') { Fail 'Nombre de version o perfil invalido.' 25 }
$FolderName='XiaoZhi_Care_' + $CareVersion + '_Installer_' + $SupportedBoardSku + '_' + $stamp
$OutDir=Join-Path $OutRoot $FolderName
if(Test-Path -LiteralPath $OutDir){Fail 'La salida ya existe. Se conserva sin sobrescribir.' 26}
New-Item -ItemType Directory -Path $OutDir | Out-Null
New-Item -ItemType Directory -Path (Join-Path $OutDir 'firmware') | Out-Null
New-Item -ItemType Directory -Path (Join-Path $OutDir 'tools') | Out-Null
New-Item -ItemType Directory -Path (Join-Path $OutDir 'backup') | Out-Null

Copy-Item -LiteralPath (Join-Path $RuntimeDir 'INSTALAR_XIAOZHI_CARE.bat') -Destination $OutDir
Copy-Item -LiteralPath (Join-Path $RuntimeDir 'installer.ps1') -Destination $OutDir
Copy-Item -LiteralPath (Join-Path $RuntimeDir 'i18n.ps1') -Destination $OutDir
Copy-Item -LiteralPath (Join-Path $RuntimeDir 'graphics.ps1') -Destination $OutDir
Copy-Item -LiteralPath (Join-Path $RuntimeDir 'PREVISUALIZAR_GRAFICOS.bat') -Destination $OutDir
Copy-Item -LiteralPath (Join-Path $RuntimeDir 'restaurar.ps1') -Destination $OutDir
Copy-Item -LiteralPath (Join-Path $RuntimeDir 'restore-core.ps1') -Destination $OutDir
Copy-Item -LiteralPath (Join-Path $RuntimeDir 'activity.ps1') -Destination $OutDir
Copy-Item -LiteralPath (Join-Path $RuntimeDir 'VERIFICAR_RESPALDOS.bat') -Destination $OutDir
Copy-Item -LiteralPath (Join-Path $DocsDir 'GUIA_DP045D.txt') -Destination $OutDir
Copy-Item -LiteralPath (Join-Path $Root 'LEEME_DP045_INTEGRACION.md') -Destination $OutDir
foreach($k in $files.Keys){
    Copy-XzFileWithActivity $files[$k] (Join-Path $OutDir ('firmware\'+$k)) ('Copiando firmware: ' + $k)
}
if ($EnableGraphics) {
    & $Python (Join-Path $Root 'v2\graphic_assets.py') --base $files['generated_assets.bin'] --graphics (Join-Path $RepoRoot 'assets.bin') --app $files['xiaozhi.bin'] --pins $pinsPath --output (Join-Path $OutDir 'graphics')
    if ($LASTEXITCODE -ne 0) { Fail 'No se generaron recursos graficos completos; no se crea ZIP.' 27 }
}
if(Test-Path (Join-Path $DocsDir 'INSTRUCCIONES_USUARIO.txt')){Copy-Item (Join-Path $DocsDir 'INSTRUCCIONES_USUARIO.txt') $OutDir}
if(Test-Path (Join-Path $DocsDir 'Guia_Instalacion_XiaoZhi_Care_V1.docx')){Copy-Item (Join-Path $DocsDir 'Guia_Instalacion_XiaoZhi_Care_V1.docx') $OutDir}

$fw=@()
foreach($k in $files.Keys){
    $dest=Join-Path $OutDir ('firmware\'+$k)
    $fw += [ordered]@{file=$k;sha256=Sha $dest;size=(Get-Item $dest).Length}
}
$release=[ordered]@{
    schema='xiaozhi-care-installer-release'
    schema_version=1
    care_version=$CareVersion
    milestone=$Milestone
    project_name='xiaozhi'
    base_xiaozhi_versions=$BaseXiaoZhiVersions
    supported_chip='ESP32-S3'
    required_flash_mb=16
    required_psram_mb=8
    supported_board_sku=$SupportedBoardSku
    interface_languages=@('es','en','pt')
    language_detection='windows-ui-culture-with-manual-selection'
    install_strategy='preserve-nvs-migrate-xiaozhi-v2-partitions'
    created_at=(Get-Date).ToString('o')
    firmware=$fw
}
$release | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutDir 'release.json') -Encoding UTF8
if ($EnableGraphics) {
    $release.graphics = [ordered]@{catalog='graphics/graphics.json'; sha256=Sha (Join-Path $OutDir 'graphics\graphics.json')}
    . (Join-Path $RuntimeDir 'graphics.ps1')
    Test-CareGraphicsPackage $OutDir ([pscustomobject]$release) | Out-Null
    $release | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutDir 'release.json') -Encoding UTF8
}

$hashLines=@()
Get-ChildItem (Join-Path $OutDir 'firmware') -File | Sort-Object Name | ForEach-Object {
    $hashLines += ((Sha $_.FullName) + '  firmware/' + $_.Name)
}
$hashLines | Set-Content -LiteralPath (Join-Path $OutDir 'SHA256SUMS.txt') -Encoding ASCII
if ($EnableGraphics) {
    Get-ChildItem -LiteralPath (Join-Path $OutDir 'graphics') -File | Sort-Object Name | ForEach-Object {
        ((Sha $_.FullName) + '  graphics/' + $_.Name) | Add-Content -LiteralPath (Join-Path $OutDir 'SHA256SUMS.txt') -Encoding ASCII
    }
}

$zip=Join-Path $OutRoot ($FolderName + '.zip')
if(Test-Path -LiteralPath $zip){Fail 'El ZIP ya existe. Se conserva sin sobrescribir.' 26}
Compress-XzDirectoryWithActivity $OutDir $zip 'Comprimiendo instalador'

Write-Host ''
Write-Host '====================================================================' -ForegroundColor DarkCyan
Write-Host '  INSTALADOR FINAL CREADO' -ForegroundColor Green
Write-Host '====================================================================' -ForegroundColor DarkCyan
Write-Ok ('Carpeta: ' + $OutDir)
Write-Ok ('ZIP para distribuir: ' + $zip)
Write-Host ''
Write-Warn 'Antes de publicarlo, probar el ZIP en una ESP32-S3 N16R8 de prueba con XiaoZhi 2.5.0 funcional.'
exit 0
