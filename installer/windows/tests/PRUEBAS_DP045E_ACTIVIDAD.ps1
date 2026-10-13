# DP-045E - pruebas offline. NO esptool, NO puertos COM, NO archivos privados.
$ErrorActionPreference = 'Stop'
$runtime = Join-Path $PSScriptRoot '..\runtime'
. (Join-Path $runtime 'activity.ps1')
$passed = 0
$temp = Join-Path $env:TEMP ('XZ_DP045E_TEST_' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($temp)
function Assert-Xz([bool]$Ok,[string]$Description) {
    if (-not $Ok) { throw ('Fallo: ' + $Description) }
    $script:passed++
    Write-Host ('[OK] ' + $Description) -ForegroundColor Green
}
try {
    $file = Join-Path $temp 'archivo simulado con espacios.bin'
    [byte[]]$data = New-Object byte[] (2MB + 47)
    $rnd = [Random]::new(2037)
    $rnd.NextBytes($data)
    [IO.File]::WriteAllBytes($file,$data)
    $h = Get-XzFileHash $file 'Calculando SHA-256 de prueba'
    $reference = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
    Assert-Xz ($h -ceq $reference) 'SHA-256 incremental identico al estandar'

    $state = Start-XzActivity 'Comprobando puntos de actividad'
    Start-Sleep -Milliseconds 1150
    Update-XzActivity $state
    Stop-XzActivity $state $true
    Assert-Xz ($state.Closed -and $state.LastMs -ge 1000) 'Punto visible tras un segundo y cierre correcto'

    # El reloj empieza ANTES de lanzar la tarea: prueba el tiempo realmente
    # disponible para ver el avance, sin depender de la velocidad del equipo.
    $state = Start-XzActivity 'Esperando tarea asincrona'
    $task = [Threading.Tasks.Task]::Delay(1300)
    $null = Wait-XzTask $task $state
    Stop-XzActivity $state $true
    Assert-Xz ($state.Closed -and $state.LastMs -ge 1000) 'Espera asincrona mantiene puntos sin bloqueo'

    $arg = 'C:\carpeta de prueba\archivo con espacios.bin'
    $encoded = ConvertTo-XzWindowsArgument $arg
    Assert-Xz ($encoded -ceq ('"' + $arg + '"')) 'Argumentos con espacios entrecomillados'

    $psExe = Join-Path $PSHOME 'powershell.exe'
    if (-not (Test-Path -LiteralPath $psExe)) { throw 'Prueba requiere Windows PowerShell 5.1.' }
    $valid = Invoke-XzProcessActivity $psExe @('-NoProfile','-NonInteractive','-Command',"Start-Sleep -Seconds 2; Write-Output 'XZ_PRUEBA_OK'") 'Ejecutando proceso ficticio' '' 15
    Assert-Xz ($valid.Code -eq 0 -and $valid.Text.Contains('XZ_PRUEBA_OK')) 'Proceso nativo: puntos durante ejecucion y salida correcta'

    $bad = Invoke-XzProcessActivity $psExe @('-NoProfile','-NonInteractive','-Command',"[Console]::Error.WriteLine('XZ_ERROR_SIMULADO'); exit 7") 'Probando error ficticio' '' 15
    Assert-Xz ($bad.Code -eq 7 -and $bad.Text.Contains('XZ_ERROR_SIMULADO')) 'Codigo de salida y stderr sin ocultar el error'

    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = Join-Path $temp 'archivos de prueba.zip'
    $z = [IO.Compression.ZipFile]::Open($zip,[IO.Compression.ZipArchiveMode]::Create)
    try {
        $entry = $z.CreateEntry('subdirectorio/prueba.txt')
        $writer = New-Object IO.StreamWriter($entry.Open())
        try { $writer.Write('DP045E') } finally { $writer.Dispose() }
    } finally { $z.Dispose() }
    $dest = Join-Path $temp 'extraido'
    Expand-XzArchiveWithActivity $zip $dest 'Extrayendo ZIP ficticio'
    Assert-Xz (([IO.File]::ReadAllText((Join-Path $dest 'subdirectorio\prueba.txt'))) -ceq 'DP045E') 'Extraccion controlada de ZIP'

    $unsafeZip = Join-Path $temp 'ruta-insegura.zip'
    $z = [IO.Compression.ZipFile]::Open($unsafeZip,[IO.Compression.ZipArchiveMode]::Create)
    try {
        $entry = $z.CreateEntry('../archivo_fuera_del_directorio.txt')
        $writer = New-Object IO.StreamWriter($entry.Open())
        try { $writer.Write('DENEGAR') } finally { $writer.Dispose() }
    } finally { $z.Dispose() }
    $rejected = $false
    try { Expand-XzArchiveWithActivity $unsafeZip (Join-Path $temp 'aislado') 'Probando ZIP peligroso' }
    catch { $rejected = $true }
    Assert-Xz ($rejected -and -not (Test-Path (Join-Path $temp 'archivo_fuera_del_directorio.txt'))) 'ZIP con ruta externa rechazado'

    $copied = Join-Path $temp 'copia de prueba.bin'
    Copy-XzFileWithActivity $file $copied 'Copiando fichero ficticio'
    Assert-Xz ((Get-FileHash -LiteralPath $copied -Algorithm SHA256).Hash -ceq $reference) 'Copia por bloques con hash identico'

    $srcFolder = Join-Path $temp 'carpeta paquete'
    [void][IO.Directory]::CreateDirectory($srcFolder)
    [IO.File]::WriteAllText((Join-Path $srcFolder 'ejemplo.txt'),'PAQUETE_DP045E')
    $builtZip = Join-Path $temp 'paquete creado.zip'
    Compress-XzDirectoryWithActivity $srcFolder $builtZip 'Comprimiendo paquete ficticio'
    $testArchive = [IO.Compression.ZipFile]::OpenRead($builtZip)
    try { $entryNames = @($testArchive.Entries | ForEach-Object { $_.FullName }) }
    finally { $testArchive.Dispose() }
    Assert-Xz ($entryNames.Count -eq 1 -and $entryNames[0] -ceq 'ejemplo.txt') 'ZIP creado solo con archivos del origen'

    # Comprobacion de arquitectura: restauracion intacta y bloqueo de escritura conservado.
    $rest = [IO.File]::ReadAllText((Join-Path $runtime 'restaurar.ps1'))
    Assert-Xz ($rest.Contains('if (-not $EnableFlashWrite)') -and
        $rest.Contains("if (`$Mode -eq 'BackupOnly')") -and
        $rest.Contains("'write-flash'") -and $rest.Contains('exit 0')) 'Bloqueos Restore y BackupOnly conservados'

    $p = Join-Path $PSScriptRoot '..\preparar_paquete.ps1'
    $generator = [IO.File]::ReadAllText($p)
    Assert-Xz ($generator.Contains("'activity.ps1'")) 'El generador incluye el modulo de actividad'

    Write-Host ("`nPRUEBAS DP-045E: {0} APROBADAS (SIN HARDWARE)" -f $passed) -ForegroundColor Green
    exit 0
} catch {
    Write-Host ('[ERROR] ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
} finally {
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
}
