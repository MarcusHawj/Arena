#pragma once
#include <GL/glew.h>
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// World shader: directional diffuse lighting + distance fog.
// Fog fades geometry into the sky color so the arena edge never looks abrupt.
// ---------------------------------------------------------------------------
static const char* WORLD_VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uProj;
uniform mat4 uView;
uniform mat4 uModel;

out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uProj * uView * worldPos;
}
)";

static const char* WORLD_FS = R"(#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;

uniform vec3  uColor;
uniform vec3  uLightDir;   // normalized, pointing from the light
uniform vec3  uCamPos;
uniform vec3  uFogColor;
uniform float uFlash;      // 0..1, whites out the surface (hit feedback)

out vec4 FragColor;

void main() {
    vec3 n = normalize(vNormal);
    float diffuse = max(dot(n, -uLightDir), 0.0);
    vec3 col = uColor * (0.58 + 0.50 * diffuse);

    col = mix(col, vec3(1.0), uFlash);

    float dist = length(vWorldPos - uCamPos);
    float fog = smoothstep(20.0, 58.0, dist);
    col = mix(col, uFogColor, fog);

    FragColor = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------------------
// HUD shader: 2D quads in pixel coordinates with alpha blending.
// ---------------------------------------------------------------------------
static const char* HUD_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;   // pixels, origin bottom-left
uniform vec2 uScreen;
void main() {
    vec2 ndc = aPos / uScreen * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

static const char* HUD_FS = R"(#version 330 core
uniform vec4 uColor;
out vec4 FragColor;
void main() { FragColor = uColor; }
)";

inline GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error:\n%s\n", log);
        std::exit(1);
    }
    return s;
}

inline GLuint link_program(const char* vs_src, const char* fs_src) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Program link error:\n%s\n", log);
        std::exit(1);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}
