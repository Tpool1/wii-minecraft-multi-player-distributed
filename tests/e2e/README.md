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
