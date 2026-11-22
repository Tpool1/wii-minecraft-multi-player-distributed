/*
	Copyright (c) 2022 ByteBit/xtreme8000

	This file is part of CavEX.

	CavEX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	CavEX is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with CavEX.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "../game/alloc_metrics.h"
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <zlib.h>
#include <unistd.h>
#include <m-lib/m-string.h>

#include "../cNBT/nbt.h"

#include "region_archive.h"
#include "server_world.h"

#define CHUNK_EXISTS(offset, sectors) ((offset) >= 2 && (sectors) >= 1)

static uint32_t conv_u32_native(uint8_t* data) {
	return ((uint32_t)data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

static void conv_native_u32(uint32_t in, uint8_t* data) {
	data[0] = in >> 24;
	data[1] = (in >> 16) & 0xFF;
	data[2] = (in >> 8) & 0xFF;
	data[3] = in & 0xFF;
}

static size_t fread_u32(uint32_t* out, FILE* f) {
	size_t res = fread(out, sizeof(uint32_t), 1, f);
	*out = conv_u32_native((uint8_t*)out);
	return res;
}

static size_t fwrite_u32(uint32_t in, FILE* f) {
	uint8_t tmp[sizeof(uint32_t)];
	conv_native_u32(in, tmp);

	return fwrite(tmp, sizeof(uint32_t), 1, f);
}

static int sort_region_chunks(const void* a, const void* b) {
	uint32_t offset_a = (*(const uint32_t*)a) >> 8;
	uint32_t offset_b = (*(const uint32_t*)b) >> 8;
	return offset_a - offset_b;
}

static bool rebuild_occupied_list(struct region_archive* ra) {
	assert(ra);

	ra->occupied_index = 0;
	ra->occupied_sorted[ra->occupied_index++] = (0 << 8) | 2;

	for(size_t k = 0; k < REGION_SIZE * REGION_SIZE; k++) {
		uint32_t offset = ra->offsets[k] >> 8;
		uint32_t sectors = ra->offsets[k] & 0xFF;

		if(CHUNK_EXISTS(offset, sectors))
			ra->occupied_sorted[ra->occupied_index++] = ra->offsets[k];
	}

	qsort(ra->occupied_sorted, ra->occupied_index, sizeof(uint32_t),
		  sort_region_chunks);

	uint32_t end = 2;
	for(size_t k = 1; k < ra->occupied_index; k++) {
		uint32_t offset = ra->occupied_sorted[k] >> 8;

		if(offset < end)
			return false;

		end = offset + (ra->occupied_sorted[k] & 0xFF);
	}

	return true;
}

bool region_archive_create_new(struct region_archive* ra, string_t world_name,
							   w_coord_t x, w_coord_t z,
							   enum world_dim dimension) {
	assert(ra && world_name);

	if(dimension == WORLD_DIM_OVERWORLD) {
		string_init_printf(ra->file_name, "%s/region/r.%i.%i.mcr",
						   string_get_cstr(world_name), x, z);
	} else {
		string_init_printf(ra->file_name, "%s/DIM-1/region/r.%i.%i.mcr",
						   string_get_cstr(world_name), x, z);
	}

	const char *path = string_get_cstr(ra->file_name);
	int flags = O_CREAT | O_EXCL | O_WRONLY;
#ifdef O_BINARY
	flags |= O_BINARY;
#endif
	int fd = open(path, flags, 0666);
	FILE *f = fd < 0 ? NULL : fdopen(fd, "wb");

	if(!f) {
		if(fd >= 0) {
			close(fd);
			remove(path);
		}
		string_clear(ra->file_name);
		return false;
	}
	static const uint8_t zeros[256] = {0};
	bool ok = true;
	for(size_t i = 0; ok && i < 2 * REGION_SECTOR_SIZE; i += sizeof(zeros))
		ok = fwrite(zeros, sizeof(zeros), 1, f) == 1;
	if(fclose(f) != 0) ok = false;
	if(!ok) remove(path);
	string_clear(ra->file_name);
	if(!ok) return false;

	return region_archive_create(ra, world_name, x, z, dimension);
}

/* A failed index close can leave either generation on disk. Refresh before
 * any subsequent allocation or lookup so a possibly live sector is never reused. */
