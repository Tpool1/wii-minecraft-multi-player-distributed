/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Compile the production transport against the test-only 32-byte envelope.
 * The joined game, heartbeat, queue, parser and socket code is unchanged. */
#define lan_encode_command probe_encode_command
#define lan_decode_command probe_decode_command
#define lan_encode_state probe_encode_state
#define lan_decode_state probe_decode_state
#include "lan_transport.c"
