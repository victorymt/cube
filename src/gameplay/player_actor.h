#ifndef VOXELCRAFT_PLAYER_ACTOR_H
#define VOXELCRAFT_PLAYER_ACTOR_H

#include "gameplay/player_types.h"
#include "world/actor_body.h"

static inline ActorBody PlayerActorBody(const Player *player)
{
    if (!player) return (ActorBody){ 0 };
    return (ActorBody){
        .position = player->position,
        .velocity = player->velocity,
        .yaw = player->yaw,
        .pitch = player->pitch,
        .onGround = player->onGround,
        .floating = player->floating
    };
}

static inline Player PlayerFromActorBody(const ActorBody *body)
{
    if (!body) return (Player){ 0 };
    return (Player){
        .position = body->position,
        .velocity = body->velocity,
        .yaw = body->yaw,
        .pitch = body->pitch,
        .onGround = body->onGround,
        .floating = body->floating
    };
}

static inline void PlayerApplyActorBody(Player *player, const ActorBody *body)
{
    if (!player || !body) return;
    player->position = body->position;
    player->velocity = body->velocity;
    player->yaw = body->yaw;
    player->pitch = body->pitch;
    player->onGround = body->onGround;
    player->floating = body->floating;
}

#endif
