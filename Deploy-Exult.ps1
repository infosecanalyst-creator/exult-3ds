<#
  Deploy-Exult.ps1 - the engine behind "Exult-3DS.cmd". Double-click that instead.

  Menu (no arguments):
    1  Install / update     ftpd must be running on the 3DS. Sends only what changed.
    2  Play with live log   Homebrew Launcher on the 3DS, press Y. Exult's output shows here.
    3  Get log + config     Pulls exult.log and exult.cfg into .\from-3ds\
    4  Send edited config   Pushes .\from-3ds\exult.cfg back
    5  Full re-send         Re-uploads everything (e.g. after formatting the SD card)
    6  Sync saves with PC   Two-way merge with your PC Exult saves (ftpd open on the 3DS)

  Power-user switches: -Install -Run -GetLog -PushConfig -SyncSaves -Force -Ip x.x.x.x -FtpPort 5000
  The 3DS is found automatically and remembered in .\settings.json.
#>
param(
  [string]$Ip,
  [switch]$Install, [switch]$Run, [switch]$GetLog, [switch]$PushConfig, [switch]$SyncSaves, [switch]$Force, [switch]$IncludeSaves,
  [int]$FtpPort = 5000,
  [string]$App,
  [string]$PcExultCfg = "C:\Ultima\Config\exult.cfg",
  [string[]]$SearchRoots = @("$PSScriptRoot\games", "C:\Ultima", "C:\GOG Games", "${env:ProgramFiles(x86)}\GOG Galaxy\Games", "$env:USERPROFILE\OneDrive\Desktop\Ultima Sources", "$env:USERPROFILE\Desktop\Ultima Sources")
)
$ErrorActionPreference = "Stop"
$kit      = Split-Path -Parent $MyInvocation.MyCommand.Path
$sdKit    = Join-Path $kit "SD\3ds\exult"
$settingsPath = Join-Path $kit "settings.json"
if (-not $App) {
  $built = Join-Path $kit "build\out\exult.3dsx"        # prefer a fresh local rebuild
  $App = if (Test-Path $built) { $built } else { Join-Path $sdKit "exult.3dsx" }
}

# ---------------------------------------------------------------- settings
$settings = @{ ip = ""; sent = @{}; synced = @{} }
if (Test-Path $settingsPath) {
  $j = Get-Content $settingsPath -Raw | ConvertFrom-Json
  $settings.ip = [string]$j.ip
  if ($j.sent) { $j.sent.PSObject.Properties | ForEach-Object { $settings.sent[$_.Name] = [string]$_.Value } }
  if ($j.synced) { $j.synced.PSObject.Properties | ForEach-Object { $settings.synced[$_.Name] = [string]$_.Value } }
}
function Save-Settings { $settings | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $settingsPath }

# ---------------------------------------------------------------- 3DS discovery
# True if an FTP server answers on addr:port (reads the "220" greeting, so a NAS
# web page or AirPlay receiver that also uses port 5000 isn't mistaken for the 3DS).
function Test-Port([string]$addr, [int]$port, [int]$ms = 800) {
  $c = New-Object System.Net.Sockets.TcpClient
  try {
    if (-not ($c.ConnectAsync($addr, $port).Wait($ms)) -or -not $c.Connected) { return $false }
    $st = $c.GetStream(); $st.ReadTimeout = 1500
    $buf = New-Object byte[] 256; $n = $st.Read($buf, 0, $buf.Length)
    return ($n -ge 3 -and [Text.Encoding]::ASCII.GetString($buf, 0, 3) -eq "220")
  } catch { return $false } finally { $c.Close() }
}

function Find-3DS {
  # Try the remembered address first, then sweep the local /24 for ftpd.
  if ($settings.ip -and (Test-Port $settings.ip $FtpPort 800)) { return $settings.ip }
  $nets = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.PrefixOrigin -ne "WellKnown" -and $_.IPAddress -notlike "169.254.*" -and $_.IPAddress -ne "127.0.0.1" } |
    ForEach-Object { ($_.IPAddress -split '\.')[0..2] -join '.' } | Select-Object -Unique
  foreach ($net in $nets) {
    Write-Host "  looking for the 3DS (ftpd on port $FtpPort) on $net.x ..." -NoNewline
    $clients = @{}
    foreach ($h in 1..254) {
      $c = New-Object System.Net.Sockets.TcpClient
      $clients["$net.$h"] = @{ c = $c; t = $c.ConnectAsync("$net.$h", $FtpPort) }
    }
    Start-Sleep -Milliseconds 1500
    $open = @()
    foreach ($k in $clients.Keys) {
      $e = $clients[$k]
      if ($e.t.IsCompleted -and -not $e.t.IsFaulted -and $e.c.Connected) { $open += $k }
      $e.c.Close()
    }
    $hit = $open | Where-Object { Test-Port $_ $FtpPort } | Select-Object -First 1
    if ($hit) { Write-Host " found $hit" -ForegroundColor Green; return $hit }
    Write-Host " nothing"
  }
  return $null
}

