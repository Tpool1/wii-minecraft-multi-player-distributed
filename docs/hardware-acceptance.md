# Physical Wii acceptance

This run qualifies one pair of consoles, controllers, network and display chain. Record one row per gate with the build SHA-256, fixture hash, date, operator, harness settings, evidence filename and verdict. Use **pass**, **fail** or **blocked**. A pending run has no verdict. The thresholds below are proposed acceptance criteria for a two-player radius-two session. [System diagram](fcavex-system-architecture.drawio) · [Test cases and fixtures](fcavex-test-design.drawio)

## Evidence available for comparison

| Environment | Reproducible evidence | What it covers |
|---|---|---|
| Native host | `tests/run.ps1` process status and suite output | Launch selection, chunk interest, persistence helpers, message encoding, bounds and two native loopback endpoints. |
| Wii software in Dolphin | Separate role logs, `run.json`, `gameplay.json`, projector captures and cold-restart results where recorded | Emulated guests executing the named Wii build. The Windows projector composites two DWM surfaces; the runner sets Dolphin volume to 75 for A and 0 for B. These settings alone do not establish physical sound or video timing. |
| Physical Wii A and B | Both SD logs, LAN observations, capture settings and synchronized TV recording | Controller pairing, real heap behavior, wireless/wired link, video resets, audio selection and measured input-to-screen delay. Run H01–H04 below. |

The earlier two-instance homebrew and three-message socket smoke checks established that the lab could boot two guests and exchange bytes. They do not count as fCavEX gameplay or hardware acceptance. Preserve failures alongside successful runs.

## Before booting

1. Record each model number, video cable/converter, output mode and refresh rate, network adapter, controller and SD identifier. Verify that each Wii supports the planned network/output path. Confirm the TV's aspect/game-mode settings and two independently readable inputs to the compositor.
2. Hash one freshly built `boot.dol` and copy it to `sd:/apps/fcavex/` on **both** cards. Use permitted assets and copy `config_wii.json` as `config.json`. Put a disposable, hash-identified world on A. B gets its own writable card.
3. Configure the `network` object. A uses `role="host"`, `bind="<A's private IP>"`, `port="45838"` and `world="saves/Fixture"`. B uses `role="peer"`, `bind="<B's private IP>"`, `host="<A's private IP>"` and the same string-valued port. Set `trace="on"` and `metrics="on"` for both. Start A, load the fixture, then start B. Record the first frame and player slot on each.
4. Pair one Wii Remote with each console. Keep the players’ input paths independent. Route Wii A and Wii B video to separate capture/compositor inputs, preserve each source's aspect ratio, and route the composite output to the TV. Select A as the only audible game source. An HDMI switch selects one input at a time.

## Acceptance gates

| Gate | Procedure | Proposed pass criterion |
|---|---|---|
| H01 · LAN and controls | On the isolated LAN, exchange 200 identifiable 32-byte application payloads each way in the network diagnostic and reconnect three times. In the game move A alone, B alone, then both. Point at four corners and perform one accepted edit per player. Save paired logs and video. | All 400 payloads accounted for; reconnects complete. Each physical input controls only its player. Both views show both accepted edits and the same block types. Keep diagnostic and gameplay outcomes separate. |
| H02 · Separation, durability and soak | At radius two, separate until views are disjoint, edit at both ends, meet again and compare revision-matched blocks. Checkpoint, restart the host and rejoin B from its own card. Repeat ten times. For the soak run one hour then four hours; exercise dense terrain, edits and reconnects every five minutes. Swap host/peer roles for another hour. | World, player pose and inventory survive the declared durable checkpoint. No restarts, allocation failures or queue growth at matched idle states. Target 20 Hz, tick work p95 ≤40 ms / p99 ≤50 ms, VI swap interval p95 ≤2 refreshes / p99 ≤3. |
| H02 · Memory on each console | Retain per-console `metricsNN/frame.csv`, `tick.csv` and `memory.csv`. Compare the same role, player positions, live columns and workload at the beginning and end. Instrument the allocator if a per-bank high-water mark or largest span cannot be reported. | Proposed free margins: MEM1 ≥2 MiB and MEM2 ≥4 MiB per console; largest usable span ≥2× next required allocation. Matched-state growth ≤256 KiB per bank over the final hour. Mark unmeasured margins blocked. |
| H03 · Two feeds and audio | Use a known, permitted flash-and-click test source if the game is silent. Show advancing numbered frames in each input for 60 minutes. Reset A, B and both feeds three times per case. Flash and click A, then B. Start the recovery timer when valid source video returns. | Each view returns to the correct cell and aspect within 5 s; an unaffected feed keeps advancing. Only A is audible once; its absolute flash-to-click offset ≤50 ms. |
| H04 · Latency and skew | Film at ≥240 fps with the indicator and sampled TV pixels at the same height. For each player take 100 presses direct to TV and 100 composed. Split a common numbered flash into both capture paths to isolate compositor skew; then compare the same game event ID between Wiis. | Report median, p95, max and measurement uncertainty. Input-to-screen p95 ≤100 ms per path; composed p95 minus direct p95 ≤2 refreshes. Common-flash skew p95 ≤1 refresh, max ≤2. Shared-game-event skew p95 ≤100 ms. |

Count latency in recorded frames × 1000 / measured camera fps. Label visible button travel as a surrogate when electrical closure is unavailable. Record the actual refresh periods for each mode. Identical nominal frame rates do not guarantee synchronized scanout.

The current telemetry records sampled unreserved arena minima, aggregate newlib allocator totals and bank-tagged framebuffer/FIFO bytes. Arena minima are not total free heap, and aggregate newlib data does not identify a largest free block for each bank. Keep A’s memory independent from B’s. Track dropped VI samples and missing CSV windows alongside percentile reports.

If A alone misses the memory/tick targets, compare a small external authority using the same world and route before changing the render distance. If both clients miss the targets, inspect their mesh, chunk decode and presentation costs first.
