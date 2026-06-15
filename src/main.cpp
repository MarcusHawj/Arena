// arena — a first-person arena shooter built on SDL2 + OpenGL 3.3 core.
//
// Systems: procedural arenas, wave-based combat, 7 unlockable weapons,
// ADS, melee, ranged enemy AI with separation, XP/level/prestige
// progression (saved to disk), rebindable controls, procedural audio,
// pixel-font menus, and prestige particle auras.
//
// Usage: ./arena [--screenshot out.ppm]   (renders one frame headless)

#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "shaders.h"
#include "font.h"
#include "audio.h"
#include "level.h"
#include "entities.h"
#include "progression.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Palette — deep navy world, warm orange accents, clean white UI.
// ---------------------------------------------------------------------------
namespace palette {
const glm::vec3 sky        {0.082f, 0.118f, 0.204f};
const glm::vec3 floor      {0.145f, 0.192f, 0.290f};
const glm::vec3 wall       {0.220f, 0.290f, 0.435f};
const glm::vec3 wall_trim  {0.318f, 0.404f, 0.573f};
const glm::vec3 enemy_body {0.910f, 0.463f, 0.173f};
const glm::vec3 enemy_dark {0.710f, 0.329f, 0.110f};
const glm::vec3 enemy_head {0.941f, 0.541f, 0.235f};
const glm::vec4 hud_white  {0.95f, 0.96f, 0.98f, 0.9f};
const glm::vec4 hud_orange {0.91f, 0.46f, 0.17f, 0.95f};
const glm::vec4 hud_dim    {1.0f, 1.0f, 1.0f, 0.18f};
const glm::vec4 hurt_red   {0.85f, 0.15f, 0.10f, 1.0f};

// Per-arena tint applied to floor/walls so each level reads differently
inline glm::vec3 arena_tint(int style) {
    if (style == 1) return {1.05f, 0.92f, 0.80f};  // FOUNDRY: warm
    if (style == 2) return {0.86f, 0.92f, 1.10f};  // CITADEL: cold
    return {1, 1, 1};
}
// Prestige aura colors
inline glm::vec3 prestige_color(int p) {
    switch (p) {
        case 1: return {0.95f, 0.45f, 0.15f};  // ember
        case 2: return {0.30f, 0.55f, 1.00f};  // blue flame
        case 3: return {0.60f, 0.85f, 1.00f};  // electric
        case 4: return {1.00f, 0.84f, 0.30f};  // gold
        case 5: return {0.80f, 0.50f, 1.00f};  // prismatic
    }
    return {1, 1, 1};
}
}

static int WIN_W = 1280, WIN_H = 720;

// Cube mesh — correctly wound so backface culling keeps walls solid.
static const float CUBE_VERTS[] = {
    -.5f,-.5f,-.5f,  0,0,-1,   .5f, .5f,-.5f,  0,0,-1,   .5f,-.5f,-.5f,  0,0,-1,
    -.5f,-.5f,-.5f,  0,0,-1,  -.5f, .5f,-.5f,  0,0,-1,   .5f, .5f,-.5f,  0,0,-1,
    -.5f,-.5f, .5f,  0,0, 1,   .5f,-.5f, .5f,  0,0, 1,   .5f, .5f, .5f,  0,0, 1,
    -.5f,-.5f, .5f,  0,0, 1,   .5f, .5f, .5f,  0,0, 1,  -.5f, .5f, .5f,  0,0, 1,
    -.5f,-.5f,-.5f, -1,0, 0,  -.5f, .5f, .5f, -1,0, 0,  -.5f, .5f,-.5f, -1,0, 0,
    -.5f,-.5f,-.5f, -1,0, 0,  -.5f,-.5f, .5f, -1,0, 0,  -.5f, .5f, .5f, -1,0, 0,
     .5f,-.5f,-.5f,  1,0, 0,   .5f, .5f,-.5f,  1,0, 0,   .5f, .5f, .5f,  1,0, 0,
     .5f,-.5f,-.5f,  1,0, 0,   .5f, .5f, .5f,  1,0, 0,   .5f,-.5f, .5f,  1,0, 0,
    -.5f,-.5f,-.5f,  0,-1,0,   .5f,-.5f,-.5f,  0,-1,0,   .5f,-.5f, .5f,  0,-1,0,
    -.5f,-.5f,-.5f,  0,-1,0,   .5f,-.5f, .5f,  0,-1,0,  -.5f,-.5f, .5f,  0,-1,0,
    -.5f, .5f,-.5f,  0, 1,0,   .5f, .5f, .5f,  0, 1,0,   .5f, .5f,-.5f,  0, 1,0,
    -.5f, .5f,-.5f,  0, 1,0,  -.5f, .5f, .5f,  0, 1,0,   .5f, .5f, .5f,  0, 1,0,
};

struct Renderer {
    GLuint world_prog = 0, hud_prog = 0;
    GLuint cube_vao = 0, cube_vbo = 0;
    GLuint hud_vao = 0, hud_vbo = 0;
    GLint u_proj, u_view, u_model, u_color, u_light, u_campos, u_fog, u_flash;
    GLint u_screen, u_hudcolor;
    glm::mat4 proj, view;
    glm::vec3 cam_pos;

