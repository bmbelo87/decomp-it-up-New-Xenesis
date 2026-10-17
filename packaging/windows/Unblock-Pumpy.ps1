# Desbloqueia o Pumpy.exe contra o Windows SmartScreen (Mark of the Web)
$exePath = Join-Path $PSScriptRoot "Pumpy.exe"
if (Test-Path $exePath) {
    Unblock-File -Path $exePath -ErrorAction SilentlyContinue
    Write-Host "[OK] Pumpy.exe desbloqueado com sucesso! Pode executar normalmente." -ForegroundColor Green
} else {
    Write-Host "[AVISO] Pumpy.exe nao encontrado nesta pasta." -ForegroundColor Yellow
}
Start-Sleep -Seconds 2
