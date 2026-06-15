#pragma once
#include "level.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>

// ---------------------------------------------------------------------------
// Shared movement helper: move an AABB-shaped body one axis at a time,
// cancelling the axis that would push it into a wall. Moving per-axis is
// what makes you slide smoothly along walls instead of sticking to them.
// ---------------------------------------------------------------------------
inline glm::vec3 move_with_collision(const glm::vec3& pos, const glm::vec3& delta,
                                     const glm::vec3& half_extents, float height,
                                     const Level& level, bool* grounded = nullptr) {
    glm::vec3 p = pos;

    auto collides = [&](const glm::vec3& feet) {
        AABB body{{feet.x - half_extents.x, feet.y, feet.z - half_extents.z},
                  {feet.x + half_extents.x, feet.y + height, feet.z + half_extents.z}};
        for (const AABB& w : level.wall_boxes())
            if (body.overlaps(w)) return true;
        return false;
    };

    for (int axis = 0; axis < 3; ++axis) {
        glm::vec3 trial = p;
        trial[axis] += delta[axis];
        if (axis == 1 && trial.y < 0.0f) trial.y = 0.0f;  // floor plane
        if (!collides(trial)) p = trial;
    }

    if (grounded) *grounded = p.y <= 0.001f;
    return p;
}

// ---------------------------------------------------------------------------
// Player: feet position + look angles. Camera eye sits at feet + EYE_HEIGHT.
// ---------------------------------------------------------------------------
struct Player {
    static constexpr float EYE_HEIGHT = 1.6f;
    static constexpr float HEIGHT = 1.8f;
    static constexpr float SPEED = 6.0f;
    static constexpr float JUMP_VEL = 7.5f;
    static constexpr float GRAVITY = -20.0f;
    static constexpr float AIR_CONTROL = 6.0f;   // how fast you can steer mid-air

    glm::vec3 pos{0};
    glm::vec3 horiz_vel{0};   // horizontal momentum, for air control
    float vel_y = 0.0f;
    float yaw = glm::half_pi<float>();  // radians; facing +z initially
    float pitch = 0.0f;
    bool grounded = true;

    int hp = 100;
    int mag = 12;             // rounds in magazine
    float reload_timer = 0;   // > 0 while reloading
    float hurt_flash = 0;     // red vignette intensity
    float recoil = 0;         // viewmodel kick, decays after firing
    float fire_timer = 0;     // time until next shot allowed
    float melee_timer = 0;    // > 0 during a melee swing
    float ads = 0;            // 0 = hip, 1 = fully aimed
    int score = 0;

    glm::vec3 eye() const { return pos + glm::vec3(0, EYE_HEIGHT, 0); }

    glm::vec3 forward() const {
        return {std::cos(pitch) * std::cos(yaw), std::sin(pitch),
                std::cos(pitch) * std::sin(yaw)};
    }

    void update(float dt, const glm::vec3& wish_dir, bool jump, const Level& level) {
        glm::vec3 fwd{std::cos(yaw), 0, std::sin(yaw)};
        glm::vec3 right{-fwd.z, 0, fwd.x};
        glm::vec3 wish = fwd * wish_dir.z + right * wish_dir.x;
        if (glm::length(wish) > 0.001f) wish = glm::normalize(wish) * SPEED;

        if (grounded) {
            // On ground: instant, responsive control
            horiz_vel = wish;
            if (jump) { vel_y = JUMP_VEL; grounded = false; }
        } else {
            // In air: accelerate toward wish direction but keep momentum,
            // so a jump started while moving can still be steered (air-strafe)
            glm::vec3 diff = wish - horiz_vel;
            horiz_vel += diff * std::min(1.0f, AIR_CONTROL * dt);
        }

        vel_y += GRAVITY * dt;
        if (grounded && vel_y < 0) vel_y = 0;

        glm::vec3 delta = horiz_vel * dt + glm::vec3(0, vel_y * dt, 0);
        pos = move_with_collision(pos, delta, {0.3f, 0, 0.3f}, HEIGHT, level, &grounded);
        if (grounded && vel_y < 0) vel_y = 0;

        if (reload_timer > 0) {
            reload_timer -= dt;
            if (reload_timer <= 0) mag = mag_capacity;
        }
        fire_timer = std::max(0.0f, fire_timer - dt);
        melee_timer = std::max(0.0f, melee_timer - dt);
        hurt_flash = std::max(0.0f, hurt_flash - dt * 2.0f);
        recoil = std::max(0.0f, recoil - dt * 6.0f);
    }

