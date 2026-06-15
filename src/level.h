#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <random>
#include <vector>

// ---------------------------------------------------------------------------
// Axis-aligned bounding box + the two queries the whole game runs on:
// box-vs-box overlap (movement collision) and ray-vs-box (shooting).
// ---------------------------------------------------------------------------
struct AABB {
    glm::vec3 min{0}, max{0};

    bool overlaps(const AABB& o) const {
        return min.x < o.max.x && max.x > o.min.x &&
               min.y < o.max.y && max.y > o.min.y &&
               min.z < o.max.z && max.z > o.min.z;
    }

    // Slab method. Returns true and the entry distance t if the ray hits.
    bool ray_hit(const glm::vec3& origin, const glm::vec3& dir, float& t) const {
        float tmin = 0.0f, tmax = 1e9f;
        for (int a = 0; a < 3; ++a) {
            float inv = 1.0f / dir[a];
            float t0 = (min[a] - origin[a]) * inv;
            float t1 = (max[a] - origin[a]) * inv;
            if (t0 > t1) std::swap(t0, t1);
            tmin = std::max(tmin, t0);
            tmax = std::min(tmax, t1);
            if (tmax < tmin) return false;
        }
        t = tmin;
        return true;
    }
};

// ---------------------------------------------------------------------------
// Level: a grid of cells, each either open floor or a solid wall block.
// Generation: solid border, then scattered pillars and short wall runs.
// The spawn region is always kept clear.
// ---------------------------------------------------------------------------
class Level {
public:
    static constexpr int   GRID = 24;       // cells per side
    static constexpr float CELL = 2.0f;     // world units per cell
    static constexpr float WALL_H = 3.0f;

    explicit Level(uint32_t seed, int style = 0) : style_(style), rng_(seed) {
        generate();
    }

    int style() const { return style_; }

    bool is_wall(int cx, int cz) const {
        if (cx < 0 || cz < 0 || cx >= GRID || cz >= GRID) return true;
        return cells_[cz * GRID + cx] != 0;
    }

    const std::vector<AABB>& wall_boxes() const { return walls_; }

    // World-space center of a cell, on the floor
    glm::vec3 cell_center(int cx, int cz) const {
        return {(cx + 0.5f) * CELL, 0.0f, (cz + 0.5f) * CELL};
    }

    glm::vec3 random_open_cell(const glm::vec3& avoid, float min_dist) {
        std::uniform_int_distribution<int> d(1, GRID - 2);
        for (int tries = 0; tries < 256; ++tries) {
            int cx = d(rng_), cz = d(rng_);
            if (is_wall(cx, cz)) continue;
            glm::vec3 p = cell_center(cx, cz);
            if (glm::distance(p, avoid) >= min_dist) return p;
        }
        return cell_center(GRID / 2, GRID / 2);
    }

    glm::vec3 spawn_point() const { return cell_center(3, 3); }

private:
    void generate() {
        cells_.assign(GRID * GRID, 0);

        // Border walls
        for (int i = 0; i < GRID; ++i) {
            set_wall(i, 0); set_wall(i, GRID - 1);
            set_wall(0, i); set_wall(GRID - 1, i);
        }

        // Scattered pillars — density varies by arena style
        std::uniform_int_distribution<int> d(2, GRID - 3);
        std::uniform_real_distribution<float> f(0.0f, 1.0f);
        int pillars = 38, runs = 12;
        if (style_ == 1) { pillars = 24; runs = 20; }  // FOUNDRY: long walls
        if (style_ == 2) { pillars = 54; runs = 8;  }  // CITADEL: dense cover
        for (int n = 0; n < pillars; ++n) set_wall(d(rng_), d(rng_));

        // Short wall runs for corridors and cover
        for (int n = 0; n < runs; ++n) {
            int x = d(rng_), z = d(rng_);
            bool horizontal = f(rng_) < 0.5f;
            int len = 2 + int(f(rng_) * 4.0f);
            for (int k = 0; k < len; ++k)
                set_wall(horizontal ? x + k : x, horizontal ? z : z + k);
        }

        // Keep the spawn neighbourhood clear
        for (int z = 2; z <= 5; ++z)
            for (int x = 2; x <= 5; ++x)
                cells_[z * GRID + x] = 0;

        // Bake wall AABBs once; collision and rendering both use these
        walls_.clear();
        for (int z = 0; z < GRID; ++z)
            for (int x = 0; x < GRID; ++x)
                if (is_wall(x, z))
                    walls_.push_back({
                        {x * CELL, 0.0f, z * CELL},
                        {(x + 1) * CELL, WALL_H, (z + 1) * CELL}});
    }

    void set_wall(int cx, int cz) {
        if (cx >= 0 && cz >= 0 && cx < GRID && cz < GRID)
            cells_[cz * GRID + cx] = 1;
    }

    std::vector<uint8_t> cells_;
    std::vector<AABB> walls_;
    int style_ = 0;
    std::mt19937 rng_;
};
