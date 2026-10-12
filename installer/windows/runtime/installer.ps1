[CmdletBinding()]
param(
    [string]$DetectedCulture = '',
    [string]$GraphicProfileId = '',
    [string]$GraphicSelectionPath = '',
    [switch]$PreviewGraphics
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$FirmwareDir = Join-Path $Root 'firmware'
$ToolsDir = Join-Path $Root 'tools'
$LocalData = [Environment]::GetFolderPath('LocalApplicationData')
if ([string]::IsNullOrWhiteSpace($LocalData)) {
    throw 'No se pudo determinar la carpeta local de respaldos.'
}
$BackupRoot = Join-Path $LocalData 'XiaoZhi Care\Respaldos'
$ReleasePath = Join-Path $Root 'release.json'
$I18nPath = Join-Path $Root 'i18n.ps1'
if (-not (Test-Path -LiteralPath $I18nPath)) { Write-Host '[ERROR] i18n.ps1 missing.' -ForegroundColor Red; exit 3 }
. $I18nPath
if (-not $PreviewGraphics) { Select-XzLanguage $DetectedCulture }
$GraphicsPath = Join-Path $Root 'graphics.ps1'
if (Test-Path -LiteralPath $GraphicsPath) { . $GraphicsPath }

$EsptoolVersion = '5.4.0'
$EsptoolZipUrl = 'https://github.com/espressif/esptool/releases/download/v5.4.0/esptool-v5.4.0-windows-amd64.zip'
$EsptoolZipSha256 = 'b7f6b9dd301a210b31f4829118c909c84aae23107f9ca1fdc14ccf4d7384be2e'

function Write-Title([string]$Text) {
    Write-Host ''
    Write-Host ('=' * 68) -ForegroundColor DarkCyan
    Write-Host ('  ' + $Text) -ForegroundColor Cyan
    Write-Host ('=' * 68) -ForegroundColor DarkCyan
}

function Write-Ok([string]$Text) { Write-Host ('[OK]    ' + $Text) -ForegroundColor Green }
function Write-Warn([string]$Text) { Write-Host ('[' + (T 'warning_prefix') + '] ' + $Text) -ForegroundColor Yellow }
function Write-Fail([string]$Text) { Write-Host ('[ERROR] ' + $Text) -ForegroundColor Red }

function Stop-Safe([string]$Message, [int]$Code = 2) {
    Write-Host ''
    Write-Fail $Message
    Write-Host ''
    Write-Host (T 'no_extra_write') -ForegroundColor Yellow
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
    return $items
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
        throw ((T 'esptool_code') -f $code)
    }
    return [pscustomobject]@{ ExitCode = $code; Text = $text }
}