static bool load_offsets(struct region_archive *ra) {
	FILE *f = fopen(string_get_cstr(ra->file_name), "rb");
	if(!f) return false;
	bool ok = fread(ra->offsets, sizeof(uint32_t), REGION_SIZE * REGION_SIZE, f)
		== REGION_SIZE * REGION_SIZE;
	if(fclose(f) != 0) ok = false;
	if(!ok) return false;
	for(size_t i = 0; i < REGION_SIZE * REGION_SIZE; ++i)
		ra->offsets[i] = conv_u32_native((uint8_t *)(ra->offsets + i));
	return rebuild_occupied_list(ra);
}

bool region_archive_create(struct region_archive* ra, string_t world_name,
						   w_coord_t x, w_coord_t z, enum world_dim dimension) {
	assert(ra && world_name);

	ra->offsets = alloc_payload(AM_ARCHIVE, sizeof(uint32_t) * REGION_SIZE * REGION_SIZE);

	if(!ra->offsets)
		return false;

	ra->occupied_sorted
		= alloc_payload(AM_ARCHIVE, sizeof(uint32_t) * (REGION_SIZE * REGION_SIZE + 1));

	if(!ra->occupied_sorted) {
		alloc_free(ra->offsets);
		return false;
	}

	if(dimension == WORLD_DIM_OVERWORLD) {
		string_init_printf(ra->file_name, "%s/region/r.%i.%i.mcr",
						   string_get_cstr(world_name), x, z);
	} else {
		string_init_printf(ra->file_name, "%s/DIM-1/region/r.%i.%i.mcr",
						   string_get_cstr(world_name), x, z);
	}

	ra->x = x;
	ra->z = z;

	ra->needs_refresh = false;
	ilist_regions_init_field(ra);
	if(!load_offsets(ra)) {
		alloc_free(ra->offsets);
		alloc_free(ra->occupied_sorted);
		string_clear(ra->file_name);
		return false;
	}

	return true;
}

void region_archive_destroy(struct region_archive* ra) {
	assert(ra && ra->offsets && ra->occupied_sorted);

	alloc_free(ra->offsets);
	alloc_free(ra->occupied_sorted);
	string_clear(ra->file_name);
}

bool region_archive_contains(struct region_archive* ra, w_coord_t x,
							 w_coord_t z, bool* chunk_exists) {
	assert(ra && chunk_exists);

	if(CHUNK_REGION_COORD(x) != ra->x || CHUNK_REGION_COORD(z) != ra->z) {
		*chunk_exists = false;
		return false;
	}

	if(ra->needs_refresh) {
		if(!load_offsets(ra)) { *chunk_exists = false; return false; }
		ra->needs_refresh = false;
	}

	int rx = x & (REGION_SIZE - 1);
	int rz = z & (REGION_SIZE - 1);

	uint32_t offset = ra->offsets[rx + rz * REGION_SIZE] >> 8;
	uint32_t sectors = ra->offsets[rx + rz * REGION_SIZE] & 0xFF;

	*chunk_exists = CHUNK_EXISTS(offset, sectors);
	return true;
}

struct column_nbt {
	nbt_node *root;
	nbt_node *blocks, *metadata, *sky, *torch, *height;
};

/* One column at a time. Reject excessive inflated metadata before parsing or
 * writing; silently replacing an unreadable previous record would lose tags. */
static nbt_node *parse_column(const uint8_t *data, size_t bytes) {
	enum { MAX_NBT_BYTES = 2 * 1024 * 1024 };
	z_stream stream = {0};
	if(inflateInit2(&stream, 15 + 32) != Z_OK) return NULL;
	stream.next_in = (Bytef *)data;
	stream.avail_in = bytes;
	struct buffer raw = BUFFER_INIT;
	uint8_t block[1024];
	int status;
	do {
		stream.next_out = block;
		stream.avail_out = sizeof(block);
		status = inflate(&stream, Z_NO_FLUSH);
		size_t length = sizeof(block) - stream.avail_out;
		if(length > MAX_NBT_BYTES - raw.len || (length && buffer_append(&raw, block, length))) {
			status = Z_MEM_ERROR;
			break;
		}
	} while(status == Z_OK);
	inflateEnd(&stream);
	nbt_node *root = status == Z_STREAM_END ? nbt_parse(raw.data, raw.len) : NULL;
	buffer_free(&raw);
	return root;
}

