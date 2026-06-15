#pragma once
#include <glm/glm.hpp>
#include <SDL2/SDL.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Weapons: a data-driven table. Each unlocks at a level; the player cycles
// through whatever they've unlocked. "pierce" shots pass through enemies
// (railgun); "pellets" > 1 makes a shotgun spread.
// ---------------------------------------------------------------------------
struct Weapon {
    const char* name;
    int   unlock_level;
    int   mag_size;
    float fire_delay;   // seconds between shots (lower = faster)
    float reload_time;
    int   damage;
    int   pellets;      // shots per trigger pull (shotgun)
    float spread;       // radians of cone half-angle
    float ads_fov;      // zoomed field of view in degrees
    bool  pierce;       // railgun passes through targets
};

inline const Weapon WEAPONS[] = {
    {"PISTOL",   1,  12, 0.28f, 0.9f, 1, 1, 0.006f, 55.0f, false},
    {"SMG",      5,  30, 0.075f,1.3f, 1, 1, 0.030f, 58.0f, false},
    {"SHOTGUN", 10,   6, 0.65f, 1.6f, 1, 8, 0.090f, 60.0f, false},
    {"RIFLE",   15,  24, 0.13f, 1.5f, 2, 1, 0.012f, 50.0f, false},
    {"DMR",     20,  10, 0.45f, 1.7f, 3, 1, 0.004f, 35.0f, false},
    {"RAILGUN", 30,   4, 1.10f, 2.0f, 5, 1, 0.0f,   30.0f, true },
    {"MINIGUN", 40, 120, 0.045f,3.0f, 1, 1, 0.045f, 62.0f, false},
};
inline constexpr int WEAPON_COUNT = sizeof(WEAPONS) / sizeof(WEAPONS[0]);

// ---------------------------------------------------------------------------
// Arenas: selectable layouts. The generator in level.h reads style to vary
// density, lighting tint, and floor/wall palette.
// ---------------------------------------------------------------------------
struct ArenaDef {
    const char* name;
    uint32_t seed;
    int unlock_level;
};
inline const ArenaDef ARENAS[] = {
    {"COURTYARD", 1337u,  1},
    {"FOUNDRY",   90210u, 10},
    {"CITADEL",   55555u, 25},
};
inline constexpr int ARENA_COUNT = sizeof(ARENAS) / sizeof(ARENAS[0]);

// ---------------------------------------------------------------------------
// Rebindable actions. Defaults below; the settings menu writes new scancodes.
// ---------------------------------------------------------------------------
enum Action {
    ACT_FORWARD, ACT_BACK, ACT_LEFT, ACT_RIGHT,
    ACT_JUMP, ACT_RELOAD, ACT_MELEE, ACT_NEXT_WEAPON, ACT_PREV_WEAPON,
    ACT_COUNT
};

inline const char* action_name(int a) {
    switch (a) {
        case ACT_FORWARD: return "MOVE FORWARD";
        case ACT_BACK:    return "MOVE BACK";
        case ACT_LEFT:    return "MOVE LEFT";
        case ACT_RIGHT:   return "MOVE RIGHT";
        case ACT_JUMP:    return "JUMP";
        case ACT_RELOAD:  return "RELOAD";
        case ACT_MELEE:   return "MELEE";
        case ACT_NEXT_WEAPON: return "NEXT WEAPON";
        case ACT_PREV_WEAPON: return "PREV WEAPON";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Settings + progression, persisted to a small text file next to the binary.
// Mouse buttons (shoot, ADS) are fixed to left/right click by design.
// ---------------------------------------------------------------------------
struct Profile {
    // settings
    float sensitivity = 0.0022f;
    float volume = 0.6f;
    bool  fullscreen = false;
    SDL_Scancode binds[ACT_COUNT] = {
        SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D,
        SDL_SCANCODE_SPACE, SDL_SCANCODE_R, SDL_SCANCODE_F,
        SDL_SCANCODE_E, SDL_SCANCODE_Q,
    };

    // progression
    int level = 1;
    int xp = 0;
    int prestige = 0;

    static constexpr int LEVEL_CAP = 50;
    static constexpr int PRESTIGE_CAP = 5;

    int xp_to_next() const { return 80 + level * 40; }

    // returns number of times leveled (for level-up SFX/banner)
    int add_xp(int amount) {
        int leveled = 0;
        xp += amount;
        while (level < LEVEL_CAP && xp >= xp_to_next()) {
            xp -= xp_to_next();
            level++;
            leveled++;
        }
        if (level >= LEVEL_CAP) xp = 0;
        return leveled;
    }

    bool can_prestige() const {
        return level >= LEVEL_CAP && prestige < PRESTIGE_CAP;
    }

    void do_prestige() {
        if (!can_prestige()) return;
        prestige++;
        level = 1;
        xp = 0;
    }

    // Highest weapon index unlocked at the current level
    int top_weapon() const {
        int top = 0;
        for (int i = 0; i < WEAPON_COUNT; ++i)
            if (level >= WEAPONS[i].unlock_level) top = i;
        return top;
    }

    void save(const std::string& path) const {
        std::ofstream o(path);
        if (!o) return;
        o << "sensitivity " << sensitivity << "\n";
        o << "volume " << volume << "\n";
        o << "fullscreen " << (fullscreen ? 1 : 0) << "\n";
        o << "level " << level << "\n";
        o << "xp " << xp << "\n";
        o << "prestige " << prestige << "\n";
        for (int i = 0; i < ACT_COUNT; ++i)
            o << "bind " << i << " " << int(binds[i]) << "\n";
    }

    void load(const std::string& path) {
        std::ifstream in(path);
        if (!in) return;
        std::string key;
        while (in >> key) {
            if (key == "sensitivity") in >> sensitivity;
            else if (key == "volume") in >> volume;
            else if (key == "fullscreen") { int v; in >> v; fullscreen = v; }
            else if (key == "level") in >> level;
            else if (key == "xp") in >> xp;
            else if (key == "prestige") in >> prestige;
            else if (key == "bind") {
                int idx, sc; in >> idx >> sc;
                if (idx >= 0 && idx < ACT_COUNT) binds[idx] = SDL_Scancode(sc);
            } else {
                std::string discard; std::getline(in, discard);
            }
        }
    }
};
