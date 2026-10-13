# XiaoZhi Care DP-045D - modulo de recuperacion independiente.
# Solo declara funciones. No ejecuta comandos ni accede al dispositivo al cargarse.
Set-StrictMode -Version 2.0
if (-not (Get-Command Get-XzFileHash -ErrorAction SilentlyContinue)) {
    . (Join-Path $PSScriptRoot 'activity.ps1')
}
$script:Dp45FlashSize = 16777216
$script:Dp45EsptoolVersion = '5.4.0'
$script:Dp45EsptoolZipSha = 'b7f6b9dd301a210b31f4829118c909c84aae23107f9ca1fdc14ccf4d7384be2e'
$script:Dp45EsptoolExeSha = '074e4bca8a6282e48356a24321b0f7d674d73a79aba91b2f5f015f988a8df2fb'
$script:Dp45EsptoolUrl = 'https://github.com/espressif/esptool/releases/download/v5.4.0/esptool-v5.4.0-windows-amd64.zip'

function Get-Dp45Hash([string]$Path) {
    return (Get-XzFileHash $Path (T 'activity_sha'))
}

function Assert-Dp45PlainPath([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw (T 'd45r_link')
    }
    return $item
}

function Get-Dp45BackupRoot {
    $local = [Environment]::GetFolderPath('LocalApplicationData')
    if ([string]::IsNullOrWhiteSpace($local)) { throw (T 'd45_local_missing') }
    return (Join-Path $local 'XiaoZhi Care\Respaldos')
}