    int mag_capacity = 12;    // set from current weapon

    AABB box() const {
        return {{pos.x - 0.3f, pos.y, pos.z - 0.3f},
                {pos.x + 0.3f, pos.y + HEIGHT, pos.z + 0.3f}};
    }
};

// ---------------------------------------------------------------------------
// Enemy projectile: a dodgeable tracer with travel time. Dies on contact
// with walls or the player (segment-vs-AABB test each step so fast shots
// can't tunnel through thin targets).
// ---------------------------------------------------------------------------
struct Projectile {
    glm::vec3 pos{0};
    glm::vec3 vel{0};
    float life = 4.0f;
    bool dead = false;

    void update(float dt, Player& player, const Level& level) {
        if (dead) return;
        life -= dt;
        if (life <= 0) { dead = true; return; }

        glm::vec3 step = vel * dt;
        float len = glm::length(step);
        if (len <= 0) return;
        glm::vec3 dir = step / len;

        float t;
        if (player.box().ray_hit(pos, dir, t) && t <= len) {
            player.hp -= 8;
            player.hurt_flash = 1.0f;
            dead = true;
            return;
        }
        for (const AABB& w : level.wall_boxes())
            if (w.ray_hit(pos, dir, t) && t <= len) { dead = true; return; }

        pos += step;
    }
};

// ---------------------------------------------------------------------------
// Health pickup: floats in place; touching it heals 25 (only if hurt),
// then it disappears and respawns after a delay.
// ---------------------------------------------------------------------------
struct Pickup {
    glm::vec3 pos{0};
    bool active = true;
    float respawn = 0;

    void update(float dt, Player& player) {
        if (!active) {
            respawn -= dt;
            if (respawn <= 0) active = true;
            return;
        }
        if (player.hp < 100 && glm::distance(player.pos, pos) < 1.1f) {
            player.hp = std::min(100, player.hp + 25);
            active = false;
            respawn = 14.0f;
        }
    }
};

// ---------------------------------------------------------------------------
// Enemy: WANDER until the player is near, then CHASE — closing distance,
// strafing at mid-range, firing dodgeable projectiles when it has clear
// line of sight, and melee-ing on contact. On death it crumples (DYING)
// before being removed; the wave system in main respawns the next group.
// ---------------------------------------------------------------------------
struct Enemy {
    enum class State { Wander, Chase, Dying };

    static constexpr float HEIGHT = 1.8f;
    static constexpr float WANDER_SPEED = 1.3f;
    static constexpr float CHASE_SPEED = 3.4f;
    static constexpr float STRAFE_SPEED = 2.2f;
    static constexpr float AGGRO_RANGE = 14.0f;
    static constexpr float ATTACK_RANGE = 1.6f;
    static constexpr float FIRE_RANGE = 13.0f;
    static constexpr float PROJ_SPEED = 10.0f;
    static constexpr float DEATH_TIME = 0.8f;
    static constexpr int   MAX_HP = 3;

    glm::vec3 pos{0};
    float yaw = 0;
    State state = State::Wander;
    bool active = true;
    int hp = MAX_HP;
    float wander_timer = 0;
    glm::vec3 wander_dir{1, 0, 0};
    float attack_cooldown = 0;
    float shoot_cooldown = 1.0f;   // grace period after spawning
    float strafe_timer = 0;
    float strafe_dir = 1.0f;
    float death_timer = 0;
    float flash = 0;               // white flash when shot

    void kill() {                  // call once when hp reaches zero
        state = State::Dying;
        death_timer = DEATH_TIME;
    }