static bool load_column(struct region_archive *ra, w_coord_t x, w_coord_t z,
						struct column_nbt *nodes) {
	bool chunk_exists;
	if(!region_archive_contains(ra, x, z, &chunk_exists) || !chunk_exists) return false;

	int rx = x & (REGION_SIZE - 1);
	int rz = z & (REGION_SIZE - 1);

	uint32_t offset = ra->offsets[rx + rz * REGION_SIZE] >> 8;
	uint32_t sectors = ra->offsets[rx + rz * REGION_SIZE] & 0xFF;

	// TODO: little endian

	FILE* f = fopen(string_get_cstr(ra->file_name), "rb");

	if(!f)
		return false;

	if(fseek(f, offset * REGION_SECTOR_SIZE, SEEK_SET) != 0) {
		fclose(f);
		return false;
	}

	uint32_t length;
	if(!fread_u32(&length, f)
	   || length < 2 || length > sectors * REGION_SECTOR_SIZE - sizeof(uint32_t)) {
		fclose(f);
		return false;
	}

	uint8_t type;
	if(!fread(&type, sizeof(uint8_t), 1, f) || type > 3) {
		fclose(f);
		return false;
	}

	void* nbt_compressed = alloc_payload(AM_ARCHIVE, length - 1);

	if(!nbt_compressed) {
		fclose(f);
		return false;
	}

	if(!fread(nbt_compressed, length - 1, 1, f)) {
		alloc_free(nbt_compressed);
		fclose(f);
		return false;
	}

	nbt_node* chunk = parse_column(nbt_compressed, length - 1);

	alloc_free(nbt_compressed);
	fclose(f);

	if(!chunk)
		return false;

	nbt_node* n_x = nbt_find_by_path(chunk, ".Level.xPos");
	nbt_node* n_z = nbt_find_by_path(chunk, ".Level.zPos");

	if(!n_x || !n_z || n_x->type != TAG_INT || n_z->type != TAG_INT
	   || n_x->payload.tag_int != x || n_z->payload.tag_int != z) {
		nbt_free(chunk);
		return false;
	}

	nbt_node* n_blocks = nbt_find_by_path(chunk, ".Level.Blocks");
	nbt_node* n_metadata = nbt_find_by_path(chunk, ".Level.Data");
	nbt_node* n_skyl = nbt_find_by_path(chunk, ".Level.SkyLight");
	nbt_node* n_torchl = nbt_find_by_path(chunk, ".Level.BlockLight");
	nbt_node* n_height = nbt_find_by_path(chunk, ".Level.HeightMap");

	if(!n_blocks || !n_metadata || !n_skyl || !n_torchl || !n_height
	   || n_blocks->type != TAG_BYTE_ARRAY || n_metadata->type != TAG_BYTE_ARRAY
	   || n_skyl->type != TAG_BYTE_ARRAY || n_torchl->type != TAG_BYTE_ARRAY
	   || n_height->type != TAG_BYTE_ARRAY
	   || n_blocks->payload.tag_byte_array.length
		   != CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT
	   || n_metadata->payload.tag_byte_array.length
		   != CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT / 2
	   || n_skyl->payload.tag_byte_array.length
		   != CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT / 2
	   || n_torchl->payload.tag_byte_array.length
		   != CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT / 2
	   || n_height->payload.tag_byte_array.length != CHUNK_SIZE * CHUNK_SIZE) {
		nbt_free(chunk);
		return false;
	}

	*nodes = (struct column_nbt){chunk, n_blocks, n_metadata, n_skyl, n_torchl, n_height};
	return true;
}

