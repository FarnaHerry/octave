# ci-package.ps1 — Windows 打包（**不编译**），与 ci-package.sh 对齐：
# build job 传来的运行时目录（ectave.exe + assets/）→ ectave-v<版本>-win64.zip。
#
# 同样不打内置 Octave 引擎（engines/ 是 Linux 的 ldd 闭包脚本产物），Windows 侧走
# 「PATH 上的 octave」：装官方 Octave 安装包并勾选加入 PATH 即可。
#
# 用法：pwsh -File packaging/ci-package.ps1 -RuntimeDir dist-src

param(
    [Parameter(Mandatory = $true)][string]$RuntimeDir
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# 版本号唯一来源是 CMakeLists 的 project(... VERSION ...)（旧版读 mcpp.toml）。
$versionLine = Select-String -Path "$root\CMakeLists.txt" `
    -Pattern '^project\(ectave VERSION ([^ )]+)' | Select-Object -First 1
if (-not $versionLine) { throw "version not found in CMakeLists.txt" }
$version = $versionLine.Matches[0].Groups[1].Value

if (-not (Test-Path "$RuntimeDir\ectave.exe" -PathType Leaf)) {
    throw "runtime dir has no ectave.exe: $RuntimeDir"
}
if (-not (Test-Path "$RuntimeDir\assets" -PathType Container)) {
    throw "runtime dir has no assets/: $RuntimeDir"
}

$dist = "$root\dist"
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Path $dist | Out-Null

Copy-Item -Recurse "$RuntimeDir\*" $dist
Copy-Item "$root\packaging\dist-README.md" "$dist\README.md" -Force

# 本地手动打包时若已放置内置引擎，一并带上（CI 里不存在）。
if (Test-Path "$root\engines\octave\bin\octave-cli.exe") {
    Write-Host "note: bundling engines/octave into the package"
    New-Item -ItemType Directory -Path "$dist\engines" | Out-Null
    Copy-Item -Recurse "$root\engines\octave" "$dist\engines\octave"
}

$out = "$root\ectave-v$version-win64.zip"
if (Test-Path $out) { Remove-Item $out }
Compress-Archive -Path "$dist\*" -DestinationPath $out

Write-Host "produced: $out"
Get-Item $out | Select-Object Name, Length
