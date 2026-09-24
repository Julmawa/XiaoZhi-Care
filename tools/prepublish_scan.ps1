param(
    [Parameter(Mandatory=$false)]
    [string]$Root = (Get-Location).Path
)

$ErrorActionPreference = 'Stop'

$SelfPath = $null
try { $SelfPath = (Resolve-Path -LiteralPath $PSCommandPath).Path } catch {}

Write-Host "XiaoZhi Care - pre-publication scan" -ForegroundColor Cyan
Write-Host "Root: $Root"
Write-Host ""

$badExtensions = @('.bin','.elf','.map','.dump','.nvs','.log','.bak','.old')
$badDirNames = @('build','backup','backups','logs','salida')
$hardPatterns = @(
    '-----BEGIN [A-Z ]*PRIVATE KEY-----',
    'gh[pousr]_[A-Za-z0-9_]{20,}',
    'AKIA[0-9A-Z]{16}',
    'sk-[A-Za-z0-9_-]{20,}'
)
$reviewPatterns = @(
    '(?i)\b(password|passwd|wifi_password|api[_-]?key|token|secret)\b\s*[:=]\s*["''][^"'']{3,}["'']',
    '(?i)\b(ssid)\b\s*[:=]\s*["''][^"'']{2,}["'']'
)

$errors = New-Object System.Collections.Generic.List[string]
$warnings = New-Object System.Collections.Generic.List[string]

Get-ChildItem -LiteralPath $Root -Recurse -Force -File | ForEach-Object {
    $f = $_
    if ($badExtensions -contains $f.Extension.ToLowerInvariant()) {
        $errors.Add("Forbidden artifact: " + $f.FullName)
    }
}

Get-ChildItem -LiteralPath $Root -Recurse -Force -Directory | ForEach-Object {
    if ($badDirNames -contains $_.Name.ToLowerInvariant()) {
        $errors.Add("Forbidden directory: " + $_.FullName)
    }
}

$textExtensions = @('.c','.cc','.cpp','.h','.hpp','.md','.txt','.json','.yml','.yaml','.ini','.cfg','.conf','.html','.js','.css','.ps1','.bat','.cmake','.py','.toml')
Get-ChildItem -LiteralPath $Root -Recurse -Force -File | Where-Object {
    $isText = $textExtensions -contains $_.Extension.ToLowerInvariant() -or $_.Name -in @('CMakeLists.txt','Kconfig','Kconfig.projbuild')
    $isSelf = $false
    if ($SelfPath) {
        try { $isSelf = ((Resolve-Path -LiteralPath $_.FullName).Path -eq $SelfPath) } catch {}
    }
    $isText -and (-not $isSelf)
} | ForEach-Object {
    $f = $_
    try {
        $content = Get-Content -LiteralPath $f.FullName -Raw -ErrorAction Stop
        foreach ($p in $hardPatterns) {
            if ($content -match $p) {
                $errors.Add("Possible secret: " + $f.FullName + " [" + $p + "]")
            }
        }
        foreach ($p in $reviewPatterns) {
            if ($content -match $p) {
                $warnings.Add("Review possible credential/example: " + $f.FullName)
            }
        }
    } catch {}
}

Write-Host ""
if ($warnings.Count -gt 0) {
    Write-Host "REVIEW:" -ForegroundColor Yellow
    $warnings | Sort-Object -Unique | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Yellow }
    Write-Host ""
}

if ($errors.Count -gt 0) {
    Write-Host "BLOCKERS:" -ForegroundColor Red
    $errors | Sort-Object -Unique | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
    Write-Host ""
    Write-Host "Result: NOT READY FOR PUBLICATION" -ForegroundColor Red
    exit 2
}

Write-Host "Result: no automatic publication blockers found." -ForegroundColor Green
Write-Host "Manual review is still required." -ForegroundColor Yellow
exit 0
