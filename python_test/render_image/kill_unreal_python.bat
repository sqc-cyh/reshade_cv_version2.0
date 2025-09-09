@echo off
echo 正在终止所有Unreal和Python进程...
echo.

REM 检查并终止UnrealEditor进程
echo 检查UnrealEditor进程...
tasklist /FI "IMAGENAME eq UnrealEditor.exe" >nul 2>&1
if %errorlevel% equ 0 (
    echo 发现UnrealEditor进程，正在终止...
    taskkill /F /IM "UnrealEditor.exe" >nul 2>&1
    if %errorlevel% equ 0 (
        echo UnrealEditor进程已终止
    ) else (
        echo 无法终止UnrealEditor进程
    )
) else (
    echo 未发现UnrealEditor进程
)

REM 检查并终止UnrealCEFSubProcess进程
echo 检查UnrealCEFSubProcess进程...
tasklist /FI "IMAGENAME eq UnrealCEFSubProcess.exe" >nul 2>&1
if %errorlevel% equ 0 (
    echo 发现UnrealCEFSubProcess进程，正在终止...
    taskkill /F /IM "UnrealCEFSubProcess.exe" >nul 2>&1
    if %errorlevel% equ 0 (
        echo UnrealCEFSubProcess进程已终止
    ) else (
        echo 无法终止UnrealCEFSubProcess进程
    )
) else (
    echo 未发现UnrealCEFSubProcess进程
)

REM 检查并终止UE4Editor进程
echo 检查UE4Editor进程...
tasklist /FI "IMAGENAME eq UE4Editor.exe" >nul 2>&1
if %errorlevel% equ 0 (
    echo 发现UE4Editor进程，正在终止...
    taskkill /F /IM "UE4Editor.exe" >nul 2>&1
    if %errorlevel% equ 0 (
        echo UE4Editor进程已终止
    ) else (
        echo 无法终止UE4Editor进程
    )
) else (
    echo 未发现UE4Editor进程
)

REM 检查并终止UE5Editor进程
echo 检查UE5Editor进程...
tasklist /FI "IMAGENAME eq UE5Editor.exe" >nul 2>&1
if %errorlevel% equ 0 (
    echo 发现UE5Editor进程，正在终止...
    taskkill /F /IM "UE5Editor.exe" >nul 2>&1
    if %errorlevel% equ 0 (
        echo UE5Editor进程已终止
    ) else (
        echo 无法终止UE5Editor进程
    )
) else (
    echo 未发现UE5Editor进程
)

REM 检查并终止python.exe进程
echo 检查python.exe进程...
tasklist /FI "IMAGENAME eq python.exe" >nul 2>&1
if %errorlevel% equ 0 (
    echo 发现python.exe进程，正在终止...
    taskkill /F /IM "python.exe" >nul 2>&1
    if %errorlevel% equ 0 (
        echo python.exe进程已终止
    ) else (
        echo 无法终止python.exe进程
    )
) else (
    echo 未发现python.exe进程
)

REM 检查并终止pythonw.exe进程
echo 检查pythonw.exe进程...
tasklist /FI "IMAGENAME eq pythonw.exe" >nul 2>&1
if %errorlevel% equ 0 (
    echo 发现pythonw.exe进程，正在终止...
    taskkill /F /IM "pythonw.exe" >nul 2>&1
    if %errorlevel% equ 0 (
        echo pythonw.exe进程已终止
    ) else (
        echo 无法终止pythonw.exe进程
    )
) else (
    echo 未发现pythonw.exe进程
)

REM 检查并终止UnrealTraceServer进程
echo 检查UnrealTraceServer进程...
tasklist /FI "IMAGENAME eq UnrealTraceServer.exe" >nul 2>&1
if %errorlevel% equ 0 (
    echo 发现UnrealTraceServer进程，正在终止...
    taskkill /F /IM "UnrealTraceServer.exe" >nul 2>&1
    if %errorlevel% equ 0 (
        echo UnrealTraceServer进程已终止
    ) else (
        echo 无法终止UnrealTraceServer进程
    )
) else (
    echo 未发现UnrealTraceServer进程
)

REM 使用PowerShell查找并终止其他可能的Unreal相关进程
echo 使用PowerShell查找其他Unreal相关进程...
powershell -Command "Get-Process | Where-Object {$_.ProcessName -like '*unreal*' -or $_.ProcessName -like '*ue4*' -or $_.ProcessName -like '*ue5*'} | ForEach-Object { Write-Host '发现进程:' $_.ProcessName '(PID:' $_.Id ')'; Stop-Process -Id $_.Id -Force; Write-Host '已终止进程:' $_.ProcessName }"

echo.
echo 所有Unreal和Python进程检查完成！
echo.
pause
