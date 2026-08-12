$ErrorActionPreference = "Stop"

$source = Split-Path -Parent $MyInvocation.MyCommand.Path
$target = Join-Path $env:USERPROFILE ".vscode\extensions\gum-syntax-0.3.0"
$oldTargets = @(
    (Join-Path $env:USERPROFILE ".vscode\extensions\seklang-syntax-0.2.0"),
    (Join-Path $env:USERPROFILE ".vscode\extensions\seklang-syntax-0.1.0"),
    (Join-Path $env:USERPROFILE ".vscode\extensions\seklang-syntax-0.0.1")
)

foreach ($path in @($target) + $oldTargets) {
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force
    }
}

New-Item -ItemType Directory -Path $target -Force | Out-Null
Copy-Item -Path (Join-Path $source "*") -Destination $target -Recurse -Force

Write-Host "Gum extension installed to $target"
Write-Host "Restart VS Code or run: Developer: Reload Window"
