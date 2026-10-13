# XiaoZhi Care DP-045E - actividad visible, reutilizable en Windows PowerShell 5.1.
# Solo funciones; importar este archivo no realiza operaciones de hardware.

function Start-XzActivity([string]$Label) {
    Write-Host ($Label + ' ') -NoNewline -ForegroundColor Cyan
    return @{ Watch = [Diagnostics.Stopwatch]::StartNew(); LastMs = 0L; Closed = $false }
}

function Update-XzActivity([hashtable]$Activity) {
    if ($null -eq $Activity -or $Activity.Closed) { return }
    $ms = [long]$Activity.Watch.ElapsedMilliseconds
    if (($ms - $Activity.LastMs) -ge 1000) {
        Write-Host '.' -NoNewline -ForegroundColor Cyan
        $Activity.LastMs = $ms
    }
}

function Stop-XzActivity([hashtable]$Activity, [bool]$Success = $true) {
    if ($null -eq $Activity -or $Activity.Closed) { return }
    $Activity.Closed = $true
    $Activity.Watch.Stop()
    if ($Success) { Write-Host ' [OK]' -ForegroundColor Green }
    else { Write-Host ' [ERROR]' -ForegroundColor Red }
}

# Espera tareas asincronas sin bloquear visualmente. No confundir puntos con porcentaje.
function Wait-XzTask($Task, [hashtable]$Activity) {
    # No usar Wait(850): si una tarea dura ~1,3 s podria terminar en la
    # segunda espera sin actualizar el indicador al pasar el primer segundo.
    # IsCompleted tampoco envuelve los fallos en AggregateException.
    while (-not $Task.IsCompleted) {
        Start-Sleep -Milliseconds 200
        Update-XzActivity $Activity
    }
    Update-XzActivity $Activity
    # GetResult propaga la excepcion original de la tarea.
    return $Task.GetAwaiter().GetResult()
}

# Hash incremental: evita llamadas a Get-FileHash que pueden bloquear la consola.
# Es el mismo SHA256 estandar; los puntos solo representan tiempo transcurrido.
function Get-XzFileHash([string]$Path, [string]$Label = '') {
    $state = $null
    $ok = $false
    if ($Label) { $state = Start-XzActivity $Label }
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = $null
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        [byte[]]$buffer = New-Object byte[] 65536
        while (($n = $stream.Read($buffer,0,$buffer.Length)) -gt 0) {
            [void]$sha.TransformBlock($buffer,0,$n,$buffer,0)
            if ($state) { Update-XzActivity $state }
        }
        [void]$sha.TransformFinalBlock([byte[]]@(),0,0)
        $ok = $true
        return ([BitConverter]::ToString($sha.Hash)).Replace('-','').ToUpperInvariant()
    } finally {
        if ($stream) { $stream.Dispose() }
        $sha.Dispose()
        if ($state) { Stop-XzActivity $state $ok }
    }
}

