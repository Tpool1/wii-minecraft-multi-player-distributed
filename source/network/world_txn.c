/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "world_txn.h"
#include "checkpoint_file.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define FCX_MKDIR(p) _mkdir(p)
#define FCX_RMDIR(p) _rmdir(p)
#else
#include <unistd.h>
#define FCX_MKDIR(p) mkdir(p, 0777)
#define FCX_RMDIR(p) rmdir(p)
#endif

/* Each host transaction spans old region sectors and the A/B metadata files.
 * Read-verified .rec + .ok precedes mutation. A global marker follows every
 * successful mutation and remains while old records are removed. Crashes
 * before that point replay the first preimage for each touched key. */
#define ACTIVE "fcx-undo-v1"
#define COMMITTED "commit.ok"
#define ABSENT UINT32_MAX
enum {
	PATH_CAP = 640,
	KEY_CAP = 56,
	HEADER = 16,
	SECTOR = 4096,
	METADATA_LIMIT = 16 * 1024 * 1024
};
static const char* const files[]
	= {"level.dat", ("level.dat" CHECKPOINT_PREVIOUS), "peer-b-0.fcx",
	   "peer-b-1.fcx"};
struct entry {
	bool column, nether;
	int x, z, meta;
};

static bool path_join(char out[PATH_CAP], const char* base, const char* name) {
	int n = snprintf(out, PATH_CAP, "%s/%s", base, name);
	return n >= 0 && n < PATH_CAP;
}
static int region_coord(int x) {
	return x >= 0 ? x / 32 : -1 - ((-1 - x) / 32);
}
static bool data_path(char out[PATH_CAP], const char* world,
					  const struct entry* e) {
	if(!e->column)
		return path_join(out, world, files[e->meta]);
	char name[96];
	snprintf(name, sizeof name, "%sregion/r.%d.%d.mcr",
			 e->nether ? "DIM-1/" : "", region_coord(e->x), region_coord(e->z));
	return path_join(out, world, name);
}
static void key_for(const struct entry* e, char out[KEY_CAP]) {
	if(e->column)
		snprintf(out, KEY_CAP, "%c.%d.%d.rec", e->nether ? 'n' : 'o', e->x,
				 e->z);
	else
		snprintf(out, KEY_CAP, "m%d.rec", e->meta);
}
static bool parse_key(const char* name, struct entry* e) {
	char canonical[KEY_CAP];
	*e = (struct entry) {0};
	if(name[0] == 'm' && name[1] >= '0' && name[1] <= '3')
		e->meta = name[1] - '0';
	else if(name[0] == 'o' || name[0] == 'n') {
		e->column = true;
		e->nether = name[0] == 'n';
		char* end;
		if(name[1] != '.')
			return false;
		errno = 0;
		long x = strtol(name + 2, &end, 10);
		if(end == name + 2 || errno || *end != '.' || x < -1000000
		   || x > 1000000)
			return false;
		const char* start = end + 1;
		long z = strtol(start, &end, 10);
		if(end == start || errno || strcmp(end, ".rec") || z < -1000000
		   || z > 1000000)
			return false;
		e->x = (int)x;
		e->z = (int)z;
	} else
		return false;
	key_for(e, canonical);
	return !strcmp(name, canonical);
}
static uint32_t decode(const unsigned char* p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8
		| p[3];
}
static void encode(unsigned char* p, uint32_t n) {
	p[0] = n >> 24;
	p[1] = n >> 16;
	p[2] = n >> 8;
	p[3] = n;
}
static uint32_t hash(uint32_t h, const void* bytes, size_t n) {
	const unsigned char* p = bytes;
	while(n--)
		h = (h ^ *p++) * UINT32_C(16777619);
	return h;
}
static bool pump(FILE* src, FILE* dst, uint32_t remaining, uint32_t* sum) {
	unsigned char block[1024];
	while(remaining) {
		size_t n = remaining > sizeof block ? sizeof block : remaining;
		if(fread(block, 1, n, src) != n
		   || (dst && fwrite(block, 1, n, dst) != n))
			return false;
		*sum = hash(*sum, block, n);
		remaining -= n;
	}
	return true;
}
static bool close_out(FILE* f, bool ok) {
	if(fflush(f) != 0 || ferror(f))
		ok = false;
	return fclose(f) == 0 && ok;
}
static long index_offset(const struct entry* e) {
	return 4L * ((e->x & 31) + 32 * (e->z & 31));
}
static bool seek_payload(FILE* f, uint32_t word) {
	uint32_t sector = word >> 8;
	return sector >= 2 && sector <= (uint32_t)(LONG_MAX / SECTOR)
		&& fseek(f, (long)sector * SECTOR, SEEK_SET) == 0;
}
static bool remove_if_present(const char* name) {
	return remove(name) == 0 || errno == ENOENT;
}

