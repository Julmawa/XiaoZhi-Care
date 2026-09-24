[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$FirmwareDir = Join-Path $Root 'firmware'
$ToolsDir = Join-Path $Root 'tools'
$BackupRoot = Join-Path $Root 'backup'
$ReleasePath = Join-Path $Root 'release.json'

$EsptoolVersion = '5.3.1'
$EsptoolZipUrl = 'https://github.com/espressif/esptool/releases/download/v5.3.1/esptool-v5.3.1-windows-amd64.zip'
$EsptoolZipSha256 = '2b4a73c45db27426685896f64ce3e557f63a64f43cc100cb65c0cc3486af96d3'

function Write-Title([string]$Text) {
    Write-Host ''
    Write-Host ('=' * 68) -ForegroundColor DarkCyan
    Write-Host ('  ' + $Text) -ForegroundColor Cyan
    Write-Host ('=' * 68) -ForegroundColor DarkCyan
}

function Write-Ok([string]$Text) { Write-Host ('[OK]    ' + $Text) -ForegroundColor Green }
function Write-Warn([string]$Text) { Write-Host ('[AVISO] ' + $Text) -ForegroundColor Yellow }
function Write-Fail([string]$Text) { Write-Host ('[ERROR] ' + $Text) -ForegroundColor Red }

function Stop-Safe([string]$Message, [int]$Code = 2) {
    Write-Host ''
    Write-Fail $Message
    Write-Host ''
    Write-Host 'No se realizo ninguna escritura adicional en la placa.' -ForegroundColor Yellow
    exit $Code
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-NullTerminatedAscii([byte[]]$Bytes, [int]$Offset, [int]$Length) {
    if ($Offset -lt 0 -or ($Offset + $Length) -gt $Bytes.Length) { return '' }
    $end = $Offset
    $limit = $Offset + $Length
    while ($end -lt $limit -and $Bytes[$end] -ne 0) { $end++ }
    if ($end -le $Offset) { return '' }
    return [System.Text.Encoding]::ASCII.GetString($Bytes, $Offset, $end - $Offset)
}

function Read-U16LE([byte[]]$Bytes, [int]$Offset) {
    return [uint16]($Bytes[$Offset] -bor ($Bytes[$Offset + 1] -shl 8))
}

function Read-U32LE([byte[]]$Bytes, [int]$Offset) {
    if ($Offset -lt 0 -or ($Offset + 4) -gt $Bytes.Length) { throw 'Lectura U32 fuera de rango.' }
    return [BitConverter]::ToUInt32($Bytes, $Offset)
}

function Get-EspImageDataLength([byte[]]$Flash, [int]$AppOffset) {
    # Devuelve hasta el final del ultimo segmento ESP-IDF. Limita la busqueda
    # del SKU al contenido real de la imagen y evita falsos positivos por
    # restos de una imagen OTA anterior en sectores no sobrescritos.
    if (($AppOffset + 24) -gt $Flash.Length) { return 0 }
    if ($Flash[$AppOffset] -ne 0xE9) { return 0 }
    $segmentCount = [int]$Flash[$AppOffset + 1]
    if ($segmentCount -lt 1 -or $segmentCount -gt 16) { return 0 }
    $pos = $AppOffset + 24
    for ($i = 0; $i -lt $segmentCount; $i++) {
        if (($pos + 8) -gt $Flash.Length) { return 0 }
        $dataLen = [uint32](Read-U32LE $Flash ($pos + 4))
        $pos += 8
        if ($dataLen -gt 0x00400000) { return 0 }
        if (($pos + [int64]$dataLen) -gt $Flash.Length) { return 0 }
        $pos += [int]$dataLen
    }
    return ($pos - $AppOffset)
}

function Get-AppDescriptionFromFlash([byte[]]$Flash, [int]$AppOffset) {
    $result = [ordered]@{ valid = $false; offset = $AppOffset; image_length = 0; version = ''; project = '' }
    if (($AppOffset + 256) -gt $Flash.Length) { return [pscustomobject]$result }
    if ($Flash[$AppOffset] -ne 0xE9) { return [pscustomobject]$result }

    # esp_app_desc_t starts at sizeof(esp_image_header_t=24) + sizeof(segment_header=8) = 32.
    $desc = $AppOffset + 32
    # ESP_APP_DESC_MAGIC_WORD = 0xABCD5432. Se compara byte a byte para
    # evitar la interpretacion con signo de literales hexadecimales en Windows PowerShell 5.1.
    if ($Flash[$desc] -ne 0x32 -or $Flash[$desc+1] -ne 0x54 -or
        $Flash[$desc+2] -ne 0xCD -or $Flash[$desc+3] -ne 0xAB) {
        return [pscustomobject]$result
    }

    # magic(4) + secure_version(4) + reserv1[2](8) = 16 bytes.
    $result.version = Get-NullTerminatedAscii $Flash ($desc + 16) 32
    $result.project = Get-NullTerminatedAscii $Flash ($desc + 48) 32
    $result.image_length = Get-EspImageDataLength $Flash $AppOffset
    if ($result.image_length -le 0 -or $result.image_length -gt 0x003F0000) { return [pscustomobject]$result }
    $result.valid = $true
    return [pscustomobject]$result
}

function Test-ByteSequence([byte[]]$Bytes, [int]$Start, [int]$Count, [byte[]]$Needle) {
    if ($Needle.Length -eq 0 -or $Count -lt $Needle.Length) { return $false }
    $last = $Start + $Count - $Needle.Length
    for ($i = $Start; $i -le $last; $i++) {
        $ok = $true
        for ($j = 0; $j -lt $Needle.Length; $j++) {
            if ($Bytes[$i + $j] -ne $Needle[$j]) { $ok = $false; break }
        }
        if ($ok) { return $true }
    }
    return $false
}

function Test-AppContainsSku([byte[]]$Flash, [int]$Offset, [int]$Size, [string]$Sku) {
    if (($Offset + $Size) -gt $Flash.Length) { return $false }
    # NUL-terminated exact runtime SKU string, avoids accepting a longer profile by prefix.
    $needle = [System.Text.Encoding]::ASCII.GetBytes($Sku + [char]0)
    return Test-ByteSequence $Flash $Offset $Size $needle
}

function Get-PartitionTableFromFlash([byte[]]$Flash) {
    $items = @()
    $base = 0x8000
    $max = 0xC00
    for ($pos = 0; $pos -lt $max; $pos += 32) {
        $o = $base + $pos
        if (($o + 32) -gt $Flash.Length) { break }
        if ($Flash[$o] -eq 0xFF -and $Flash[$o + 1] -eq 0xFF) { break }
        if ($Flash[$o] -ne 0xAA -or $Flash[$o + 1] -ne 0x50) { continue }
        $type = $Flash[$o + 2]
        $subtype = $Flash[$o + 3]
        $offset = Read-U32LE $Flash ($o + 4)
        $size = Read-U32LE $Flash ($o + 8)
        $label = Get-NullTerminatedAscii $Flash ($o + 12) 16
        $flags = Read-U32LE $Flash ($o + 28)
        $items += [pscustomobject]@{
            label = $label; type = $type; subtype = $subtype;
            offset = [uint32]$offset; size = [uint32]$size; flags = [uint32]$flags
        }
    }
    return ,$items
}

function Get-Partition([object[]]$Items, [string]$Label) {
    return $Items | Where-Object { $_.label -eq $Label } | Select-Object -First 1
}

function Test-Partition([object[]]$Items, [string]$Label, [uint32]$Offset, [uint32]$Size) {
    $p = Get-Partition $Items $Label
    if ($null -eq $p) { return $false }
    return ([uint32]$p.offset -eq $Offset -and [uint32]$p.size -eq $Size)
}

function Get-LayoutKind([object[]]$Items) {
    $common =
        (Test-Partition $Items 'nvs'      0x00009000 0x00004000) -and
        (Test-Partition $Items 'otadata'  0x0000D000 0x00002000) -and
        (Test-Partition $Items 'phy_init' 0x0000F000 0x00001000) -and
        (Test-Partition $Items 'ota_0'    0x00020000 0x003F0000) -and
        (Test-Partition $Items 'ota_1'    0x00410000 0x003F0000)
    if (-not $common) { return 'unsupported' }

    $baseV2 = (Test-Partition $Items 'assets' 0x00800000 0x00800000) -and
              ($null -eq (Get-Partition $Items 'care_data')) -and
              ($null -eq (Get-Partition $Items 'voice'))
    if ($baseV2) { return 'xiaozhi-v2-16m' }

    $care = (Test-Partition $Items 'assets'    0x00800000 0x00600000) -and
            (Test-Partition $Items 'care_data' 0x00E00000 0x00100000) -and
            (Test-Partition $Items 'voice'     0x00F00000 0x00100000)
    if ($care) { return 'xiaozhi-care-16m' }

    return 'unsupported'
}

function Invoke-Esptool([string[]]$Arguments, [switch]$AllowFailure) {
    $lines = & $script:Esptool @Arguments 2>&1
    $code = $LASTEXITCODE
    $text = ($lines | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
    if ($code -ne 0 -and -not $AllowFailure) {
        Write-Host $text
        throw "esptool termino con codigo $code"
    }
    return [pscustomobject]@{ ExitCode = $code; Text = $text }
}

function Ensure-Esptool {
    New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null
    $existing = Get-ChildItem -Path $ToolsDir -Filter 'esptool.exe' -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $existing) {
        Write-Ok ('Grabador encontrado: ' + $existing.FullName)
        return $existing.FullName
    }

    Write-Host 'No se encontro el grabador. Se descargara esptool oficial de Espressif.' -ForegroundColor Cyan
    $zip = Join-Path $ToolsDir ('esptool-v' + $EsptoolVersion + '-windows-amd64.zip')
    try {
        Invoke-WebRequest -UseBasicParsing -Uri $EsptoolZipUrl -OutFile $zip
    } catch {
        Stop-Safe ('No se pudo descargar esptool. Verifique Internet. Detalle: ' + $_.Exception.Message) 10
    }
    $got = Get-Sha256 $zip
    if ($got -ne $EsptoolZipSha256) {
        Remove-Item -Force $zip -ErrorAction SilentlyContinue
        Stop-Safe 'La firma SHA-256 del grabador descargado no coincide. Se cancela por seguridad.' 11
    }
    Write-Ok 'Descarga de esptool verificada por SHA-256.'

    $dest = Join-Path $ToolsDir ('esptool-v' + $EsptoolVersion)
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    Expand-Archive -LiteralPath $zip -DestinationPath $dest -Force
    $exe = Get-ChildItem -Path $dest -Filter 'esptool.exe' -File -Recurse | Select-Object -First 1
    if ($null -eq $exe) { Stop-Safe 'El paquete de esptool no contiene esptool.exe.' 12 }
    return $exe.FullName
}

function Get-ComPorts {
    $names = @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
    $descriptions = @{}
    try {
        Get-CimInstance Win32_SerialPort -ErrorAction Stop | ForEach-Object {
            $descriptions[$_.DeviceID] = $_.Name
        }
    } catch { }
    $out = @()
    foreach ($n in $names) {
        $d = $n
        if ($descriptions.ContainsKey($n)) { $d = $descriptions[$n] }
        $out += [pscustomobject]@{ Port = $n; Description = $d }
    }
    return ,$out
}

function Select-ComPort {
    $ports = @(Get-ComPorts)
    if ($ports.Count -eq 0) { Stop-Safe 'No se encontro ningun puerto COM. Conecte la ESP32-S3 por USB.' 20 }
    if ($ports.Count -eq 1) {
        Write-Ok ('Puerto detectado: ' + $ports[0].Description)
        return $ports[0].Port
    }
    Write-Host 'Se encontraron varios puertos:'
    for ($i = 0; $i -lt $ports.Count; $i++) {
        Write-Host ('  [{0}] {1}' -f ($i + 1), $ports[$i].Description)
    }
    while ($true) {
        $answer = Read-Host 'Elija el numero correspondiente a la ESP32-S3'
        $n = 0
        if ([int]::TryParse($answer, [ref]$n) -and $n -ge 1 -and $n -le $ports.Count) {
            return $ports[$n - 1].Port
        }
        Write-Warn 'Opcion no valida.'
    }
}

function Confirm-Yes([string]$Question) {
    $a = Read-Host ($Question + ' [S/N]')
    return ($a.Trim().ToUpperInvariant() -eq 'S')
}

function Verify-ReleaseFiles($Release) {
    if ($null -eq $Release.firmware) { Stop-Safe 'release.json no contiene la lista firmware.' 30 }
    foreach ($f in $Release.firmware) {
        $path = Join-Path $FirmwareDir ([string]$f.file)
        if (-not (Test-Path -LiteralPath $path)) { Stop-Safe ('Falta el archivo de firmware: ' + $f.file) 31 }
        $hash = Get-Sha256 $path
        if ($hash -ne ([string]$f.sha256).ToLowerInvariant()) {
            Stop-Safe ('SHA-256 incorrecto en ' + $f.file + '. El paquete puede estar dañado.') 32
        }
    }
    Write-Ok 'Integridad del paquete de firmware verificada.'
}

function Find-FirmwareEntry($Release, [string]$Name) {
    return $Release.firmware | Where-Object { $_.file -eq $Name } | Select-Object -First 1
}

function Write-InstallLog([string]$BackupDir, [string]$Text) {
    $log = Join-Path $BackupDir 'instalacion.log'
    Add-Content -LiteralPath $log -Value ((Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' ' + $Text) -Encoding UTF8
}

# ---------------------------------------------------------------------------
# Inicio
# ---------------------------------------------------------------------------
Clear-Host
Write-Title 'XIAOZHI CARE - INSTALADOR V1'
Write-Host 'Instalacion asistida para usuarios sin entorno de desarrollo.'
Write-Host ''
Write-Host 'COMPATIBILIDAD DE ESTA VERSION' -ForegroundColor White
Write-Host '  - ESP32-S3 N16R8'
Write-Host '  - 16 MB Flash'
Write-Host '  - 8 MB PSRAM'
Write-Host '  - XiaoZhi previamente instalado y funcionando'
Write-Host '  - Perfil soportado por esta release (se verificara antes de grabar)'
Write-Host ''
Write-Warn 'Este instalador NO borra toda la memoria y conserva la NVS de XiaoZhi.'
Write-Warn 'Antes de modificar la placa crea un respaldo completo de 16 MB.'
Write-Host ''
if (-not (Confirm-Yes '¿Desea comenzar la verificacion?')) { exit 0 }

if (-not (Test-Path -LiteralPath $ReleasePath)) {
    Stop-Safe 'Falta release.json. Este paquete todavia no fue preparado para distribucion.' 40
}
$Release = Get-Content -LiteralPath $ReleasePath -Raw -Encoding UTF8 | ConvertFrom-Json

Write-Title '1. VERIFICANDO PAQUETE'
Write-Host ('XiaoZhi Care: ' + $Release.care_version)
Write-Host ('Base XiaoZhi compatible: ' + (($Release.base_xiaozhi_versions -join ', ')))
Write-Host ('Perfil: ' + $Release.supported_board_sku)
Verify-ReleaseFiles $Release

$script:Esptool = Ensure-Esptool
Write-Ok ('esptool v' + $EsptoolVersion + ' listo.')

Write-Title '2. DETECTANDO PLACA'
$Port = Select-ComPort
Write-Host ('Conectando a ' + $Port + ' ...')

try {
    $chip = Invoke-Esptool -Arguments @('--port', $Port, 'chip-id')
} catch {
    Stop-Safe ('No se pudo comunicar con la placa en ' + $Port + '. Mantenga BOOT presionado al conectar si fuera necesario.') 50
}
if ($chip.Text -notmatch 'ESP32-S3') { Stop-Safe 'La placa conectada NO es una ESP32-S3. Esta release no es compatible.' 51 }
Write-Ok 'Chip: ESP32-S3.'

if ($chip.Text -notmatch '(?i)Embedded\s+PSRAM\s+8MB|PSRAM\s+8MB') {
    Write-Host $chip.Text -ForegroundColor DarkGray
    Stop-Safe 'No se pudo confirmar PSRAM integrada de 8 MB (R8). Esta V1 exige ESP32-S3 N16R8.' 52
}
Write-Ok 'PSRAM: 8 MB detectada.'

$flash = Invoke-Esptool -Arguments @('--port', $Port, 'flash-id')
if ($flash.Text -notmatch '(?i)Detected\s+flash\s+size:\s*16\s*MB|flash\s+size[^\r\n]*16\s*MB') {
    Write-Host $flash.Text -ForegroundColor DarkGray
    Stop-Safe 'La Flash no fue identificada como 16 MB.' 53
}
Write-Ok 'Flash: 16 MB detectada.'

$sec = Invoke-Esptool -Arguments @('--port', $Port, '--no-stub', 'get_security_info') -AllowFailure
if ($sec.ExitCode -ne 0) { Stop-Safe 'No se pudo consultar el estado de seguridad de la ESP32-S3.' 54 }
if ($sec.Text -notmatch '(?i)Secure\s*Boot:\s*Disabled' -or
    $sec.Text -notmatch '(?i)Flash\s*Encryption:\s*Disabled') {
    Write-Host $sec.Text -ForegroundColor DarkGray
    Stop-Safe 'No se pudo confirmar Secure Boot y Flash Encryption desactivados. La V1 solo instala en placas sin proteccion de Flash.' 55
}
Write-Ok 'Secure Boot: desactivado. Flash Encryption: desactivado.'

Write-Title '3. COMPROBACION DEL XIAOZHI EXISTENTE'
Write-Host 'Antes de continuar, el XiaoZhi actual debe funcionar correctamente.'
Write-Host 'La configuracion de display, microfono, audio y pines NO se detecta por el bootloader.'
Write-Host 'Esta release fue compilada para un unico perfil de hardware soportado.'
Write-Host ''
if (-not (Confirm-Yes '¿Antes de ejecutar este instalador funcionaban correctamente pantalla, microfono y parlante?')) {
    Stop-Safe 'Primero debe dejar XiaoZhi funcionando correctamente con su hardware.' 60
}

Write-Title '4. RESPALDO COMPLETO ANTES DE MODIFICAR'
New-Item -ItemType Directory -Force -Path $BackupRoot | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$BackupDir = Join-Path $BackupRoot ('antes-xiaozhi-care-' + $stamp)
New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null
$FullBackup = Join-Path $BackupDir 'flash-completa-16MB.bin'
Write-Host 'Leyendo 16 MB de la placa. Este respaldo permite una recuperacion manual completa.'
try {
    $r = Invoke-Esptool -Arguments @('--port', $Port, '--baud', '460800', 'read-flash', '0x0', '0x1000000', $FullBackup)
} catch {
    Stop-Safe 'No se pudo crear el respaldo completo. No se instalara nada.' 61
}
if (-not (Test-Path $FullBackup) -or (Get-Item $FullBackup).Length -ne 0x1000000) {
    Stop-Safe 'El respaldo completo no tiene 16 MB. No se instalara nada.' 62
}
$BackupHash = Get-Sha256 $FullBackup
Set-Content -LiteralPath (Join-Path $BackupDir 'flash-completa-16MB.sha256.txt') -Value ($BackupHash + '  flash-completa-16MB.bin') -Encoding ASCII
Write-Ok ('Respaldo completo creado: ' + $BackupDir)
Write-Warn 'El respaldo puede contener Wi-Fi y datos privados. No lo comparta.'

[byte[]]$FlashBytes = [System.IO.File]::ReadAllBytes($FullBackup)
$Partitions = @(Get-PartitionTableFromFlash $FlashBytes)
$Layout = Get-LayoutKind $Partitions
if ($Layout -eq 'unsupported') {
    Stop-Safe 'La tabla de particiones no corresponde a XiaoZhi v2 de 16 MB ni a XiaoZhi Care. Se conserva el respaldo y se cancela.' 63
}
if ($Layout -eq 'xiaozhi-v2-16m') { Write-Ok 'Tabla detectada: XiaoZhi v2 oficial de 16 MB.' }
if ($Layout -eq 'xiaozhi-care-16m') { Write-Ok 'Tabla detectada: XiaoZhi Care (actualizacion/reinstalacion).' }

$Ota0 = Get-AppDescriptionFromFlash $FlashBytes 0x00020000
$Ota1 = Get-AppDescriptionFromFlash $FlashBytes 0x00410000
$ValidApps = @()
foreach ($a in @($Ota0, $Ota1)) { if ($a.valid) { $ValidApps += $a } }
if ($ValidApps.Count -eq 0) { Stop-Safe 'No se encontro una aplicacion valida en las particiones OTA.' 64 }

$compatibleAppCount = 0
$ambiguousXiaoZhiApp = $false
foreach ($a in $ValidApps) {
    if ($a.project -ne 'xiaozhi') { continue }

    $containsSku = Test-AppContainsSku $FlashBytes ([int]$a.offset) ([int]$a.image_length) ([string]$Release.supported_board_sku)
    if (-not $containsSku) {
        # Fail closed: una segunda imagen XiaoZhi valida con otro perfil podria ser
        # la particion activa. V1 no intenta adivinar cual OTA esta en uso.
        $ambiguousXiaoZhiApp = $true
        continue
    }

    $versionOk = $false
    foreach ($v in $Release.base_xiaozhi_versions) { if ($a.version -eq [string]$v) { $versionOk = $true; break } }
    if ($versionOk) { $compatibleAppCount++ }
}
if ($ambiguousXiaoZhiApp) {
    Write-Host ('OTA0: project=' + $Ota0.project + ' version=' + $Ota0.version)
    Write-Host ('OTA1: project=' + $Ota1.project + ' version=' + $Ota1.version)
    Stop-Safe ('Hay una imagen XiaoZhi valida en otra OTA cuyo perfil no coincide con ' + $Release.supported_board_sku + '. V1 cancela por seguridad.') 65
}
if ($compatibleAppCount -eq 0) {
    Write-Host ('OTA0: project=' + $Ota0.project + ' version=' + $Ota0.version)
    Write-Host ('OTA1: project=' + $Ota1.project + ' version=' + $Ota1.version)
    Stop-Safe ('No se pudo confirmar XiaoZhi compatible y perfil ' + $Release.supported_board_sku + ' en el firmware instalado.') 66
}
Write-Ok ('XiaoZhi compatible detectado. Perfil: ' + $Release.supported_board_sku)

Write-Title '5. LISTO PARA INSTALAR'
Write-Host ('Version Care: ' + $Release.care_version)
Write-Host ('Perfil preservado: ' + $Release.supported_board_sku)
Write-Host ('Puerto: ' + $Port)
Write-Host ('Respaldo: ' + $BackupDir)
Write-Host ''
Write-Host 'La instalacion:' -ForegroundColor White
Write-Host '  - NO ejecuta erase-flash.'
Write-Host '  - NO borra la particion NVS (Wi-Fi/configuracion de XiaoZhi).'
Write-Host '  - Conserva los datos Care si ya estaban instalados.'
Write-Host '  - Si viene de XiaoZhi oficial, crea areas nuevas care_data y voice.'
Write-Host ''
if (-not (Confirm-Yes '¿CONFIRMA instalar XiaoZhi Care ahora?')) { Write-Warn 'Instalacion cancelada por el usuario.'; exit 0 }

# Desde aqui hay escritura en Flash.
Write-InstallLog $BackupDir 'Comienza escritura de XiaoZhi Care.'

try {
    if ($Layout -eq 'xiaozhi-v2-16m') {
        Write-Host 'Preparando los 2 MB nuevos de XiaoZhi Care...'
        Invoke-Esptool -Arguments @('--port', $Port, '--baud', '460800', 'erase-region', '0xE00000', '0x200000') | Out-Null
        Write-InstallLog $BackupDir 'Borradas regiones nuevas care_data/voice (solo migracion desde base XiaoZhi).'
    }

    $boot = Join-Path $FirmwareDir 'bootloader.bin'
    $part = Join-Path $FirmwareDir 'partition-table.bin'
    $ota = Join-Path $FirmwareDir 'ota_data_initial.bin'
    $app = Join-Path $FirmwareDir 'xiaozhi.bin'
    $assets = Join-Path $FirmwareDir 'generated_assets.bin'

    Write-Host 'Grabando XiaoZhi Care...'
    $args = @(
        '--chip','esp32s3','--port',$Port,'--baud','460800',
        '--before','default-reset','--after','hard-reset',
        'write-flash','--flash-mode','dio','--flash-size','16MB','--flash-freq','80m',
        '0x0',$boot,
        '0x8000',$part,
        '0xd000',$ota,
        '0x20000',$app,
        '0x800000',$assets
    )
    $wr = Invoke-Esptool -Arguments $args
    Write-InstallLog $BackupDir 'write-flash finalizo correctamente.'
} catch {
    Write-InstallLog $BackupDir ('ERROR durante escritura: ' + $_.Exception.Message)
    Write-Fail 'La grabacion no termino correctamente.'
    Write-Host ('El respaldo completo esta en: ' + $BackupDir) -ForegroundColor Yellow
    Write-Host 'No intente borrar la placa. Conserve esa carpeta para recuperacion.' -ForegroundColor Yellow
    exit 70
}

Write-Title 'INSTALACION COMPLETADA'
Write-Ok 'XiaoZhi Care fue grabado correctamente.'
Write-Ok 'La NVS original de XiaoZhi no fue borrada.'
Write-Ok ('Respaldo de seguridad: ' + $BackupDir)
Write-Host ''
Write-Host 'En el primer arranque, care_data y voice pueden formatearse automaticamente' -ForegroundColor Cyan
Write-Host 'si esta es la primera instalacion de XiaoZhi Care.' -ForegroundColor Cyan
Write-Host ''
Write-Host 'Compruebe:'
Write-Host '  1. que XiaoZhi inicia normalmente;'
Write-Host '  2. pantalla, microfono y parlante;'
Write-Host '  3. conexion Wi-Fi;'
Write-Host '  4. panel web de XiaoZhi Care;'
Write-Host '  5. LEDs / recordatorios / radio segun su hardware.'
Write-Host ''
Write-Host 'No necesita Visual Studio, VS Code, ESP-IDF ni Python para usar este instalador.' -ForegroundColor Green
exit 0
