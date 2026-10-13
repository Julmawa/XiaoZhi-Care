# Pruebas offline DP-045D. NO usa esptool, puertos COM ni respaldos reales.
$ErrorActionPreference = 'Stop'
$Runtime = Join-Path $PSScriptRoot '..\runtime'
. (Join-Path $Runtime 'i18n.ps1')
$script:XzLang = 'es'
. (Join-Path $Runtime 'restore-core.ps1')

$passed = 0
function Ok([string]$message) {
    $script:passed++
    Write-Host ('[OK] ' + $message) -ForegroundColor Green
}
function Expect-Failure([string]$message,[scriptblock]$code) {
    $failed = $false
    try { & $code | Out-Null } catch { $failed = $true }
    if (-not $failed) { throw ('La prueba debia rechazar: ' + $message) }
    Ok $message
}
function Make-Partition([string]$Name,[uint32]$Offset,[uint32]$Size) {
    [byte[]]$entry = New-Object byte[] 32
    $entry[0] = 0xAA; $entry[1] = 0x50
    [Array]::Copy([BitConverter]::GetBytes($Offset),0,$entry,4,4)
    [Array]::Copy([BitConverter]::GetBytes($Size),0,$entry,8,4)
    [byte[]]$n = [Text.Encoding]::ASCII.GetBytes($Name)
    [Array]::Copy($n,0,$entry,12,[Math]::Min(15,$n.Length))
    return ,$entry
}

