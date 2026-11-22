/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "server_lan.h"
#include "client_interface.h"
#include "inventory_logic.h"
#include "lan_transport.h"
#include "peer_record.h"
#include "../game/session_trace.h"
#include "../item/window_container.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define REMOTE_ENTITY UINT32_C(0xfffffffe)

static struct server_player *actor(struct server_local *s, unsigned slot) {
    return slot ? &s->peer : &s->player;
}

static bool send_to(unsigned slot, struct client_rpc *msg) {
    if(slot) return lan_host_send(msg);
    clin_rpc_send(msg);
    return true;
}

static bool subscribed(const struct lan_view *v, w_coord_t x, w_coord_t z) {
    for(size_t i = 0; i < v->count; ++i)
        if(v->columns[i].x == x && v->columns[i].z == z) return true;
    return false;
}

void server_local_publish_block(struct server_local *s, w_coord_t x, w_coord_t y,
                                w_coord_t z, struct block_data block) {
    struct client_rpc msg = {.type = CRPC_SET_BLOCK, .payload.set_block = {x,y,z,block}};
    if(!lan_host_enabled()) { clin_rpc_send(&msg); return; }
    int cx = WCOORD_CHUNK_OFFSET(x), cz = WCOORD_CHUNK_OFFSET(z);
    for(unsigned slot = 0; slot < 2; ++slot)
        if(subscribed(&s->views[slot],cx,cz)) send_to(slot,&msg);
}

static void pose(unsigned slot, const struct server_player *p) {
    send_to(slot, &(struct client_rpc){.type=CRPC_PLAYER_POS,
        .payload.player_pos={{p->x,p->y,p->z},{p->rx,p->ry}}});
}

static void avatar(unsigned recipient, const struct server_player *other, bool present) {
    send_to(recipient, &(struct client_rpc){.type=CRPC_REMOTE_PLAYER,
        .payload.remote_player={.entity_id=REMOTE_ENTITY,.present=present,
        .position={other->x,other->y,other->z},.rotation={other->rx,other->ry}}});
}

static void inv_slot(unsigned slot, struct inventory *inv, size_t index) {
    send_to(slot,&(struct client_rpc){.type=CRPC_INVENTORY_SLOT,
        .payload.inventory_slot={WINDOWC_INVENTORY,index,inv->items[index]}});
}

static bool simple_block(unsigned id) {
    return id == BLOCK_STONE || id == BLOCK_COBBLESTONE || id == BLOCK_DIRT ||
           id == BLOCK_GRASS || id == BLOCK_PLANKS || id == BLOCK_SAND || id == BLOCK_GRAVEL;
}

static bool overlaps_actor(const struct server_player *p, int x, int y, int z) {
    return p->finished_loading && p->x + 0.3 > x && p->x - 0.3 < x + 1 &&
        p->z + 0.3 > z && p->z - 0.3 < z + 1 && p->y + 0.18 > y && p->y - 1.62 < y + 1;
}

static bool valid_reach(const struct server_player *p, int x, int y, int z) {
    double dx = x + 0.5 - p->x, dy = y + 0.5 - p->y, dz = z + 0.5 - p->z;
    return y >= 0 && y < WORLD_HEIGHT && dx*dx + dy*dy + dz*dz <= 36;
}

static bool change_block(struct server_local *s, unsigned slot, struct server_rpc *c,
                         int *x, int *y, int *z) {
    bool place = c->type == SRPC_BLOCK_PLACE;
    struct server_player *p = actor(s,slot);
    if(!p->finished_loading || !c->action_seq || c->action_seq <= s->last_action[slot])
        return false;
    s->last_action[slot] = c->action_seq;
    if(!c->has_expected_block || !valid_reach(p,*x,*y,*z)) return false;
    struct block_data old;
    if(place) {
        if((unsigned)c->payload.block_place.side >= SIDE_MAX ||
           !server_world_get_block(&s->world,*x,*y,*z,&old) || !old.type) return false;
        int dx,dy,dz;
        blocks_side_offset(c->payload.block_place.side,&dx,&dy,&dz);
        *x += dx; *y += dy; *z += dz;
    }
    if(!valid_reach(p,*x,*y,*z) ||
       !subscribed(&s->views[slot],WCOORD_CHUNK_OFFSET(*x),WCOORD_CHUNK_OFFSET(*z)) ||
       !server_world_get_block(&s->world,*x,*y,*z,&old) ||
       old.type != c->expected_block) return false;
    struct inventory *inv = &p->inventory;
    size_t index = INVENTORY_SLOT_HOTBAR + inventory_get_hotbar(inv);
    struct block_data next = {.type=BLOCK_AIR};
    struct item_data award = {0};
    if(place) {
        if(old.type != BLOCK_AIR || !inv->items[index].count || !simple_block(inv->items[index].id) ||
           overlaps_actor(&s->player,*x,*y,*z) || (s->peer_joined && overlaps_actor(&s->peer,*x,*y,*z)))
            return false;
        next.type = inv->items[index].id;
    } else {
        if(!simple_block(old.type)) return false;
        award.id = old.type == BLOCK_STONE ? BLOCK_COBBLESTONE :
                   old.type == BLOCK_GRASS ? BLOCK_DIRT : old.type;
        award.count = 1;
        /* The LAN slice puts the drop directly in its owner's inventory. */
        bool room = false;
        for(size_t k=INVENTORY_SLOT_MAIN; k<INVENTORY_SIZE; ++k) {
            if(!inv->items[k].id || (inv->items[k].id == award.id && inv->items[k].count < 64)) {
                index=k; room=true; break;
            }
        }
        if(!room) return false;
    }
    if(!server_world_set_block(s,*x,*y,*z,next)) return false;
    if(place) inventory_consume(inv,index);
    else if(inv->items[index].id) ++inv->items[index].count;
    else inventory_set_slot(inv,index,award);
    inv_slot(slot,inv,index);
    return true;
}

static bool motion_blocked(struct server_local *s, struct server_player *p,
                           double x, double y, double z) {
    double dx=x-p->x, dy=y-p->y, dz=z-p->z;
    unsigned steps=(unsigned)ceil(fmax(fabs(dx),fmax(fabs(dy),fabs(dz)))*4);
    struct entity probe={.on_server=true,.world=&s->world};
    for(unsigned i=1;i<=steps;++i) {
        float t=(float)i/steps;
        struct AABB box;
        /* The local solver rests at block faces; tolerate its sub-centimetre rounding. */
        aabb_setsize_centered(&box,0.58f,1.78f,0.58f);
        aabb_translate(&box,p->x+t*dx,p->y+t*dy+0.9f-1.62f,p->z+t*dz);
        if(entity_aabb_intersection(&probe,&box)) return true;
    }
    return false;
}

static void command(struct server_local *s,unsigned slot,struct server_rpc *c) {
    struct server_player *p=actor(s,slot);
    if(!p->finished_loading || (slot && !s->peer_joined)) return;
    switch(c->type) {
    case SRPC_PLAYER_POS: {
        double x=c->payload.player_pos.x,y=c->payload.player_pos.y,z=c->payload.player_pos.z;
        double dx=x-p->x,dy=y-p->y,dz=z-p->z;
        if(!isfinite(x)||!isfinite(y)||!isfinite(z)||!isfinite(c->payload.player_pos.rx)||
           !isfinite(c->payload.player_pos.ry)||!isfinite(c->payload.player_pos.vel_y)||
           fabs(c->payload.player_pos.rx)>36000 || fabs(c->payload.player_pos.ry)>180 ||
           fabs(x)>1000000||fabs(z)>1000000||y < -2||y>WORLD_HEIGHT+4||
           dx*dx+dy*dy+dz*dz>9.0 || !subscribed(&s->views[slot],
           WCOORD_CHUNK_OFFSET((int)floor(x)),WCOORD_CHUNK_OFFSET((int)floor(z))) ||
           motion_blocked(s,p,x,y,z)) {
            pose(slot,p);
            return;
        }
        p->x=x; p->y=y; p->z=z;
        p->rx=c->payload.player_pos.rx; p->ry=c->payload.player_pos.ry;
        p->old_vel_y=p->vel_y; p->vel_y=c->payload.player_pos.vel_y;
        if(s->peer_joined && actor(s,!slot)->finished_loading) avatar(!slot,p,true);
        return;
    }
    case SRPC_HOTBAR_SLOT:
        if(c->payload.hotbar_slot.slot < INVENTORY_SIZE_HOTBAR)
            inventory_set_hotbar(&p->inventory,c->payload.hotbar_slot.slot);
        return;
    case SRPC_BLOCK_PLACE:
    case SRPC_BLOCK_DIG: {
        bool place=c->type==SRPC_BLOCK_PLACE;
        if(!place && !c->payload.block_dig.finished) return;
        int x=place ? c->payload.block_place.x : c->payload.block_dig.x;
        int y=place ? c->payload.block_place.y : c->payload.block_dig.y;
        int z=place ? c->payload.block_place.z : c->payload.block_dig.z;
        bool ok=change_block(s,slot,c,&x,&y,&z);
        session_trace("EDIT actor=%c seq=%lu op=%s x=%d y=%d z=%d accepted=%d",
            slot?'B':'A',(unsigned long)c->action_seq,place?"place":"dig",x,y,z,ok);
        if(!ok && valid_reach(p,x,y,z)) {
            struct block_data truth;
            if(server_world_get_block(&s->world,x,y,z,&truth))
                send_to(slot,&(struct client_rpc){.type=CRPC_SET_BLOCK,
                    .payload.set_block={x,y,z,truth}});
        }
        return;
    }
    case SRPC_WINDOW_CLICK:
        send_to(slot,&(struct client_rpc){.type=CRPC_WINDOW_TRANSACTION,
            .payload.window_transaction={c->payload.window_click.window,
            c->payload.window_click.action_id,false}});
        return;
    default: return;
    }
}