function Get-3DSAddress([switch]$Required) {
  if ($Ip) { return $Ip }
  $found = Find-3DS
  if (-not $found) {
    Write-Host "`nCouldn't find the 3DS. Make sure ftpd is open on it and it's on the same Wi-Fi." -ForegroundColor Yellow
    $found = (Read-Host "Or type the IP shown in ftpd (Enter to cancel)").Trim()
    if (-not $found) { throw "No 3DS address." }
  }
  if ($settings.ip -ne $found) { $settings.ip = $found; $settings.sent = @{}; Save-Settings }   # new 3DS/IP: assume nothing is there
  return $found
}

# ---------------------------------------------------------------- FTP helpers
function UrlPath([string]$rel) { (($rel -replace '\\','/').Split('/') | ForEach-Object { [uri]::EscapeDataString($_) }) -join '/' }
function Stamp($file) { $fi = Get-Item -LiteralPath $file; "$($fi.Length)|$($fi.LastWriteTimeUtc.Ticks)" }

function Send-Files($items, [string]$label, [string]$base) {
  $items = @($items | Where-Object { $_ })
  if (-not $Force) { $items = @($items | Where-Object { $settings.sent[$_.Remote] -ne (Stamp $_.Local) }) }
  if ($items.Count -eq 0) { Write-Host "  $label : up to date" -ForegroundColor DarkGray; return }
  $n = 0; $total = $items.Count
  for ($i = 0; $i -lt $total; $i += 40) {
    $batch = @($items[$i..([Math]::Min($i + 39, $total - 1))])
    $cargs = @("-s", "-S", "--ftp-pasv", "--ftp-create-dirs", "--connect-timeout", "10", "--retry", "2")
    foreach ($f in $batch) { $cargs += @("-T", $f.Local, "$base/$(UrlPath $f.Remote)") }
    & curl.exe @cargs
    if ($LASTEXITCODE -ne 0) { Save-Settings; throw "Upload stopped (curl exit $LASTEXITCODE). Is ftpd still open on the 3DS? Run it again to resume." }
    foreach ($f in $batch) { $settings.sent[$f.Remote] = Stamp $f.Local }
    $n += $batch.Count
    Write-Progress -Activity "Sending $label" -Status "$n / $total" -PercentComplete (100 * $n / $total)
  }
  Write-Progress -Activity "Sending $label" -Completed
  Save-Settings
  Write-Host "  $label : sent $total file(s)" -ForegroundColor Green
}