$temp = Join-Path $env:TEMP ('XiaoZhi_DP045D_TEST_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    $folder = Join-Path $temp 'antes-xiaozhi-care-20261012-130001'
    New-Item -ItemType Directory -Path $folder | Out-Null
    $bin = Join-Path $folder 'flash-completa-16MB.bin'
    $shaFile = Join-Path $folder 'flash-completa-16MB.sha256.txt'
    $fs = [IO.File]::Create($bin)
    try {
        $fs.SetLength(16777216)
        $fs.Position=0; $fs.WriteByte(0xE9)
        $fs.Position=0x8000
        foreach ($p in @(
            (Make-Partition 'nvs' 0x9000 0x4000),
            (Make-Partition 'otadata' 0xD000 0x2000),
            (Make-Partition 'phy_init' 0xF000 0x1000),
            (Make-Partition 'ota_0' 0x20000 0x3F0000),
            (Make-Partition 'ota_1' 0x410000 0x3F0000),
            (Make-Partition 'assets' 0x800000 0x600000),
            (Make-Partition 'care_data' 0xE00000 0x100000),
            (Make-Partition 'voice' 0xF00000 0x100000))) {
            $fs.Write($p,0,$p.Length)
        }
        $fs.WriteByte(0xFF); $fs.WriteByte(0xFF)
    } finally { $fs.Dispose() }

    $real = Get-Dp45Hash $bin
    [IO.File]::WriteAllText($shaFile,($real + '  flash-completa-16MB.bin' + "`r`n"),[Text.Encoding]::ASCII)
    $info = Test-Dp45Backup $folder
    if ($info.Sha -cne $real) { throw 'SHA correcto no aceptado.' }
    Ok 'Respaldo sintetico de 16 MB con SHA-256 correcto'
    if ((Test-Dp45FlashLayout $bin) -cne 'xiaozhi-care-16m') { throw 'Layout no detectado.' }
    Ok 'Layout XiaoZhi Care reconocido'

    # Regression: ESP-IDF uses an EB EB MD5 trailer, not a partition.
    # This was the cause of the false layout rejection in Preflight.
    $fs = [IO.File]::Open($bin,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite)
    try {
        [byte[]]$rawEntries = New-Object byte[] (8 * 32)
        $fs.Position = 0x8000
        if ($fs.Read($rawEntries,0,$rawEntries.Length) -ne $rawEntries.Length) {
            throw 'No se pudo leer tabla sintetica.'
        }
        $md5 = [System.Security.Cryptography.MD5]::Create()
        try { [byte[]]$sum = $md5.ComputeHash($rawEntries) }
        finally { $md5.Dispose() }
        [byte[]]$entry = New-Object byte[] 32
        $entry[0] = 0xEB; $entry[1] = 0xEB
        for ($k=2;$k -lt 16;$k++) { $entry[$k] = 0xFF }
        [Array]::Copy($sum,0,$entry,16,16)
        $fs.Position = 0x8100
        $fs.Write($entry,0,$entry.Length)
        $fs.WriteByte(0xFF); $fs.WriteByte(0xFF)
    } finally { $fs.Dispose() }
    if ((Test-Dp45FlashLayout $bin) -cne 'xiaozhi-care-16m') {
        throw 'Una tabla ESP-IDF con MD5 correcto fue rechazada.'
    }
    Ok 'Tabla ESP-IDF con entrada MD5 valida aceptada'

    $fs = [IO.File]::Open($bin,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite)
    try {
        $fs.Position = 0x8110
        $fs.WriteByte([byte]($entry[16] -bxor 1))
    } finally { $fs.Dispose() }
    Expect-Failure 'Tabla con MD5 modificado rechazada' { Test-Dp45FlashLayout $bin }

    $fs = [IO.File]::Open($bin,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite)
    try {
        $fs.Position = 0x8110
        $fs.WriteByte($entry[16])
    } finally { $fs.Dispose() }
    if ((Test-Dp45FlashLayout $bin) -cne 'xiaozhi-care-16m') {
        throw 'No se recupero validacion tras reparar el MD5 sintetico.'
    }
    Ok 'Tabla original recuperada y verificada'

    $real = Get-Dp45Hash $bin
    [IO.File]::WriteAllText($shaFile,($real + '  flash-completa-16MB.bin' + "`r`n"),[Text.Encoding]::ASCII)
    if ($info.Mac -ne '') { throw 'Se invento una MAC en un respaldo historico.' }
    Ok 'Sin MAC: identidad no inventada'

    [IO.File]::WriteAllText($shaFile,('SHA256=' + $real),[Text.Encoding]::ASCII)
    Expect-Failure 'Formato de SHA ajeno rechazado' { Test-Dp45Backup $folder }
    [IO.File]::WriteAllText($shaFile,((('0' * 64)) + '  flash-completa-16MB.bin' + "`r`n"),[Text.Encoding]::ASCII)
    Expect-Failure 'SHA-256 distinto rechazado' { Test-Dp45Backup $folder }
    [IO.File]::WriteAllText($shaFile,($real + '  flash-completa-16MB.bin' + "`r`n"),[Text.Encoding]::ASCII)

    $log = Join-Path $folder 'esptool-respaldo-115200.log'
    [IO.File]::WriteAllText($log,"MAC: AA:BB:CC:DD:EE:FF`r`n",[Text.Encoding]::UTF8)
    $info = Test-Dp45Backup $folder
    if ($info.Mac -cne 'AA:BB:CC:DD:EE:FF') { throw 'MAC no extraida.' }
    Ok 'MAC extraida de registro esptool'
    $secondLog = Join-Path $folder 'esptool-respaldo-230400.log'
    [IO.File]::WriteAllText($secondLog,"MAC: 11:22:33:44:55:66`r`n",[Text.Encoding]::UTF8)
    Expect-Failure 'Registros de MAC contradictorios rechazados' { Test-Dp45Backup $folder }
    Remove-Item -LiteralPath $secondLog

    if ((Get-Dp45Mac "MAC: AA:BB:CC:DD:EE:FF`n") -cne 'AA:BB:CC:DD:EE:FF') { throw 'MAC de placa no reconocida.' }
    Ok 'MAC de placa extraida estrictamente'
    Expect-Failure 'Salida sin MAC rechazada' { Get-Dp45Mac 'chip not connected' }

    $fs = [IO.File]::Open($bin,[IO.FileMode]::Open,[IO.FileAccess]::Write)
    try { $fs.SetLength(28) } finally { $fs.Dispose() }
    Expect-Failure 'Respaldo truncado rechazado' { Test-Dp45Backup $folder }
    Ok 'Ninguna prueba uso hardware ni respaldos privados'
    Write-Host ("`nPRUEBAS DP-045D: {0} APROBADAS" -f $passed) -ForegroundColor Green
    exit 0
} catch {
    Write-Host ('[ERROR] ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
} finally {
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
}