static void peer_command(struct server_rpc *c, void *user) {
    command(user,1,c);
}

bool server_lan_save(struct server_local *s) {
    if(!s->player.has_pos) return true;
    dict_server_chunks_it_t it;
    dict_server_chunks_it(it,s->world.chunks);
    while(!dict_server_chunks_end_p(it)) {
        int64_t key=dict_server_chunks_ref(it)->key;
        if(!server_world_save_chunk_obj(&s->world,false,S_CHUNK_X(key),S_CHUNK_Z(key),
                                       &dict_server_chunks_ref(it)->value)) return false;
        dict_server_chunks_next(it);
    }
    if(s->peer_record && !peer_record_save(s)) return false;
    struct server_player *p=&s->player;
    return level_archive_write_player(&s->level,(vec3){p->x,p->y,p->z},
               (vec2){p->rx,p->ry},NULL,p->dimension) &&
        level_archive_write_inventory(&s->level,&p->inventory) &&
        level_archive_write(&s->level,LEVEL_PLAYER_HEALTH,&p->health) &&
        level_archive_write(&s->level,LEVEL_TIME,&s->world_time) &&
        level_archive_checkpoint(&s->level);
}

static void peer_closed(struct server_local *s) {
    if(!s->peer_joined) return;
    s->peer_joined=false;
    s->world.peer_interest=false;
    s->views[1].count=0;
    s->peer.finished_loading=false;
    avatar(0,&s->peer,false);
    bool ok=server_lan_save(s);
    session_trace("PEER_SAVE ok=%d",ok);
}

static void link_changed(bool up, void *user) {
    struct server_local *s=user;
    if(!up) peer_closed(s);
    session_trace("LINK role=host connected=%d",up);
}

void server_lan_pump(struct server_local *s) {
    lan_host_pump(link_changed,peer_command,s);
    if(!s->player.has_pos || !lan_host_connected() || s->peer_joined) return;
    if(!s->peer_record) {
        if(!inventory_create(&s->peer.inventory,NULL,s,INVENTORY_SIZE,0,0,0)) {
            lan_host_disconnect(); return;
        }
        s->peer.x=s->player.x+2; s->peer.y=s->player.y; s->peer.z=s->player.z;
        s->peer.rx=s->player.rx; s->peer.ry=s->player.ry;
        s->peer.dimension=s->player.dimension; s->peer.health=MAX_PLAYER_HEALTH;
        inventory_set_slot(&s->peer.inventory,INVENTORY_SLOT_HOTBAR,
                           (struct item_data){.id=BLOCK_DIRT,.count=16});
        if(!peer_record_load(s)) {
            inventory_destroy(&s->peer.inventory); lan_host_disconnect(); return;
        }
        s->peer_record=true;
    }
    s->peer_joined=true; s->peer.has_pos=true; s->peer.finished_loading=false;
    s->views[1].count=0; s->last_action[1]=0;
    send_to(1,&(struct client_rpc){.type=CRPC_WORLD_RESET,
        .payload.world_reset={s->world.dimension,0}});
}

bool server_lan_local_command(struct server_local *s,struct server_rpc *c) {
    if(c->type == SRPC_LOAD_WORLD) return false;
    if(c->type == SRPC_UNLOAD_WORLD) {
        if(!s->player.has_pos) return true;
        if(!server_lan_save(s)) {
            session_trace("HOST_SAVE ok=0");
            clin_rpc_send(&(struct client_rpc){.type=CRPC_HOST_SAVE_RESULT,
                                               .payload.host_save_result=false});
            return true;
        }
        if(lan_host_connected()) lan_host_disconnect();
        if(s->peer_record) inventory_destroy(&s->peer.inventory);
        s->peer_record=false; s->peer_joined=false; s->peer.has_pos=false;
        s->views[0].count=s->views[1].count=0;
        s->last_action[0]=s->last_action[1]=0;
        return false;
    }
    command(s,0,c);
    return true;
}

// One complete column per turn; a stalled peer cannot monopolize the host's stream.
static void trim_view(struct server_local *s,unsigned slot,int cx,int cz) {
    struct lan_view *v=&s->views[slot];
    for(size_t i=0;i<v->count;) {
        int x=v->columns[i].x,z=v->columns[i].z;
        if(abs(x-cx)<=LAN_RADIUS && abs(z-cz)<=LAN_RADIUS) { ++i; continue; }
        send_to(slot,&(struct client_rpc){.type=CRPC_UNLOAD_CHUNK,.payload.unload_chunk={x,z}});
        v->columns[i]=v->columns[--v->count];
    }
}