function Get-TreeItems([string]$localRoot, [string]$remotePrefix) {
  $root = (Resolve-Path -LiteralPath $localRoot).Path.TrimEnd('\')
  Get-ChildItem -LiteralPath $root -Recurse -File | ForEach-Object {
    @{ Local = $_.FullName; Remote = "$remotePrefix/" + $_.FullName.Substring($root.Length + 1) }
  }
}

# ---------------------------------------------------------------- actions
function Do-Install {
  $addr = Get-3DSAddress
  $base = "ftp://${addr}:$FtpPort/3ds/exult"
  Write-Host "Sending to $base" -ForegroundColor Cyan
  Send-Files @(@{ Local = $App; Remote = "exult.3dsx" }) "Exult app" $base
  Send-Files (Get-TreeItems (Join-Path $sdKit "data") "data") "Exult data" $base

  $found = @{}
  foreach ($root in $SearchRoots | Where-Object { Test-Path $_ }) {
    Get-ChildItem -Path $root -Recurse -Directory -Filter STATIC -ErrorAction SilentlyContinue |
      Where-Object { Test-Path (Join-Path $_.FullName "U7CHUNKS") } | ForEach-Object {
        $g = if (Test-Path (Join-Path $_.FullName "SISPEECH.SPC")) { "serpentisle" } else { "blackgate" }
        if (-not $found.ContainsKey($g)) { $found[$g] = $_.Parent.FullName }
      }
  }
  if ($found.Count -eq 0) { Write-Warning "No Ultima VII data (STATIC\U7CHUNKS) found under: $($SearchRoots -join ', ')" }
  foreach ($g in $found.Keys) {
    foreach ($sub in "STATIC", "mods", "patch") {
      $p = Join-Path $found[$g] $sub
      if (Test-Path $p) { Send-Files (Get-TreeItems $p "$g/$sub") "$g $sub" $base }
    }
    }
  foreach ($g in "blackgate", "serpentisle") { if (-not $found.ContainsKey($g)) { Write-Warning "No $g data found - it won't show in Exult's menu." } }
  if ($IncludeSaves) { Do-SyncSaves }
  Write-Host "`nInstalled. You can now just start Exult from the Homebrew Launcher on the 3DS - no PC needed." -ForegroundColor Cyan
}

function Do-Run {
  $link = (Get-Command 3dslink.exe -ErrorAction SilentlyContinue).Source
  if (-not $link) { $link = @("C:\devkitPro\tools\bin\3dslink.exe", "$env:DEVKITPRO\tools\bin\3dslink.exe") | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1 }
  if (-not $link) { throw "3dslink.exe not found (it comes with devkitPro, C:\devkitPro\tools\bin)." }
  Write-Host "On the 3DS: open the Homebrew Launcher and press Y. (Ctrl+C here stops the log; Exult keeps running.)" -ForegroundColor Yellow
  # No -a: 3dslink finds the netloader by broadcast. Fall back to the remembered IP if that fails.
  & $link -s $App
  if ($LASTEXITCODE -ne 0 -and $settings.ip) { & $link -a $settings.ip -s $App }
}

function Do-GetLog {
  $addr = Get-3DSAddress
  $out = Join-Path $kit "from-3ds"; New-Item -ItemType Directory -Force $out | Out-Null
  foreach ($f in "exult.log", "exult.cfg") {
    & curl.exe -s -S --ftp-pasv "ftp://${addr}:$FtpPort/3ds/exult/$f" -o (Join-Path $out $f)
    if ($LASTEXITCODE -eq 0) { Write-Host "  got $f" -ForegroundColor Green } else { Write-Warning "couldn't fetch $f" }
  }
  if (Test-Path (Join-Path $out "exult.log")) { Start-Process notepad.exe (Join-Path $out "exult.log") }
}

function Do-PushConfig {
  $cfg = Join-Path $kit "from-3ds\exult.cfg"
  if (-not (Test-Path $cfg)) { throw "Use 'Get log + config' first, edit from-3ds\exult.cfg, then send it." }
  $addr = Get-3DSAddress
  $script:Force = $true
  Send-Files @(@{ Local = $cfg; Remote = "exult.cfg" }) "config" "ftp://${addr}:$FtpPort/3ds/exult"
}

# ---------------------------------------------------------------- save sync
# Exult saves are identical on every platform. The PC keeps them under each
# game's savegame_path (e.g. C:\Ultima\Saves\Exult-BG, with mod saves in
# mods\<Mod>\saves); the 3DS uses the same layout under /3ds/exult/saves/bg|si.
# Sync is a three-way merge against the state recorded at the last sync, so a
# save is never deleted or silently overwritten: if both sides changed the same
# slot, the 3DS version is kept in a new slot on both sides.

$remoteSaveRoot = @{ blackgate = "saves/bg"; serpentisle = "saves/si" }

function Get-PcSaveRoots {
  $roots = @{}
  if (Test-Path $PcExultCfg) {
    $cfg = Get-Content -LiteralPath $PcExultCfg -Raw
    foreach ($g in "blackgate", "serpentisle") {
      $m = [regex]::Match($cfg, "<$g>(.*?)</$g>", "Singleline")
      if ($m.Success) {
        $sp = [regex]::Match($m.Groups[1].Value, "<savegame_path>\s*(.*?)\s*</savegame_path>", "Singleline")
        $gp = [regex]::Match($m.Groups[1].Value, "<path>\s*(.*?)\s*</path>", "Singleline")
        if ($sp.Success -and $sp.Groups[1].Value) { $roots[$g] = $sp.Groups[1].Value }
        elseif ($gp.Success -and $gp.Groups[1].Value) { $roots[$g] = $gp.Groups[1].Value }
      }
    }
  }
  return $roots
}

function Get-FtpList([string]$url) {
  # LIST output from ftpd: "-rw-rw-rw- 1 3DS 3DS 150169 Oct 23 19:26 exult00bg.sav"
  $lines = & curl.exe -s --ftp-pasv "$url/" 2>$null
  if ($LASTEXITCODE -ne 0 -or -not $lines) { return @() }
  foreach ($line in @($lines)) {
    if ($line -match '^([dl-])\S*\s+\S+\s+\S+\s+\S+\s+\d+\s+\S+\s+\S+\s+\S+\s+(.+?)\s*$') {
      [pscustomobject]@{ Name = $matches[2]; IsDir = ($matches[1] -eq 'd') }
    }
  }
}

function Get-Hash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }

