# Five-minute loopback transport profiles

Run this opt-in suite from PowerShell 7 on Windows after preparing the Wii dependencies. For example, from the checkout root:

```powershell
./tests/wire/profiles/run.ps1 -Compiler clang -IncludeDir ./build/claw/staging/source
```

The runner compiles the production session, queue, parser and serializers using `LAN_TEST_IO` and starts five private host/peer pairs. It requests separate ephemeral ports bound to IPv4 loopback. Each pair runs as two below-normal-priority processes. No network configuration or Dolphin instance is required. Results go in a new subdirectory of `build/tests/wire-profiles`, or an explicit new `-OutputDirectory`. The five profiles run concurrently for 300 seconds each. `-DurationSeconds 40` provides a shorter development check.

Each host sends 200 sequential probes spaced across the requested interval. A peer verifies each probe and echoes its sequence and arrival timestamp. Check both directions' rolling FNV-1a digests, arrival order, duplicate count, payload size, measured connection span and the per-probe delay lower bound. The numbered probes travel through a test-only 32-byte envelope consisting of a real nine-byte encoded time or acknowledgment RPC plus 23 deterministic check bytes. The game uses its native RPC lengths. The checksums are integrity checks for this fixture.

| Profile | Gate before the host's actual Winsock send |
| --- | --- |
| nominal | Immediate |
| constant50 | 50 ms for each state probe |
| constant100 | 100 ms for each state probe |
| jitter | 30 and 70 ms alternating for successive probes |
| stall | 2,000 ms on probe 101 |

Every successful read and write uses the local TCP stack. A gated send returns would-block until the deadline, including on retries and partial writes. The peer echoes without an added delay. This experiment measures application-level send gating, ordered delivery and queue recovery. It does not inject IP packet loss, emulate physical WLAN contention, execute a gameplay edit or measure display latency.

The two native processes share the Windows performance-counter clock. Forward time runs from host enqueue to peer receipt. Reverse time runs from peer receipt to host acknowledgment, so includes the response work. Their sum is application round trip. Percentiles use nearest rank. `samples.csv` retains 1,000 observations. `summary.json` contains per-profile median, p95, p99, maximum, counts and checksums. The raw endpoint logs and `manifest.json` record source and executable hashes and the bound ports. Recheck a completed run with `verify.ps1 -RunDirectory <path>`.
