@echo off
setlocal

cd /d "%~dp0"

if not exist "gradlew.bat" (
    echo [ERROR] gradlew.bat 不存在
    exit /b 1
)

if exist "%LOCALAPPDATA%\Android\Sdk" (
    set "SDK_DIR=%LOCALAPPDATA%\Android\Sdk"
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$sdk = $env:LOCALAPPDATA + '\Android\Sdk'; Set-Content -Path 'local.properties' -Value ('sdk.dir=' + $sdk.Replace('\','/')) -Encoding ascii"
    echo [INFO] 已写入 local.properties
) else (
    echo [ERROR] 未找到 Android SDK
    echo [HINT] 请先安装 Android SDK，或创建 local.properties 指向 sdk.dir
    echo [HINT] 例如：sdk.dir=C\:\\Users\\你的用户名\\AppData\\Local\\Android\\Sdk
    exit /b 1
)

call gradlew.bat assembleDebug
if errorlevel 1 (
    echo [ERROR] APK 打包失败
    exit /b 1
)

echo.
echo [OK] APK 打包完成
echo [PATH] app\build\outputs\apk\debug\app-debug.apk
exit /b 0