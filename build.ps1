param(
    [ValidateSet('pico_w','pico2_w')][string]$Board = 'pico_w',
    [switch]$Bootstrap, [switch]$Test, [string]$PythonExecutable = '',
    [switch]$DisableHapticCorrection,
    [ValidateRange(0,256)][int]$LeftTrimQ8 = 256,
    [ValidateRange(0,256)][int]$RightTrimQ8 = 256
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$tools = Join-Path $projectRoot 'tools'
if (!$PythonExecutable) {
    $localPython = Join-Path $tools 'runtime/python/python.exe'
    $PythonExecutable = if (Test-Path $localPython) { $localPython } else { (Get-Command python -ErrorAction Stop).Source }
}
$arm = Join-Path $tools 'arm'
$sdk = Join-Path $tools 'pico-sdk'

if ($Bootstrap -or !(Test-Path "$arm/bin/arm-none-eabi-gcc.exe") -or !(Test-Path "$sdk/pico_sdk_init.cmake")) {
    & $PythonExecutable "$tools/bootstrap.py"
    if ($LASTEXITCODE -ne 0) { throw 'Dependency setup failed' }
    $sdk = Join-Path $tools 'pico-sdk'
}
& $PythonExecutable "$tools/bootstrap.py" --libraries-only
if ($LASTEXITCODE -ne 0) { throw 'Library setup failed' }
$cmake = Join-Path $tools 'python/cmake/data/bin/cmake.exe'
$ninja = Join-Path $tools 'python/bin/ninja.exe'
if (!(Test-Path $cmake) -or !(Test-Path $ninja)) { throw 'Missing CMake/Ninja. Run build.ps1 -Bootstrap with a Python installation that includes pip.' }
$env:Path = "$arm/bin;$tools/python/bin;$env:Path"
$env:PYTHONPATH = "$tools/python"
$nativeGcc = Get-Command gcc -ErrorAction SilentlyContinue
if ($nativeGcc) {
    $env:CC = $nativeGcc.Source
    $env:CXX = (Get-Command g++).Source
} elseif (!(Get-Command cl -ErrorAction SilentlyContinue)) {
    $devcmd = Get-ChildItem "$env:ProgramFiles/Microsoft Visual Studio/*/*/Common7/Tools/VsDevCmd.bat" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (!$devcmd) { throw 'Install Visual Studio C++ Build Tools or provide native GCC on PATH.' }
    $setup = '"' + $devcmd.FullName + '" -arch=x64 -host_arch=x64 >nul && set'
    & $env:ComSpec /d /s /c $setup | ForEach-Object {
        $parts = $_ -split '=',2
        if ($parts.Length -eq 2 -and $parts[0]) { [Environment]::SetEnvironmentVariable($parts[0],$parts[1],'Process') }
    }
    if (!(Get-Command cl -ErrorAction SilentlyContinue)) { throw 'Visual Studio environment initialization failed' }
}
$env:VSLANG = '1033'
$build = Join-Path $projectRoot "firmware/build/$Board"
$picoW = if ($Board -eq 'pico_w') { 'ON' } else { 'OFF' }
$correction = if ($DisableHapticCorrection) { 'OFF' } else { 'ON' }
& $cmake -S "$projectRoot/firmware" -B $build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release "-DPICO_BOARD=$Board" "-DPICO_W_BUILD=$picoW" "-DPICO_SDK_PATH=$sdk" `
    "-DPICO_TOOLCHAIN_PATH=$arm" "-DCMAKE_MAKE_PROGRAM=$ninja" "-DPython3_EXECUTABLE=$PythonExecutable" `
    "-DHAPTIC_CORRECTION=$correction" "-DHAPTIC_LEFT_TRIM_Q8=$LeftTrimQ8" "-DHAPTIC_RIGHT_TRIM_Q8=$RightTrimQ8" `
    '-DVERSION=integrated-0.4.4-mode-led' "-DPICOTOOL_FETCH_FROM_GIT_PATH=$tools/picotool"
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
& $cmake --build $build --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed' }
if ($Test) {
    & $PythonExecutable -B "$projectRoot/firmware/tests/run_host_tests.py"
    if ($LASTEXITCODE -ne 0) { throw 'Host tests failed' }
}
New-Item -ItemType Directory -Force "$projectRoot/dist" | Out-Null
$name = if ($Board -eq 'pico_w') { 'Pico-W.uf2' } else { 'Pico-2-W.uf2' }
Copy-Item -LiteralPath "$build/pico-dualsense-switch-bridge.uf2" -Destination "$projectRoot/dist/$name" -Force
Write-Host "Built dist/$name"
