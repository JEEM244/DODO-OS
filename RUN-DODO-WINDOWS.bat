@echo off
setlocal EnableExtensions
title DODO-OS | QEMU Launcher
color 0A
cd /d "%~dp0"

set "ROOT=%CD%"
set "ISO=%ROOT%\build\dodo-os.iso"
set "QEMU="
set "SKIP_BUILD=0"
if /I "%~1"=="--skip-build" set "SKIP_BUILD=1"

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

rem A temporary build directory preserves the last ISO if a rebuild fails.
if "%SKIP_BUILD%"=="1" (
    echo [INFO] Omitiendo compilacion; se usara la ISO existente.
) else (
    where wsl.exe >nul 2>&1
    if not errorlevel 1 (
        echo [1/3] Compilando una ISO temporal desde el codigo actual con WSL...
        echo       Esto requiere una distro Linux con las dependencias del proyecto.
        if not exist "%ROOT%\build" mkdir "%ROOT%\build"
        wsl.exe --cd "%ROOT%" --exec bash -lc "make BUILD_DIR=build/.wsl-build clean all"
        if errorlevel 1 (
            echo.
            echo [AVISO] No se pudo compilar con WSL.
            echo         Se conservara cualquier ISO anterior que ya exista.
        ) else (
            copy /Y "%ROOT%\build\.wsl-build\dodo-os.iso" "%ISO%" >nul
            if errorlevel 1 (
                echo [ERROR] La compilacion termino pero no se pudo copiar la ISO final.
                goto END
            )
            rem Do not leave a checksum from a previous image beside the new image.
            del /Q "%ROOT%\build\dodo-os.iso.sha256" >nul 2>&1
            echo [OK] ISO nueva copiada desde la compilacion temporal.
        )
    ) else (
        echo [AVISO] WSL no esta disponible. Se usara la ISO existente si la hay.
        echo         Para compilar, instala WSL2 Ubuntu y las dependencias del proyecto.
    )
)

if not exist "%ISO%" (
    echo.
    echo [ERROR] No se encontro la ISO:
    echo %ISO%
    echo.
    echo Para compilarla, sigue docs\BUILDING.md o descarga el artefacto de GitHub Actions.
    echo QEMU ejecuta la ISO; no compila el kernel.
    goto END
)

echo.
echo [2/3] ISO lista:
echo       %ISO%
echo.
echo [3/3] Iniciando DODO-OS...
echo.
echo La consola de texto de DODO aparece en la ventana de QEMU.
echo La salida serie tambien se muestra aqui. Prueba: help, about, version, mem, echo hola
echo Puedes escribir con el teclado en QEMU o usar esta consola serie.
echo Para detener DODO, usa halt; para cerrar QEMU, cierra su ventana.
echo.

"%QEMU%" -M q35 -m 512M -vga std -cdrom "%ISO%" -boot d -serial stdio -no-reboot -no-shutdown
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
