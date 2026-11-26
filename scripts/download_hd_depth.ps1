# Optional 1-click installer for Depth Anything 3 Monocular Large (DA3Mono-Large, Apache-2.0, ~1.34 GB)
$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ModelDir = Join-Path $RootDir "assets\models"
$OutFile = Join-Path $ModelDir "da3_mono_large.onnx"
$Url = "https://huggingface.co/yuvraj108c/Depth-Anything-3-Onnx/resolve/main/DA3Mono-Large.onnx"

New-Item -ItemType Directory -Force -Path $ModelDir | Out-Null
Write-Host "Downloading DA3Mono-Large (Apache-2.0, ~1.34 GB) into $OutFile ..."
Invoke-WebRequest -Uri $Url -OutFile "$OutFile.part"
Move-Item -Force "$OutFile.part" $OutFile
Write-Host "Done! Restart Nebel (or click HD 3D / Render) to use DA3Mono-Large."