    void init() {
        world_prog = link_program(WORLD_VS, WORLD_FS);
        hud_prog = link_program(HUD_VS, HUD_FS);
        u_proj   = glGetUniformLocation(world_prog, "uProj");
        u_view   = glGetUniformLocation(world_prog, "uView");
        u_model  = glGetUniformLocation(world_prog, "uModel");
        u_color  = glGetUniformLocation(world_prog, "uColor");
        u_light  = glGetUniformLocation(world_prog, "uLightDir");
        u_campos = glGetUniformLocation(world_prog, "uCamPos");
        u_fog    = glGetUniformLocation(world_prog, "uFogColor");
        u_flash  = glGetUniformLocation(world_prog, "uFlash");
        u_screen   = glGetUniformLocation(hud_prog, "uScreen");
        u_hudcolor = glGetUniformLocation(hud_prog, "uColor");

        glGenVertexArrays(1, &cube_vao);
        glGenBuffers(1, &cube_vbo);
        glBindVertexArray(cube_vao);
        glBindBuffer(GL_ARRAY_BUFFER, cube_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE_VERTS), CUBE_VERTS, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float),
                              (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(1);

        glGenVertexArrays(1, &hud_vao);
        glGenBuffers(1, &hud_vbo);
        glBindVertexArray(hud_vao);
        glBindBuffer(GL_ARRAY_BUFFER, hud_vbo);
        glBufferData(GL_ARRAY_BUFFER, 6*2*sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
    }

    void begin_frame(const glm::vec3& eye, const glm::vec3& fwd, float fov) {
        glClearColor(palette::sky.r, palette::sky.g, palette::sky.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        cam_pos = eye;
        proj = glm::perspective(glm::radians(fov),
                                float(WIN_W)/float(WIN_H), 0.05f, 120.0f);
        view = glm::lookAt(eye, eye + fwd, {0, 1, 0});
        glUseProgram(world_prog);
        glUniformMatrix4fv(u_proj, 1, GL_FALSE, glm::value_ptr(proj));
        glUniformMatrix4fv(u_view, 1, GL_FALSE, glm::value_ptr(view));
        glm::vec3 light = glm::normalize(glm::vec3{0.35f, -1.0f, 0.25f});
        glUniform3fv(u_light, 1, glm::value_ptr(light));
        glUniform3fv(u_campos, 1, glm::value_ptr(cam_pos));
        glUniform3fv(u_fog, 1, glm::value_ptr(palette::sky));
        glBindVertexArray(cube_vao);
    }

    void draw_box(const glm::vec3& center, const glm::vec3& size,
                  const glm::vec3& color, float yaw = 0, float flash = 0) {
        glm::mat4 m(1.0f);
        m = glm::translate(m, center);
        if (yaw != 0) m = glm::rotate(m, yaw, {0, 1, 0});
        m = glm::scale(m, size);
        glUniformMatrix4fv(u_model, 1, GL_FALSE, glm::value_ptr(m));
        glUniform3fv(u_color, 1, glm::value_ptr(color));
        glUniform1f(u_flash, flash);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    void draw_rect(float x, float y, float w, float h, const glm::vec4& color) {
        float v[12] = {x,y, x+w,y, x+w,y+h, x+w,y+h, x,y+h, x,y};
        glUseProgram(hud_prog);
        glBindVertexArray(hud_vao);
        glBindBuffer(GL_ARRAY_BUFFER, hud_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
        glUniform2f(u_screen, float(WIN_W), float(WIN_H));
        glUniform4fv(u_hudcolor, 1, glm::value_ptr(color));
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // Pixel-font text. Origin is bottom-left of the string, y up.
    void draw_text(const std::string& s, float x, float y, float scale,
                   const glm::vec4& color) {
        float cx = x;
        for (char ch : s) {
            const font::Glyph* g = font::find(ch);
            if (g) {
                for (int r = 0; r < font::GLYPH_H; ++r)
                    for (int c = 0; c < font::GLYPH_W; ++c)
                        if (g->rows[r][c] == '#')
                            draw_rect(cx + c*scale,
                                      y + (font::GLYPH_H-1-r)*scale,
                                      scale, scale, color);
            }
            cx += 6.0f * scale;
        }
    }

    void text_centered(const std::string& s, float cx, float y, float scale,
                       const glm::vec4& color) {
        draw_text(s, cx - font::text_width(s, scale)/2.0f, y, scale, color);
    }
};

// HUD-space helpers begin/end (2D, blended, no depth)
static void hud_begin() {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
static void hud_end() {
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

// ---------------------------------------------------------------------------
// Particle: tiny cube that rises and fades. Used for prestige auras and
// weapon impact sparks.
// ---------------------------------------------------------------------------
struct Particle {
    glm::vec3 pos, vel, color;
    float life, max_life, size;
};

struct ParticleSystem {
    std::vector<Particle> parts;

    void spawn(glm::vec3 p, glm::vec3 v, glm::vec3 c, float life, float size) {
        parts.push_back({p, v, c, life, life, size});
    }
    void update(float dt) {
        for (Particle& pt : parts) {
            pt.pos += pt.vel * dt;
            pt.vel.y += 2.0f * dt;        // gentle upward drift for auras
            pt.life -= dt;
        }
        parts.erase(std::remove_if(parts.begin(), parts.end(),
            [](const Particle& p){ return p.life <= 0; }), parts.end());
    }
    void draw(Renderer& r) const {
        for (const Particle& pt : parts) {
            float f = pt.life / pt.max_life;
            r.draw_box(pt.pos, glm::vec3(pt.size * f), pt.color, 0, 0.6f);
        }
    }
};

// ---------------------------------------------------------------------------
// Enemy figure (with crumple-on-death squash) and held weapon.
// ---------------------------------------------------------------------------
static void draw_enemy(Renderer& r, const Enemy& e) {
    if (!e.active) return;
    const glm::vec3 p = e.pos;
    const float yaw = -e.yaw;
    const float fl = e.flash;
    using namespace palette;

    float squash = 1.0f;
    if (e.state == Enemy::State::Dying)
        squash = std::max(0.12f, e.death_timer / Enemy::DEATH_TIME);

    auto part = [&](glm::vec3 off, glm::vec3 size, glm::vec3 col) {
        off.y *= squash; size.y *= squash;
        float c = std::cos(yaw), s = std::sin(yaw);
        glm::vec3 ro{off.x*c + off.z*s, off.y, -off.x*s + off.z*c};
        r.draw_box(p + ro, size, col, yaw, fl);
    };
    part({0, 1.05f, 0},        {0.55f, 0.65f, 0.35f}, enemy_body);
    part({0, 1.62f, 0},        {0.38f, 0.38f, 0.38f}, enemy_head);
    part({0, 1.66f, 0.165f},   {0.30f, 0.10f, 0.06f}, glm::vec3(0.1f,0.12f,0.18f));
    part({ 0.36f, 1.10f, 0},   {0.16f, 0.55f, 0.16f}, enemy_dark);
    part({-0.36f, 1.10f, 0},   {0.16f, 0.55f, 0.16f}, enemy_dark);
    part({ 0.15f, 0.36f, 0},   {0.18f, 0.72f, 0.18f}, enemy_dark);
    part({-0.15f, 0.36f, 0},   {0.18f, 0.72f, 0.18f}, enemy_dark);
    part({ 0.36f, 0.95f, 0.28f}, {0.09f, 0.10f, 0.50f}, glm::vec3(0.10f,0.13f,0.19f));
    part({ 0.36f, 1.00f, 0.42f}, {0.05f, 0.05f, 0.20f}, glm::vec3(0.07f,0.09f,0.14f));
}

// First-person viewmodel: weapon shape varies per weapon id; ADS pulls it
// toward center; prestige aura particles trail from the muzzle.
static void draw_viewmodel(Renderer& r, const Player& player, int weapon_id,
                           int prestige, float swing) {
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(r.world_prog);
    glm::mat4 identity(1.0f);
    glUniformMatrix4fv(r.u_view, 1, GL_FALSE, glm::value_ptr(identity));
    glUniform3f(r.u_campos, 0, 0, 0);

    float kick = player.recoil * 0.05f;
    // ADS slides the gun from the right hip toward screen center
    float x = glm::mix(0.21f, 0.0f, player.ads);
    float y = glm::mix(-0.17f, -0.12f, player.ads);
    float zbase = glm::mix(-0.45f, -0.40f, player.ads);
    const glm::vec3 mid {0.20f, 0.26f, 0.36f};
    const glm::vec3 dark{0.13f, 0.17f, 0.25f};

    // Size/length scales loosely with weapon for visual variety
    float len = 0.22f, barrel = 0.13f, w = 0.045f;
    switch (weapon_id) {
        case 1: len=0.26f; barrel=0.16f; break;             // SMG
        case 2: len=0.20f; barrel=0.10f; w=0.075f; break;   // SHOTGUN
        case 3: len=0.30f; barrel=0.20f; break;             // RIFLE
        case 4: len=0.34f; barrel=0.26f; w=0.035f; break;   // DMR
        case 5: len=0.32f; barrel=0.24f; w=0.06f; break;    // RAILGUN
        case 6: len=0.28f; barrel=0.18f; w=0.085f; break;   // MINIGUN
    }
    // Melee swing rotates the gun down-and-back briefly (used as a bash)
    float sk = swing * 0.18f;

    r.draw_box({x, y - sk, zbase + kick - sk}, {w, w*1.2f, len}, mid);
    r.draw_box({x, y + 0.015f - sk, zbase - len*0.7f + kick - sk},
               {w*0.55f, w*0.6f, barrel}, dark);
    r.draw_box({x, y - 0.08f - sk, zbase + 0.06f + kick - sk},
               {w*0.85f, w*1.9f, w*1.1f}, dark);
    r.draw_box({x, y + 0.02f - sk, zbase + kick - sk},
               {w*0.35f, w*0.5f, len*0.7f}, palette::enemy_body);

    if (player.recoil > 0.7f)
        r.draw_box({x, y + 0.015f, zbase - len*0.7f - barrel*0.6f + kick},
                   {0.08f, 0.08f, 0.08f}, palette::enemy_body, 0, 0.85f);

    glUniform3fv(r.u_campos, 1, glm::value_ptr(r.cam_pos));
}

// ---------------------------------------------------------------------------
// Game state machine for menus vs play.
// ---------------------------------------------------------------------------
enum class Screen { MainMenu, Settings, Keybinds, HowToPlay, ArenaSelect,
                    Playing, Paused, Dead };

static std::string prestige_numeral(int p) {
    const char* n[] = {"", "I", "II", "III", "IV", "V"};
    return (p >= 0 && p <= 5) ? n[p] : "";
}

static void save_screenshot(const std::string& path) {
    std::vector<unsigned char> px(WIN_W * WIN_H * 3);
    glReadPixels(0, 0, WIN_W, WIN_H, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << WIN_W << ' ' << WIN_H << "\n255\n";
    for (int yy = WIN_H - 1; yy >= 0; --yy)
        out.write(reinterpret_cast<char*>(&px[yy * WIN_W * 3]), WIN_W * 3);
}

// Simple vertical menu: returns index under the mouse, draws highlighted rows.
static int draw_menu(Renderer& r, const std::vector<std::string>& items,
                     float cx, float top, float row_h, float scale,
                     float mouse_x, float mouse_y) {
    int hovered = -1;
    for (size_t i = 0; i < items.size(); ++i) {
        float y = top - i * row_h;
        float w = font::text_width(items[i], scale);
        bool hit = mouse_x > cx - w/2 - 20 && mouse_x < cx + w/2 + 20 &&
                   mouse_y > y - 8 && mouse_y < y + font::GLYPH_H*scale + 8;
        if (hit) {
            hovered = int(i);
            r.draw_rect(cx - w/2 - 20, y - 8, w + 40,
                        font::GLYPH_H*scale + 16, palette::hud_dim);
        }
        r.text_centered(items[i], cx, y, scale,
                        hit ? palette::hud_orange : palette::hud_white);
    }
    return hovered;
}

int main(int argc, char** argv) {
    std::string screenshot_path;
    for (int i = 1; i < argc - 1; ++i)
        if (std::string(argv[i]) == "--screenshot") screenshot_path = argv[i+1];
    const bool headless = !screenshot_path.empty();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    // Profile path next to the executable
    std::string profile_path = "arena_profile.txt";
    Profile profile;
    profile.load(profile_path);

    Uint32 win_flags = SDL_WINDOW_OPENGL;
    if (profile.fullscreen && !headless) win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_Window* window = SDL_CreateWindow("arena",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, win_flags);
    if (!window) { std::fprintf(stderr, "Window failed: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { std::fprintf(stderr, "GLEW failed\n"); return 1; }

    Audio audio;
    bool have_audio = !headless && audio.init();
    if (have_audio) audio.set_volume(profile.volume);
    auto sfx = [&](Audio::Sfx s, float g = 1.0f){ if (have_audio) audio.play(s, g); };

    Renderer renderer;
    renderer.init();

    std::mt19937 rng(headless ? 1337u : SDL_GetTicks() ^ 0x5f3759df);

    // --- world state (initialized when a match starts) ---
    Level* level = nullptr;
    Player player;
    std::vector<Enemy> enemies;
    std::vector<Projectile> projectiles;
    std::vector<Pickup> pickups;
    ParticleSystem particles;
    int wave = 1, kills = 0, weapon_id = 0, arena_id = 0;
    float intermission = 0, levelup_banner = 0, aura_timer = 0;

    auto current_weapon = [&]() -> const Weapon& { return WEAPONS[weapon_id]; };

    auto start_match = [&](int arena) {
        arena_id = arena;
        delete level;
        level = new Level(ARENAS[arena].seed, arena);
        player = Player{};
        player.pos = level->spawn_point();
        weapon_id = profile.top_weapon();
        player.mag_capacity = current_weapon().mag_size;
        player.mag = player.mag_capacity;
        enemies.clear(); projectiles.clear(); particles.parts.clear();
        wave = 1; kills = 0; intermission = 0;
        std::uniform_real_distribution<float> a(0, 6.28f);
        for (int i = 0; i < 4; ++i) {
            Enemy e; e.pos = level->random_open_cell(player.pos, 12.0f);
            e.yaw = a(rng); enemies.push_back(e);
        }
        pickups.assign(4, Pickup{});
        for (Pickup& pk : pickups) pk.pos = level->random_open_cell(player.pos, 6.0f);
    };

    auto spawn_wave = [&](int count) {
        enemies.clear(); projectiles.clear();
        std::uniform_real_distribution<float> a(0, 6.28f);
        for (int i = 0; i < count; ++i) {
            Enemy e; e.pos = level->random_open_cell(player.pos, 12.0f);
            e.yaw = a(rng); enemies.push_back(e);
        }
    };

    Screen screen = headless ? Screen::Playing : Screen::MainMenu;
    if (headless) start_match(0);

    int settings_focus = -1;   // which keybind row is awaiting a new key
    bool relative_mouse = false;
    auto set_relative = [&](bool on) {
        if (on != relative_mouse) { SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
                                    relative_mouse = on; }
    };

    uint64_t prev = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    bool running = true;
    int frame = 0;
    float world_time = 0;
    int mouse_x = WIN_W/2, mouse_y = WIN_H/2;
    bool click = false, rclick_down = false;

    while (running) {
        uint64_t now = SDL_GetPerformanceCounter();
        float dt = std::min(float((now - prev)/freq), 0.05f);
        prev = now;
        if (headless) dt = 1.0f/60.0f;
        world_time += dt;
        click = false;

        bool want_play = (screen == Screen::Playing);
        set_relative(want_play && !headless);

        // ----- events -----
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;

            if (ev.type == SDL_MOUSEMOTION) {
                if (screen == Screen::Playing) {
                    player.yaw   += ev.motion.xrel * profile.sensitivity;
                    player.pitch -= ev.motion.yrel * profile.sensitivity;
                    player.pitch = glm::clamp(player.pitch, -1.45f, 1.45f);
                } else {
                    mouse_x = ev.motion.x;
                    mouse_y = WIN_H - ev.motion.y;   // flip to y-up
                }
            }
            if (ev.type == SDL_MOUSEBUTTONDOWN) {
                if (ev.button.button == SDL_BUTTON_LEFT) click = true;
                if (ev.button.button == SDL_BUTTON_RIGHT) rclick_down = true;
            }
            if (ev.type == SDL_MOUSEBUTTONUP &&
                ev.button.button == SDL_BUTTON_RIGHT) rclick_down = false;

            if (ev.type == SDL_KEYDOWN) {
                SDL_Scancode sc = ev.key.keysym.scancode;

                // Rebind capture: next key pressed becomes the binding
                if (screen == Screen::Keybinds && settings_focus >= 0) {
                    if (sc != SDL_SCANCODE_ESCAPE) {
                        profile.binds[settings_focus] = sc;
                        sfx(Audio::MENU);
                    }
                    settings_focus = -1;
                    continue;
                }
                if (sc == SDL_SCANCODE_ESCAPE) {
                    sfx(Audio::MENU);
                    if (screen == Screen::Playing) screen = Screen::Paused;
                    else if (screen == Screen::Paused) screen = Screen::Playing;
                    else if (screen == Screen::Settings ||
                             screen == Screen::Keybinds ||
                             screen == Screen::HowToPlay ||
                             screen == Screen::ArenaSelect)
                        screen = Screen::MainMenu;
                }
                if (screen == Screen::Playing) {
                    if (sc == profile.binds[ACT_RELOAD] &&
                        player.mag < player.mag_capacity && player.reload_timer <= 0)
                        player.reload_timer = current_weapon().reload_time;
                    if (sc == profile.binds[ACT_MELEE] && player.melee_timer <= 0) {
                        player.melee_timer = 0.35f;
                        sfx(Audio::MELEE);
                        // Melee hits all enemies in a short cone in front
                        glm::vec3 o = player.eye(), d = player.forward();
                        for (Enemy& e : enemies) {
                            if (!e.active || e.state == Enemy::State::Dying) continue;
                            glm::vec3 to = e.pos + glm::vec3(0,0.9f,0) - o;
                            if (glm::length(to) < 2.6f &&
                                glm::dot(glm::normalize(to), d) > 0.6f) {
                                e.hp -= 3; e.flash = 1.0f;
                                if (e.hp <= 0) { e.kill(); kills++;
                                    int lv = profile.add_xp(35);
                                    if (lv) { levelup_banner = 2.5f; sfx(Audio::LEVELUP);
                                              weapon_id = profile.top_weapon(); }
                                    player.score += 100; }
                            }
                        }
                    }
                    if (sc == profile.binds[ACT_NEXT_WEAPON]) {
                        int top = profile.top_weapon();
                        weapon_id = (weapon_id + 1) % (top + 1);
                        player.mag_capacity = current_weapon().mag_size;
                        player.mag = player.mag_capacity; sfx(Audio::MENU);
                    }
                    if (sc == profile.binds[ACT_PREV_WEAPON]) {
                        int top = profile.top_weapon();
                        weapon_id = (weapon_id + top) % (top + 1);
                        player.mag_capacity = current_weapon().mag_size;
                        player.mag = player.mag_capacity; sfx(Audio::MENU);
                    }
                }
            }
        }

        // ----- menu screens -----
        if (screen != Screen::Playing && !headless) {
            renderer.begin_frame({0,2,0}, {0,0,-1}, 70.0f);  // dim 3D backdrop
            hud_begin();
            float cx = WIN_W/2.0f;

            // Title + prestige badge always on top of menu screens
            if (screen == Screen::MainMenu || screen == Screen::Paused) {
                renderer.text_centered("ARENA", cx, WIN_H - 150.0f, 12.0f,
                                       palette::hud_orange);
                std::string sub = "LEVEL " + std::to_string(profile.level);
                if (profile.prestige > 0)
                    sub += "   PRESTIGE " + prestige_numeral(profile.prestige);
                renderer.text_centered(sub, cx, WIN_H - 210.0f, 3.0f, palette::hud_white);
            }

            std::vector<std::string> items;
            if (screen == Screen::MainMenu)
                items = {"PLAY", "SELECT ARENA", "SETTINGS", "CONTROLS",
                         "HOW TO PLAY",
                         profile.can_prestige() ? "PRESTIGE" : "", "QUIT"};
            else if (screen == Screen::Paused)
                items = {"RESUME", "SETTINGS", "CONTROLS", "QUIT TO MENU"};

            if (screen == Screen::MainMenu || screen == Screen::Paused) {
                int hov = draw_menu(renderer, items, cx, WIN_H - 290.0f, 56.0f,
                                    4.0f, mouse_x, mouse_y);
                if (click && hov >= 0) {
                    sfx(Audio::MENU);
                    const std::string& it = items[hov];
                    if (it == "PLAY") { start_match(arena_id); screen = Screen::Playing; }
                    else if (it == "RESUME") screen = Screen::Playing;
                    else if (it == "SELECT ARENA") screen = Screen::ArenaSelect;
                    else if (it == "SETTINGS") screen = Screen::Settings;
                    else if (it == "CONTROLS") screen = Screen::Keybinds;
                    else if (it == "HOW TO PLAY") screen = Screen::HowToPlay;
                    else if (it == "PRESTIGE") {
                        profile.do_prestige(); profile.save(profile_path);
                        sfx(Audio::LEVELUP);
                    }
                    else if (it == "QUIT") running = false;
                    else if (it == "QUIT TO MENU") {
                        profile.save(profile_path); screen = Screen::MainMenu;
                    }
                }
            }
            else if (screen == Screen::Settings) {
                renderer.text_centered("SETTINGS", cx, WIN_H - 120.0f, 6.0f,
                                       palette::hud_orange);
                // Sliders are click-zones: left half decreases, right half increases
                auto slider = [&](const std::string& label, float y, float val) {
                    renderer.draw_text(label, cx - 300, y, 3.0f, palette::hud_white);
                    renderer.draw_rect(cx, y, 280, 16, palette::hud_dim);
                    renderer.draw_rect(cx, y, 280*val, 16, palette::hud_orange);
                };
                float sens01 = (profile.sensitivity - 0.0006f)/0.005f;
                slider("SENSITIVITY", WIN_H - 250.0f, glm::clamp(sens01,0.0f,1.0f));
                slider("VOLUME", WIN_H - 310.0f, profile.volume);
                renderer.draw_text("FULLSCREEN", cx - 300, WIN_H - 370.0f, 3.0f,
                                   palette::hud_white);
                renderer.draw_rect(cx, WIN_H - 372.0f, 60, 24,
                                   profile.fullscreen ? palette::hud_orange : palette::hud_dim);
                renderer.text_centered("BACK  [ESC]", cx, WIN_H - 460.0f, 3.0f,
                                       palette::hud_white);
                if (click) {
                    if (mouse_y > WIN_H-254.0f && mouse_y < WIN_H-234.0f &&
                        mouse_x > cx && mouse_x < cx+280) {
                        float f = (mouse_x - cx)/280.0f;
                        profile.sensitivity = 0.0006f + f*0.005f; sfx(Audio::MENU);
                    }
                    if (mouse_y > WIN_H-314.0f && mouse_y < WIN_H-294.0f &&
                        mouse_x > cx && mouse_x < cx+280) {
                        profile.volume = glm::clamp((mouse_x-cx)/280.0f,0.0f,1.0f);
                        if (have_audio) audio.set_volume(profile.volume);
                        sfx(Audio::MENU);
                    }
                    if (mouse_y > WIN_H-372.0f && mouse_y < WIN_H-348.0f &&
                        mouse_x > cx && mouse_x < cx+60) {
                        profile.fullscreen = !profile.fullscreen;
                        SDL_SetWindowFullscreen(window,
                            profile.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        sfx(Audio::MENU);
                    }
                }
            }
            else if (screen == Screen::Keybinds) {
                renderer.text_centered("CONTROLS", cx, WIN_H - 100.0f, 6.0f,
                                       palette::hud_orange);
                renderer.text_centered("CLICK A ROW THEN PRESS A KEY", cx,
                                       WIN_H - 150.0f, 2.0f, palette::hud_white);
                for (int i = 0; i < ACT_COUNT; ++i) {
                    float y = WIN_H - 200.0f - i*42.0f;
                    renderer.draw_text(action_name(i), cx - 320, y, 2.5f,
                                       palette::hud_white);
                    const char* kn = SDL_GetScancodeName(profile.binds[i]);
                    bool waiting = settings_focus == i;
                    std::string val = waiting ? "PRESS KEY" :
                                      (kn && *kn ? kn : "?");
                    bool hov = mouse_x > cx+40 && mouse_x < cx+320 &&
                               mouse_y > y-6 && mouse_y < y+24;
                    if (hov) renderer.draw_rect(cx+40, y-6, 280, 30, palette::hud_dim);
                    renderer.draw_text(val, cx + 60, y, 2.5f,
                        waiting ? palette::hud_orange : palette::hud_white);
                    if (click && hov) { settings_focus = i; sfx(Audio::MENU); }
                }
                renderer.text_centered("BACK  [ESC]", cx, 60.0f, 3.0f,
                                       palette::hud_white);
            }
            else if (screen == Screen::HowToPlay) {
                renderer.text_centered("HOW TO PLAY", cx, WIN_H - 110.0f, 6.0f,
                                       palette::hud_orange);
                const char* lines[] = {
                    "MOVE WITH WASD - JUMP WITH SPACE - AIM WITH MOUSE",
                    "LEFT CLICK SHOOTS - RIGHT CLICK AIMS DOWN SIGHT",
                    "F MELEES - R RELOADS - Q/E SWAP WEAPONS",
                    "",
                    "CLEAR EVERY ENEMY TO ADVANCE THE WAVE",
                    "EACH WAVE ADDS ONE MORE ENEMY",
                    "GRAB SPINNING CROSSES TO HEAL",
                    "",
                    "EARN XP TO LEVEL UP AND UNLOCK NEW WEAPONS",
                    "REACH LEVEL 50 TO PRESTIGE FOR A GLOWING AURA",
                };
                for (int i = 0; i < 10; ++i)
                    renderer.text_centered(lines[i], cx, WIN_H - 180.0f - i*38.0f,
                                           2.3f, palette::hud_white);
                renderer.text_centered("BACK  [ESC]", cx, 60.0f, 3.0f,
                                       palette::hud_white);
            }
            else if (screen == Screen::ArenaSelect) {
                renderer.text_centered("SELECT ARENA", cx, WIN_H - 120.0f, 6.0f,
                                       palette::hud_orange);
                std::vector<std::string> items2;
                for (int i = 0; i < ARENA_COUNT; ++i) {
                    bool locked = profile.level < ARENAS[i].unlock_level &&
                                  profile.prestige == 0;
                    std::string s = ARENAS[i].name;
                    if (locked) s += "  (LVL " +
                        std::to_string(ARENAS[i].unlock_level) + ")";
                    items2.push_back(s);
                }
                int hov = draw_menu(renderer, items2, cx, WIN_H - 230.0f, 56.0f,
                                    4.0f, mouse_x, mouse_y);
                if (click && hov >= 0) {
                    bool locked = profile.level < ARENAS[hov].unlock_level &&
                                  profile.prestige == 0;
                    if (!locked) { arena_id = hov; sfx(Audio::MENU);
                                   start_match(arena_id); screen = Screen::Playing; }
                }
                renderer.text_centered("BACK  [ESC]", cx, 60.0f, 3.0f,
                                       palette::hud_white);
            }
            else if (screen == Screen::Dead) {
                renderer.text_centered("YOU DIED", cx, WIN_H/2.0f + 60.0f, 10.0f,
                                       palette::hud_orange);
                renderer.text_centered("WAVE " + std::to_string(wave) +
                    "   KILLS " + std::to_string(kills), cx, WIN_H/2.0f - 10.0f,
                    3.0f, palette::hud_white);
                std::vector<std::string> items3 = {"RETRY", "QUIT TO MENU"};
                int hov = draw_menu(renderer, items3, cx, WIN_H/2.0f - 80.0f,
                                    52.0f, 4.0f, mouse_x, mouse_y);
                if (click && hov == 0) { start_match(arena_id); screen = Screen::Playing; }
                if (click && hov == 1) { profile.save(profile_path);
                                         screen = Screen::MainMenu; }
            }

            hud_end();
            if (!headless) SDL_GL_SwapWindow(window);
            continue;
        }

        // ===== PLAYING =====
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        glm::vec3 wish{0};
        if (keys[profile.binds[ACT_FORWARD]]) wish.z += 1;
        if (keys[profile.binds[ACT_BACK]])    wish.z -= 1;
        if (keys[profile.binds[ACT_RIGHT]])   wish.x += 1;
        if (keys[profile.binds[ACT_LEFT]])    wish.x -= 1;
        bool jump = keys[profile.binds[ACT_JUMP]];

        // ADS smoothing toward held state
        float ads_target = rclick_down ? 1.0f : 0.0f;
        player.ads += (ads_target - player.ads) * std::min(1.0f, dt * 12.0f);

        player.update(dt, wish, jump, *level);

        // Shooting with per-weapon fire rate, pellets, spread, pierce
        const Weapon& wpn = current_weapon();
        if (click && player.mag > 0 && player.reload_timer <= 0 &&
            player.fire_timer <= 0) {
            player.mag--;
            player.recoil = 1.0f;
            player.fire_timer = wpn.fire_delay;
            sfx(Audio::SHOOT, 0.9f);
            glm::vec3 origin = player.eye();
            std::uniform_real_distribution<float> sp(-1.0f, 1.0f);
            for (int pellet = 0; pellet < wpn.pellets; ++pellet) {
                glm::vec3 dir = player.forward();
                float spread = wpn.spread * (1.0f - player.ads * 0.7f);
                dir = glm::normalize(dir + glm::vec3(sp(rng), sp(rng), sp(rng)) * spread);

                float wall_t = 1e9f, t;
                for (const AABB& w : level->wall_boxes())
                    if (w.ray_hit(origin, dir, t)) wall_t = std::min(wall_t, t);

                // Sort enemy hits by distance; pierce hits all up to a wall
                bool any = false;
                for (Enemy& e : enemies) {
                    if (!e.active || e.state == Enemy::State::Dying) continue;
                    if (e.box().ray_hit(origin, dir, t) && t < wall_t) {
                        e.hp -= wpn.damage; e.flash = 1.0f; any = true;
                        particles.spawn(origin + dir*t, glm::vec3(0),
                                        palette::enemy_body, 0.25f, 0.12f);
                        if (e.hp <= 0) {
                            e.kill(); kills++; player.score += 100;
                            int lv = profile.add_xp(35);
                            if (lv) { levelup_banner = 2.5f; sfx(Audio::LEVELUP);
                                      weapon_id = profile.top_weapon(); }
                        }
                        if (!wpn.pierce) break;
                    }
                }
                if (any) sfx(Audio::HIT, 0.6f);
            }
            if (player.mag == 0) player.reload_timer = wpn.reload_time;
        }

        // Enemy AI + separation
        size_t before_proj = projectiles.size();
        for (Enemy& e : enemies)
            if (e.active) e.update(dt, player, *level, rng, projectiles);
        Enemy::separate(enemies, *level, dt);
        if (projectiles.size() > before_proj) sfx(Audio::ENEMY_SHOOT, 0.5f);

        float hp_before = float(player.hp);
        for (Projectile& pr : projectiles) pr.update(dt, player, *level);
        if (player.hp < hp_before) sfx(Audio::HURT, 0.7f);
        projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(),
            [](const Projectile& p){ return p.dead; }), projectiles.end());

        for (Pickup& pk : pickups) {
            bool was = pk.active;
            pk.update(dt, player);
            if (was && !pk.active) sfx(Audio::PICKUP);
        }

        // Wave progression
        int enemies_left = 0;
        for (const Enemy& e : enemies) if (e.active) enemies_left++;
        if (enemies_left == 0) {
            if (intermission <= 0) intermission = 2.5f;
            intermission -= dt;
            if (intermission <= 0) { wave++; spawn_wave(3 + wave); }
        }

        // Prestige aura: emit particles around the weapon over time
        if (profile.prestige > 0) {
            aura_timer -= dt;
            if (aura_timer <= 0) {
                aura_timer = 0.04f;
                glm::vec3 c = palette::prestige_color(profile.prestige);
                glm::vec3 base = player.eye() + player.forward()*0.6f
                    + glm::vec3(0,-0.25f,0);
                std::uniform_real_distribution<float> j(-0.12f, 0.12f);
                particles.spawn(base + glm::vec3(j(rng),j(rng),j(rng)),
                                glm::vec3(j(rng)*0.5f, 0.4f, j(rng)*0.5f),
                                c, 0.5f, 0.07f);
            }
        }
        particles.update(dt);

        levelup_banner = std::max(0.0f, levelup_banner - dt);

        if (player.hp <= 0) {
            profile.save(profile_path);
            screen = headless ? Screen::Playing : Screen::Dead;
            sfx(Audio::HURT);
        }

        // ----- render world -----
        float base_fov = 72.0f;
        float fov = glm::mix(base_fov, wpn.ads_fov, player.ads);
        float shake = player.recoil * 0.6f + player.hurt_flash * 0.5f;
        glm::vec3 jitter = shake * 0.012f *
            glm::vec3(std::sin(world_time*73.0f), std::cos(world_time*61.0f), 0);
        renderer.begin_frame(player.eye() + jitter, player.forward(), fov);

        glm::vec3 tint = palette::arena_tint(level->style());
        const float W = Level::GRID * Level::CELL;
        renderer.draw_box({W/2, -0.5f, W/2}, {W, 1.0f, W}, palette::floor * tint);

        for (const AABB& w : level->wall_boxes()) {
            glm::vec3 c = (w.min + w.max) * 0.5f;
            glm::vec3 s = w.max - w.min;
            renderer.draw_box({c.x, s.y*0.5f - 0.15f, c.z},
                              {s.x, s.y - 0.3f, s.z}, palette::wall * tint);
            renderer.draw_box({c.x, s.y - 0.15f, c.z},
                              {s.x, 0.3f, s.z}, palette::wall_trim * tint);
        }
        for (const Enemy& e : enemies) draw_enemy(renderer, e);

        for (const Projectile& pr : projectiles) {
            float pyaw = -std::atan2(pr.vel.z, pr.vel.x);
            renderer.draw_box(pr.pos, {0.32f, 0.09f, 0.09f},
                              palette::enemy_body, pyaw, 0.55f);
        }
        for (const Pickup& pk : pickups) {
            if (!pk.active) continue;
            float bob = 0.08f * std::sin(world_time*2.2f);
            glm::vec3 c = pk.pos + glm::vec3(0, 0.65f + bob, 0);
            float spin = world_time * 1.8f;
            const glm::vec3 white{0.92f, 0.94f, 0.97f};
            renderer.draw_box(c, {0.13f, 0.46f, 0.13f}, white, spin);
            renderer.draw_box(c, {0.46f, 0.13f, 0.13f}, white, spin);
        }
        particles.draw(renderer);

        float swing = player.melee_timer > 0 ?
            std::sin((1.0f - player.melee_timer/0.35f) * 3.14159f) : 0.0f;
        draw_viewmodel(renderer, player, weapon_id, profile.prestige, swing);

        // ----- HUD -----
        hud_begin();
        const float cx = WIN_W/2.0f, cy = WIN_H/2.0f;
        float spread_gap = 14.0f - player.ads * 8.0f;
        renderer.draw_rect(cx - spread_gap - 8, cy-1, 8, 2, palette::hud_white);
        renderer.draw_rect(cx + spread_gap, cy-1, 8, 2, palette::hud_white);
        renderer.draw_rect(cx-1, cy + spread_gap, 2, 8, palette::hud_white);
        renderer.draw_rect(cx-1, cy - spread_gap - 8, 2, 8, palette::hud_white);
        renderer.draw_rect(cx-1.5f, cy-1.5f, 3, 3, palette::hud_orange);

        // Enemies remaining ticks (top center)
        float tw = enemies_left * 16.0f - 6.0f;
        for (int i = 0; i < enemies_left; ++i)
            renderer.draw_rect(cx - tw/2 + i*16, WIN_H-36.0f, 10, 10, palette::hud_orange);

        // Health bar + number
        float hpf = std::max(0, player.hp)/100.0f;
        renderer.draw_rect(24, 50, 240, 20, palette::hud_dim);
        renderer.draw_rect(24, 50, 240*hpf, 20,
                           hpf > 0.35f ? palette::hud_white : palette::hud_orange);
        renderer.draw_text(std::to_string(std::max(0,player.hp)), 30, 78, 2.5f,
                           palette::hud_white);

        // Weapon name + ammo
        renderer.draw_text(wpn.name, WIN_W - 360.0f, 78, 2.5f, palette::hud_white);
        std::string ammo = std::to_string(player.mag) + "/" +
                           std::to_string(player.mag_capacity);
        if (player.reload_timer > 0) ammo = "RELOAD";
        renderer.draw_text(ammo, WIN_W - 360.0f, 44, 3.5f, palette::hud_orange);

        // XP bar (bottom center) with level + prestige
        float xpf = profile.level >= Profile::LEVEL_CAP ? 1.0f :
                    float(profile.xp)/float(profile.xp_to_next());
        renderer.draw_rect(cx - 200, 24, 400, 10, palette::hud_dim);
        renderer.draw_rect(cx - 200, 24, 400*xpf, 10, palette::hud_orange);
        std::string lvtxt = "LVL " + std::to_string(profile.level);
        if (profile.prestige > 0) lvtxt += "  P" + prestige_numeral(profile.prestige);
        renderer.text_centered(lvtxt, cx, 40, 2.0f, palette::hud_white);

        // Wave banner during intermission
        if (enemies_left == 0 && intermission > 0)
            renderer.text_centered("WAVE " + std::to_string(wave+1), cx,
                                   cy + 80, 5.0f, palette::hud_orange);
        // Level-up banner
        if (levelup_banner > 0) {
            renderer.text_centered("LEVEL UP", cx, cy + 140, 6.0f, palette::hud_orange);
            int top = profile.top_weapon();
            if (profile.level == WEAPONS[top].unlock_level)
                renderer.text_centered("UNLOCKED " + std::string(WEAPONS[top].name),
                                       cx, cy + 100, 3.0f, palette::hud_white);
        }

        // Hurt vignette
        if (player.hurt_flash > 0) {
            glm::vec4 red = palette::hurt_red; red.a = 0.35f*player.hurt_flash;
            renderer.draw_rect(0, 0, WIN_W, 26, red);
            renderer.draw_rect(0, WIN_H-26.0f, WIN_W, 26, red);
            renderer.draw_rect(0, 0, 26, WIN_H, red);
            renderer.draw_rect(WIN_W-26.0f, 0, 26, WIN_H, red);
        }
        hud_end();

        if (!headless) {
            SDL_GL_SwapWindow(window);
        } else if (++frame >= 3) {
            save_screenshot(screenshot_path);
            running = false;
        }
    }

    profile.save(profile_path);
    delete level;
    if (have_audio) audio.shutdown();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
