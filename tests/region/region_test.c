/* Exercise real archive creation/index/publication on a temporary disk.
 * Only the compressor is replaced, so exact sector edge lengths are selectable. */
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

static int fail_close;
static int checked_close(FILE *f) {
    int result = fclose(f);
    if(fail_close && --fail_close == 0) return EOF;
    return result;
}
#define fclose checked_close
#include "../../source/network/region_archive.c"
#undef fclose

static size_t payload_len;
static unsigned char pattern;
struct buffer nbt_dump_compressed(const nbt_node *tree, nbt_compression_strategy strategy) {
    (void)tree; (void)strategy;
    struct buffer b = {malloc(payload_len), payload_len, payload_len};
    memset(b.data, pattern, payload_len);
    return b;
}
void buffer_free(struct buffer *b) { free(b->data); }
nbt_node *nbt_parse_compressed(const void *buf, size_t len) { (void)buf; (void)len; return NULL; }
nbt_node *nbt_find_by_path(nbt_node *n, const char *p) { (void)n; (void)p; return NULL; }
void nbt_free(nbt_node *n) { (void)n; }
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void) {
    string_t world; string_init_set_str(world, ".");
    struct region_archive ra;
    struct server_chunk chunk = {0};
    CHECK(region_archive_create_new(&ra, world, 0, 0, WORLD_DIM_OVERWORLD));
    payload_len = 4092; pattern = 41;  /* Five envelope bytes need a second sector. */
    CHECK(region_archive_set_blocks(&ra, 0, 0, &chunk));
    uint32_t first = ra.offsets[0];
    CHECK(first == (2u << 8 | 2u));
    FILE *f = fopen("region/r.0.0.mcr","rb"); CHECK(f);
    CHECK(fseek(f,0,SEEK_END) == 0 && ftell(f) == 4*4096);
    fclose(f);
    payload_len = 42; pattern = 92;
    fail_close = 1; /* Payload stream failed closing; preserve the committed index. */
    CHECK(!region_archive_set_blocks(&ra, 0, 0, &chunk));
    f = fopen("region/r.0.0.mcr","rb"); CHECK(f);
    uint32_t saved; CHECK(fread_u32(&saved,f)==1 && saved==first); fclose(f);
    CHECK(region_archive_set_blocks(&ra, 0, 0, &chunk));
    CHECK(ra.offsets[0] == (4u << 8 | 1u));
    CHECK(region_archive_set_blocks(&ra, 1, 0, &chunk));
    CHECK(ra.offsets[1] == (2u << 8 | 1u)); /* Released sector reused. */
    region_archive_destroy(&ra);
    CHECK(!region_archive_create_new(&ra, world, 0, 0, WORLD_DIM_OVERWORLD));
    CHECK(region_archive_create(&ra, world, 0, 0, WORLD_DIM_OVERWORLD));
    CHECK(ra.offsets[0] == (4u << 8 | 1u) && ra.offsets[1] == (2u << 8 | 1u));
    f = fopen("region/r.0.0.mcr","rb"); CHECK(f);
    CHECK(fseek(f,4*4096+5,SEEK_SET)==0 && fgetc(f)==92); fclose(f);
    fail_close = 2; /* Index close reports error even though this disk has the new word. */
    CHECK(!region_archive_set_blocks(&ra, 0, 0, &chunk));
    bool exists = false;
    CHECK(region_archive_contains(&ra, 0, 0, &exists) && exists);
    CHECK(ra.offsets[0] == (3u << 8 | 1u));
    region_archive_destroy(&ra); string_clear(world);
    puts("PASS region boundary padding, failed-close retention, copy-on-write, reuse, cold reopen and exclusive create.");
    return 0;
}
