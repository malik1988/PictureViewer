<#
.SYNOPSIS
  把 Picture Viewer 的发布目录打包成 MSIX（可侧载 / 可上传商店）。

.PARAMETER SourceDir
  发布目录，默认 release/PictureViewer-v1.2.0。

.PARAMETER OutFile
  输出的 .msix 路径。

.PARAMETER Publisher
  清单中的 Publisher，必须与签名证书主题一致。

.PARAMETERThumbprint
  可选：代码签名证书指纹。提供则打包后立即签名。

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File build-msix.ps1 `
    -Publisher "CN=Your Name" -Thumbprint "AABB..."
#>
param(
  [string]$SourceDir = "d:\Projects\Cpp\picviewer\release\PictureViewer-v1.2.0",
  [string]$OutFile   = "d:\Projects\Cpp\picviewer\build\PictureViewer-v1.2.0.msix",
  [string]$Publisher = "CN=ReplaceWithYourPublisher",
  [string]$Thumbprint = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$repo     = "d:\Projects\Cpp\picviewer"
$manifest = Join-Path $PSScriptRoot "Package.appxmanifest"
$sdkBin   = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64"
$makeappx = Join-Path $sdkBin "makeappx.exe"
$signtool = Join-Path $sdkBin "signtool.exe"
$staging  = Join-Path $repo "build\msix-staging"

foreach ($tool in @($makeappx, $signtool)) {
  if (-not (Test-Path $tool)) { throw "缺少 SDK 工具：$tool" }
}
if (-not (Test-Path (Join-Path $SourceDir "PictureViewer.exe"))) {
  throw "源目录找不到 PictureViewer.exe：$SourceDir"
}

# 1. 准备 staging 目录
if (Test-Path $staging) { [System.IO.Directory]::Delete($staging, $true) }
New-Item -ItemType Directory -Path $staging -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $staging "Images") -Force | Out-Null

Copy-Item (Join-Path $SourceDir "PictureViewer.exe") $staging -Force
foreach ($dll in @("libwebp.dll", "libsharpyuv.dll")) {
  $src = Join-Path $SourceDir $dll
  if (Test-Path $src) { Copy-Item $src $staging -Force }
}
Copy-Item (Join-Path $repo "resources\picture_viewer.ico") $staging -Force

# 2. 从 ICO 生成商店要求的 Logo PNG
function Save-Logo([System.Drawing.Icon]$ico, [int]$size, [string]$path) {
  $bmp = New-Object System.Drawing.Bitmap($size, $size)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g.DrawImage($ico.ToBitmap(), 0, 0, $size, $size)
  $g.Dispose()
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
}
$icon = [System.Drawing.Icon]::new((Join-Path $staging "picture_viewer.ico"))
Save-Logo $icon 50  (Join-Path $staging "Images\StoreLogo.png")
Save-Logo $icon 150 (Join-Path $staging "Images\Square150x150Logo.png")
Save-Logo $icon 44  (Join-Path $staging "Images\Square44x44Logo.png")

# 宽磁贴：把 150x150 居中放到 310x150
$wide = New-Object System.Drawing.Bitmap(310, 150)
$wg = [System.Drawing.Graphics]::FromImage($wide)
$wg.Clear([System.Drawing.Color]::FromArgb(32, 32, 34))
$wg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$wg.DrawImage($icon.ToBitmap(), 80, 0, 150, 150)
$wg.Dispose()
$wide.Save((Join-Path $staging "Images\Wide310x150Logo.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$wide.Dispose()
$icon.Dispose()

# 3. 写入清单（替换 Publisher 占位符）；MakeAppx 要求文件名为 AppxManifest.xml
$xml = Get-Content $manifest -Raw
$xml = $xml -replace 'CN=ReplaceWithYourPublisher', $Publisher
[System.IO.File]::WriteAllText((Join-Path $staging "AppxManifest.xml"), $xml, [System.Text.UTF8Encoding]::new($false))

# 4. 打包（/h 需显式给算法，省略时用默认 SHA256）
if (Test-Path $OutFile) { [System.IO.File]::Delete($OutFile) }
New-Item -ItemType Directory -Path (Split-Path $OutFile) -Force | Out-Null
& $makeappx pack /d $staging /p $OutFile
if ($LASTEXITCODE -ne 0) { throw "makeappx 打包失败（退出码 $LASTEXITCODE）" }
Write-Output "PACKED: $OutFile"

# 5. 可选签名
if ($Thumbprint) {
  & $signtool sign /sha1 $Thumbprint /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $OutFile
  if ($LASTEXITCODE -ne 0) { throw "签名失败（退出码 $LASTEXITCODE）" }
  Write-Output "SIGNED: $OutFile"
} else {
  Write-Output "未签名。侧载前需签名；商店提交可由 Partner Center 代签。"
}

Get-Item $OutFile | Select-Object FullName, Length
