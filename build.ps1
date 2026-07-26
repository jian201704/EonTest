# ============================================================
# EonTest 一键编译脚本
# 用法：在 PowerShell 中执行 .\build.ps1
# ============================================================

$ErrorActionPreference = "Stop"

Write-Host "=== EonTest Build ===" -ForegroundColor Cyan
Write-Host "Initializing VS 2026 x64 Dev Shell..." -ForegroundColor Gray

# 初始化 VS 开发环境
Import-Module "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll" -Force
Enter-VsDevShell -VsInstallPath "C:\Program Files\Microsoft Visual Studio\18\Community" -SkipAutomaticLocation -Arch amd64 | Out-Null

Write-Host "Building..." -ForegroundColor Gray

# 编译
cmake --build e:\SourceCode\EonTest\build --config Release

if ($LASTEXITCODE -eq 0) {
    Write-Host "=== BUILD SUCCESS ===" -ForegroundColor Green
    Write-Host "Output: e:\SourceCode\EonTest\build\bin\" -ForegroundColor Gray
} else {
    Write-Host "=== BUILD FAILED ===" -ForegroundColor Red
    exit 1
}
