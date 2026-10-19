param(
  [ValidateSet('safe', 'bold', 'compact', 'narrow', 'strong', 'slim', 'small', 'tiny')]
  [string]$Preset = 'safe',

  [string]$ThemeFontDir = '',

  [string]$WindowsFont = ''
)

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)

if (-not $ThemeFontDir) {
  $ThemeFontDir = Join-Path $repoRoot 'Copy to SD Card root directory to update\THEME_Unified Menu Material theme\TFT35\font'
}

$pythonExe = Join-Path $repoRoot '.venv\Scripts\python.exe'
$patchScript = Join-Path $repoRoot 'buildroot\scripts\patch_vietnamese_font.py'

$sourceFont = Join-Path $ThemeFontDir 'word_unicode.fon'
$backupFont = Join-Path $ThemeFontDir 'word_unicode_backup.fon'
$outputFont = Join-Path $ThemeFontDir 'word_unicode.fon'

if (-not (Test-Path $pythonExe)) {
  throw "Python environment not found: $pythonExe"
}

if (-not (Test-Path $patchScript)) {
  throw "Patch script not found: $patchScript"
}

if (-not (Test-Path $ThemeFontDir)) {
  throw "Theme font directory not found: $ThemeFontDir"
}

foreach ($requiredFile in @('word_unicode.fon', 'byte_ascii.fon', 'large_byte_ascii.fon', '8x16_byte_ascii.fon')) {
  $requiredPath = Join-Path $ThemeFontDir $requiredFile
  if (-not (Test-Path $requiredPath)) {
    throw "Required font file missing: $requiredPath"
  }
}

if ($WindowsFont -and ($WindowsFont -ne 'C:\Windows\Fonts\tahoma.ttf')) {
  Write-Host 'Ignoring custom font and forcing Tahoma.'
}

$WindowsFont = 'C:\Windows\Fonts\tahoma.ttf'

if (-not (Test-Path $WindowsFont)) {
  throw "Tahoma not found: $WindowsFont"
}

if (-not (Test-Path $backupFont)) {
  Copy-Item $sourceFont $backupFont -Force
  Write-Host "Created backup: $backupFont"
}
else {
  Write-Host "Using existing backup: $backupFont"
}

$fontSize = 22
$threshold = 96
$xOffset = 0
$yOffset = 0
$paddingX = 0
$fitHeight = 22
$embolden = 0

switch ($Preset) {
  'safe' {
    $fontSize = 22
    $threshold = 96
    $xOffset = 0
    $yOffset = 0
    $paddingX = 0
    $fitHeight = 22
    $embolden = 0
  }
  'bold' {
    $fontSize = 22
    $threshold = 80
    $xOffset = 0
    $yOffset = 0
    $paddingX = 0
    $fitHeight = 22
    $embolden = 1
  }
  'compact' {
    $fontSize = 20
    $threshold = 88
    $xOffset = 0
    $yOffset = 1
    $paddingX = 1
    $fitHeight = 22
    $embolden = 0
  }
  'narrow' {
    $fontSize = 17
    $threshold = 80
    $xOffset = 0
    $yOffset = 0
    $paddingX = 2
    $fitHeight = 21
    $embolden = 0
  }
  'strong' {
    $fontSize = 18
    $threshold = 68
    $xOffset = 0
    $yOffset = 1
    $paddingX = 1
    $fitHeight = 22
    $embolden = 1
  }
  'slim' {
    $fontSize = 16
    $threshold = 78
    $xOffset = 0
    $yOffset = 0
    $paddingX = 2
    $fitHeight = 21
    $embolden = 0
  }
  'small' {
    $fontSize = 17
    $threshold = 78
    $xOffset = 0
    $yOffset = 0
    $paddingX = 1
    $fitHeight = 20
    $embolden = 0
  }
  'tiny' {
    $fontSize = 15
    $threshold = 76
    $xOffset = 0
    $yOffset = 0
    $paddingX = 2
    $fitHeight = 20
    $embolden = 0
  }
}

Write-Host "Preset      : $Preset"
Write-Host "Theme dir   : $ThemeFontDir"
Write-Host "Windows font: $WindowsFont"
Write-Host "font-size   : $fontSize"
Write-Host "threshold   : $threshold"
Write-Host "x-offset    : $xOffset"
Write-Host "y-offset    : $yOffset"
Write-Host "padding-x   : $paddingX"
Write-Host "fit-height  : $fitHeight"
Write-Host "embolden    : $embolden"

& $pythonExe $patchScript $backupFont $outputFont $WindowsFont --font-size $fontSize --threshold $threshold --x-offset $xOffset --y-offset $yOffset --padding-x $paddingX --fit-height $fitHeight --embolden $embolden

Write-Host ''
Write-Host 'Vietnamese glyph patch completed.'
Write-Host 'Next: copy the entire TFT35 folder to the SD card and reset the TFT.'