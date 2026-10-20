@echo off
setlocal EnableExtensions
title DODO-OS | QEMU Launcher
color 0A
cd /d "%~dp0"

set "ROOT=%CD%"
set "ISO=%ROOT%\build\dodo-os.iso"
set "QEMU="
set "BUILD_OK=0"

echo.
echo  ==========================================
echo           DODO-OS - QEMU LAUNCHER
echo  ==========================================
echo.
echo  Repo: %ROOT%
echo.

rem Find QEMU in PATH first, then common Windows installation paths.
where qemu-system-x86_64.exe >nul 2>&1
if not errorlevel 1 set "QEMU=qemu-system-x86_64.exe"
if not defined QEMU if defined QEMU_HOME if exist "%QEMU_HOME%\qemu-system-x86_64.exe" set "QEMU=%QEMU_HOME%\qemu-system-x86_64.exe"
if not defined QEMU if exist "%ProgramFiles%\qemu\qemu-system-x86_64.exe" set "QEMU=%ProgramFiles%\qemu\qemu-system-x86_64.exe"
if not defined QEMU if exist "%ProgramFiles%\QEMU\qemu-system-x86_64.exe" set "QEMU=%ProgramFiles%\QEMU\qemu-system-x86_64.exe"

if not defined QEMU (
    echo [ERROR] No se encontro qemu-system-x86_64.exe.
    echo.
    echo Instala QEMU para Windows y agrega su carpeta al PATH,
    echo o define QEMU_HOME con la carpeta donde esta el ejecutable.
    echo Descarga oficial: https://www.qemu.org/download/
    goto END
)

echo [OK] QEMU: %QEMU%
echo.

rem Always try to rebuild from the current source when WSL is available.
where wsl.exe >nul 2>&1
if not errorlevel 1 (
    echo [1/3] Intentando compilar la ISO desde el codigo actual con WSL...
    echo       Esto requiere una distro Linux con las dependencias del proyecto.
    wsl.exe --cd "%ROOT%" --exec bash -lc "make clean all"
    if errorlevel 1 (
        echo.
        echo [AVISO] No se pudo compilar con WSL.
        echo         Si ya existe una ISO, se intentara usarla.
    ) else (
        set "BUILD_OK=1"
    )
) else (
    echo [AVISO] WSL no esta disponible. Se usara la ISO existente si la hay.
)

if not exist "%ISO%" (
    echo.
    echo [ERROR] No se encontro la ISO:
    echo %ISO%
    echo.
    echo Para crearla, usa WSL2 Ubuntu e instala las dependencias descritas en:
    echo docs\BUILDING.md
    echo.
    echo QEMU ejecuta la ISO; no compila el kernel.
    goto END
)

echo.
echo [2/3] ISO lista:
echo       %ISO%
echo.
echo [3/3] Iniciando DODO-OS...
echo.
echo La consola interactiva aparece aqui. Prueba: help, about, version, mem, echo hola
echo Para salir, cierra QEMU con Ctrl+C o usa el comando halt dentro de DODO.
echo La pantalla grafica esta desactivada porque esta version usa consola serie.
echo.

"%QEMU%" -M q35 -m 512M -cdrom "%ISO%" -boot d -display none -serial stdio -no-reboot -no-shutdown
set "QEMU_EXIT=%ERRORLEVEL%"
echo.
if not "%QEMU_EXIT%"=="0" echo [AVISO] QEMU termino con codigo %QEMU_EXIT%.

:END
echo.
echo ==========================================
echo  El lanzador termino. Esta ventana no se cerrara.
echo ==========================================
pause
endlocal
