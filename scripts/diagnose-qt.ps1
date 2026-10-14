param(
    [string]$IdaDir = $env:IDADIR,
    [string]$QtPrefix = $env:CANTORDUST_QT_PREFIX
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($IdaDir)) {
    $process = Get-Process -Name 'ida', 'ida64', 'idaq', 'idaq64' `
        -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $process) {
        try {
            $IdaDir = Split-Path $process.Path -Parent
        } catch {
            # Access to the process executable is optional.
        }
    }
}

if ([string]::IsNullOrWhiteSpace($IdaDir)) {
    Write-Host 'IDA path not detected. Run with -IdaDir "C:\path\to\IDA".'
} elseif (-not (Test-Path -LiteralPath $IdaDir -PathType Container)) {
    throw "IDA directory does not exist: $IdaDir"
} else {
    Write-Host "IDA installation: $IdaDir"
    $dlls = @(Get-ChildItem -LiteralPath $IdaDir -Filter 'Qt6Core.dll' `
        -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 3)

    if ($dlls.Count -eq 0) {
        Write-Host 'Qt6Core.dll not found below that directory.'
    } else {
        foreach ($dll in $dlls) {
            Write-Host "Qt runtime DLL: $($dll.FullName)"
            Write-Host "  Product version: $($dll.VersionInfo.ProductVersion)"
            Write-Host "  File version:    $($dll.VersionInfo.FileVersion)"
        }
        Write-Host 'Use the Qt runtime version as a starting point; a custom IDA build may differ.'
    }
}

if ([string]::IsNullOrWhiteSpace($QtPrefix)) {
    Write-Host 'Qt development prefix: not supplied (-QtPrefix).'
} elseif (-not (Test-Path -LiteralPath $QtPrefix -PathType Container)) {
    Write-Host "Qt development prefix does not exist: $QtPrefix"
} else {
    Write-Host "Qt development prefix: $QtPrefix"
    $config = Join-Path $QtPrefix 'lib/cmake/Qt6/Qt6Config.cmake'
    Write-Host "  Qt6Config.cmake found: $(Test-Path -LiteralPath $config -PathType Leaf)"

    $coreTargets = Join-Path $QtPrefix 'lib/cmake/Qt6Core/Qt6CoreTargets.cmake'
    if (Test-Path -LiteralPath $coreTargets -PathType Leaf) {
        $namespaceEvidence = @(Select-String -LiteralPath $coreTargets `
            -Pattern '(?<![A-Za-z0-9_])QT_NAMESPACE=QT(?![A-Za-z0-9_])' `
            -ErrorAction SilentlyContinue)
        if ($namespaceEvidence.Count -gt 0) {
            Write-Host '  Qt6CoreTargets.cmake: exports QT_NAMESPACE=QT.'
        } else {
            Write-Host '  Qt6CoreTargets.cmake: no QT_NAMESPACE=QT found.'
        }
    } else {
        Write-Host '  Qt6CoreTargets.cmake: not found.'
    }
    Write-Host '  CMake will perform the authoritative namespace check.'
}

Write-Host 'Expected for this plugin: Qt6 Core/Gui/Widgets development files, namespace QT.'
