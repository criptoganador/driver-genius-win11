# =====================================================
# cleanup_phantom_devices.ps1
# Limpia dispositivos fantasma de instalaciones anteriores
# EJECUTAR COMO ADMINISTRADOR
# =====================================================

# Verificar que se corre como Administrador
if (-NOT ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "[ERROR] Este script necesita ejecutarse como Administrador." -ForegroundColor Red
    Write-Host "Haz clic derecho en PowerShell y selecciona 'Ejecutar como administrador'."
    pause
    exit 1
}

Write-Host "=======================================" -ForegroundColor Cyan
Write-Host " Limpieza de dispositivos fantasma USB " -ForegroundColor Cyan
Write-Host "=======================================" -ForegroundColor Cyan
Write-Host ""

# Encontrar todos los dispositivos Unknown del VID/PID de la cámara
$fantasmas = Get-PnpDevice | Where-Object {
    $_.InstanceId -like "*VID_0C45&PID_60B0*" -and
    $_.Status -ne "OK"
}

if ($fantasmas.Count -eq 0) {
    Write-Host "[OK] No se encontraron dispositivos fantasma. El sistema está limpio." -ForegroundColor Green
    pause
    exit 0
}

Write-Host "Se encontraron $($fantasmas.Count) dispositivo(s) fantasma:" -ForegroundColor Yellow
Write-Host ""
$fantasmas | ForEach-Object {
    Write-Host "  -> $($_.FriendlyName) [$($_.InstanceId)]" -ForegroundColor Gray
}

Write-Host ""
Write-Host "Eliminando..." -ForegroundColor Yellow
Write-Host ""

$eliminados = 0
$errores = 0

$fantasmas | ForEach-Object {
    $result = & pnputil /remove-device $_.InstanceId 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  [OK] Eliminado: $($_.FriendlyName)" -ForegroundColor Green
        $eliminados++
    } else {
        Write-Host "  [SKIP] No se pudo eliminar: $($_.FriendlyName)" -ForegroundColor DarkYellow
        $errores++
    }
}

Write-Host ""
Write-Host "=======================================" -ForegroundColor Cyan
Write-Host "  Eliminados: $eliminados | Omitidos: $errores" -ForegroundColor Cyan
Write-Host "=======================================" -ForegroundColor Cyan
pause