bool region_archive_get_blocks(struct region_archive *ra, w_coord_t x, w_coord_t z,
							   struct server_chunk *sc) {
	struct column_nbt n;
	if(!load_column(ra, x, z, &n)) return false;
	sc->ids = n.blocks->payload.tag_byte_array.data;
	sc->metadata = n.metadata->payload.tag_byte_array.data;
	sc->lighting_sky = n.sky->payload.tag_byte_array.data;
	sc->lighting_torch = n.torch->payload.tag_byte_array.data;
	sc->heightmap = n.height->payload.tag_byte_array.data;
	alloc_adopt(AM_AUTHORITY, sc->ids, n.blocks->payload.tag_byte_array.length);
	alloc_adopt(AM_AUTHORITY, sc->metadata, n.metadata->payload.tag_byte_array.length);
	alloc_adopt(AM_AUTHORITY, sc->lighting_sky, n.sky->payload.tag_byte_array.length);
	alloc_adopt(AM_AUTHORITY, sc->lighting_torch, n.torch->payload.tag_byte_array.length);
	alloc_adopt(AM_AUTHORITY, sc->heightmap, n.height->payload.tag_byte_array.length);

	/* Transfer just the five array allocations to the resident server chunk. */
	n.blocks->type = n.metadata->type = n.sky->type = n.torch->type = n.height->type = TAG_INVALID;
	nbt_free(n.root);
	return true;
}

/* Keep the prior record referenced until the replacement body and padding have
 * been closed successfully. The header update is a separate stream/close. */
static bool store_record(struct region_archive *ra, size_t slot, struct buffer body) {
	if(!body.len || body.len > 255u * REGION_SECTOR_SIZE - 5u) return false;
	if(ra->needs_refresh) {
		if(!load_offsets(ra)) return false;
		ra->needs_refresh = false;
	}
	uint32_t sectors = (body.len + 5 + REGION_SECTOR_SIZE - 1) / REGION_SECTOR_SIZE;
	uint32_t offset = 0;
	for(size_t i = 0; i < ra->occupied_index; ++i) {
		uint32_t entry = ra->occupied_sorted[i];
		offset = (entry >> 8) + (entry & 0xFF);
		if(i + 1 == ra->occupied_index || offset + sectors <= (ra->occupied_sorted[i+1] >> 8))
			break;
	}
	if(offset < 2 || offset > 0xFFFFFFu || offset + sectors > LONG_MAX / REGION_SECTOR_SIZE)
		return false;
	const char *path = string_get_cstr(ra->file_name);
	FILE *f = fopen(path, "rb+");
	if(!f) return false;
	bool ok = fseek(f, (long)offset * REGION_SECTOR_SIZE, SEEK_SET) == 0
		&& fwrite_u32(body.len + 1, f) == 1 && fputc(2, f) != EOF
		&& fwrite(body.data, 1, body.len, f) == body.len;
	static const uint8_t padding[256] = {0};
	size_t remain = sectors * REGION_SECTOR_SIZE - body.len - 5;
	while(ok && remain) {
		size_t bytes = remain < sizeof(padding) ? remain : sizeof(padding);
		ok = fwrite(padding, 1, bytes, f) == bytes;
		remain -= bytes;
	}
	if(fclose(f) != 0) ok = false;
	if(!ok) return false;

	f = fopen(path, "rb+");
	if(!f) return false;
	uint32_t entry = (offset << 8) | sectors;
	ok = fseek(f, (long)(slot * sizeof(uint32_t)), SEEK_SET) == 0 && fwrite_u32(entry, f) == 1;
	if(fclose(f) != 0) ok = false;
	if(!ok) {
		ra->needs_refresh = true;
		return false;
	}
	ra->offsets[slot] = entry;
	return rebuild_occupied_list(ra);
}

static struct buffer new_column(w_coord_t x, w_coord_t z, const struct server_chunk *sc) {
	struct nbt_list root_list_sentinel = (struct nbt_list) {
		.data = NULL,
	};
	struct nbt_list level_list_sentinel = (struct nbt_list) {
		.data = NULL,
	};
	struct nbt_list empty_list_sentinel = (struct nbt_list) {
		.data = &(nbt_node) {.type = TAG_COMPOUND},
	};

	INIT_LIST_HEAD(&root_list_sentinel.entry);
	INIT_LIST_HEAD(&level_list_sentinel.entry);
	INIT_LIST_HEAD(&empty_list_sentinel.entry);