    void update(float dt, Player& player, const Level& level, std::mt19937& rng,
                std::vector<Projectile>& projectiles) {
        flash = std::max(0.0f, flash - dt * 6.0f);

        if (state == State::Dying) {
            death_timer -= dt;
            if (death_timer <= 0) active = false;
            return;
        }

        attack_cooldown = std::max(0.0f, attack_cooldown - dt);
        shoot_cooldown = std::max(0.0f, shoot_cooldown - dt);

        glm::vec3 to_player = player.pos - pos;
        float dist = glm::length(to_player);
        state = (dist < AGGRO_RANGE) ? State::Chase : State::Wander;

        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        glm::vec3 move{0};

        if (state == State::Chase) {
            glm::vec3 dir = glm::normalize(glm::vec3{to_player.x, 0, to_player.z});
            yaw = std::atan2(dir.z, dir.x);

            if (dist > 6.0f) {
                move = dir * CHASE_SPEED;        // close the gap
            } else {
                strafe_timer -= dt;              // orbit at mid-range
                if (strafe_timer <= 0) {
                    strafe_dir = (u(rng) < 0.5f) ? -1.0f : 1.0f;
                    strafe_timer = 0.8f + u(rng);
                }
                move = glm::vec3{-dir.z, 0, dir.x} * strafe_dir * STRAFE_SPEED;
            }

            if (dist < ATTACK_RANGE && attack_cooldown <= 0) {
                player.hp -= 10;
                player.hurt_flash = 1.0f;
                attack_cooldown = 0.9f;
            }

            // Ranged attack — only with clear line of sight to the player
            if (dist > 3.0f && dist < FIRE_RANGE && shoot_cooldown <= 0) {
                glm::vec3 muzzle = pos + glm::vec3(0, 1.25f, 0) + dir * 0.55f;
                glm::vec3 target = player.pos + glm::vec3(0, 1.2f, 0);
                glm::vec3 aim = target - muzzle;
                float aim_dist = glm::length(aim);
                aim /= aim_dist;

                bool blocked = false;
                float t;
                for (const AABB& w : level.wall_boxes())
                    if (w.ray_hit(muzzle, aim, t) && t < aim_dist) {
                        blocked = true;
                        break;
                    }

                if (!blocked) {
                    glm::vec3 spread{u(rng) - 0.5f, u(rng) - 0.5f, u(rng) - 0.5f};
                    aim = glm::normalize(aim + spread * 0.07f);
                    projectiles.push_back({muzzle, aim * PROJ_SPEED});
                    shoot_cooldown = 1.5f + u(rng) * 1.2f;
                }
            }
        } else {
            wander_timer -= dt;
            if (wander_timer <= 0) {
                std::uniform_real_distribution<float> a(0, glm::two_pi<float>());
                float ang = a(rng);
                wander_dir = {std::cos(ang), 0, std::sin(ang)};
                wander_timer = 1.5f + a(rng) * 0.4f;
            }
            move = wander_dir * WANDER_SPEED;
            yaw = std::atan2(wander_dir.z, wander_dir.x);
        }

        pos = move_with_collision(pos, move * dt, {0.35f, 0, 0.35f}, HEIGHT, level);
    }

    AABB box() const {
        return {{pos.x - 0.4f, pos.y, pos.z - 0.4f},
                {pos.x + 0.4f, pos.y + HEIGHT, pos.z + 0.4f}};
    }

    // Push overlapping enemies apart so a group spreads out around the
    // player instead of collapsing into one stack. Run once per frame
    // after all enemies have moved.
    static void separate(std::vector<Enemy>& enemies, const Level& level,
                         float dt) {
        constexpr float MIN_DIST = 1.5f;
        for (size_t i = 0; i < enemies.size(); ++i) {
            if (!enemies[i].active || enemies[i].state == State::Dying) continue;
            glm::vec3 push{0};
            for (size_t j = 0; j < enemies.size(); ++j) {
                if (i == j || !enemies[j].active ||
                    enemies[j].state == State::Dying)
                    continue;
                glm::vec3 d = enemies[i].pos - enemies[j].pos;
                d.y = 0;
                float dist = glm::length(d);
                if (dist > 0.001f && dist < MIN_DIST)
                    push += (d / dist) * (MIN_DIST - dist);
            }
            if (glm::length(push) > 0.001f)
                enemies[i].pos = move_with_collision(
                    enemies[i].pos, push * 6.0f * dt, {0.35f, 0, 0.35f},
                    HEIGHT, level);
        }
    }
};