# Citar correctamente argumentos Windows (ProcessStartInfo, sin cmd.exe ni shell).
function ConvertTo-XzWindowsArgument([string]$Argument) {
    if ($null -eq $Argument) { throw 'Argumento nulo.' }
    if ($Argument.Length -gt 0 -and $Argument -notmatch '[\s"]') { return $Argument }
    $sb = New-Object Text.StringBuilder
    [void]$sb.Append('"')
    $slashes = 0
    foreach ($ch in $Argument.ToCharArray()) {
        if ($ch -eq [char]'\') { $slashes++; continue }
        if ($ch -eq [char]'"') {
            [void]$sb.Append(('\' * (2 * $slashes + 1)))
            [void]$sb.Append('"')
        } else {
            [void]$sb.Append(('\' * $slashes))
            [void]$sb.Append($ch)
        }
        $slashes = 0
    }
    [void]$sb.Append(('\' * (2 * $slashes)))
    [void]$sb.Append('"')
    return $sb.ToString()
}

# Ejecuta una herramienta nativa sin usar shell; stdout/stderr se consumen
# simultaneamente para evitar deadlocks por buffers llenos.
function Invoke-XzProcessActivity([string]$FilePath,[string[]]$Arguments,[string]$Label,[string]$LogPath = '',[int]$TimeoutSeconds = 0) {
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    $psi.Arguments = (@($Arguments | ForEach-Object { ConvertTo-XzWindowsArgument ([string]$_) }) -join ' ')
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $psi
    $state = Start-XzActivity $Label
    $ok = $false
    try {
        if (-not $process.Start()) { throw 'No se pudo iniciar la herramienta.' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        while (-not $process.WaitForExit(200)) {
            Update-XzActivity $state
            if ($TimeoutSeconds -gt 0 -and $state.Watch.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
                try { $process.Kill(); [void]$process.WaitForExit(3000) } catch { }
                throw 'Tiempo maximo superado en el proceso de prueba.'
            }
        }
        $process.WaitForExit()
        Update-XzActivity $state
        $out = $stdout.GetAwaiter().GetResult()
        $err = $stderr.GetAwaiter().GetResult()
        $resultText = $out + $(if ($err) { "`r`n" + $err } else { '' })
        if ($LogPath) { [IO.File]::WriteAllText($LogPath,$resultText,[Text.Encoding]::UTF8) }
        $ok = ($process.ExitCode -eq 0)
        return [pscustomobject]@{ Code=[int]$process.ExitCode; Text=$resultText }
    } finally {
        Stop-XzActivity $state $ok
        $process.Dispose()
    }
}

# Descarga HTTPS por bloques; los tiempos de espera de red tambien muestran puntos.
# La integridad se comprueba DESPUES por el llamador con su SHA-256 fijado.
function Receive-XzFileWithActivity([string]$Uri,[string]$Destination,[string]$Label) {
    Add-Type -AssemblyName System.Net.Http
    $http = New-Object System.Net.Http.HttpClient
    $http.Timeout = [TimeSpan]::FromMinutes(15)
    $response = $null; $source = $null; $target = $null
    $state = Start-XzActivity $Label
    $ok = $false
    try {
        $task = $http.GetAsync($Uri,[Net.Http.HttpCompletionOption]::ResponseHeadersRead)
        $response = Wait-XzTask $task $state
        $response.EnsureSuccessStatusCode() | Out-Null
        $expectedLength = $response.Content.Headers.ContentLength
        $source = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
        $target = [IO.File]::Create($Destination)
        [byte[]]$buf = New-Object byte[] 65536
        [long]$total = 0
        while ($true) {
            $readTask = $source.ReadAsync($buf,0,$buf.Length)
            $n = Wait-XzTask $readTask $state
            if ($n -eq 0) { break }
            $target.Write($buf,0,$n)
            $total += $n
            Update-XzActivity $state
        }
        $target.Flush()
        if ($expectedLength -and $expectedLength -ne $total) { throw 'Descarga incompleta.' }
        $ok = $true
    } finally {
        if ($target) { $target.Dispose() }
        if ($source) { $source.Dispose() }
        if ($response) { $response.Dispose() }
        $http.Dispose()
        Stop-XzActivity $state $ok
    }
}

# Extraer ZIP revisando que todas las rutas queden dentro de la carpeta destino.
function Expand-XzArchiveWithActivity([string]$Archive,[string]$Destination,[string]$Label) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = $null
    $state = Start-XzActivity $Label
    $ok = $false
    try {
        $destRoot = [IO.Path]::GetFullPath($Destination).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
        [void][IO.Directory]::CreateDirectory($destRoot)
        $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName.Replace('/',[IO.Path]::DirectorySeparatorChar)
            $full = [IO.Path]::GetFullPath((Join-Path $destRoot $name))
            if (-not $full.StartsWith($destRoot,[StringComparison]::OrdinalIgnoreCase)) {
                throw 'ZIP contiene una ruta fuera del destino.'
            }
            if ([string]::IsNullOrEmpty($entry.Name)) {
                [void][IO.Directory]::CreateDirectory($full)
            } else {
                [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($full))
                $inputStream = $null; $outputStream = $null
                try {
                    $inputStream = $entry.Open()
                    $outputStream = [IO.File]::Open($full,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
                    [byte[]]$buf = New-Object byte[] 65536
                    while (($n = $inputStream.Read($buf,0,$buf.Length)) -gt 0) {
                        $outputStream.Write($buf,0,$n)
                        Update-XzActivity $state
                    }
                } finally {
                    if ($outputStream) { $outputStream.Dispose() }
                    if ($inputStream) { $inputStream.Dispose() }
                }
            }
            Update-XzActivity $state
        }
        $ok = $true
    } finally {
        if ($zip) { $zip.Dispose() }
        Stop-XzActivity $state $ok
    }
}

# Copia archivos grandes manteniendo puntos mientras avanza la transferencia.
function Copy-XzFileWithActivity([string]$Source,[string]$Destination,[string]$Label) {
    $state = Start-XzActivity $Label
    $inputStream = $null; $outputStream = $null; $ok = $false
    try {
        $inputStream = [IO.File]::OpenRead($Source)
        $outputStream = [IO.File]::Open($Destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
        [byte[]]$buf = New-Object byte[] 65536
        while (($n = $inputStream.Read($buf,0,$buf.Length)) -gt 0) {
            $outputStream.Write($buf,0,$n)
            Update-XzActivity $state
        }
        $outputStream.Flush()
        $ok = $true
    } finally {
        if ($outputStream) { $outputStream.Dispose() }
        if ($inputStream) { $inputStream.Dispose() }
        Stop-XzActivity $state $ok
    }
}

# Compresion controlada por bloques; no incorpora nada fuera del directorio.
function Compress-XzDirectoryWithActivity([string]$SourceDirectory,[string]$ZipPath,[string]$Label) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $base = [IO.Path]::GetFullPath($SourceDirectory).TrimEnd('\','/')
    if (-not (Test-Path -LiteralPath $base -PathType Container)) { throw 'Carpeta de origen ausente.' }
    if (Test-Path -LiteralPath $ZipPath) { throw 'El ZIP ya existe; no se sobrescribira.' }
    $state = Start-XzActivity $Label
    $archive = $null; $ok = $false
    try {
        $archive = [IO.Compression.ZipFile]::Open($ZipPath,[IO.Compression.ZipArchiveMode]::Create)
        foreach ($file in @(Get-ChildItem -LiteralPath $base -Recurse -File | Sort-Object FullName)) {
            $relative = $file.FullName.Substring($base.Length+1).Replace('\','/')
            $entry = $archive.CreateEntry($relative,[IO.Compression.CompressionLevel]::Optimal)
            $source = $null; $target = $null
            try {
                $source = [IO.File]::OpenRead($file.FullName)
                $target = $entry.Open()
                [byte[]]$buf = New-Object byte[] 65536
                while (($n = $source.Read($buf,0,$buf.Length)) -gt 0) {
                    $target.Write($buf,0,$n)
                    Update-XzActivity $state
                }
            } finally {
                if ($target) { $target.Dispose() }
                if ($source) { $source.Dispose() }
            }
            Update-XzActivity $state
        }
        $ok = $true
    } finally {
        if ($archive) { $archive.Dispose() }
        Stop-XzActivity $state $ok
    }
}
