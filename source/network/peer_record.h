/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PEER_RECORD_H
#define PEER_RECORD_H
#include <stdbool.h>
struct server_local;
bool peer_record_save(struct server_local *s);
bool peer_record_load(struct server_local *s);
#endif