function Sync-SaveDir([string]$game, [string]$localDir, [string]$remoteRel, [string]$base, [ref]$stats) {
  $cache = Join-Path (Join-Path (Join-Path (Join-Path $kit "from-3ds") "sync") $game) $remoteRel
  New-Item -ItemType Directory -Force $cache | Out-Null
  $pattern = '^exult(\d+)(bg|si)\.sav$'

  $local = @{}
  if (Test-Path -LiteralPath $localDir) {
    Get-ChildItem -LiteralPath $localDir -File | Where-Object { $_.Name -match $pattern } |
      ForEach-Object { $local[$_.Name.ToLower()] = $_.FullName }
  }
  $remote = @{}
  foreach ($e in @(Get-FtpList "$base/$remoteRel")) {
    if (-not $e.IsDir -and $e.Name -match $pattern) {
      $dst = Join-Path $cache $e.Name.ToLower()
      & curl.exe -s -S --ftp-pasv "$base/$remoteRel/$($e.Name)" -o $dst
      if ($LASTEXITCODE -eq 0) { $remote[$e.Name.ToLower()] = $dst }
    }
  }
  if ($local.Count -eq 0 -and $remote.Count -eq 0) { return }

  # Next free slot number across both sides (keeps the bg/si suffix).
  $nums = @(@($local.Keys) + @($remote.Keys) | ForEach-Object { if ($_ -match $pattern) { [int]$matches[1] } })
  $next = if ($nums.Count) { [int](($nums | Measure-Object -Maximum).Maximum) + 1 } else { 0 }
  $suffix = if ($game -eq "serpentisle") { "si" } else { "bg" }

  function Put([string]$src, [string]$name) {
    & curl.exe -s -S --ftp-pasv --ftp-create-dirs -T $src "$base/$remoteRel/$name"
    if ($LASTEXITCODE -ne 0) { throw "Upload of $name failed (curl $LASTEXITCODE). Is ftpd still open?" }
  }
  function Get-Local([string]$src, [string]$name) {
    New-Item -ItemType Directory -Force $localDir | Out-Null
    Copy-Item -LiteralPath $src -Destination (Join-Path $localDir $name) -Force
  }

  $names = @(@($local.Keys) + @($remote.Keys) | Sort-Object -Unique)
  foreach ($n in $names) {
    $key = "$game/$remoteRel/$n"
    $lh = if ($local.ContainsKey($n)) { Get-Hash $local[$n] } else { $null }
    $rh = if ($remote.ContainsKey($n)) { Get-Hash $remote[$n] } else { $null }
    $was = $settings.synced[$key]
    if ($lh -and $rh -and $lh -eq $rh) {
      $settings.synced[$key] = $lh
    } elseif ($lh -and -not $rh) {
      Put $local[$n] $n; $settings.synced[$key] = $lh; $stats.Value.up++
    } elseif ($rh -and -not $lh) {
      Get-Local $remote[$n] $n; $settings.synced[$key] = $rh; $stats.Value.down++
    } elseif ($was -eq $lh) {
      # PC copy unchanged since last sync -> the 3DS has the newer one
      Get-Local $remote[$n] $n; $settings.synced[$key] = $rh; $stats.Value.down++
    } elseif ($was -eq $rh) {
      Put $local[$n] $n; $settings.synced[$key] = $lh; $stats.Value.up++
    } else {
      # Both changed: keep both. The 3DS version moves to a new slot on both sides.
      $newName = ("exult{0:D2}{1}.sav" -f $next, $suffix); $next++
      Get-Local $remote[$n] $newName
      Put $remote[$n] $newName
      Put $local[$n] $n
      $settings.synced[$key] = $lh
      $settings.synced["$game/$remoteRel/$newName"] = $rh
      $stats.Value.conflicts++
      Write-Host "    both sides changed $n - kept the 3DS version as $newName" -ForegroundColor Yellow
    }
  }
}

