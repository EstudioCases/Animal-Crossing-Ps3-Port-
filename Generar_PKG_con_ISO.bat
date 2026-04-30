@echo off
setlocal

cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Generar_PKG_con_ISO.ps1"
set "ERR=%ERRORLEVEL%"

echo.
if not "%ERR%"=="0" (
    echo Fallo con codigo %ERR%.
) else (
    echo Terminado correctamente.
)
echo.
pause
exit /b %ERR%
