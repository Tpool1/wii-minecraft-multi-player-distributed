# Wire and session checks

Run `tests/wire/run.ps1 -Compiler <path-to-clang.exe> -IncludeDir <Wii-build-staging/source>` on Windows. The suite compiles the production framing and RPC code against the game's real headers and starts two native processes on an ephemeral Windows loopback port. Logs go to `build/tests/wire`, or the specified `-OutputDirectory`.

* `lan_frame_test.c` checks exact header bytes, short reads, two logical records in one read, continuation assembly and rejection of invalid sizes, versions and flags.
* `lan_rpc_test.c` checks edit inputs, coordinates, double precision motion, all outbound RPC kinds, owned 80 KiB chunk planes and a checksum failure. The sender and serialized data are mutated before the receiver's arrays are checked.
* `lan_transport_test.c` exercises a host and peer with a complete chunk, edit and response. The host ends the first connection. Both sides complete a fresh handshake and snapshot before the second command acknowledgment. Each process returns a separate verdict.

## Wire contract

The 24-byte header uses network byte order. Bytes 0–3 identify `FCVX`, byte 4 is protocol version 1, byte 5 is message kind, bytes 6–7 are fragment flags, bytes 8–11 are fragment length, bytes 12–15 are the host session epoch and bytes 16–19 are the sender sequence. Bytes 20–23 are reserved and must be zero in this version. First and last fragment flags are 1 and 2. Each logical message consumes one sender sequence. TCP may split or combine fragments.

The connection begins with HELLO and WELCOME carrying the same build/protocol compatibility string. The host assigns the peer's slot outside the wire payload and issues a fresh epoch. The peer joins the world selected by the host. Independent asset/world hash negotiation is still open. Host-only load/unload/pause commands have no wire encoding. Client edit requests carry a monotonic action sequence and observed destination block type. A type precondition cannot distinguish a block changed away and back to the same type.

The limit is 8,192 payload bytes per fragment, 82,000 bytes per logical message and 256 KiB of pending outbound data. One record is assembled at a time. A full chunk encodes coordinates, extents, all four planes and a 32-bit FNV-1a checksum over the plane bytes. Successful peer decoding transfers four separate owned buffers to the game's chunk handler. Encoding borrows the sender's buffers until it returns. A host sending the same chunk to both views must encode the peer copy before handing the original allocations to the local inbox.

Both roles pump nonblocking I/O on their existing owner thread. The listener binds one explicit loopback or RFC 1918 IPv4 address. Stream, frame, queue, or session failures discard the connection and its pending bytes. A one-second heartbeat and ten-second inactivity limit allow the peer to reconnect and bootstrap a fresh view. This is a private trusted LAN protocol with no authentication or encryption.

The native test validates the shared parser, serializer, queue and session logic using Winsock. The Wii build uses libogc `net_*` and needs two Wii guests or physical consoles for a game-level test. The native runner has no dependency on Dolphin, captures, private save files or Wii controllers.
