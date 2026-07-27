#!/usr/bin/env pwsh
# Setup pre-commit hooks for NukCPGDrop

$ErrorActionPreference = "Stop"

function Test-Command($cmd) {
    return (Get-Command $cmd -ErrorAction SilentlyContinue) -ne $null
}

Write-Host "=== NukCPGDrop Pre-commit Setup ===" -ForegroundColor Cyan

# Check requirements
$checks = @(
    @{ Name = "pre-commit"; Cmd = "pre-commit" },
    @{ Name = "dotnet"; Cmd = "dotnet" },
    @{ Name = "clang-format"; Cmd = "clang-format" },
    @{ Name = "Python"; Cmd = "python" }
)

$missing = @()
foreach ($check in $checks) {
    if (Test-Command $check.Cmd) {
        Write-Host "  [✓] $($check.Name) found" -ForegroundColor Green
    } else {
        Write-Host "  [✗] $($check.Name) NOT found" -ForegroundColor Red
        $missing += $check.Name
    }
}

if ($missing.Count -gt 0) {
    Write-Host "`nMissing tools: $($missing -join ', ')" -ForegroundColor Yellow
    Write-Host "Install missing tools and re-run this script." -ForegroundColor Yellow
    exit 1
}

# Install pre-commit hooks
Write-Host "`nInstalling pre-commit hooks..." -ForegroundColor Cyan
pre-commit install --hook-type pre-commit --hook-type pre-push

# Restore .NET tools
Write-Host "`nRestoring .NET tools..." -ForegroundColor Cyan
dotnet tool restore

# Verify git hooks are executable
$hookDir = ".git/hooks"
if (Test-Path $hookDir) {
    Write-Host "`nGit hooks installed in: $hookDir" -ForegroundColor Cyan
}

Write-Host "`nSetup complete!" -ForegroundColor Green
