# Run the two-Wii game test on Windows

Build the current Wii ELF or DOL and generate the three disposable SD images first. The launcher needs PowerShell 7, Node.js 18 or newer, Windows Desktop Window Manager, the .NET Framework 4 compiler, and a working Dolphin installation. It creates a new run directory, a separate Dolphin user directory for each role, and writable copies of the supplied cards. It accepts the app's own TCP listener on 127.0.0.1:45838. Run one two-player scenario at a time.

From the repository root in PowerShell, use the actual paths on your computer.

~~~powershell
$pair = @{
    Game       = (Resolve-Path './build/claw/boot.elf').Path
    Dolphin    = 'C:\path\to\Dolphin.exe'
    HostCard   = 'C:\path\to\generated\A.raw'
    PeerCard   = 'C:\path\to\generated\B.raw'
    OutputRoot = 'C:\path\to\disposable-evidence'
}
& ./tests/e2e/run.ps1 @pair -Cold
~~~

| Option | What it exercises |
| --- | --- |
| Default | Both guests enter distinct slots, move with their own controllers, and each edits the world. Both clients must apply each edit. The projected pane for B disconnects and returns after peer rejoin. |
| `-Cold` | Adds a normal in-game host Save & quit using the Wii Remote, shuts down both processes, then restarts both from the same private SD cards with neutral controls. Requires four marker read-backs and both saved poses. |
| `-SoakMinutes 5 -Rejoins 10` | Keeps both guests and the projector live for five uninterrupted minutes, then closes and relaunches B ten times while A continues running. `-SoakMinutes` accepts 0-120, default 0. `-Rejoins` accepts 1-50, default 1. |
| `-Traverse` | Drives movement across column boundaries and checks the guest subscription traces. |
| `-AvatarView` | Turns the two players toward each other through their own Nunchuk sticks. Inspect the paired picture to check avatar rendering. |
| `-Offline` | Runs one real single-player control. Supply `offline.raw` for HostCard. This requires slot 0 to enter the world and move at least 0.35 blocks. |
| `-Play` | Starts two human-controlled guests and their live projector with the keyboard assignments below. |

### Human controls

| Action | A | B |
| --- | --- | --- |
| Move | W/A/S/D | Arrow keys |
| Aim | I/J/K/L | Numpad 8/4/5/6 |
| Dig / place | F / R | Numpad 1 / 3 |
| Jump / GUI confirm | Space / T | Right Ctrl / Numpad 0 |
| Inventory / game menu | G / H | Numpad 7 / Insert |
| Previous / next hotbar | Q / E | Home / End |

Press F11 in the projector for fullscreen. To save A's world, press H and then G and wait for the game to return to world selection before closing the projector with Escape. If just B's guest exits, use F5 in the projector to rejoin. Each Dolphin profile uses background input. A supplies audio and B's emulator volume is zero. The automated test uses timed expressions in separate emulated Wii Remote/Nunchuk profiles. A physical pad can be selected in each profile for hands-on testing.

### Read a run

`run.json` records ELF and Dolphin hashes, card provenance, process IDs, the host listener, mode, errors and final status. In a cold run, check:

- `gameplay.json` checks that each local player moved at least 0.35 blocks and the other client observed a corresponding position within 0.35 blocks. Each accepted edit must produce the expected type in the next host read-back at that coordinate and a matching peer read-back within three seconds.
- `in-game.png` is a single capture of the live DWM projector. The adjacent crops and `in-game-frames.txt` document the two panes. The projector letterboxes each renderer's client area and only captures its own unobscured window. The two read-back logs establish gameplay.
- `peer-offline.png`, `peer-rejoined.png`, and `projector-connections.txt` show the host's uninterrupted thumbnail registration and the replacement peer renderer. The launcher verifies the replacement renderer's process, executable and window identity.
- `soak.json` samples both guests' local and remote position-event counts every ten seconds, checks process continuity, and fails after 45 seconds without a new sample in any direction. `rejoins.json` and `warm-NN.json` record every new peer PID, fresh host checkpoint, restored peer position and peer movement the host observed after that reconnect. Long runs also save the final disconnect and rejoin pictures.
- `cold.json` compares both post-restart client observations with the warm session's marker values, including at least one changed block. Player position tolerance is 0.40 blocks. B's expected pose is the last host-confirmed position before the successful checkpoint. The report separately records how far B's final client prediction had moved. `A-before-cold.log`, `B-first.log`, `B-before-cold.log`, and each final profile's `Logs/dolphin.log` preserve the sessions.
- Use the fixture inspector on the **closed** host and peer cards to corroborate block values, positions, inventories, and the peer sidecar. Hardware LAN behavior, real controller routing, audio quality and physical scan-out timing require real Wii tests.

See the [fixture builder](../../tools/multiplayer_fixture/README.md) and [hardware acceptance procedure](../../docs/hardware-acceptance.md).

### Check the 40 ms traversal target

From the repository root, point `$run` at a completed `-Traverse` run made with fresh fixture cards. The closed host card contains one boot's timing data. Each row summarizes 100 ticks. The check compares the worst per-window p95 for tick work against 40 ms in windows that include both loading (bit 2) and paired play (bit 8). It also reports the worst paired-only window (mask 8). These phase bits record activity. The separate `traverse.json` records whether the players reached disjoint subscriptions.

~~~powershell
$run = 'C:/path/to/completed-traverse-run'
$ErrorActionPreference = 'Stop'
$meta = Get-Content -LiteralPath "$run/run.json" -Raw | ConvertFrom-Json
$travel = Get-Content -LiteralPath "$run/traverse.json" -Raw | ConvertFrom-Json
if ($meta.mode -ne 'traverse' -or !$meta.passed -or !$travel.passed) { throw 'A successful Traverse run is required.' }
$json = & python ./tools/multiplayer_fixture/make_fixture.py metrics "$run/A/Load/WiiSD.raw"
if ($LASTEXITCODE) { throw 'Host card metrics read failed.' }
$boots = $json -join "`n" | ConvertFrom-Json -AsHashtable
if ($boots.Count -ne 1) { throw 'Cannot identify a single Traverse boot on this card.' }
$boot = @($boots.Keys)[0]
$work = @($boots[$boot]['tick.csv'] | ConvertFrom-Csv | Where-Object series -eq 'tick_work_ms')
if (!$work.Count -or @($work | Where-Object { $_.n -ne '100' -or $_.p95 -notmatch '^\d+$' -or $_.phase_bits -notmatch '^([1-9]|1[0-5])$' }).Count) {
    throw 'Missing or invalid 100-tick work samples with phase masks.'
}
$byP95 = $work | Sort-Object { [int]$_.p95 } -Descending
$paired = $byP95 | Where-Object phase_bits -eq '8' | Select-Object -First 1
$mixed = $byP95 | Where-Object { ([int]$_.phase_bits -band 10) -eq 10 } | Select-Object -First 1
if (!$paired -or !$mixed) { throw 'Both paired-only and mixed loading/paired windows are required.' }
"Paired-only p95 $($paired.p95) ms. Mixed p95 $($mixed.p95) ms in window $($mixed.window)."
if ([int]$mixed.p95 -gt 40) { throw 'Mixed tick-work p95 exceeded 40 ms.' }
~~~

The original CLAW traversal returned 14 ms paired-only and 43 ms in mixed window 7. The final guard intentionally fails on that evidence.