static bool check_record(const char* world, const char* record, const char* key,
						 const struct entry* e, bool restore) {
	FILE* in = fopen(record, "rb");
	if(!in)
		return false;
	unsigned char h[HEADER];
	bool ok = fread(h, 1, HEADER, in) == HEADER && !memcmp(h, "FCU1", 4);
	uint32_t word = 0, bytes = 0, expected = 0, sum = 0;
	if(ok) {
		word = decode(h + 4);
		bytes = decode(h + 8);
		expected = decode(h + 12);
		ok = e->column ?
			((word == 0 || ((word >> 8) >= 2 && (word & 255)))
			 && bytes == (word & 255) * SECTOR) :
			(word == 0 && (bytes == ABSENT || bytes <= METADATA_LIMIT));
		sum = hash(hash(UINT32_C(2166136261), key, strlen(key)), h, 12);
	}
	char target[PATH_CAP];
	FILE* out = NULL;
	if(ok && restore) {
		ok = data_path(target, world, e);
		if(ok && bytes != ABSENT) {
			out = fopen(target, e->column ? "rb+" : "wb");
			ok = out && (!e->column || !bytes || seek_payload(out, word));
		}
	}
	if(ok && bytes != ABSENT)
		ok = pump(in, out, bytes, &sum);
	if(ok)
		ok = fgetc(in) == EOF && !ferror(in) && sum == expected;
	if(fclose(in) != 0)
		ok = false;
	if(out) {
		if(ok && e->column)
			ok = fseek(out, index_offset(e), SEEK_SET) == 0
				&& fwrite(h + 4, 1, 4, out) == 4;
		ok = close_out(out, ok);
	}
	if(ok && restore && bytes == ABSENT)
		ok = remove_if_present(target);
	return ok;
}

enum operation { CHECK, RESTORE, CLEAN };

/* A zero-length directory entry is enough to acknowledge fully verified bytes.
 * No rename or duplicate FAT cluster chain is involved in either commit point.
 */
static int marker_state(const char* path) {
	struct stat st;
	if(stat(path, &st) != 0)
		return errno == ENOENT ? 0 : -1;
	return S_ISREG(st.st_mode) && st.st_size == 0 ? 1 : -1;
}
static bool make_marker(const char* path) {
	FILE* f = fopen(path, "wb");
	return f && close_out(f, true) && marker_state(path) == 1;
}
static bool visit(const char* world, const char* dir, enum operation op) {
	char path[PATH_CAP];
	DIR* d = opendir(dir);
	if(!d)
		return errno == ENOENT;
	bool ok = true;
	while(ok) {
		errno = 0;
		struct dirent* ent = readdir(d);
		if(!ent) {
			ok = errno == 0;
			break;
		}
		char name[KEY_CAP + 3];
		size_t length = strlen(ent->d_name);
		if(length >= sizeof name) {
			ok = false;
			break;
		}
		/* A FAT short-name entry may enumerate in uppercase. */
		for(size_t i = 0; i <= length; ++i) {
			char c = ent->d_name[i];
			name[i] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
		}
		if(!strcmp(name, ".") || !strcmp(name, "..")
		   || !strcmp(name, COMMITTED))
			continue;
		size_t n = strlen(name);
		bool ready = n > 3 && !strcmp(name + n - 3, ".ok");
		char key[KEY_CAP];
		size_t len = n - (ready ? 3 : 0);
		if(len >= sizeof key) {
			ok = false;
			break;
		}
		memcpy(key, name, len);
		key[len] = 0;
		struct entry e;
		if(!parse_key(key, &e) || !path_join(path, dir, name)) {
			ok = false;
			break;
		}
		if(op == CLEAN)
			ok = remove_if_present(path);
		else if(ready)
			ok = marker_state(path) == 1 && path_join(path, dir, key)
				&& check_record(world, path, key, &e, op == RESTORE);
		/* A partial .rec without .ok never authorized a live-file mutation. */
	}
	if(closedir(d) != 0)
		ok = false;
	if(ok && op == CLEAN)
		ok = path_join(path, dir, COMMITTED) && remove_if_present(path)
			&& FCX_RMDIR(dir) == 0;
	return ok;
}