	nbt_node root = (nbt_node) {
		.type = TAG_COMPOUND,
		.name = "",
		.payload.tag_compound = &root_list_sentinel,
	};

	nbt_node level = (nbt_node) {
		.type = TAG_COMPOUND,
		.name = "Level",
		.payload.tag_compound = &level_list_sentinel,
	};

	struct nbt_list root_list = (struct nbt_list) {
		.data = &level,
	};

	list_add_head(&root_list.entry, &root_list_sentinel.entry);

	nbt_node level_list_nodes[] = {
		{
			.type = TAG_BYTE_ARRAY,
			.name = "Blocks",
			.payload.tag_byte_array.data = sc->ids,
			.payload.tag_byte_array.length
			= CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT,
		},
		{
			.type = TAG_BYTE_ARRAY,
			.name = "Data",
			.payload.tag_byte_array.data = sc->metadata,
			.payload.tag_byte_array.length
			= CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT / 2,
		},
		{
			.type = TAG_BYTE_ARRAY,
			.name = "SkyLight",
			.payload.tag_byte_array.data = sc->lighting_sky,
			.payload.tag_byte_array.length
			= CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT / 2,
		},
		{
			.type = TAG_BYTE_ARRAY,
			.name = "BlockLight",
			.payload.tag_byte_array.data = sc->lighting_torch,
			.payload.tag_byte_array.length
			= CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT / 2,
		},
		{
			.type = TAG_BYTE_ARRAY,
			.name = "HeightMap",
			.payload.tag_byte_array.data = sc->heightmap,
			.payload.tag_byte_array.length = CHUNK_SIZE * CHUNK_SIZE,
		},
		{
			.type = TAG_LIST,
			.name = "Entities",
			.payload.tag_list = &empty_list_sentinel,
		},
		{
			.type = TAG_LIST,
			.name = "TileEntities",
			.payload.tag_list = &empty_list_sentinel,
		},
		{.type = TAG_LONG, .name = "LastUpdate", .payload.tag_long = 0},
		{.type = TAG_INT, .name = "xPos", .payload.tag_int = x},
		{.type = TAG_INT, .name = "zPos", .payload.tag_int = z},
		{.type = TAG_BYTE, .name = "TerrainPopulated", .payload.tag_byte = 1},
	};

	struct nbt_list
		level_list[sizeof(level_list_nodes) / sizeof(*level_list_nodes)];

	for(size_t k = 0; k < sizeof(level_list) / sizeof(*level_list); k++) {
		level_list[k].data = level_list_nodes + k;
		list_add_tail(&level_list[k].entry, &level_list_sentinel.entry);
	}

	return nbt_dump_compressed(&root, STRAT_INFLATE);
}

bool region_archive_set_blocks(struct region_archive *ra, w_coord_t x,
							   w_coord_t z, struct server_chunk *sc) {
	assert(ra && sc);
	bool exists;
	if(!region_archive_contains(ra, x, z, &exists)) return false;
	struct buffer body;
	if(exists) {
		struct column_nbt n;
		if(!load_column(ra, x, z, &n)) return false;
		memcpy(n.blocks->payload.tag_byte_array.data, sc->ids, n.blocks->payload.tag_byte_array.length);
		memcpy(n.metadata->payload.tag_byte_array.data, sc->metadata, n.metadata->payload.tag_byte_array.length);
		memcpy(n.sky->payload.tag_byte_array.data, sc->lighting_sky, n.sky->payload.tag_byte_array.length);
		memcpy(n.torch->payload.tag_byte_array.data, sc->lighting_torch, n.torch->payload.tag_byte_array.length);
		memcpy(n.height->payload.tag_byte_array.data, sc->heightmap, n.height->payload.tag_byte_array.length);
		body = nbt_dump_compressed(n.root, STRAT_INFLATE);
		nbt_free(n.root);
	} else {
		body = new_column(x, z, sc);
	}
	if(!body.data) return false;
	int rx = x & (REGION_SIZE - 1), rz = z & (REGION_SIZE - 1);
	bool success = store_record(ra, rx + rz * REGION_SIZE, body);
	buffer_free(&body);
	return success;
}
