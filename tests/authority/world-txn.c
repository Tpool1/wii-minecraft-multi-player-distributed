/* Crash-point tests on normal files. A live journal at the simulated boot is an
 * interrupted save. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0777)
#endif
#if __has_include("../../source/network/world_txn.h")
#include "../../source/network/world_txn.h"
#else
/* The preceding version independently committed region and player files. */
#define fcx_txn_before_column(...) true
#define fcx_txn_before_players(...) true
#define fcx_txn_recover(...) true
#define fcx_txn_commit(...) true
#endif
#define CHECK(x)                                                               \
	do {                                                                       \
		if(!(x)) {                                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);                    \
			return 1;                                                          \
		}                                                                      \
	} while(0)
static const char* region = "region/r.0.0.mcr";
static void overwrite(const char* path, int offset, int c, size_t n,
					  bool create) {
	FILE* f = fopen(path, create ? "wb" : "rb+");
	if(!f || fseek(f, offset, SEEK_SET))
		abort();
	while(n--)
		if(fputc(c, f) == EOF)
			abort();
	if(fclose(f))
		abort();
}
static int get(const char* path, int offset) {
	FILE* f = fopen(path, "rb");
	if(!f)
		return -1;
	if(fseek(f, offset, SEEK_SET))
		abort();
	int c = fgetc(f);
	if(fclose(f))
		abort();
	return c;
}
static void seed(void) {
	overwrite(region, 0, 0, 5 * 4096, true);
	overwrite(region, 2, 2, 1, false);
	overwrite(region, 3, 1, 1, false);
	overwrite(region, 6, 3, 1, false);
	overwrite(region, 7, 1, 1, false);
	overwrite(region, 2 * 4096, 'A', 4096, false);
	overwrite(region, 3 * 4096, 'B', 4096, false);
	overwrite("level.dat", 0, 'L', 9, true);
	overwrite("level.dat.fcx-prev", 0, 'P', 9, true);
	overwrite("peer-b-1.fcx", 0, '1', 9, true);
	remove("peer-b-0.fcx");
}
int main(void) {
	MKDIR("region");
	for(int phase = 0; phase < 6; ++phase) {
		seed();
		CHECK(fcx_txn_before_column(".", false, 0, 0));
		overwrite(region, 2, 4, 1, false);
		overwrite(region, 4 * 4096, 'C', 4096, false);
		if(phase >= 1) {
			CHECK(fcx_txn_before_column(".", false, 1, 0));
			overwrite(region, 6, 2, 1, false);
			overwrite(region, 2 * 4096, 'D', 4096, false);
			/* Re-saving a column in the same transaction preserves its first
			 * preimage. */
			CHECK(fcx_txn_before_column(".", false, 0, 0));
		}
		if(phase >= 2) {
			CHECK(fcx_txn_before_players("."));
			overwrite("peer-b-0.fcx", 0, 'N', 9, true);
		}
		if(phase >= 3)
			overwrite("level.dat.fcx-prev", 0, 'L', 9, true);
		if(phase >= 4)
			overwrite("level.dat", 0, 'M', 9, true);
		if(phase == 5)
			CHECK(fcx_txn_commit("."));
		CHECK(fcx_txn_recover("."));
		CHECK(fcx_txn_recover("."));
		if(phase < 5) {
			CHECK(get(region, 2) == 2 && get(region, 6) == 3);
			CHECK(get(region, 2 * 4096) == 'A' && get(region, 3 * 4096) == 'B');
			CHECK(get("level.dat", 0) == 'L'
				  && get("level.dat.fcx-prev", 0) == 'P');
			CHECK(get("peer-b-0.fcx", 0) == -1
				  && get("peer-b-1.fcx", 0) == '1');
		} else {
			CHECK(get(region, 2) == 4 && get(region, 6) == 2
				  && get(region, 2 * 4096) == 'D');
			CHECK(get("level.dat", 0) == 'M' && get("peer-b-0.fcx", 0) == 'N');
		}
	}
	/* Interrupted record creation precedes its authorization marker; no live
	 * bytes changed. */
	seed();
	MKDIR("fcx-undo-v1");
	overwrite("fcx-undo-v1/o.0.0.rec", 0, '?', 5, true);
	CHECK(fcx_txn_recover("."));
	CHECK(get(region, 2) == 2 && get(region, 2 * 4096) == 'A');

	/* Reject corruption before touching any member of a partially committed
	 * world. */
	CHECK(fcx_txn_before_column(".", false, 0, 0));
	CHECK(fcx_txn_before_column(".", false, 1, 0));
	overwrite(region, 2, 4, 1, false);
	overwrite(region, 2 * 4096, 'X', 4096, false);
	overwrite(region, 6, 2, 1, false);
	const char* record = "fcx-undo-v1/o.1.0.rec";
	int original = get(record, 16);
	CHECK(original == 'B');
	overwrite(record, 16, '?', 1, false);
	CHECK(!fcx_txn_recover("."));
	CHECK(get(region, 2) == 4 && get(region, 6) == 2
		  && get(region, 2 * 4096) == 'X');
	overwrite(record, 16, original, 1, false);
	/* Rollback had finished this one location before a second interruption. */
	overwrite(region, 2, 2, 1, false);
	overwrite(region, 2 * 4096, 'A', 4096, false);
	CHECK(fcx_txn_recover("."));
	CHECK(get(region, 2) == 2 && get(region, 6) == 3
		  && get(region, 3 * 4096) == 'B');

	/* A crash during cleanup has already committed. Missing old records are
	 * expected. */
	CHECK(fcx_txn_before_column(".", false, 0, 0));
	overwrite(region, 2, 4, 1, false);
	overwrite("fcx-undo-v1/commit.ok", 0, 0, 0, true);
	CHECK(remove("fcx-undo-v1/o.0.0.rec") == 0);
	CHECK(fcx_txn_recover("."));
	CHECK(get(region, 2) == 4);
	seed();

	/* Unknown or lost prepared preimage blocks recovery. */
	CHECK(fcx_txn_before_column(".", false, 0, 0));
	CHECK(remove("fcx-undo-v1/o.0.0.rec") == 0);
	CHECK(!fcx_txn_recover("."));
	return 0;
}
