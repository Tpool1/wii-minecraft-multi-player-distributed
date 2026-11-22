/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FCX_WORLD_TXN_H
#define FCX_WORLD_TXN_H
#include <stdbool.h>
#include <stdint.h>
bool fcx_txn_before_column(const char* world, bool nether, int32_t x,
						   int32_t z);
bool fcx_txn_before_players(const char* world);
bool fcx_txn_commit(const char* world);
bool fcx_txn_recover(const char* world);
#endif
