param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[switch]$UseLocalDependencies)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$installation=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Visual Studio with the MSVC C++ tools is required.' }
$devcmd=Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
$environment=& cmd.exe /d /s /c """$devcmd"" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize Visual Studio developer environment.' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process') }
}
$cmake=Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest=Join-Path (Split-Path $cmake) 'ctest.exe'
$build=Join-Path $projectRoot ('build/windows-'+$Configuration.ToLowerInvariant())
$configure=@('-S',$projectRoot,'-B',$build,'-G','Ninja',"-DCMAKE_BUILD_TYPE=$Configuration")
if ($UseLocalDependencies) {
    $configure+=@(
        "-DFETCHCONTENT_SOURCE_DIR_RAYLIB=$projectRoot/.deps/raylib-02566453880e06bec97f5b8160fa6fe1e98397ca",
        "-DFETCHCONTENT_SOURCE_DIR_RAYGUI=$projectRoot/.deps/raygui-020a61bebcbe288b4414de3416e219ef40af847a",
        "-DFETCHCONTENT_SOURCE_DIR_YAML_CPP=$projectRoot/.deps/yaml-cpp-f7320141120f720aecc4c32be25586e7da9eb978")
}
& $cmake @configure
if ($LASTEXITCODE -ne 0) { throw 'Configure failed.' }
& $cmake --build $build --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
& $ctest --test-dir $build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }

