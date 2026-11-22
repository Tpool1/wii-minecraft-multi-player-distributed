/* Test a real compressed McRegion on disk with the same cNBT as the game. */
#include "../../source/network/region_archive.c"
#define CHECK(x) do { if(!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while(0)
static void name(FILE *f, const char *s) {
    size_t n = strlen(s);
    fputc(n >> 8, f); fputc(n & 255, f); fwrite(s, n, 1, f);
}
static void tag(FILE *f, int type, const char *s) { fputc(type, f); name(f,s); }
static void array(FILE *f, const char *s, size_t bytes) {
    tag(f,TAG_BYTE_ARRAY,s); fwrite_u32(bytes,f);
    for(size_t i=0; i<bytes; ++i) fputc(0,f);
}
static void list(FILE *f, const char *s, const char *key) {
    tag(f,TAG_LIST,s); fputc(TAG_COMPOUND,f); fwrite_u32(1,f);
    tag(f,TAG_STRING,key); name(f,"keep this"); fputc(0,f);
}
static struct buffer seed(void) {
    FILE *f=tmpfile(); assert(f);
    tag(f,TAG_COMPOUND,""); tag(f,TAG_COMPOUND,"Level");
    tag(f,TAG_INT,"xPos"); fwrite_u32(0,f); tag(f,TAG_INT,"zPos"); fwrite_u32(0,f);
    array(f,"Blocks",32768); array(f,"Data",16384); array(f,"SkyLight",16384);
    array(f,"BlockLight",16384); array(f,"HeightMap",256);
    list(f,"Entities","EntitySentinel"); list(f,"TileEntities","TileSentinel");
    tag(f,TAG_STRING,"PluginSentinel"); name(f,"keep this");
    fputc(0,f); fputc(0,f);
    long len=ftell(f); rewind(f); unsigned char *raw=malloc((size_t)len);
    assert(raw && fread(raw,1,(size_t)len,f)==(size_t)len); fclose(f);
    nbt_node *root=nbt_parse(raw,(size_t)len); assert(root); free(raw);
    struct buffer deflated=nbt_dump_compressed(root,STRAT_INFLATE); nbt_free(root);
    return deflated;
}
static bool kept(nbt_node *root, const char *key) {
    nbt_node *node=nbt_find_by_name(root,key);
    return node && node->type==TAG_STRING && !strcmp(node->payload.tag_string,"keep this");
}
int main(void) {
    string_t world; string_init_set_str(world,"metadata");
    struct region_archive ra;
    struct server_chunk sc={0};
    CHECK(region_archive_create_new(&ra,world,0,0,WORLD_DIM_OVERWORLD));
    struct buffer data=seed(); CHECK(data.data && store_record(&ra,0,data)); buffer_free(&data);
    CHECK(region_archive_get_blocks(&ra,0,0,&sc)); sc.ids[0]=77;
    CHECK(region_archive_set_blocks(&ra,0,0,&sc));
    free(sc.ids); free(sc.metadata); free(sc.lighting_sky); free(sc.lighting_torch); free(sc.heightmap);
    region_archive_destroy(&ra);
    CHECK(region_archive_create(&ra,world,0,0,WORLD_DIM_OVERWORLD));
    FILE *f=fopen("metadata/region/r.0.0.mcr","rb"); CHECK(f);
    CHECK(fseek(f,(ra.offsets[0]>>8)*4096,SEEK_SET)==0);
    uint32_t size; CHECK(fread_u32(&size,f)==1 && size>1 && fgetc(f)==2);
    unsigned char *body=malloc(size-1); CHECK(body && fread(body,1,size-1,f)==size-1); fclose(f);
    nbt_node *root=nbt_parse_compressed(body,size-1); free(body); CHECK(root);
    nbt_node *blocks=nbt_find_by_path(root,".Level.Blocks");
    CHECK(blocks && blocks->payload.tag_byte_array.data[0]==77);
    CHECK(kept(root,"TileSentinel") && kept(root,"EntitySentinel") && kept(root,"PluginSentinel"));
    nbt_free(root); region_archive_destroy(&ra); string_clear(world);
    puts("PASS real-NBT edit preserves entity, tile entity and custom sentinel after cold region reopen.");
    return 0;
}
