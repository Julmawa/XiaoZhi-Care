# XiaoZhi Care - DP-045D - Restauracion asistida ESP32-S3 N16R8.
# Modo predeterminado: Verify (SOLO archivos; NO esptool ni hardware).
# Preflight: comprueba placa sin escribir Flash.
# BackupOnly: conserva un respaldo del estado actual; NO contiene escritura de Flash.
# Restore: REQUIERE -EnableFlashWrite y doble consentimiento; NO probado en hardware.
[CmdletBinding()]
param(
    [ValidateSet('Verify','Preflight','BackupOnly','Restore')][string]$Mode = 'Verify',
    [string]$DetectedCulture = '',
    [string]$BackupName = '',
    [string]$Port = '',
    [switch]$EnableFlashWrite,
    [switch]$AllowUnboundBackup
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
try {
    $i18n = Join-Path $Root 'i18n.ps1'
    $core = Join-Path $Root 'restore-core.ps1'
    if (-not (Test-Path -LiteralPath $i18n) -or -not (Test-Path -LiteralPath $core)) {
        throw 'Faltan i18n.ps1 o restore-core.ps1.'
    }
    . $i18n
    Select-XzLanguage $DetectedCulture
    . $core

    Write-Host ''
    Write-Host ('=== ' + (T 'd45r_title') + ' ===') -ForegroundColor Cyan
    if ($Mode -eq 'Verify') {
        Write-Host (T 'd45r_verify_intro') -ForegroundColor Green
    } elseif ($Mode -eq 'Preflight') {
        Write-Host (T 'd45r_preflight_intro') -ForegroundColor Yellow
    } elseif ($Mode -eq 'BackupOnly') {
        Write-Host (T 'd45r_backup_only_intro') -ForegroundColor Cyan
    } else {
        Write-Host (T 'd45r_restore_intro') -ForegroundColor Red
        if (-not $EnableFlashWrite) { throw (T 'd45r_write_locked') }
    }

    $rootBackup = Get-Dp45BackupRoot
    $backups = @(Get-Dp45AvailableBackups $rootBackup)
    $folder = ''
    if ($BackupName) {
        if ($BackupName -cnotmatch '^antes-xiaozhi-care-\d{8}-\d{6}$') { throw (T 'd45_invalid') }
        $selected = @($backups | Where-Object { $_.Name -ceq $BackupName })
        if ($selected.Count -ne 1) { throw (T 'd45_out_of_range') }
        $folder = $selected[0].FullName
    } else {
        Write-Host ''
        Write-Host ('=== ' + (T 'd45_available') + ' ===') -ForegroundColor Cyan
        for ($i=0; $i -lt $backups.Count; $i++) {
            Write-Host ('[{0}] {1}' -f ($i+1), $backups[$i].Name)
        }
        $choice = Read-Host (T 'd45_choose')
        if ([string]::IsNullOrWhiteSpace($choice)) {
            Write-Host (T 'd45_cancelled') -ForegroundColor Yellow
            exit 0
        }
        $index = 0
        if (-not [int]::TryParse($choice,[ref]$index) -or $index -lt 1 -or $index -gt $backups.Count) {
            throw (T 'd45_invalid')
        }
        $folder = $backups[$index-1].FullName
    }
    $backup = Test-Dp45Backup $folder
    Write-Host ('[OK] ' + (T 'backup_size_ok')) -ForegroundColor Green
    Write-Host ('[OK] ' + ((T 'sha_ok') -f $backup.Sha)) -ForegroundColor Green
    if ($backup.Mac) { Write-Host ('[OK] ' + ((T 'd45r_backup_mac') -f $backup.Mac)) -ForegroundColor Green }
    else { Write-Host (T 'd45r_backup_no_mac') -ForegroundColor Yellow }

    if ($Mode -eq 'Verify') {
        Write-Host ('[OK] ' + (T 'd45_done')) -ForegroundColor Green
        exit 0
    }

    $layout = Test-Dp45FlashLayout $backup.Bin
    Write-Host ('[OK] ' + ((T 'd45r_layout') -f $layout)) -ForegroundColor Green

    if ($Port -and $Port -notmatch '\ACOM[1-9][0-9]{0,2}\z') { throw (T 'd45r_port_bad') }
    if (-not $Port) {
        $ports = @([IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
        if ($ports.Count -eq 0) { throw (T 'no_com') }
        if ($ports.Count -eq 1) { $Port = $ports[0] }
        else {
            for($i=0; $i -lt $ports.Count; $i++) { Write-Host ('[{0}] {1}' -f ($i+1),$ports[$i]) }
            $answer = Read-Host (T 'choose_port')
            $idx = 0
            if (-not [int]::TryParse($answer,[ref]$idx) -or $idx -lt 1 -or $idx -gt $ports.Count) {
                throw (T 'd45_invalid')
            }
            $Port = $ports[$idx-1]
        }
    }
    Write-Host ((T 'connecting') -f $Port) -ForegroundColor Cyan
    $exe = Get-Dp45Esptool $Root
    $device = Get-Dp45Device $exe $Port
    Write-Host ('[OK] ' + ((T 'd45r_target_mac') -f $device.Mac)) -ForegroundColor Green
    if ($backup.Mac) {
        if ($backup.Mac -cne $device.Mac) { throw (T 'd45r_mac_mismatch') }
        Write-Host ('[OK] ' + (T 'd45r_mac_matched')) -ForegroundColor Green
    } else {
        Write-Host (T 'd45r_identity_unproven') -ForegroundColor Yellow
        if ($Mode -eq 'Restore' -and -not $AllowUnboundBackup) { throw (T 'd45r_legacy_locked') }
    }
    if ($Mode -eq 'Preflight') {
        Write-Host ('[OK] ' + (T 'd45r_preflight_done')) -ForegroundColor Green
        exit 0
    }

    # Modalidad dedicada a comprobar el segundo respaldo en hardware real.
    # Este bloque TERMINA ANTES de cualquier codigo de escritura de Flash.
    if ($Mode -eq 'BackupOnly') {
        Write-Host (T 'd45r_second_intro') -ForegroundColor Yellow
        $confirmBackup = Read-Host (T 'd45r_second_prompt')
        if ($confirmBackup -cne 'RESPALDAR') {
            Write-Host (T 'd45_cancelled') -ForegroundColor Yellow
            exit 0
        }
        Write-Host (T 'd45r_backup_only_wait') -ForegroundColor Cyan
        $current = Save-Dp45CurrentFlash $exe $Port $rootBackup $device.Mac
        Write-Host ('[OK] ' + ((T 'd45r_second_done') -f $current.Folder)) -ForegroundColor Green
        Write-Host ('[OK] ' + (T 'd45r_backup_only_done')) -ForegroundColor Green
        Write-Host ((T 'd45r_private_paths') -f $current.Folder) -ForegroundColor Yellow
        exit 0
    }

    # A partir de aqui: Restore explicitamente habilitado, aun SIN escritura.
    # Se crea ANTES un segundo respaldo completo verificado del estado actual.
    Write-Host (T 'd45r_second_intro') -ForegroundColor Yellow
    $confirmBackup = Read-Host (T 'd45r_second_prompt')
    if ($confirmBackup -cne 'RESPALDAR') { throw (T 'd45_cancelled') }
    $current = Save-Dp45CurrentFlash $exe $Port $rootBackup $device.Mac
    Write-Host ('[OK] ' + ((T 'd45r_second_done') -f $current.Folder)) -ForegroundColor Green

    if (-not $backup.Mac) {
        # Respaldo historico sin MAC: NO se simula una identidad verificada.
        $legacy = Read-Host ((T 'd45r_legacy_prompt') -f $device.Mac)
        if ($legacy -cne ('LEGADO ' + $device.Mac)) { throw (T 'd45_cancelled') }
    }

    # Revalidar fuente tras la lectura prolongada y comprobar misma placa.
    $again = Test-Dp45Backup $folder
    if ($again.Sha -cne $backup.Sha) { throw (T 'sha_mismatch') }
    $another = Get-Dp45Device $exe $Port
    if ($another.Mac -cne $device.Mac) { throw (T 'd45r_device_changed') }
    Assert-Dp45DiskSpace $rootBackup

    Write-Host (T 'd45r_danger') -ForegroundColor Red
    Write-Host ((T 'd45r_second_done') -f $current.Folder) -ForegroundColor Yellow
    $token = 'RESTAURAR ' + $device.Mac
    $typed = Read-Host ((T 'd45r_final_prompt') -f $token)
    if ($typed -cne $token) { throw (T 'd45_cancelled') }

    # PUNTO DE NO RETORNO: una sola escritura completa, sin erase-flash previo.
    # Keep conserva cabecera original del bootloader; no se reajusta DIO/frecuencia/tamano.
    Write-Host (T 'd45r_write_started') -ForegroundColor Red
    $logWrite = Join-Path $current.Folder 'esptool-restauracion.log'
    $writeArgs = @('--chip','esp32s3','--port',$Port,'--baud','115200',
        '--before','default-reset','--after','hard-reset','write-flash',
        '--flash-mode','keep','--flash-freq','keep','--flash-size','keep','0x0',$backup.Bin)
    $written = Invoke-Dp45Tool $exe $writeArgs $logWrite
    if ($written.Code -ne 0) { throw ((T 'd45r_write_failed') -f $current.Folder) }

    # Verificar realmente TODO el contenido de la Flash, no solo el resultado de esptool.
    $readback = Join-Path $current.Folder 'flash-despues-restauracion-16MB.bin'
    $logVerify = Join-Path $current.Folder 'esptool-verificacion.log'
    Write-Host (T 'd45r_readback_started') -ForegroundColor Cyan
    $rb = Invoke-Dp45Tool $exe @('--chip','esp32s3','--port',$Port,'--baud','115200',
        '--before','default-reset','--after','hard-reset','read-flash','0x0','0x1000000',$readback) $logVerify
    if ($rb.Code -ne 0 -or -not (Test-Path -LiteralPath $readback) -or
        (Get-Item -LiteralPath $readback).Length -ne $script:Dp45FlashSize) {
        throw ((T 'd45r_readback_failed') -f $current.Folder)
    }
    if ((Get-Dp45Hash $readback) -cne $backup.Sha) {
        throw ((T 'd45r_readback_mismatch') -f $current.Folder)
    }
    Write-Host ('[OK] ' + (T 'd45r_restore_done')) -ForegroundColor Green
    Write-Host ((T 'd45r_private_paths') -f $current.Folder) -ForegroundColor Yellow
    exit 0
} catch {
    Write-Host ''
    Write-Host ('[ERROR] ' + $_.Exception.Message) -ForegroundColor Red
    if (Get-Command T -ErrorAction SilentlyContinue) {
        Write-Host (T 'd45r_no_autoretry') -ForegroundColor Yellow
    }
    exit 1
}