function Get-Dp45AvailableBackups([string]$BackupRoot) {
    if (-not (Test-Path -LiteralPath $BackupRoot -PathType Container)) {
        throw ((T 'backup_root_missing') -f $BackupRoot)
    }
    Assert-Dp45PlainPath $BackupRoot | Out-Null
    $result = @(Get-ChildItem -LiteralPath $BackupRoot -Directory |
        Where-Object { $_.Name -cmatch '^antes-xiaozhi-care-\d{8}-\d{6}$' -and
            (($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) } |
        Sort-Object Name -Descending)
    if ($result.Count -eq 0) { throw (T 'backup_none') }
    return $result
}

function Test-Dp45Backup([string]$Folder) {
    Assert-Dp45PlainPath $Folder | Out-Null
    $bin = Join-Path $Folder 'flash-completa-16MB.bin'
    $shaFile = Join-Path $Folder 'flash-completa-16MB.sha256.txt'
    if (-not (Test-Path -LiteralPath $bin -PathType Leaf)) { throw ((T 'backup_file_missing') -f $bin) }
    if (-not (Test-Path -LiteralPath $shaFile -PathType Leaf)) { throw (T 'sha_missing') }
    $binItem = Assert-Dp45PlainPath $bin
    Assert-Dp45PlainPath $shaFile | Out-Null
    if ($binItem.Length -ne $script:Dp45FlashSize) { throw ((T 'backup_size_bad') -f $binItem.Length) }

    # Solo acepta EXACTAMENTE el formato producido por el instalador:
    # <sha256><dos espacios>flash-completa-16MB.bin[fin de linea opcional]
    $line = [IO.File]::ReadAllText($shaFile, [Text.Encoding]::ASCII)
    $match = [regex]::Match($line, '(?i)\A([0-9a-f]{64})  flash-completa-16MB\.bin\r?\n?\z')
    if (-not $match.Success) { throw (T 'sha_format_bad') }
    $real = Get-Dp45Hash $bin
    if ($real -cne $match.Groups[1].Value.ToUpperInvariant()) { throw (T 'sha_mismatch') }

    # Archivo nuevo opcional; la instalacion anterior solo produjo logs esptool.
    $macs = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $macOrigin = 'none'
    $manifestFile = Join-Path $Folder 'device.json'
    if (Test-Path -LiteralPath $manifestFile -PathType Leaf) {
        Assert-Dp45PlainPath $manifestFile | Out-Null
        $meta = Get-Content -LiteralPath $manifestFile -Raw -Encoding UTF8 | ConvertFrom-Json
        if (-not $meta -or -not $meta.mac -or ([string]$meta.mac -notmatch '(?i)\A(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\z')) {
            throw (T 'd45r_mac_invalid')
        }
        [void]$macs.Add(([string]$meta.mac).ToUpperInvariant())
        $macOrigin = 'device.json'
    }
    foreach ($log in @(Get-ChildItem -LiteralPath $Folder -File -Filter 'esptool-respaldo-*.log')) {
        Assert-Dp45PlainPath $log.FullName | Out-Null
        if ($log.Length -gt 2MB) { throw (T 'd45r_log_large') }
        $content = Get-Content -LiteralPath $log.FullName -Raw
        $matches = [regex]::Matches($content, '(?im)^\s*(?:Base MAC|MAC):\s*((?:[a-f0-9]{2}:){5}[a-f0-9]{2})(?:\s+\([^()\r\n]{1,120}\))?\s*$')
        foreach ($one in $matches) { [void]$macs.Add($one.Groups[1].Value.ToUpperInvariant()) }
        if ($matches.Count -gt 0 -and $macOrigin -eq 'none') { $macOrigin = 'esptool-log' }
    }
    if ($macs.Count -gt 1) { throw (T 'd45r_mac_conflict') }
    $mac = ''
    foreach ($m in $macs) { $mac = $m }
    return [pscustomobject]@{ Folder=$Folder; Bin=$bin; Sha=$real; Mac=$mac; MacOrigin=$macOrigin }
}

function Test-Dp45FlashLayout([string]$Bin) {
    # La tabla de ESP-IDF admite una entrada MD5 especial (EB EB), de 32 bytes.
    # Validarla es obligatorio; NO equivale a aceptar una tabla desconocida.
    # Se mantienen las comprobaciones estrictas de perfiles y offsets.
    $stream = [IO.File]::OpenRead($Bin)
    try {
        if ($stream.Length -ne $script:Dp45FlashSize) { throw (T 'backup_size_bad') }
        if ($stream.ReadByte() -ne 0xE9) { throw (T 'd45r_boot_bad') }
        $stream.Position = 0x8000
        [byte[]]$buf = New-Object byte[] 3072
        if ($stream.Read($buf,0,$buf.Length) -ne $buf.Length) { throw (T 'd45r_layout_bad') }
        $table = @{}
        $foundEnd = $false
        $foundMd5 = $false
        for ($o=0; $o -lt $buf.Length; $o+=32) {
            if ($buf[$o] -eq 0xFF -and $buf[$o+1] -eq 0xFF) {
                $foundEnd = $true
                break
            }
            # ESP-IDF gen_esp32part.py: magic EB EB, 14 FF y digest MD5 de
            # todos los registros anteriores, en bytes 16..31 de la entrada.
            if ($buf[$o] -eq 0xEB -and $buf[$o+1] -eq 0xEB) {
                if ($foundMd5 -or $o -eq 0) { throw (T 'd45r_layout_bad') }
                for ($k=2; $k -lt 16; $k++) {
                    if ($buf[$o+$k] -ne 0xFF) { throw (T 'd45r_layout_bad') }
                }
                $md5 = [System.Security.Cryptography.MD5]::Create()
                try { [byte[]]$digest = $md5.ComputeHash($buf, 0, $o) }
                finally { $md5.Dispose() }
                for ($k=0; $k -lt 16; $k++) {
                    if ($buf[$o+16+$k] -ne $digest[$k]) { throw (T 'd45r_layout_bad') }
                }
                $foundMd5 = $true
                continue
            }
            # Despues del MD5 solo se acepta un marcador de fin de tabla.
            if ($foundMd5 -or $buf[$o] -ne 0xAA -or $buf[$o+1] -ne 0x50) {
                throw (T 'd45r_layout_bad')
            }
            $label = [Text.Encoding]::ASCII.GetString($buf, $o+12, 16).Split([char]0)[0]
            if ([string]::IsNullOrWhiteSpace($label) -or $table.ContainsKey($label)) {
                throw (T 'd45r_layout_bad')
            }
            $offset = [BitConverter]::ToUInt32($buf,$o+4)
            $size = [BitConverter]::ToUInt32($buf,$o+8)
            if ($size -eq 0 -or ([uint64]$offset + [uint64]$size) -gt [uint64]$script:Dp45FlashSize) {
                throw (T 'd45r_layout_bad')
            }
            $table[$label] = [pscustomobject]@{offset=$offset; size=$size}
        }
        if (-not $foundEnd) { throw (T 'd45r_layout_bad') }
        $expected = @{
            nvs=@(0x9000,0x4000);otadata=@(0xD000,0x2000);
            phy_init=@(0xF000,0x1000);ota_0=@(0x20000,0x3F0000);
            ota_1=@(0x410000,0x3F0000)
        }
        foreach ($name in $expected.Keys) {
            if (-not $table.ContainsKey($name) -or $table[$name].offset -ne $expected[$name][0] -or
                $table[$name].size -ne $expected[$name][1]) { throw (T 'd45r_layout_bad') }
        }
        if (-not $table.ContainsKey('assets') -or $table['assets'].offset -ne 0x800000) {
            throw (T 'd45r_layout_bad')
        }
        $kind = ''
        if ($table['assets'].size -eq 0x800000 -and -not $table.ContainsKey('care_data') -and -not $table.ContainsKey('voice')) {
            $kind = 'xiaozhi-v2-16m'
        } elseif ($table['assets'].size -eq 0x600000 -and $table.ContainsKey('care_data') -and $table.ContainsKey('voice') -and
            $table['care_data'].offset -eq 0xE00000 -and $table['care_data'].size -eq 0x100000 -and
            $table['voice'].offset -eq 0xF00000 -and $table['voice'].size -eq 0x100000) {
            $kind = 'xiaozhi-care-16m'
        } else { throw (T 'd45r_layout_bad') }
        return $kind
    } finally { $stream.Dispose() }
}

function Get-Dp45Esptool([string]$RuntimeRoot) {
    $toolsDir = Join-Path $RuntimeRoot 'tools'
    $dir = Join-Path $toolsDir ('esptool-v' + $script:Dp45EsptoolVersion)
    $exes = @(Get-ChildItem -LiteralPath $dir -Filter 'esptool.exe' -File -Recurse -ErrorAction SilentlyContinue)
    if ($exes.Count -gt 1) { throw (T 'd45r_esptool_multiple') }
    if ($exes.Count -eq 1) {
        Assert-Dp45PlainPath $exes[0].FullName | Out-Null
        if ((Get-Dp45Hash $exes[0].FullName) -cne $script:Dp45EsptoolExeSha.ToUpperInvariant()) {
            throw (T 'd45r_esptool_hash')
        }
        return $exes[0].FullName
    }
    if (Test-Path -LiteralPath $dir) { throw (T 'd45r_esptool_incomplete') }
    if (-not (Test-Path -LiteralPath $toolsDir)) { New-Item -ItemType Directory -Path $toolsDir | Out-Null }
    $zip = Join-Path $toolsDir ('esptool-v' + $script:Dp45EsptoolVersion + '-windows-amd64.zip')
    if (Test-Path -LiteralPath $zip) {
        if ((Get-Dp45Hash $zip) -cne $script:Dp45EsptoolZipSha.ToUpperInvariant()) { throw (T 'd45r_esptool_hash') }
    } else {
        $part = $zip + '.partial-' + [guid]::NewGuid().ToString('N')
        try {
            Receive-XzFileWithActivity $script:Dp45EsptoolUrl $part (T 'activity_download')
            if ((Get-Dp45Hash $part) -cne $script:Dp45EsptoolZipSha.ToUpperInvariant()) { throw (T 'd45r_esptool_hash') }
            Move-Item -LiteralPath $part -Destination $zip
        } finally {
            if (Test-Path -LiteralPath $part) { Remove-Item -LiteralPath $part -Force }
        }
    }
    $tempDir = $dir + '.partial-' + [guid]::NewGuid().ToString('N')
    try {
        Expand-XzArchiveWithActivity $zip $tempDir (T 'activity_extract')
        $found = @(Get-ChildItem -LiteralPath $tempDir -Filter 'esptool.exe' -File -Recurse)
        if ($found.Count -ne 1 -or (Get-Dp45Hash $found[0].FullName) -cne $script:Dp45EsptoolExeSha.ToUpperInvariant()) {
            throw (T 'd45r_esptool_hash')
        }
        Move-Item -LiteralPath $tempDir -Destination $dir
        $relativeExe = $found[0].FullName.Substring($tempDir.Length).TrimStart('\','/')
        return (Join-Path $dir $relativeExe)
    } finally {
        if (Test-Path -LiteralPath $tempDir) { Remove-Item -LiteralPath $tempDir -Recurse -Force }
    }
}

function Invoke-Dp45Tool([string]$Exe, [string[]]$Arguments, [string]$Log = '') {
    return (Invoke-XzProcessActivity $Exe $Arguments (T 'activity_esptool') $Log)
}

function Get-Dp45Mac([string]$Output) {
    $found = [regex]::Matches($Output, '(?im)^\s*(?:Base MAC|MAC):\s*((?:[0-9a-f]{2}:){5}[0-9a-f]{2})\s*$')
    $macs = @($found | ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Select-Object -Unique)
    if ($macs.Count -ne 1) { throw (T 'd45r_mac_missing') }
    return $macs[0]
}

function Get-Dp45Device([string]$Exe,[string]$Port) {
    $baseArgs = @('--chip','esp32s3','--port',$Port,'--baud','115200','--before','default-reset','--after','hard-reset')
    $chip = Invoke-Dp45Tool $Exe ($baseArgs + @('flash-id'))
    if ($chip.Code -ne 0 -or $chip.Text -notmatch 'ESP32-S3') { throw (T 'wrong_chip') }
    if ($chip.Text -notmatch '(?i)Embedded\s+PSRAM\s+8MB|PSRAM\s+8MB') { throw (T 'psram_fail') }
    if ($chip.Text -notmatch '(?i)Detected\s+flash\s+size:\s*16\s*MB|flash\s+size[^\r\n]*16\s*MB') { throw (T 'flash_fail') }
    $security = Invoke-Dp45Tool $Exe ($baseArgs[0..5] + @('--no-stub','get-security-info'))
    if ($security.Code -ne 0 -or $security.Text -notmatch '(?i)Secure\s*Boot:\s*Disabled' -or
        $security.Text -notmatch '(?i)Flash\s*Encryption:\s*Disabled') { throw (T 'security_fail') }
    $macResult = Invoke-Dp45Tool $Exe ($baseArgs + @('read-mac'))
    if ($macResult.Code -ne 0) { throw (T 'd45r_mac_missing') }
    $mac = Get-Dp45Mac $macResult.Text
    return [pscustomobject]@{ Mac=$mac; Port=$Port }
}

function Assert-Dp45DiskSpace([string]$Root) {
    $drivePath = [IO.Path]::GetPathRoot([IO.Path]::GetFullPath($Root))
    $drive = [IO.DriveInfo]::new($drivePath)
    if ($drive.AvailableFreeSpace -lt 150MB) { throw (T 'd45r_disk_low') }
}

function Save-Dp45CurrentFlash([string]$Exe,[string]$Port,[string]$BackupRoot,[string]$Mac) {
    Assert-Dp45DiskSpace $BackupRoot
    $folder = Join-Path $BackupRoot ('antes-restauracion-care-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Path $folder -ErrorAction Stop | Out-Null
    $bin = Join-Path $folder 'flash-completa-16MB.bin'
    $done = $false
    foreach ($baud in @('460800','230400','115200')) {
        $part = Join-Path $folder ('lectura-parcial-' + $baud + '.bin')
        $log = Join-Path $folder ('esptool-respaldo-' + $baud + '.log')
        Write-Host ((T 'backup_try') -f $baud) -ForegroundColor Cyan
        $args = @('--chip','esp32s3','--port',$Port,'--baud',$baud,
            '--before','default-reset','--after','hard-reset','read-flash','0x0','0x1000000',$part)
        $result = Invoke-Dp45Tool $Exe $args $log
        if ($result.Code -eq 0 -and (Test-Path -LiteralPath $part) -and
            (Get-Item -LiteralPath $part).Length -eq $script:Dp45FlashSize) {
            Move-Item -LiteralPath $part -Destination $bin
            $done = $true
            break
        }
        Write-Host ((T 'backup_retry') -f $baud) -ForegroundColor Yellow
    }
    if (-not $done) { throw ((T 'd45r_second_backup_fail') -f $folder) }
    $hash = Get-Dp45Hash $bin
    [IO.File]::WriteAllText((Join-Path $folder 'flash-completa-16MB.sha256.txt'),
        ($hash + '  flash-completa-16MB.bin' + "`r`n"),[Text.Encoding]::ASCII)
    $meta = [ordered]@{ schema='xiaozhi-care-backup-device-v1'; mac=$Mac; captured_at=(Get-Date).ToString('o') }
    $meta | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $folder 'device.json') -Encoding UTF8
    $verified = Test-Dp45Backup $folder
    if ($verified.Sha -cne $hash -or $verified.Mac -cne $Mac) { throw (T 'd45r_second_backup_fail') }
    return $verified
}
