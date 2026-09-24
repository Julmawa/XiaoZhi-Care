param(
    [string]$ProjectPath = "C:\xiaozhi-esp32-128"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PackageRoot = Split-Path -Parent $ScriptDir

Write-Host "XiaoZhi Care Universal U2 - aplicar archivos" -ForegroundColor Cyan
Write-Host "Proyecto destino: $ProjectPath"

if (!(Test-Path $ProjectPath)) {
    throw "No existe el proyecto destino: $ProjectPath"
}

$BackupDir = Join-Path $ProjectPath ("backup-before-care-u2-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null

$items = @(
    "components\care-web",
    "components\care-voice",
    "components\care-mcp",
    "components\care-daily\CMakeLists.txt",
    "main\main.cc"
)

foreach ($item in $items) {
    $src = Join-Path $PackageRoot $item
    $dst = Join-Path $ProjectPath $item
    $bak = Join-Path $BackupDir $item

    if (Test-Path $dst) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $bak) | Out-Null
        Copy-Item $dst $bak -Recurse -Force
    }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
    Copy-Item $src $dst -Recurse -Force
    Write-Host "Reemplazado: $item"
}

$mainCmake = Join-Path $ProjectPath "main\CMakeLists.txt"
if (Test-Path $mainCmake) {
    Copy-Item $mainCmake (Join-Path $BackupDir "main\CMakeLists.txt") -Force
    $text = Get-Content $mainCmake -Raw

    $deps = @("care-daily", "care-web", "care-core", "care-storage", "care-mcp", "care-voice")
    foreach ($dep in $deps) {
        if ($text -notmatch "(?m)^\s*$([regex]::Escape($dep))\s*$") {
            if ($text -match "xiaozhi-fonts") {
                $text = $text -replace "(?m)^(\s*)xiaozhi-fonts\s*$", "`$1$dep`r`n`$1xiaozhi-fonts"
            } else {
                $text = $text -replace "\)\s*$", "    $dep`r`n)"
            }
            Write-Host "Agregada dependencia main: $dep"
        }
    }

    [System.IO.File]::WriteAllText(
        (Resolve-Path $mainCmake),
        $text,
        [System.Text.UTF8Encoding]::new($false)
    )
} else {
    Write-Warning "No encontre main\CMakeLists.txt. Revisar dependencias manualmente."
}

Write-Host ""
Write-Host "Listo. Backup creado en:" -ForegroundColor Green
Write-Host $BackupDir
Write-Host ""
Write-Host "Ahora ejecutar:"
Write-Host "cd $ProjectPath"
Write-Host "idf.py build"
