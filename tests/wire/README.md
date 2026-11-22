# Wire and session checks

Run `tests/wire/run.ps1 -Compiler <path-to-clang.exe> -IncludeDir <Wii-build-staging/source>` on Windows. The suite compiles the production framing and RPC code against the game's real headers and starts two native processes on an ephemeral Windows loopback port. Logs go to `build/tests/wire`, or the specified `-OutputDirectory`.

* `lan_entity_test.c` checks the lookup used by movement and pickup for a missing entity, a later valid spawn, duplicate removal and retention of the local player.
* `lan_frame_test.c` checks exact header bytes, short reads, two logical records in one read, continuation assembly and rejection of invalid sizes, versions and flags.
* `lan_rpc_test.c` checks edit inputs, coordinates, double precision motion, independent nonzero golden bytes for every allowed command and outbound scalar RPC, owned 80 KiB chunk planes and a checksum failure. The sender and serialized data are mutated before the receiver's arrays are checked.
* `lan_transport_test.c` exercises a host and peer with a complete chunk, edit and response. The host ends the first connection. Both sides complete a fresh handshake and snapshot before the second command acknowledgment. Each process returns a separate verdict.

## Wire contract

The 24-byte header uses network byte order. Bytes 0–3 identify `FCVX`, byte 4 is protocol version 1, byte 5 is message kind, bytes 6–7 are fragment flags, bytes 8–11 are fragment length, bytes 12–15 are the host session epoch and bytes 16–19 are the sender sequence. Bytes 20–23 are reserved and must be zero in this version. First and last fragment flags are 1 and 2. Each logical message consumes one sender sequence. TCP may split or combine fragments.

The connection begins with HELLO and WELCOME carrying the same 16-byte identity. Its four network-order words are the RPC schema revision, pinned base build tag, multiplayer rules revision and a 32-bit fingerprint calculated after block and item registration. The fingerprint includes assigned IDs, gameplay names, material results for the 16 block metadata values, digging parameters, light/placement flags, item stacking, fuel, damage, tool and armor values. Texture coordinates, texture files and machine-native pointers are excluded. A mismatch closes the stream before any player joins or chunk arrives. The tag identifies compatibility with this build family. A rules change outside those registry fields needs a revision bump. The host assigns the peer's slot outside the wire payload and issues a fresh epoch. The peer joins the world selected and saved by its host. Host-only load/unload/pause commands have no wire encoding. Client edit requests carry a monotonic action sequence and observed destination block type. A type precondition cannot distinguish a block changed away and back to the same type.

The limit is 8,192 payload bytes per fragment, 82,000 bytes per logical message and 256 KiB of pending outbound data. One record is assembled at a time. A full chunk encodes coordinates, extents, all four planes and a 32-bit FNV-1a checksum over the plane bytes. Successful peer decoding transfers four separate owned buffers to the game's chunk handler. Encoding borrows the sender's buffers until it returns. A host sending the same chunk to both views must encode the peer copy before handing the original allocations to the local inbox.

Both roles pump nonblocking I/O on their existing owner thread. The listener binds one explicit loopback or RFC 1918 IPv4 address. Stream, frame, queue, or session failures discard the connection and its pending bytes. A one-second heartbeat and five-second inactivity limit allow the peer to reconnect and bootstrap a fresh view. This is a private trusted LAN protocol with no authentication or encryption.

The native test validates the shared parser, serializer, queue and session logic using Winsock. The Wii build uses libogc `net_*` and needs two Wii guests or physical consoles for a game-level test. The native runner has no dependency on Dolphin, captures, private save files or Wii controllers.

## Controlled stream faults

The suite also builds tests/wire/lan_fault_test.c with LAN_TEST_IO. Only that executable can inject a would-block send, suppress a receive or advance its process-local clock. All successful sends and receives use real loopback TCP. The test has no remote target option.

The fixture measures 200 numbered game records in each direction per session and requires 11 consecutive successful sessions (10 rejoins). It rejects peers presenting another protocol version, build tag or registered block material before any snapshot. The compatible peer uses a different texture coordinate and still joins. A two-second blocked sender accumulates two full chunks, then drains them. A forced burst past the queue limit must disconnect and clear queued work. A simulated partition tests the link at 4,999 ms and 5,000 ms. Three sessions apply 50 ms, 100 ms, and alternating 30/70 ms stream-send delays. These are short application-stream fault samples. The physical LAN loss, sustained five-minute profiles, and hardware timing gates remain separate.
## Wii network timing

A host launched with the trace option records `NETPUMP` and `NETIO` every 100 transport pumps. Values ending in `us` and the adjacent phase numbers are microseconds. The p95 covers complete pump time and each individual phase for connection work, socket sends, socket reads, command application and state encoding. Encoding may also occur outside the pump or inside command application. The independently ranked phase p95 values do not add up to the total p95. `NETIO` reports cumulative call and byte counts in the same window.
