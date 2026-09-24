$ErrorActionPreference = 'Stop'
$old = $ProgressPreference
$ProgressPreference = 'Continue'
try {
    Write-Host ''
    Write-Host 'PRUEBA VISUAL DE BARRA - NO TOCA LA ESP32' -ForegroundColor Cyan
    Write-Host 'La barra debe actualizarse sin agregar una linea por porcentaje.'
    Write-Host ''
    for ($i = 0; $i -le 100; $i++) {
        Write-Progress -Id 77 -Activity 'Probando barra de progreso' -Status (('{0}% completado' -f $i)) -PercentComplete $i
        Start-Sleep -Milliseconds 35
    }
    Write-Progress -Id 77 -Activity 'Probando barra de progreso' -Completed
    Write-Host '[OK] Prueba terminada.' -ForegroundColor Green
}
finally {
    $ProgressPreference = $old
}