static bool deliver_column(unsigned slot,int x,int z,struct server_chunk *sc) {
    const size_t n=CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT;
    struct client_rpc msg={.type=CRPC_CHUNK,.payload.chunk={
        x*CHUNK_SIZE,0,z*CHUNK_SIZE,CHUNK_SIZE,WORLD_HEIGHT,CHUNK_SIZE,
        sc->ids,sc->metadata,sc->lighting_sky,sc->lighting_torch}};
    if(slot) return lan_host_send(&msg);
    /* The original client consumes and frees all four planes. */
    uint8_t **out[]={&msg.payload.chunk.ids,&msg.payload.chunk.metadata,
                     &msg.payload.chunk.lighting_sky,&msg.payload.chunk.lighting_torch};
    for(unsigned k=0;k<4;++k) {
        size_t bytes = k ? n/2 : n;
        uint8_t *copy=malloc(bytes);
        if(!copy) {
            while(k>0) free(*out[--k]);
            return false;
        }
        memcpy(copy,*out[k],bytes);
        *out[k]=copy;
    }
    return send_to(0,&msg);
}

static void ready(struct server_local *s,unsigned slot) {
    struct server_player *p=actor(s,slot);
    pose(slot,p);
    send_to(slot,&(struct client_rpc){.type=CRPC_TIME_SET,.payload.time_set=s->world_time});
    send_to(slot,&(struct client_rpc){.type=CRPC_PLAYER_SET_HEALTH,
            .payload.player_set_health={p->health}});
    for(size_t k=0;k<INVENTORY_SIZE;++k)
        if(p->inventory.items[k].id) inv_slot(slot,&p->inventory,k);
    p->finished_loading=true;
    if(s->peer_joined && actor(s,!slot)->finished_loading) {
        avatar(slot,actor(s,!slot),true);
        avatar(!slot,p,true);
    }
}

static bool stream_one(struct server_local *s,unsigned slot,int cx,int cz) {
    struct lan_view *v=&s->views[slot];
    if(slot && !lan_host_can_send_chunk()) return false;
    if(v->count >= LAN_COLUMNS) return false;
    /* Near-to-far order also prevents early spawn over a not-yet-present floor. */
    for(int d=0;d<=2*LAN_RADIUS*LAN_RADIUS;++d) {
        for(int dz=-LAN_RADIUS;dz<=LAN_RADIUS;++dz) {
            for(int dx=-LAN_RADIUS;dx<=LAN_RADIUS;++dx) {
                if(dx*dx+dz*dz!=d) continue;
                int x=cx+dx,z=cz+dz;
                if(subscribed(v,x,z)) continue;
                struct server_chunk *sc=dict_server_chunks_get(s->world.chunks,S_CHUNK_ID(x,z));
                if(!sc && (!server_world_disk_has_chunk(&s->world,x,z) ||
                           !server_world_load_chunk(&s->world,x,z,&sc))) continue;
                if(!deliver_column(slot,x,z,sc)) return false;
                v->columns[v->count].x=x;
                v->columns[v->count++].z=z;
                if(!actor(s,slot)->finished_loading && x==cx && z==cz) ready(s,slot);
                return true;
            }
        }
    }
    return false;
}

void server_lan_stream(struct server_local *s) {
    int cx[2]={0},cz[2]={0};
    unsigned count=s->peer_joined ? 2 : 1;
    for(unsigned slot=0;slot<count;++slot) {
        struct server_player *p=actor(s,slot);
        cx[slot]=WCOORD_CHUNK_OFFSET((int)floor(p->x));
        cz[slot]=WCOORD_CHUNK_OFFSET((int)floor(p->z));
        trim_view(s,slot,cx[slot],cz[slot]);
    }
    s->world.peer_interest=s->peer_joined;
    s->world.peer_cx=cx[1]; s->world.peer_cz=cz[1];
    int x,z;
    if(server_world_furthest_chunk(&s->world,LAN_RADIUS,cx[0],cz[0],&x,&z) &&
       !server_world_save_chunk(&s->world,true,x,z)) {
        session_trace("CHUNK_SAVE ok=0 x=%d z=%d",x,z);
        return;
    }
    unsigned first=s->peer_joined && s->next_peer;
    if(!stream_one(s,first,cx[first],cz[first]) && s->peer_joined)
        stream_one(s,!first,cx[!first],cz[!first]);
    s->next_peer=!s->next_peer;
    server_world_random_tick(&s->world,&s->rand_src,s,cx[0],cz[0],LAN_RADIUS-1);
    server_world_tick(&s->world,s);
}
