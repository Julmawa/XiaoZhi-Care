# XiaoZhi Care DP-045D - Prueba offline del motor de SEGUNDO respaldo.
# Sustituye Invoke-Dp45Tool por un simulador: no lanza procesos ni abre COM.
$ErrorActionPreference = 'Stop'
$Runtime = Join-Path $PSScriptRoot '..\runtime'
. (Join-Path $Runtime 'i18n.ps1')
$script:XzLang = 'es'
. (Join-Path $Runtime 'restore-core.ps1')

$script:Reads = 0
$script:SimulateTruncate = $false
function Invoke-Dp45Tool([string]$Exe, [string[]]$Arguments, [string]$Log = '') {
    if ($Exe -cne '__SIMULADOR_OFFLINE__') { throw 'Se intento ejecutar una herramienta real.' }
    if ($Arguments -contains 'write-flash' -or $Arguments -contains 'erase-flash') {
        throw 'Se intento una operacion de escritura de Flash.'
    }
    if ($Arguments -notcontains 'read-flash') { throw 'Operacion de simulacion desconocida.' }
    $script:Reads++
    $out = $Arguments[$Arguments.Count - 1]
    $stream = [IO.File]::Create($out)
    try {
        if ($script:SimulateTruncate) { $stream.SetLength(28) }
        else { $stream.SetLength(16777216) }
    } finally { $stream.Dispose() }
    if ($Log) { [IO.File]::WriteAllText($Log,'SIMULADOR_OFFLINE',[Text.Encoding]::UTF8) }
    return [pscustomobject]@{ Code=0; Text='SIMULADOR_OFFLINE' }
}

$temp = Join-Path $env:TEMP ('DP045D_BACKUP_TEST_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
$passed = 0
try {
    $old = Save-Dp45CurrentFlash '__SIMULADOR_OFFLINE__' 'COM999' $temp 'AA:BB:CC:DD:EE:FF'
    if ($script:Reads -ne 1) { throw 'No se hizo exactamente una lectura simulada.' }
    if (-not (Test-Path -LiteralPath $old.Bin)) { throw 'Falta BIN simulado.' }
    if ((Get-Item -LiteralPath $old.Bin).Length -ne 16777216) { throw 'Tamano incorrecto.' }
    if ((Get-Dp45Hash $old.Bin) -cne $old.Sha) { throw 'SHA incorrecto.' }
    if ($old.Mac -cne 'AA:BB:CC:DD:EE:FF') { throw 'Fallo la MAC simulada.' }
    if (-not (Test-Path -LiteralPath (Join-Path $old.Folder 'device.json'))) { throw 'Falta device.json.' }
    $passed++
    Write-Host '[OK] Segundo respaldo simulado de 16 MB: SHA y MAC correctos.' -ForegroundColor Green

    $script:SimulateTruncate = $true
    $failed = $false
    try {
        Save-Dp45CurrentFlash '__SIMULADOR_OFFLINE__' 'COM999' $temp 'AA:BB:CC:DD:EE:FF' | Out-Null
    } catch { $failed = $true }
    if (-not $failed) { throw 'Un respaldo truncado fue aceptado.' }
    if ($script:Reads -ne 4) { throw 'No se realizaron los 3 reintentos previstos.' }
    $passed++
    Write-Host '[OK] Lecturas truncadas rechazadas sin ejecutar escritura.' -ForegroundColor Green

    $text = [IO.File]::ReadAllText((Join-Path $Runtime 'restaurar.ps1'))
    if (-not $text.Contains("[ValidateSet('Verify','Preflight','BackupOnly','Restore')]")) {
        throw 'Falta la opcion BackupOnly.'
    }
    # Seleccionar SOLO la rama independiente, no el elseif del encabezado.
    # La busqueda comienza al principio de una linea, para excluir el elseif.
    $branches = [regex]::Matches($text, '(?m)^[ \t]*if[ \t]*\(\$Mode[ \t]+-eq[ \t]+''BackupOnly''\)[ \t]*\{[ \t]*\r?$')
    if ($branches.Count -ne 1) { throw 'No se encontro una unica rama BackupOnly independiente.' }
    $start = $branches[0].Index
    $restore = $text.IndexOf('# A partir de aqui: Restore explicitamente habilitado', [StringComparison]::Ordinal)
    if ($restore -lt 0 -or $start -ge $restore) { throw 'BackupOnly no esta separado de Restore.' }
    # TrimEnd solo normaliza el espacio BLANCO que queda entre el cierre
    # de BackupOnly y el comentario de la siguiente seccion Restore.
    # Sigue siendo obligatorio un exit 0 inmediatamente antes del cierre.
    $branch = $text.Substring($start, $restore - $start)
    $read = $branch.IndexOf('Save-Dp45CurrentFlash', [StringComparison]::Ordinal)
    $finalExit = $branch.LastIndexOf('exit 0', [StringComparison]::Ordinal)
    if ($read -lt 0 -or $finalExit -lt $read -or
        $branch.TrimEnd() -notmatch '(?m)^[ \t]*exit[ \t]+0[ \t]*\r?\n[ \t]*\}\z' -or
        $branch -match '(?i)write-flash|erase-flash|EnableFlashWrite') {
        throw 'La rama BackupOnly no termina antes de escribir.'
    }
    $passed++
    Write-Host '[OK] Codigo BackupOnly separado y terminado antes de Restore.' -ForegroundColor Green

    $passed++
    Write-Host '[OK] Ninguna prueba uso COM, esptool ni respaldos privados.' -ForegroundColor Green
    Write-Host ("`nPRUEBAS SOLO RESPALDAR: {0} APROBADAS" -f $passed) -ForegroundColor Green
    exit 0
} catch {
    Write-Host ('[ERROR] ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
} finally {
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Force -Recurse }
}
