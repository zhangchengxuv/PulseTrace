param(
    [ValidateSet('MSVC', 'PortableLLVM')][string]$Toolchain = 'MSVC',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $projectRoot
try {
    if ($Toolchain -eq 'PortableLLVM') {
        $toolRoot = Join-Path $projectRoot 'build\tools'
        $llvm = Join-Path $toolRoot 'llvm-mingw-20260616-ucrt-x86_64\bin'
        $cmake = Join-Path $toolRoot 'cmake-3.31.8-windows-x86_64\bin\cmake.exe'
        if (-not (Test-Path -LiteralPath $cmake) -or -not (Test-Path -LiteralPath (Join-Path $llvm 'clang++.exe'))) {
            throw 'Portable tools missing; see README.md. This script never downloads or installs tools.'
        }
        $buildDir = "build/llvm-$($Configuration.ToLower())"
        & $cmake -S . -B $buildDir -G 'MinGW Makefiles' "-DCMAKE_CXX_COMPILER=$llvm\clang++.exe" "-DCMAKE_C_COMPILER=$llvm\clang.exe" "-DCMAKE_RC_COMPILER=$llvm\llvm-windres.exe" "-DCMAKE_MAKE_PROGRAM=$llvm\mingw32-make.exe" "-DCMAKE_BUILD_TYPE=$Configuration"
    } else {
        $cmake = 'cmake'
        $buildDir = 'build/msvc'
        & $cmake -S . -B $buildDir -A x64
    }
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    & $cmake --build $buildDir --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    $ctest = if ($Toolchain -eq 'PortableLLVM') { Join-Path (Split-Path $cmake) 'ctest.exe' } else { 'ctest' }
    & $ctest --test-dir $buildDir -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
} finally { Pop-Location }