function Sync-Gamedat([string]$localGamedat, [string]$remoteRel, [string]$base, [ref]$stats) {
  # "Journey Onward" needs a game in progress (gamedat). If the 3DS has none for
  # this game/mod yet, give it the PC's current one so you can continue at once.
  if (-not (Test-Path -LiteralPath $localGamedat)) { return }
  $existing = @(Get-FtpList "$base/$remoteRel/gamedat" | Where-Object { -not $_.IsDir })
  if ($existing.Count -gt 0) { return }
  $items = Get-TreeItems $localGamedat "$remoteRel/gamedat"
  $script:Force = $true
  Send-Files $items "game in progress ($remoteRel)" $base
  $script:Force = $false
  $stats.Value.gamedat++
}

function Do-SyncSaves {
  $addr = Get-3DSAddress
  $base = "ftp://${addr}:$FtpPort/3ds/exult"
  $roots = Get-PcSaveRoots
  if ($roots.Count -eq 0) { throw "Couldn't read save folders from $PcExultCfg." }
  Write-Host "Syncing saves with the 3DS at $addr" -ForegroundColor Cyan
  foreach ($g in "blackgate", "serpentisle") {
    if (-not $roots.ContainsKey($g)) { continue }
    $pc = $roots[$g]; $rr = $remoteSaveRoot[$g]
    $stats = [pscustomobject]@{ up = 0; down = 0; conflicts = 0; gamedat = 0 }
    # Base game saves, then each mod's saves (mods\<Mod>\saves on both sides)
    Sync-SaveDir $g $pc $rr $base ([ref]$stats)
    Sync-Gamedat (Join-Path $pc "gamedat") $rr $base ([ref]$stats)
    $mods = @{}
    $pcMods = Join-Path $pc "mods"
    if (Test-Path -LiteralPath $pcMods) {
      Get-ChildItem -LiteralPath $pcMods -Directory | ForEach-Object { $mods[$_.Name.ToLower()] = $_.Name }
    }
    foreach ($e in @(Get-FtpList "$base/$rr/mods")) { if ($e.IsDir -and -not $mods.ContainsKey($e.Name.ToLower())) { $mods[$e.Name.ToLower()] = $e.Name } }
    foreach ($m in $mods.Values) {
      Sync-SaveDir $g (Join-Path (Join-Path $pcMods $m) "saves") "$rr/mods/$m/saves" $base ([ref]$stats)
      Sync-Gamedat (Join-Path (Join-Path $pcMods $m) "gamedat") "$rr/mods/$m" $base ([ref]$stats)
    }
    Save-Settings
    Write-Host ("  {0}: {1} sent to 3DS, {2} copied to PC, {3} kept as extra slots" -f $g, $stats.up, $stats.down, $stats.conflicts) -ForegroundColor Green
    if ($stats.gamedat) { Write-Host "    + copied your PC game in progress so Journey Onward works on the 3DS" -ForegroundColor Green }
  }
  Write-Host "Saves are in Exult's Load/Save menu on both machines." -ForegroundColor Cyan
}

# ---------------------------------------------------------------- entry
$any = $Install -or $Run -or $GetLog -or $PushConfig -or $SyncSaves
if (-not $any) {
  Write-Host ""
  Write-Host "  EXULT (Ultima VII) for New 3DS" -ForegroundColor Yellow
  if ($settings.ip) { Write-Host "  3DS: $($settings.ip)" -ForegroundColor DarkGray }
  Write-Host "  1  Install / update      (ftpd open on the 3DS)"
  Write-Host "  2  Play with live log    (Homebrew Launcher, press Y)"
  Write-Host "  3  Get log + config"
  Write-Host "  4  Send edited config"
  Write-Host "  5  Full re-send          (everything, e.g. new SD card)"
  Write-Host "  6  Sync saves with PC    (ftpd open on the 3DS)"
  switch ((Read-Host "`n  Choose").Trim()) {
    "1" { $Install = $true }
    "2" { $Run = $true }
    "3" { $GetLog = $true }
    "4" { $PushConfig = $true }
    "5" { $Install = $true; $Force = $true }
    "6" { $SyncSaves = $true }
    default { return }
  }
}
try {
  if ($Install)    { Do-Install }
  if ($GetLog)     { Do-GetLog }
  if ($PushConfig) { Do-PushConfig }
  if ($SyncSaves)  { Do-SyncSaves }
  if ($Run)        { Do-Run }
} catch {
  Write-Host "`n$($_.Exception.Message)" -ForegroundColor Red
  exit 1
}
