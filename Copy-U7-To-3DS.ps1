<#
  Copy-U7-To-3DS.ps1 - puts Exult-3DS and your Ultima VII data on the 3DS SD card.

  Run in PowerShell from this folder:
      .\Copy-U7-To-3DS.ps1                 # auto-detects the SD card
      .\Copy-U7-To-3DS.ps1 -Drive J        # or name the drive letter
      .\Copy-U7-To-3DS.ps1 -IncludeSaves   # also copy your PC Exult saves

  Finds the Black Gate and Serpent Isle data by looking for STATIC\U7CHUNKS
  under C:\Ultima and your Desktop\Ultima Sources (SI is told apart by
  STATIC\SISPEECH.SPC). Exult mods (e.g. Keyring, SI Fixes) found in a
  "mods" folder next to each game are copied too.
#>
param(
  [string]$Drive,
  [switch]$IncludeSaves,
  [string[]]$SearchRoots = @("C:\Ultima", "$env:USERPROFILE\OneDrive\Desktop\Ultima Sources", "$env:USERPROFILE\Desktop\Ultima Sources")
)
$ErrorActionPreference = "Stop"
$kit = Split-Path -Parent $MyInvocation.MyCommand.Path

# --- SD card --------------------------------------------------------------
if (-not $Drive) {
  $cand = Get-PSDrive -PSProvider FileSystem | Where-Object {
    Test-Path "$($_.Root)Nintendo 3DS" } | Select-Object -First 1
  if (-not $cand) { throw "No SD card with a 'Nintendo 3DS' folder found. Insert it or pass -Drive <letter>." }
  $Drive = $cand.Name
}
$sd = "$($Drive.TrimEnd(':')):\"
if (-not (Test-Path "${sd}Nintendo 3DS")) {
  Write-Warning "$sd has no 'Nintendo 3DS' folder - are you sure this is the 3DS card?"
  if ((Read-Host "Continue? (y/n)") -ne "y") { exit 1 }
}
$dest = Join-Path $sd "3ds\exult"
Write-Host "SD card: $sd  ->  $dest" -ForegroundColor Cyan

# --- Engine + Exult data --------------------------------------------------
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item -Recurse -Force (Join-Path $kit "SD\3ds\exult\*") $dest
Write-Host "Copied exult.3dsx and Exult data files."

# --- Find the game data ---------------------------------------------------
$found = @{}
foreach ($root in $SearchRoots | Where-Object { Test-Path $_ }) {
  Get-ChildItem -Path $root -Recurse -Directory -Filter STATIC -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName "U7CHUNKS") } |
    ForEach-Object {
      $game = if (Test-Path (Join-Path $_.FullName "SISPEECH.SPC")) { "serpentisle" } else { "blackgate" }
      if (-not $found.ContainsKey($game)) { $found[$game] = $_.Parent.FullName }
    }
}
if ($found.Count -eq 0) { throw "Couldn't find any Ultima VII data (STATIC\U7CHUNKS) under: $($SearchRoots -join ', ')" }

foreach ($game in $found.Keys) {
  $src = $found[$game]; $dst = Join-Path $dest $game
  Write-Host "$game : $src" -ForegroundColor Green
  New-Item -ItemType Directory -Force -Path $dst | Out-Null
  # STATIC is all Exult needs; mods/patch come along if present. Saves are opt-in.
  foreach ($sub in "STATIC", "mods", "patch") {
    $p = Join-Path $src $sub
    if (Test-Path $p) {
      robocopy $p (Join-Path $dst $sub) /E /NFL /NDL /NJH /NJS /NP | Out-Null
      Write-Host "   + $sub"
    }
  }
  if ($IncludeSaves) {
    $gd = Join-Path $src "gamedat"
    Get-ChildItem $src -Filter "exult*.sav" -ErrorAction SilentlyContinue | Copy-Item -Destination $dst -Force
    if (Test-Path $gd) { robocopy $gd (Join-Path $dst "gamedat") /E /NFL /NDL /NJH /NJS /NP | Out-Null }
    Write-Host "   + saves"
  }
}
foreach ($g in "blackgate", "serpentisle") { if (-not $found.ContainsKey($g)) { Write-Warning "No $g data found - that game won't appear in the menu." } }

Write-Host "`nDone. Eject the card, put it in the 3DS, and launch Exult from the Homebrew Launcher." -ForegroundColor Cyan
