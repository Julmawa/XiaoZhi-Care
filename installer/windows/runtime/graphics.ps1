# DP-045. Offline package verification and selection; no serial-port access.
function Get-CareRequestedProfile([string]$SelectionPath, [string]$ProfileId) {
    if ([string]::IsNullOrWhiteSpace($SelectionPath)) { return $ProfileId }
    if (-not [string]::IsNullOrWhiteSpace($ProfileId)) { throw 'Usar perfil o archivo de preferencias, no ambos.' }
    $record = Get-Content -LiteralPath $SelectionPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $p = $record.selection
    if ($record.schema -ne 'xiaozhi-care-graphic-selection' -or $record.schema_version -ne 1 -or
        $p.chip -ne 'ESP32-S3' -or $p.board_sku -ne 'bread-compact-wifi-lcd' -or
        $p.lcd_width -ne 240 -or $p.lcd_height -ne 240 -or $p.font_family -ne 'Alibaba PuHuiTi' -or
        $p.emoji_family -ne 'Twemoji' -or $p.profile_id -notmatch '^fuente-(22|26|30)-emoji-(32|64)-(oscuro|claro)$' -or
        $p.profile_id -ne ('fuente-{0}-emoji-{1}-{2}' -f $p.font_px, $p.emoji_px, $p.theme)) {
        throw 'Preferencias de etapa 1 invalidas o incompatibles.'
    }
    # Preferences never authorize flashing, regardless of flags in the JSON.
    # The caller must still verify the complete catalog and normal installer checks.
    return [string]$p.profile_id
}

function Get-CareHash([string]$Path) {
    if (Get-Command Get-XzFileHash -ErrorAction SilentlyContinue) {
        return (Get-XzFileHash $Path (T 'activity_sha')).ToLowerInvariant()
    }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-CareVoiceHash([string]$Path) {
    $length = (Get-Item -LiteralPath $Path).Length
    if ($length -lt 12 -or $length -gt 0x600000) { throw 'Assets fuera de la particion de 6 MiB.' }
    [byte[]]$data = [IO.File]::ReadAllBytes($Path)
    $count = [BitConverter]::ToUInt32($data, 0)
    $payload = [BitConverter]::ToUInt32($data, 8)
    if ($count -lt 1 -or $count -gt 1024 -or $payload -ne ($length - 12) -or $count * 44 -gt $payload) {
        throw 'Cabecera de recursos invalida.'
    }
    $entries = @{}
    $cursor = [long](12 + $count * 44)
    $ranges = @()
    for ($i = 0; $i -lt $count; $i++) {
        $pos = 12 + $i * 44
        $name = [Text.Encoding]::ASCII.GetString($data, $pos, 32).Split([char]0)[0]
        $size = [BitConverter]::ToUInt32($data, $pos + 32)
        $offset = [BitConverter]::ToUInt32($data, $pos + 36)
        $start = [long](12 + $count * 44) + $offset
        $end = $start + 2 + $size
        if ($name -notmatch '^[A-Za-z0-9_.-]+$' -or $entries.ContainsKey($name) -or $size -eq 0 -or
            $end -gt $length -or $data[$start] -ne 0x5A -or $data[$start + 1] -ne 0x5A) {
            throw 'Recurso duplicado, truncado o fuera de limites.'
        }
        $entries[$name] = @{start=($start + 2); size=$size}
        $ranges += [pscustomobject]@{start=$start; end=$end}
    }
    foreach ($range in ($ranges | Sort-Object start)) {
        if ($range.start -ne $cursor) { throw 'Recursos superpuestos o sin referenciar.' }
        $cursor = $range.end
    }
    if ($cursor -ne $length -or -not $entries.ContainsKey('index.json')) { throw 'Indice de recursos ausente.' }
    $indexEntry = $entries['index.json']
    $index = [Text.Encoding]::UTF8.GetString($data, $indexEntry.start, $indexEntry.size) | ConvertFrom-Json
    if ($index.version -ne 1 -or -not ($index.srmodels -is [string]) -or -not $entries.ContainsKey($index.srmodels)) {
        throw 'Faltan modelos de reconocimiento de voz.'
    }
    $voice = $entries[$index.srmodels]
    if ($voice.size -lt 4) { throw 'Modelos de voz vacios.' }
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($hash.ComputeHash($data, $voice.start, $voice.size))).Replace('-', '').ToLowerInvariant() }
    finally { $hash.Dispose() }
}