struct paths {
	char dir[PATH_CAP], record[PATH_CAP], marker[PATH_CAP], target[PATH_CAP];
};
static bool preserve_entry(const char* world, const struct entry* e,
						   struct paths* p) {
	char key[KEY_CAP], ready[KEY_CAP + 3];
	key_for(e, key);
	snprintf(ready, sizeof ready, "%s.ok", key);
	if(!path_join(p->dir, world, ACTIVE)
	   || !path_join(p->marker, p->dir, COMMITTED))
		return false;
	int committed = marker_state(p->marker);
	if(committed < 0 || (committed && !visit(world, p->dir, CLEAN)))
		return false;
	if(!path_join(p->record, p->dir, key)
	   || !path_join(p->marker, p->dir, ready)
	   || !data_path(p->target, world, e))
		return false;
	if(FCX_MKDIR(p->dir) != 0 && errno != EEXIST)
		return false;
	int prepared = marker_state(p->marker);
	if(prepared)
		return prepared == 1 && check_record(world, p->record, key, e, false);
	FILE* in = fopen(p->target, "rb");
	if(!in && (e->column || errno != ENOENT))
		return false;
	unsigned char h[HEADER] = {'F', 'C', 'U', '1'};
	uint32_t bytes = ABSENT;
	bool ok = true;
	if(e->column) {
		ok = fseek(in, index_offset(e), SEEK_SET) == 0
			&& fread(h + 4, 1, 4, in) == 4;
		uint32_t word = decode(h + 4);
		bytes = (word & 255) * SECTOR;
		ok = ok && (word == 0 || ((word & 255) && seek_payload(in, word)));
	} else if(in) {
		if(fseek(in, 0, SEEK_END))
			ok = false;
		long size = ok ? ftell(in) : -1;
		ok = size >= 0 && size <= METADATA_LIMIT && fseek(in, 0, SEEK_SET) == 0;
		if(ok)
			bytes = (uint32_t)size;
	}
	encode(h + 8, bytes);
	uint32_t sum = hash(hash(UINT32_C(2166136261), key, strlen(key)), h, 12);
	FILE* out = ok ? fopen(p->record, "wb") : NULL;
	ok = out && fwrite(h, 1, HEADER, out) == HEADER;
	if(ok && bytes != ABSENT)
		ok = pump(in, out, bytes, &sum);
	if(in && fclose(in) != 0)
		ok = false;
	encode(h + 12, sum);
	if(out) {
		if(ok)
			ok = fseek(out, 0, SEEK_SET) == 0
				&& fwrite(h, 1, HEADER, out) == HEADER;
		ok = close_out(out, ok);
	}
	return ok && check_record(world, p->record, key, e, false)
		&& make_marker(p->marker);
}
static bool preserve(const char* world, const struct entry* e) {
	/* Keep path scratch off the small Wii authority thread stack. */
	struct paths* p = malloc(sizeof *p);
	if(!p)
		return false;
	bool ok = preserve_entry(world, e, p);
	free(p);
	return ok;
}
bool fcx_txn_before_column(const char* world, bool nether, int32_t x,
						   int32_t z) {
	return preserve(
		world,
		&(struct entry) {.column = true, .nether = nether, .x = x, .z = z});
}
bool fcx_txn_before_players(const char* world) {
	for(unsigned i = 0; i < sizeof files / sizeof files[0]; ++i)
		if(!preserve(world, &(struct entry) {.meta = (int)i}))
			return false;
	return true;
}

bool fcx_txn_commit(const char* world) {
	char dir[PATH_CAP], marker[PATH_CAP];
	if(!path_join(dir, world, ACTIVE) || !path_join(marker, dir, COMMITTED))
		return false;
	struct stat st;
	if(stat(dir, &st) != 0)
		return errno == ENOENT;
	if(!S_ISDIR(st.st_mode) || !make_marker(marker))
		return false;
	/* A committed marker stays until every preimage is gone. Cleanup is
	 * retryable. */
	(void)visit(world, dir, CLEAN);
	return true;
}
bool fcx_txn_recover(const char* world) {
	char dir[PATH_CAP], marker[PATH_CAP];
	if(!path_join(dir, world, ACTIVE) || !path_join(marker, dir, COMMITTED))
		return false;
	int committed = marker_state(marker);
	if(committed < 0)
		return false;
	if(committed)
		return visit(world, dir, CLEAN);
	return visit(world, dir, CHECK) && visit(world, dir, RESTORE)
		&& fcx_txn_commit(world);
}