function Invoke-EsptoolVisible([string[]]$Arguments, [string]$LogPath = '', [switch]$AllowFailure) {
    # Operaciones largas: guardamos el log tecnico completo, pero el progreso
    # se muestra con Write-Progress. Esto evita depender de retorno de carro (CR),
    # que algunas combinaciones de Windows Terminal + Windows PowerShell +
    # salida nativa canalizada convierten visualmente en nuevas lineas.
    $captured = New-Object 'System.Collections.Generic.List[string]'
    $progressActive = $false
    $progressActivity = T 'progress_processing'
    $lastProgressKey = ''
    $ansiPattern = ([char]27) + '\[[0-?]*[ -/]*[@-~]'
    $progressPattern = '(?i)(Reading from|Writing at).*?([0-9]+(?:[.,][0-9]+)?)%'

    if ($LogPath) {
        $parent = Split-Path -Parent $LogPath
        if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
        Set-Content -LiteralPath $LogPath -Value '' -Encoding UTF8
    }

    # El script usa SilentlyContinue globalmente para evitar barras no deseadas
    # de descargas. Solo durante esptool habilitamos la barra intencional.
    $oldProgressPreference = $ProgressPreference
    $ProgressPreference = 'Continue'
    $code = 0

    try {
        & $script:Esptool @Arguments 2>&1 | ForEach-Object {
            $line = $_.ToString()
            [void]$captured.Add($line)

            if ($LogPath) { Add-Content -LiteralPath $LogPath -Value $line -Encoding UTF8 }

            $clean = [regex]::Replace($line, $ansiPattern, '')
            $clean = $clean.Replace("`r", '')
            $pm = [regex]::Matches($clean, $progressPattern)

            if ($pm.Count -gt 0) {
                $m = $pm[$pm.Count - 1]
                $operation = $m.Groups[1].Value
                $pctRaw = $m.Groups[2].Value.Replace(',', '.')
                $pct = [double]::Parse($pctRaw, [System.Globalization.CultureInfo]::InvariantCulture)
                if ($pct -lt 0) { $pct = 0 }
                if ($pct -gt 100) { $pct = 100 }

                if ($operation -match '(?i)^Reading') {
                    $progressActivity = T 'progress_reading'
                }
                else {
                    $progressActivity = T 'progress_writing'
                }

                $pctText = [string]::Format([System.Globalization.CultureInfo]::InvariantCulture, '{0:F1}', $pct)
                $key = $progressActivity + '|' + $pctText
                if ($key -ne $lastProgressKey) {
                    $percentInt = [int][Math]::Round($pct)
                    Write-Progress -Id 77 -Activity $progressActivity -Status ((T 'progress_done') -f $pctText) -PercentComplete $percentInt
                    $progressActive = $true
                    $lastProgressKey = $key
                }
            }
            else {
                if ($progressActive) {
                    Write-Progress -Id 77 -Activity $progressActivity -Completed
                    $progressActive = $false
                    $lastProgressKey = ''
                }
                if ($clean.Length -gt 0) { Write-Host $clean }
                else { Write-Host '' }
            }
        }
        $code = $LASTEXITCODE
    }
    finally {
        if ($progressActive) {
            Write-Progress -Id 77 -Activity $progressActivity -Completed
        }
        $ProgressPreference = $oldProgressPreference
    }

    $text = ($captured.ToArray()) -join [Environment]::NewLine
    if ($code -ne 0 -and -not $AllowFailure) {
        throw ((T 'esptool_code') -f $code)
    }
    return [pscustomobject]@{ ExitCode = $code; Text = $text }
}
function Ensure-Esptool {
    New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null
    $versionDir = Join-Path $ToolsDir ('esptool-v' + $EsptoolVersion)
    $existing = Get-ChildItem -Path $versionDir -Filter 'esptool.exe' -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $existing) {
        $hashExe = Get-Sha256 $existing.FullName
        $hashEsperadoExe = '074e4bca8a6282e48356a24321b0f7d674d73a79aba91b2f5f015f988a8df2fb'

        if ($hashExe -ne $hashEsperadoExe) {
            Stop-Safe 'La integridad del esptool existente no es valida.' 13
        }

        Write-Ok ((T 'flasher_found') -f $existing.FullName)
        return $existing.FullName
    }

    Write-Host (T 'flasher_download') -ForegroundColor Cyan
    $zip = Join-Path $ToolsDir ('esptool-v' + $EsptoolVersion + '-windows-amd64.zip')
    $temporal = $zip + '.part'
    $http = $null
    $respuesta = $null
    $flujo = $null
    $archivoSalida = $null

    try {
        Add-Type -AssemblyName System.Net.Http

        if (Test-Path -LiteralPath $temporal) {
            Remove-Item -LiteralPath $temporal -Force
        }

        $http = New-Object System.Net.Http.HttpClient
        $http.Timeout = [TimeSpan]::FromMinutes(15)

        $respuesta = $http.GetAsync(
            $EsptoolZipUrl,
            [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead
        ).GetAwaiter().GetResult()

        $respuesta.EnsureSuccessStatusCode() | Out-Null

        $total = $respuesta.Content.Headers.ContentLength
        $flujo = $respuesta.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
        $archivoSalida = [System.IO.File]::Create($temporal)

        [byte[]]$buffer = New-Object byte[] 65536
        [long]$recibidos = 0
        $ultimoPorcentaje = -1
        $ultimoTramo = -1

        while (($leidos = $flujo.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $archivoSalida.Write($buffer, 0, $leidos)
            $recibidos += $leidos

            $mb = [Math]::Round($recibidos / 1MB, 1)

            if ($total -and $total -gt 0) {
                $porcentaje = [int][Math]::Floor(
                    100.0 * $recibidos / $total
                )

                if ($porcentaje -ne $ultimoPorcentaje) {
                    Write-Progress -Activity "Descargando esptool" `
                        -Status "$porcentaje% - $mb MB" `
                        -PercentComplete $porcentaje

                    $ultimoPorcentaje = $porcentaje
                    $tramo = [int][Math]::Floor($porcentaje / 10)
                    if ($tramo -gt $ultimoTramo -or $porcentaje -eq 100) {
                        $mbTotal = [Math]::Round($total / 1MB, 1)
                        Write-Host ("Descarga: {0}% - {1} de {2} MB" -f $porcentaje, $mb, $mbTotal) -ForegroundColor Cyan
                        $ultimoTramo = $tramo
                    }
                }
            } else {
                Write-Progress -Activity "Descargando esptool" `
                    -Status "$mb MB descargados"
            }
        }

        $archivoSalida.Flush()
        $archivoSalida.Dispose()
        $archivoSalida = $null

        if ($total -and $total -gt 0 -and $recibidos -ne $total) {
            throw "Descarga incompleta: $recibidos de $total bytes."
        }

        Move-Item -LiteralPath $temporal -Destination $zip -Force
    } catch {
        $detalle = $_.Exception.Message
        if ($archivoSalida) {
            $archivoSalida.Dispose()
            $archivoSalida = $null
        }
        if (Test-Path -LiteralPath $temporal) {
            Remove-Item -LiteralPath $temporal -Force -ErrorAction SilentlyContinue
        }
        Stop-Safe ((T 'flasher_download_fail') -f $detalle) 10
    } finally {
        Write-Progress -Activity "Descargando esptool" -Completed
        if ($flujo) { $flujo.Dispose() }
        if ($respuesta) { $respuesta.Dispose() }
        if ($http) { $http.Dispose() }
    }
    $got = Get-Sha256 $zip
    if ($got -ne $EsptoolZipSha256) {
        Remove-Item -Force $zip -ErrorAction SilentlyContinue
        Stop-Safe (T 'flasher_hash_bad') 11
    }
    Write-Ok (T 'flasher_hash_ok')

    $dest = Join-Path $ToolsDir ('esptool-v' + $EsptoolVersion)
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    Expand-Archive -LiteralPath $zip -DestinationPath $dest -Force
    $exe = Get-ChildItem -Path $dest -Filter 'esptool.exe' -File -Recurse | Select-Object -First 1
    if ($null -eq $exe) { Stop-Safe (T 'flasher_exe_missing') 12 }
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
    return $out
}

function Select-ComPort {
    $ports = @(Get-ComPorts)
    if ($ports.Count -eq 0) { Stop-Safe (T 'no_com') 20 }
    if ($ports.Count -eq 1) {
        Write-Ok ((T 'port_detected') -f $ports[0].Description)
        return $ports[0].Port
    }
    Write-Host (T 'multiple_ports')
    for ($i = 0; $i -lt $ports.Count; $i++) {
        Write-Host ('  [{0}] {1}' -f ($i + 1), $ports[$i].Description)
    }
    while ($true) {
        $answer = Read-Host (T 'choose_port')
        $n = 0
        if ([int]::TryParse($answer, [ref]$n) -and $n -ge 1 -and $n -le $ports.Count) {
            return $ports[$n - 1].Port
        }
        Write-Warn (T 'invalid_option')
    }
}

function Confirm-Yes([string]$Question) { return (Xz-ConfirmYes $Question) }

function Verify-ReleaseFiles($Release) {
    if ($null -eq $Release.firmware) { Stop-Safe (T 'release_fw_missing') 30 }
    foreach ($f in $Release.firmware) {
        $path = Join-Path $FirmwareDir ([string]$f.file)
        if (-not (Test-Path -LiteralPath $path)) { Stop-Safe ((T 'firmware_missing') -f $f.file) 31 }
        $hash = Get-Sha256 $path
        if ($hash -ne ([string]$f.sha256).ToLowerInvariant()) {
            Stop-Safe ((T 'firmware_hash_bad') -f $f.file) 32
        }
    }
    Write-Ok (T 'firmware_integrity_ok')
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
if ($PreviewGraphics) {
    try {
        $Release = Get-Content -LiteralPath $ReleasePath -Raw -Encoding UTF8 | ConvertFrom-Json
        Verify-ReleaseFiles $Release
        if (-not $Release.graphics -or -not (Test-Path -LiteralPath $GraphicsPath)) { throw 'Paquete sin DP-045.' }
        $catalog = Test-CareGraphicsPackage $Root $Release
        $GraphicProfileId = Get-CareRequestedProfile $GraphicSelectionPath $GraphicProfileId
        $selected = Select-CareGraphicsProfile $catalog $GraphicProfileId -NonInteractive
        [ordered]@{ profile=$selected.profile_id; sha256=$selected.sha256; srmodels_sha256=$selected.srmodels_sha256;
                    bytes=$selected.size; hardware_tested=$catalog.hardware_tested; flash_performed=$false } | ConvertTo-Json
        exit 0
    } catch { Stop-Safe $_.Exception.Message 41 }
}
Clear-Host
Write-Title (T 'installer_title')
Write-Host (T 'assisted_install')
Write-Host ''
Write-Host (T 'compatibility') -ForegroundColor White
Write-Host '  - ESP32-S3 N16R8'
Write-Host '  - 16 MB Flash'
Write-Host '  - 8 MB PSRAM'
Write-Host (T 'compat_base')
Write-Host (T 'compat_profile')
Write-Host ''
Write-Warn (T 'warn_preserve_nvs')
Write-Warn (T 'warn_backup_first')
Write-Host ''
if (-not (Confirm-Yes (T 'start_verification'))) { exit 0 }

if (-not (Test-Path -LiteralPath $ReleasePath)) {
    Stop-Safe (T 'release_missing') 40
}
$Release = Get-Content -LiteralPath $ReleasePath -Raw -Encoding UTF8 | ConvertFrom-Json

Write-Title (T 'title_verify_package')
Write-Host ((T 'care_version') -f $Release.care_version)
Write-Host ((T 'base_version') -f ($Release.base_xiaozhi_versions -join ', '))
Write-Host ((T 'profile') -f $Release.supported_board_sku)
Verify-ReleaseFiles $Release

$SelectedGraphics = $null
if ($Release.graphics) {
    try {
        if (-not (Test-Path -LiteralPath $GraphicsPath)) { throw 'Falta graphics.ps1.' }
        $GraphicsCatalog = Test-CareGraphicsPackage $Root $Release
        $GraphicProfileId = Get-CareRequestedProfile $GraphicSelectionPath $GraphicProfileId
        $SelectedGraphics = Select-CareGraphicsProfile $GraphicsCatalog $GraphicProfileId
        Write-Ok ('DP-045: ' + $SelectedGraphics.profile_id)
    } catch { Stop-Safe $_.Exception.Message 41 }
} elseif (-not [string]::IsNullOrWhiteSpace($GraphicProfileId) -or -not [string]::IsNullOrWhiteSpace($GraphicSelectionPath)) {
    Stop-Safe 'Este paquete no contiene perfiles graficos DP-045.' 41
}

$script:Esptool = Ensure-Esptool
Write-Ok ((T 'esptool_ready') -f $EsptoolVersion)

Write-Title (T 'title_detect_board')
$Port = Select-ComPort
Write-Host ((T 'connecting') -f $Port)

try {
    $chip = Invoke-Esptool -Arguments @('--port', $Port, 'flash-id')
} catch {
    Stop-Safe ((T 'connect_fail') -f $Port) 50
}
if ($chip.Text -notmatch 'ESP32-S3') { Stop-Safe (T 'wrong_chip') 51 }
Write-Ok (T 'chip_ok')

if ($chip.Text -notmatch '(?i)Embedded\s+PSRAM\s+8MB|PSRAM\s+8MB') {
    Write-Host $chip.Text -ForegroundColor DarkGray
    Stop-Safe (T 'psram_fail') 52
}
Write-Ok (T 'psram_ok')

$flash = Invoke-Esptool -Arguments @('--port', $Port, 'flash-id')
if ($flash.Text -notmatch '(?i)Detected\s+flash\s+size:\s*16\s*MB|flash\s+size[^\r\n]*16\s*MB') {
    Write-Host $flash.Text -ForegroundColor DarkGray
    Stop-Safe (T 'flash_fail') 53
}
Write-Ok (T 'flash_detected')

$sec = Invoke-Esptool -Arguments @('--port', $Port, '--no-stub', 'get-security-info') -AllowFailure
if ($sec.ExitCode -ne 0) { Stop-Safe (T 'security_query_fail') 54 }
if ($sec.Text -notmatch '(?i)Secure\s*Boot:\s*Disabled' -or
    $sec.Text -notmatch '(?i)Flash\s*Encryption:\s*Disabled') {
    Write-Host $sec.Text -ForegroundColor DarkGray
    Stop-Safe (T 'security_fail') 55
}
Write-Ok (T 'security_ok')

Write-Title (T 'title_existing')
Write-Host (T 'existing_work')
Write-Host (T 'hardware_not_detected')
Write-Host (T 'single_profile')
Write-Host ''
if (-not (Confirm-Yes (T 'hardware_worked'))) {
    Stop-Safe (T 'fix_xiaozhi_first') 60
}

Write-Title (T 'title_backup')
$UnidadRespaldo = [System.IO.Path]::GetPathRoot($BackupRoot)
$DiscoRespaldo = [System.IO.DriveInfo]::new($UnidadRespaldo)

if ($DiscoRespaldo.AvailableFreeSpace -lt 100MB) {
    Stop-Safe 'Espacio insuficiente para crear el respaldo de seguridad.' 56
}
New-Item -ItemType Directory -Force -Path $BackupRoot | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$BackupDir = Join-Path $BackupRoot ('antes-xiaozhi-care-' + $stamp)
New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null
$FullBackup = Join-Path $BackupDir 'flash-completa-16MB.bin'
Write-Host (T 'reading_backup')
Write-Host (T 'show_progress') -ForegroundColor Cyan

$BackupOk = $false
$BackupBauds = @('460800', '230400', '115200')
$LastBackupError = ''
foreach ($baud in $BackupBauds) {
    if (Test-Path $FullBackup) { Remove-Item -Force $FullBackup -ErrorAction SilentlyContinue }
    $backupLog = Join-Path $BackupDir ('esptool-respaldo-' + $baud + '.log')
    Write-Host ''
    Write-Host ((T 'backup_try') -f $baud) -ForegroundColor Cyan
    try {
        $r = Invoke-EsptoolVisible -Arguments @(
            '--chip','esp32s3','--port',$Port,'--baud',$baud,
            '--before','default-reset','--after','hard-reset',
            'read-flash','0x0','0x1000000',$FullBackup
        ) -LogPath $backupLog -AllowFailure
        if ($r.ExitCode -eq 0 -and (Test-Path $FullBackup) -and (Get-Item $FullBackup).Length -eq 0x1000000) {
            $BackupOk = $true
            Write-Ok ((T 'backup_ok') -f $baud)
            break
        }
        $LastBackupError = 'esptool termino con codigo ' + $r.ExitCode
        if ((Test-Path $FullBackup) -and (Get-Item $FullBackup).Length -ne 0x1000000) {
            $LastBackupError += '; archivo parcial de ' + (Get-Item $FullBackup).Length + ' bytes'
        }
    } catch {
        $LastBackupError = $_.Exception.Message
    }
    Write-Warn ((T 'backup_retry') -f $baud)
}

if (-not $BackupOk) {
    if (Test-Path $FullBackup) { Remove-Item -Force $FullBackup -ErrorAction SilentlyContinue }
    Write-Host ''
    Write-Host ((T 'last_detail') -f $LastBackupError) -ForegroundColor DarkGray
    Write-Host ((T 'diag_logs') -f $BackupDir) -ForegroundColor DarkGray
    Stop-Safe (T 'backup_all_fail') 61
}
$BackupHash = Get-Sha256 $FullBackup
Set-Content -LiteralPath (Join-Path $BackupDir 'flash-completa-16MB.sha256.txt') -Value ($BackupHash + '  flash-completa-16MB.bin') -Encoding ASCII
Write-Ok ((T 'backup_created') -f $BackupDir)
Write-Warn (T 'backup_private')

[byte[]]$FlashBytes = [System.IO.File]::ReadAllBytes($FullBackup)
$Partitions = @(Get-PartitionTableFromFlash $FlashBytes)
$Layout = Get-LayoutKind $Partitions
if ($Layout -eq 'unsupported') {
    Stop-Safe (T 'layout_unsupported') 63
}
if ($Layout -eq 'xiaozhi-v2-16m') { Write-Ok (T 'layout_stock') }
if ($Layout -eq 'xiaozhi-care-16m') { Write-Ok (T 'layout_care') }

$Ota0 = Get-AppDescriptionFromFlash $FlashBytes 0x00020000
$Ota1 = Get-AppDescriptionFromFlash $FlashBytes 0x00410000
$ValidApps = @()
foreach ($a in @($Ota0, $Ota1)) { if ($a.valid) { $ValidApps += $a } }
if ($ValidApps.Count -eq 0) { Stop-Safe (T 'no_valid_ota') 64 }

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
    Stop-Safe ((T 'ota_ambiguous') -f $Release.supported_board_sku) 65
}
if ($compatibleAppCount -eq 0) {
    Write-Host ('OTA0: project=' + $Ota0.project + ' version=' + $Ota0.version)
    Write-Host ('OTA1: project=' + $Ota1.project + ' version=' + $Ota1.version)
    Stop-Safe ((T 'no_compatible') -f $Release.supported_board_sku) 66
}
Write-Ok ((T 'compatible_detected') -f $Release.supported_board_sku)

Write-Title (T 'title_ready')
Write-Host ((T 'version_care') -f $Release.care_version)
Write-Host ((T 'profile_preserved') -f $Release.supported_board_sku)
Write-Host ((T 'port_line') -f $Port)
Write-Host ((T 'backup_line') -f $BackupDir)
Write-Host ''
Write-Host (T 'install_summary') -ForegroundColor White
Write-Host (T 'install_no_erase')
Write-Host (T 'install_no_nvs')
Write-Host (T 'install_keep_care')
Write-Host (T 'install_new_areas')
Write-Host ''
if (-not (Confirm-Yes (T 'confirm_install'))) { Write-Warn (T 'install_cancelled'); exit 0 }

# Recheck after user interaction and backup, before the first erase/write.
if ($null -ne $SelectedGraphics) {
    try { Verify-ReleaseFiles $Release; Test-CareGraphicsPackage $Root $Release | Out-Null }
    catch { Stop-Safe $_.Exception.Message 41 }
}

# Desde aqui hay escritura en Flash.
Write-InstallLog $BackupDir 'Comienza escritura de XiaoZhi Care.'

try {
    if ($Layout -eq 'xiaozhi-v2-16m') {
        Write-Host (T 'preparing_care')
        $eraseLog = Join-Path $BackupDir 'esptool-preparar-care.log'
        Invoke-EsptoolVisible -Arguments @(
            '--chip','esp32s3','--port',$Port,'--baud','115200',
            '--before','default-reset','--after','hard-reset',
            'erase-region','0xE00000','0x200000'
        ) -LogPath $eraseLog | Out-Null
        Write-InstallLog $BackupDir 'Borradas regiones nuevas care_data/voice (solo migracion desde base XiaoZhi).'
    }

    $boot = Join-Path $FirmwareDir 'bootloader.bin'
    $part = Join-Path $FirmwareDir 'partition-table.bin'
    $ota = Join-Path $FirmwareDir 'ota_data_initial.bin'
    $app = Join-Path $FirmwareDir 'xiaozhi.bin'
    $assets = Join-Path $FirmwareDir 'generated_assets.bin'
    if ($null -ne $SelectedGraphics) {
        $assets = Join-Path (Join-Path $Root 'graphics') $SelectedGraphics.file
        Write-InstallLog $BackupDir ('DP-045: ' + $SelectedGraphics.profile_id + ' SHA256=' + $SelectedGraphics.sha256)
        $SelectedGraphics | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $BackupDir 'seleccion-grafica.json') -Encoding UTF8
    }

    Write-Host (T 'flashing_care')
    Write-Host (T 'flashing_progress') -ForegroundColor Cyan
    $WriteOk = $false
    $LastWriteError = ''
    foreach ($baud in @('460800','230400','115200')) {
        Write-Host ''
        Write-Host ((T 'flash_try') -f $baud) -ForegroundColor Cyan
        $writeLog = Join-Path $BackupDir ('esptool-grabacion-' + $baud + '.log')
        $args = @(
            '--chip','esp32s3','--port',$Port,'--baud',$baud,
            '--before','default-reset','--after','hard-reset',
            'write-flash','--flash-mode','dio','--flash-size','16MB','--flash-freq','80m',
            '0x0',$boot,
            '0x8000',$part,
            '0xd000',$ota,
            '0x20000',$app,
            '0x800000',$assets
        )
        $wr = Invoke-EsptoolVisible -Arguments $args -LogPath $writeLog -AllowFailure
        if ($wr.ExitCode -eq 0) {
            $WriteOk = $true
            Write-Ok ((T 'flash_ok') -f $baud)
            break
        }
        $LastWriteError = 'esptool termino con codigo ' + $wr.ExitCode
        Write-Warn ((T 'flash_retry') -f $baud)
    }
    if (-not $WriteOk) { throw ('No se pudo completar write-flash. ' + $LastWriteError) }
    Write-InstallLog $BackupDir 'write-flash finalizo correctamente.'
} catch {
    Write-InstallLog $BackupDir ('ERROR durante escritura: ' + $_.Exception.Message)
    Write-Fail (T 'flash_failed')
    Write-Host ((T 'backup_is') -f $BackupDir) -ForegroundColor Yellow
    Write-Host (T 'preserve_backup') -ForegroundColor Yellow
    exit 70
}

Write-Title (T 'title_installed')
Write-Ok (T 'care_written')
Write-Ok (T 'nvs_preserved')
Write-Ok ((T 'security_backup') -f $BackupDir)
Write-Host ''
Write-Host (T 'first_boot_1') -ForegroundColor Cyan
Write-Host (T 'first_boot_2') -ForegroundColor Cyan
Write-Host ''
Write-Host (T 'check')
Write-Host (T 'check_1')
Write-Host (T 'check_2')
Write-Host (T 'check_3')
Write-Host (T 'check_4')
Write-Host (T 'check_5')
Write-Host ''
Write-Host (T 'no_dev_tools') -ForegroundColor Green
exit 0