function Test-CareGraphicsPackage([string]$PackageRoot, $Release) {
    if ($Release.graphics.catalog -ne 'graphics/graphics.json' -or $Release.graphics.sha256 -notmatch '^[a-f0-9]{64}$') {
        throw 'Referencia de catalogo grafico invalida.'
    }
    $catalogPath = Join-Path $PackageRoot 'graphics\graphics.json'
    if ((Get-CareHash $catalogPath) -cne $Release.graphics.sha256) { throw 'El catalogo grafico cambio.' }
    $catalog = Get-Content -LiteralPath $catalogPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($catalog.schema -ne 'xiaozhi-care-graphics-v2' -or $catalog.schema_version -ne 1 -or
        $catalog.chip -ne 'ESP32-S3' -or $catalog.board_sku -ne $Release.supported_board_sku -or
        $catalog.board_sku -ne 'bread-compact-wifi-lcd' -or $catalog.lcd_width -ne 240 -or
        $catalog.lcd_height -ne 240 -or $catalog.partition_bytes -ne 0x600000) {
        throw 'Catalogo incompatible con ESP32-S3 LCD 240 x 240.'
    }
    $base = Join-Path $PackageRoot 'firmware\generated_assets.bin'
    if ((Get-CareHash (Join-Path $PackageRoot 'firmware\xiaozhi.bin')) -cne $catalog.app_sha256 -or
        (Get-CareHash $base) -cne $catalog.base_assets_sha256 -or
        (Get-CareVoiceHash $base) -cne $catalog.srmodels_sha256) {
        throw 'Los recursos no corresponden al firmware y modelos de voz de este paquete.'
    }
    $ids = @{}
    if (@($catalog.profiles).Count -lt 1) { throw 'No hay perfiles graficos verificados.' }
    foreach ($profile in $catalog.profiles) {
        if ($profile.profile_id -notmatch '^fuente-(22|26|30)-emoji-(32|64)-(oscuro|claro)$' -or
            $ids.ContainsKey($profile.profile_id) -or $profile.file -notmatch '^[a-z0-9-]+\.bin$' -or
            $profile.sha256 -notmatch '^[a-f0-9]{64}$' -or $profile.srmodels_sha256 -cne $catalog.srmodels_sha256 -or
            $profile.font_family -ne 'Alibaba PuHuiTi' -or $profile.emoji_family -ne 'Twemoji' -or
            $profile.profile_id -ne ('fuente-{0}-emoji-{1}-{2}' -f $profile.font_px, $profile.emoji_px, $profile.theme)) {
            throw 'Perfil grafico invalido o ambiguo.'
        }
        $path = Join-Path (Join-Path $PackageRoot 'graphics') $profile.file
        if ($profile.size -le 0 -or $profile.size -gt 0x600000 -or (Get-Item -LiteralPath $path).Length -ne $profile.size -or
            (Get-CareHash $path) -cne $profile.sha256 -or (Get-CareVoiceHash $path) -cne $catalog.srmodels_sha256) {
            throw ('Recursos corruptos, incompletos o modelos de voz diferentes: ' + $profile.profile_id)
        }
        $ids[$profile.profile_id] = $true
    }
    if (-not $ids.ContainsKey($catalog.default_profile)) { throw 'Perfil predeterminado ausente.' }
    return $catalog
}

function Select-CareGraphicsProfile($Catalog, [string]$ProfileId = '', [switch]$NonInteractive) {
    if ([string]::IsNullOrWhiteSpace($ProfileId)) {
        if ($NonInteractive) { $ProfileId = $Catalog.default_profile }
        else {
            Write-Host ''
            Write-Host 'DP-045 - Fuentes / Fonts / Fontes + Emojis' -ForegroundColor Cyan
            Write-Host '22 / 26 px, emojis 32: pendientes / unavailable / indisponiveis.'
            $profiles = @($Catalog.profiles)
            for ($i = 0; $i -lt $profiles.Count; $i++) {
                $p = $profiles[$i]
                Write-Host ('[{0}] {1} {2}px / {3} {4}x{4} / {5}' -f ($i+1), $p.font_family, $p.font_px, $p.emoji_family, $p.emoji_px, $p.theme)
            }
            $answer = Read-Host 'ENTER = PuHuiTi 30 / Twemoji 64 / oscuro; numero'
            if ([string]::IsNullOrWhiteSpace($answer)) { $ProfileId = $Catalog.default_profile }
            else {
                $number = 0
                if (-not [int]::TryParse($answer, [ref]$number) -or $number -lt 1 -or $number -gt $profiles.Count) {
                    throw 'Seleccion grafica invalida.'
                }
                $ProfileId = $profiles[$number-1].profile_id
            }
        }
    }
    $matches = @($Catalog.profiles | Where-Object { $_.profile_id -ceq $ProfileId })
    if ($matches.Count -ne 1) { throw ('No existe un paquete completo verificado para: ' + $ProfileId) }
    return $matches[0]
}
