// =====================================================================
//  MARS STATION — top-down survival shooter
//  C++17 + raylib 5.5. Builds for Windows (mingw-w64), Linux and the web
//  (Emscripten / WebAssembly, with touch controls for phones).
// =====================================================================
#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------
static float Rand01() { return (float)GetRandomValue(0, 100000) / 100000.0f; }
static float RandF(float a, float b) { return a + (b - a) * Rand01(); }
static Vector2 Dir(float ang) { return {cosf(ang), sinf(ang)}; }
static Color Fade2(Color c, float a) { return Fade(c, Clamp(a, 0.0f, 1.0f)); }
static Color LerpColor(Color a, Color b, float t) {
    t = Clamp(t, 0.0f, 1.0f);
    return {(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
            (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}

// Palette
static const Color C_SKY      = {22, 10, 14, 255};
static const Color C_GROUND   = {120, 52, 32, 255};
static const Color C_GROUND2  = {104, 44, 28, 255};
static const Color C_DUST     = {196, 112, 70, 255};
static const Color C_ACCENT   = {255, 170, 60, 255};
static const Color C_CYAN     = {80, 230, 255, 255};
static const Color C_PLAYER   = {235, 240, 250, 255};
static const Color C_ENEMY    = {200, 255, 120, 255};
static const Color C_DANGER   = {255, 70, 70, 255};

// World
static const float WORLD_W = 3200.0f;
static const float WORLD_H = 3200.0f;

// ---------------------------------------------------------------------
// Persistent best score (file on desktop, localStorage on web)
// ---------------------------------------------------------------------
static int LoadBest() {
#if defined(PLATFORM_WEB)
    return EM_ASM_INT({
        try { var v = localStorage.getItem('marsstation_best'); return v ? parseInt(v) : 0; }
        catch (e) { return 0; }
    });
#else
    FILE *f = fopen("marsstation_best.dat", "rb");
    if (!f) return 0;
    int v = 0;
    if (fscanf(f, "%d", &v) != 1) v = 0;
    fclose(f);
    return v;
#endif
}
static void SaveBest(int v) {
#if defined(PLATFORM_WEB)
    EM_ASM({ try { localStorage.setItem('marsstation_best', String($0)); } catch (e) {} }, v);
#else
    FILE *f = fopen("marsstation_best.dat", "wb");
    if (!f) return;
    fprintf(f, "%d", v);
    fclose(f);
#endif
}

// ---------------------------------------------------------------------
// Procedural audio (no asset files needed)
// ---------------------------------------------------------------------
static const int SR = 22050;
struct SoundBank {
    Sound shoot[6], boom[5], hit[4];
    Sound hurt, pickup, wave, dash, spit, gameover, select, music;
    int iShoot = 0, iBoom = 0, iHit = 0;
    bool ok = false;
} SB;

template <typename F>
static Sound MakeSound(float seconds, F fn) {
    int n = (int)(seconds * SR);
    short *data = (short *)MemAlloc(n * sizeof(short));
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float s = Clamp(fn(t, t / seconds), -1.0f, 1.0f);
        data[i] = (short)(s * 32000.0f);
    }
    Wave w{};
    w.frameCount = n; w.sampleRate = SR; w.sampleSize = 16; w.channels = 1; w.data = data;
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}
static float Sq(float ph) { return fmodf(ph, 1.0f) < 0.5f ? 1.0f : -1.0f; }
static float Tri(float ph) { float f = fmodf(ph, 1.0f); return 4.0f * fabsf(f - 0.5f) - 1.0f; }
static float Noise() { return Rand01() * 2.0f - 1.0f; }

static void InitSounds() {
    // Laser: falling square sweep
    Sound shoot = MakeSound(0.12f, [](float t, float k) {
        float f = 1100.0f - 800.0f * k;
        return 0.28f * Sq(t * f) * (1.0f - k) * (1.0f - k);
    });
    // Explosion: filtered noise with decay
    float lp = 0.0f;
    Sound boom = MakeSound(0.55f, [&lp](float t, float k) {
        lp += (Noise() - lp) * (0.25f - 0.2f * k);
        return 0.9f * lp * powf(1.0f - k, 2.0f) + 0.25f * sinf(t * 6.28f * (70 - 40 * k)) * (1 - k);
    });
    Sound hit = MakeSound(0.07f, [](float t, float k) {
        return 0.35f * Sq(t * (420 - 200 * k)) * (1 - k) + 0.15f * Noise() * (1 - k);
    });
    SB.hurt = MakeSound(0.35f, [](float t, float k) {
        return 0.45f * Sq(t * (220 - 150 * k)) * (1 - k) + 0.2f * Noise() * (1 - k);
    });
    SB.pickup = MakeSound(0.3f, [](float t, float k) {
        float notes[4] = {523.25f, 659.25f, 783.99f, 1046.5f};
        int i = std::min(3, (int)(k * 4));
        return 0.3f * Tri(t * notes[i]) * (1 - k * 0.6f);
    });
    SB.wave = MakeSound(0.9f, [](float t, float k) {
        float notes[3] = {392.0f, 523.25f, 659.25f};
        int i = std::min(2, (int)(k * 3.5f));
        return 0.28f * (Sq(t * notes[i]) * 0.5f + Tri(t * notes[i] * 0.5f)) * (1 - k);
    });
    SB.dash = MakeSound(0.18f, [](float t, float k) {
        return 0.3f * Noise() * (1 - k) * k * 4 + 0.15f * sinf(t * 6.28f * (300 + 900 * k)) * (1 - k);
    });
    SB.spit = MakeSound(0.15f, [](float t, float k) {
        return 0.25f * Tri(t * (300 + 300 * k)) * (1 - k);
    });
    SB.gameover = MakeSound(1.6f, [](float t, float k) {
        float notes[4] = {392.0f, 349.23f, 311.13f, 196.0f};
        int i = std::min(3, (int)(k * 4));
        return 0.3f * (Tri(t * notes[i]) + 0.3f * Sq(t * notes[i] * 0.5f)) * (1 - k * 0.8f);
    });
    SB.select = MakeSound(0.1f, [](float t, float k) { return 0.25f * Sq(t * 880) * (1 - k); });

    // Background music: 16-step dark synth loop, ~9.6 s
    const float bpm = 100.0f, step = 60.0f / bpm / 2.0f;
    const int steps = 32;
    const float bass[8] = {55.0f, 55.0f, 65.41f, 49.0f, 55.0f, 55.0f, 73.42f, 61.74f};
    const float arp[8] = {220.0f, 261.63f, 329.63f, 261.63f, 293.66f, 349.23f, 440.0f, 329.63f};
    SB.music = MakeSound(steps * step, [&](float t, float) {
        int s = (int)(t / step);
        float st = fmodf(t, step) / step;
        float b = bass[(s / 4) % 8];
        float a = arp[s % 8] * ((s / 8) % 2 ? 1.0f : 0.5f);
        float v = 0.22f * Tri(t * b) * (1.0f - 0.5f * st);
        v += 0.06f * Sq(t * a) * powf(1.0f - st, 3.0f);
        if (s % 4 == 0) v += 0.18f * sinf(t * 6.28f * (60.0f - 30.0f * st)) * powf(1.0f - st, 4.0f);  // kick
        if (s % 4 == 2) v += 0.05f * Noise() * powf(1.0f - st, 8.0f);                                   // hat
        return v;
    });

    SB.shoot[0] = shoot; SB.boom[0] = boom; SB.hit[0] = hit;
    for (int i = 1; i < 6; i++) SB.shoot[i] = LoadSoundAlias(shoot);
    for (int i = 1; i < 5; i++) SB.boom[i] = LoadSoundAlias(boom);
    for (int i = 1; i < 4; i++) SB.hit[i] = LoadSoundAlias(hit);
    for (int i = 0; i < 6; i++) SetSoundVolume(SB.shoot[i], 0.35f);
    SetSoundVolume(SB.music, 0.55f);
    SB.ok = true;
}
static void PlayShoot() { if (!SB.ok) return; Sound &s = SB.shoot[SB.iShoot++ % 6]; SetSoundPitch(s, RandF(0.9f, 1.1f)); PlaySound(s); }
static void PlayBoom()  { if (!SB.ok) return; Sound &s = SB.boom[SB.iBoom++ % 5]; SetSoundPitch(s, RandF(0.8f, 1.2f)); PlaySound(s); }
static void PlayHit()   { if (!SB.ok) return; Sound &s = SB.hit[SB.iHit++ % 4]; SetSoundPitch(s, RandF(0.9f, 1.3f)); PlaySound(s); }
static void Play(Sound &s) { if (SB.ok) PlaySound(s); }

// ---------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------
enum EnemyType { E_CRAWLER, E_SWARMER, E_SPITTER, E_BRUTE, E_BOSS };
enum PickupType { P_HEAL, P_SPREAD, P_RAPID, P_SHIELD };

struct Bullet { Vector2 p, v; float life, r; float dmg; bool enemy; Color c; };
struct Enemy {
    int type; Vector2 p, v; float hp, maxhp, r, speed, cool, flash, t, ang; bool alive; int phase;
};
struct Particle { Vector2 p, v; float life, maxlife, size; Color c; bool glow; };
struct Pickup { int type; Vector2 p; float t; };
struct FloatText { Vector2 p; float life; std::string s; Color c; float size; };
struct Prop { int kind; Vector2 p; float r, ang; };

struct Player {
    Vector2 p{WORLD_W / 2, WORLD_H / 2}, v{0, 0};
    float aim = 0, fireCool = 0, inv = 0, dashT = 0, dashCool = 0;
    Vector2 dashDir{1, 0};
    int hp = 6, maxHp = 6;
    float spreadT = 0, rapidT = 0, shieldT = 0;
    float r = 18;
};

enum Screen { S_TITLE, S_PLAY, S_PAUSE, S_GAMEOVER };

struct Game {
    Screen screen = S_TITLE;
    Player pl;
    std::vector<Bullet> bullets;
    std::vector<Enemy> enemies;
    std::vector<Particle> parts;
    std::vector<Pickup> pickups;
    std::vector<FloatText> texts;
    std::vector<Prop> props;
    std::vector<Vector2> stars;
    int score = 0, best = 0, wave = 0, kills = 0;
    int toSpawn = 0; float spawnT = 0, interT = 0; bool bossWave = false;
    float combo = 1.0f, comboT = 0;
    float shake = 0, time = 0, flashRed = 0, titleT = 0, gameOverT = 0;
    Camera2D cam{};
    bool touchMode = false;
    bool newBest = false;
    std::vector<Enemy> pending;  // enemies spawned during iteration
} G;

// ---------------------------------------------------------------------
// Input abstraction (keyboard+mouse, gamepad, touch)
// ---------------------------------------------------------------------
struct TouchPt { int id; Vector2 p; };
static std::vector<TouchPt> GetTouches() {
    std::vector<TouchPt> out;
#if defined(PLATFORM_WEB)
    int n = EM_ASM_INT({ return (window.MS_TOUCHES ? window.MS_TOUCHES.length : 0); });
    for (int i = 0; i < n && i < 10; i++) {
        TouchPt t;
        t.id = EM_ASM_INT({ return window.MS_TOUCHES[$0].id | 0; }, i);
        t.p.x = (float)EM_ASM_DOUBLE({ return window.MS_TOUCHES[$0].x; }, i);
        t.p.y = (float)EM_ASM_DOUBLE({ return window.MS_TOUCHES[$0].y; }, i);
        out.push_back(t);
    }
#else
    int n = GetTouchPointCount();
    (void)n;  // desktop: mouse handles everything
#endif
    return out;
}

struct Controls {
    Vector2 move{0, 0};
    Vector2 aimWorld{0, 0};
    bool hasAim = false;   // explicit aim point (mouse)
    bool fire = false, autoAim = false;
    bool dash = false, pause = false, confirm = false;
    // touch UI state
    int joyId = -1; Vector2 joyBase{0, 0}, joyKnob{0, 0};
    int fireId = -1, dashId = -1;
    std::vector<int> prevIds;
} CTL;

static float UIS() { return std::min(GetScreenWidth(), GetScreenHeight()) / 720.0f; }  // UI scale
static Vector2 FireBtnPos() { float u = UIS(); return {GetScreenWidth() - 150 * u, GetScreenHeight() - 150 * u}; }
static Vector2 DashBtnPos() { float u = UIS(); return {GetScreenWidth() - 330 * u, GetScreenHeight() - 95 * u}; }
static Vector2 PauseBtnPos() { float u = UIS(); return {GetScreenWidth() - 50 * u, 50 * u}; }
static float FireBtnR() { return 95 * UIS(); }
static float DashBtnR() { return 62 * UIS(); }

static void UpdateControls() {
    Controls &c = CTL;
    c.move = {0, 0}; c.fire = false; c.dash = false; c.pause = false; c.confirm = false; c.hasAim = false; c.autoAim = false;

    // Keyboard
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) c.move.y -= 1;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) c.move.y += 1;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) c.move.x -= 1;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) c.move.x += 1;
    if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_LEFT_SHIFT)) c.dash = true;
    if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE)) c.pause = true;
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) c.confirm = true;
    if (IsKeyDown(KEY_J) || IsKeyDown(KEY_K)) { c.fire = true; c.autoAim = true; }

    // Mouse (desktop & web-on-desktop)
    if (!G.touchMode) {
        c.aimWorld = GetScreenToWorld2D(GetMousePosition(), G.cam);
        c.hasAim = true;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) { c.fire = true; c.autoAim = false; }
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) c.dash = true;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) c.confirm = true;
    }

    // Gamepad
    if (IsGamepadAvailable(0)) {
        Vector2 m{GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X), GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y)};
        if (Vector2Length(m) > 0.2f) c.move = m;
        Vector2 a{GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X), GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y)};
        if (Vector2Length(a) > 0.35f) { c.aimWorld = Vector2Add(G.pl.p, Vector2Scale(a, 200)); c.hasAim = true; c.fire = true; c.autoAim = false; }
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) { c.fire = true; if (Vector2Length(a) <= 0.35f) c.autoAim = true; }
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_TRIGGER_2)) c.dash = true;
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) { c.pause = true; c.confirm = true; }
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) c.confirm = true;
    }

    // Touch
    std::vector<TouchPt> ts = GetTouches();
    if (!ts.empty()) G.touchMode = true;
    std::vector<int> ids;
    float u = UIS();
    bool joyAlive = false, fireAlive = false, dashAlive = false;
    for (auto &t : ts) {
        ids.push_back(t.id);
        bool isNew = std::find(c.prevIds.begin(), c.prevIds.end(), t.id) == c.prevIds.end();
        if (isNew) {
            c.confirm = true;
            if (G.screen == S_PLAY) {
                if (CheckCollisionPointCircle(t.p, PauseBtnPos(), 45 * u)) { c.pause = true; c.confirm = false; }
                else if (CheckCollisionPointCircle(t.p, DashBtnPos(), DashBtnR() * 1.25f)) { c.dashId = t.id; c.dash = true; }
                else if (t.p.x > GetScreenWidth() * 0.5f && t.p.y > GetScreenHeight() * 0.25f) { c.fireId = t.id; }
                else if (t.p.x <= GetScreenWidth() * 0.5f && c.joyId < 0) { c.joyId = t.id; c.joyBase = t.p; c.joyKnob = t.p; }
            }
        }
        if (t.id == c.joyId) {
            joyAlive = true;
            float maxR = 85 * u;
            Vector2 d = Vector2Subtract(t.p, c.joyBase);
            float L = Vector2Length(d);
            if (L > maxR) {  // floating joystick follows the finger
                c.joyBase = Vector2Add(c.joyBase, Vector2Scale(d, (L - maxR) / L));
                d = Vector2Subtract(t.p, c.joyBase); L = maxR;
            }
            c.joyKnob = t.p;
            if (L > 8 * u) c.move = Vector2Scale(d, 1.0f / maxR);
        }
        if (t.id == c.fireId) { fireAlive = true; c.fire = true; c.autoAim = true; }
        if (t.id == c.dashId) dashAlive = true;
    }
    if (!joyAlive) c.joyId = -1;
    if (!fireAlive) c.fireId = -1;
    if (!dashAlive) c.dashId = -1;
    c.prevIds = ids;

#ifdef MS_DEBUG
    c.fire = true; c.autoAim = true; c.move = {sinf(G.time), cosf(G.time * 0.7f)};
#endif
    float L = Vector2Length(c.move);
    if (L > 1.0f) c.move = Vector2Scale(c.move, 1.0f / L);
}

// ---------------------------------------------------------------------
// FX
// ---------------------------------------------------------------------
static void Burst(Vector2 p, int n, Color c, float spMin, float spMax, float life, float size, bool glow = true) {
    for (int i = 0; i < n; i++) {
        float a = RandF(0, 2 * PI);
        float sp = RandF(spMin, spMax);
        G.parts.push_back({p, Vector2Scale(Dir(a), sp), life * RandF(0.6f, 1.2f), life, size * RandF(0.6f, 1.3f), c, glow});
    }
}
static void AddText(Vector2 p, const std::string &s, Color c, float size = 22) { G.texts.push_back({p, 1.0f, s, c, size}); }
static void Shake(float a) { G.shake = std::max(G.shake, a); }

// ---------------------------------------------------------------------
// World generation
// ---------------------------------------------------------------------
static void GenerateWorld() {
    G.props.clear();
    // craters
    for (int i = 0; i < 90; i++) G.props.push_back({0, {RandF(0, WORLD_W), RandF(0, WORLD_H)}, RandF(30, 140), 0});
    // rocks
    for (int i = 0; i < 160; i++) G.props.push_back({1, {RandF(0, WORLD_W), RandF(0, WORLD_H)}, RandF(6, 22), RandF(0, 360)});
    // abandoned equipment: landers, rovers, solar panels
    for (int i = 0; i < 14; i++) G.props.push_back({2 + (i % 3), {RandF(200, WORLD_W - 200), RandF(200, WORLD_H - 200)}, RandF(28, 44), RandF(0, 360)});
    G.stars.clear();
    for (int i = 0; i < 120; i++) G.stars.push_back({Rand01(), Rand01()});
}

static void ResetGame() {
    G.pl = Player();
    G.bullets.clear(); G.enemies.clear(); G.pending.clear(); G.parts.clear(); G.pickups.clear(); G.texts.clear();
    G.score = 0; G.wave = 0; G.kills = 0; G.toSpawn = 0; G.spawnT = 0; G.interT = 1.5f;
    G.combo = 1; G.comboT = 0; G.shake = 0; G.flashRed = 0; G.newBest = false;
    GenerateWorld();
#ifdef MS_DEBUG
    G.wave = 4; G.pl.hp = G.pl.maxHp = 99;
#endif
}

// ---------------------------------------------------------------------
// Enemies
// ---------------------------------------------------------------------
static void SpawnEnemy(int type) {
    Enemy e{};
    e.type = type; e.alive = true; e.t = RandF(0, 10); e.phase = 0;
    // spawn just outside the visible area, inside the world
    float sw = GetScreenWidth() / G.cam.zoom, sh = GetScreenHeight() / G.cam.zoom;
    float dist = sqrtf(sw * sw + sh * sh) * 0.5f + 80;
    for (int tries = 0; tries < 20; tries++) {
        e.p = Vector2Add(G.pl.p, Vector2Scale(Dir(RandF(0, 2 * PI)), dist + RandF(0, 200)));
        if (e.p.x > 40 && e.p.y > 40 && e.p.x < WORLD_W - 40 && e.p.y < WORLD_H - 40) break;
    }
    e.p.x = Clamp(e.p.x, 40, WORLD_W - 40); e.p.y = Clamp(e.p.y, 40, WORLD_H - 40);
    float w = (float)G.wave;
    switch (type) {
        case E_CRAWLER: e.hp = 3 + w * 0.25f; e.r = 17; e.speed = 125 + w * 3; break;
        case E_SWARMER: e.hp = 1; e.r = 11; e.speed = 215 + w * 4; break;
        case E_SPITTER: e.hp = 4 + w * 0.3f; e.r = 19; e.speed = 95; e.cool = RandF(1, 2.5f); break;
        case E_BRUTE:   e.hp = 18 + w * 1.5f; e.r = 33; e.speed = 70; e.cool = 3; break;
        case E_BOSS:    e.hp = 160 + w * 25; e.r = 64; e.speed = 60; e.cool = 2; break;
    }
    e.maxhp = e.hp;
    G.pending.push_back(e);
}

static void StartWave() {
    G.wave++;
    G.bossWave = (G.wave % 5 == 0);
    G.toSpawn = 6 + G.wave * 4;
    G.spawnT = 0.5f;
    if (G.bossWave) { SpawnEnemy(E_BOSS); G.toSpawn = G.wave * 2; }
    Play(SB.wave);
}

static int PickEnemyType() {
    int w = G.wave;
    int r = GetRandomValue(0, 99);
    if (w >= 4 && r < 8 + w) return E_BRUTE;
    if (w >= 2 && r < 30 + w) return E_SPITTER;
    if (w >= 3 && r < 55) return E_SWARMER;
    return E_CRAWLER;
}

static void EnemyShoot(Enemy &e, float ang, float speed, float r, Color c) {
    G.bullets.push_back({e.p, Vector2Scale(Dir(ang), speed), 5.0f, r, 1, true, c});
}

static int ScoreFor(int type) {
    switch (type) { case E_CRAWLER: return 100; case E_SWARMER: return 60; case E_SPITTER: return 200; case E_BRUTE: return 500; default: return 5000; }
}

static void KillEnemy(Enemy &e) {
    e.alive = false;
    G.kills++;
    int pts = (int)(ScoreFor(e.type) * G.combo);
    G.score += pts;
    G.combo = std::min(8.0f, G.combo + 0.1f);
    G.comboT = 2.5f;
    Color c = e.type == E_BOSS ? C_ACCENT : C_ENEMY;
    Burst(e.p, e.type == E_BOSS ? 120 : 18 + (int)e.r, c, 60, e.type == E_BOSS ? 600 : 320, 0.7f, 5);
    Burst(e.p, 10, C_DUST, 20, 140, 1.0f, 8, false);
    AddText(e.p, "+" + std::to_string(pts), C_ACCENT, e.type == E_BOSS ? 44 : 22);
    Shake(e.type == E_BOSS ? 30 : (e.type == E_BRUTE ? 10 : 4));
    PlayBoom();
    // drops
    float chance = e.type == E_BOSS ? 1.0f : (e.type == E_BRUTE ? 0.5f : 0.07f);
    int drops = e.type == E_BOSS ? 3 : 1;
    for (int i = 0; i < drops; i++)
        if (Rand01() < chance) {
            int t = GetRandomValue(0, 3);
            if (G.pl.hp <= 2 && Rand01() < 0.5f) t = P_HEAL;
            G.pickups.push_back({t, Vector2Add(e.p, {RandF(-30, 30), RandF(-30, 30)}), 0});
        }
    if (e.type == E_BOSS) { AddText(Vector2Add(e.p, {0, -60}), "SENTINEL DESTROYED", C_CYAN, 40); }
}

static void DamagePlayer(int dmg, Vector2 from) {
    Player &p = G.pl;
    if (G.screen != S_PLAY || p.inv > 0 || p.dashT > 0) return;
    if (p.shieldT > 0) { p.shieldT = 0; p.inv = 0.6f; Burst(p.p, 30, C_CYAN, 100, 400, 0.5f, 4); PlayHit(); return; }
    p.hp -= dmg;
    p.inv = 1.1f;
    G.flashRed = 0.4f;
    G.combo = 1.0f;
    Shake(14);
    Burst(p.p, 20, C_DANGER, 80, 300, 0.5f, 4);
    p.v = Vector2Add(p.v, Vector2Scale(Vector2Normalize(Vector2Subtract(p.p, from)), 500));
    Play(SB.hurt);
    if (p.hp <= 0) {
        G.screen = S_GAMEOVER;
        G.gameOverT = 0;
        Burst(p.p, 120, C_PLAYER, 60, 500, 1.2f, 6);
        Play(SB.gameover);
        if (G.score > G.best) { G.best = G.score; G.newBest = true; SaveBest(G.best); }
    }
}

// ---------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------
static Enemy *NearestEnemy(Vector2 p, float maxD) {
    Enemy *best = nullptr; float bd = maxD * maxD;
    for (auto &e : G.enemies) {
        if (!e.alive) continue;
        float d = Vector2DistanceSqr(p, e.p);
        if (d < bd) { bd = d; best = &e; }
    }
    return best;
}

static void UpdatePlayer(float dt) {
    Player &p = G.pl;
    p.inv = std::max(0.0f, p.inv - dt);
    p.fireCool -= dt;
    p.dashCool -= dt;
    p.spreadT = std::max(0.0f, p.spreadT - dt);
    p.rapidT = std::max(0.0f, p.rapidT - dt);
    p.shieldT = std::max(0.0f, p.shieldT - dt);

    Vector2 mv = CTL.move;
    if (CTL.dash && p.dashCool <= 0) {
        p.dashDir = Vector2Length(mv) > 0.1f ? Vector2Normalize(mv) : Dir(p.aim);
        p.dashT = 0.17f; p.dashCool = 1.0f;
        Play(SB.dash);
    }
    if (p.dashT > 0) {
        p.dashT -= dt;
        p.v = Vector2Scale(p.dashDir, 1050);
        if (GetRandomValue(0, 1)) G.parts.push_back({p.p, {RandF(-30, 30), RandF(-30, 30)}, 0.3f, 0.3f, 14, Fade(C_CYAN, 0.6f), true});
    } else {
        Vector2 target = Vector2Scale(mv, 320);
        p.v = Vector2Lerp(p.v, target, Clamp(dt * 12, 0, 1));
    }
    p.p = Vector2Add(p.p, Vector2Scale(p.v, dt));
    p.p.x = Clamp(p.p.x, p.r, WORLD_W - p.r);
    p.p.y = Clamp(p.p.y, p.r, WORLD_H - p.r);

    // aim
    bool firing = CTL.fire;
    if (CTL.autoAim || (!CTL.hasAim)) {
        Enemy *t = NearestEnemy(p.p, 750);
        if (t) {
            // lead the target slightly
            Vector2 lead = Vector2Add(t->p, Vector2Scale(t->v, Vector2Distance(p.p, t->p) / 900.0f));
            p.aim = atan2f(lead.y - p.p.y, lead.x - p.p.x);
        } else if (Vector2Length(mv) > 0.1f) p.aim = atan2f(mv.y, mv.x);
    } else {
        p.aim = atan2f(CTL.aimWorld.y - p.p.y, CTL.aimWorld.x - p.p.x);
    }

    if (firing && p.fireCool <= 0) {
        float rate = p.rapidT > 0 ? 0.065f : 0.13f;
        p.fireCool = rate;
        int n = p.spreadT > 0 ? 5 : 1;
        float spread = 0.13f;
        for (int i = 0; i < n; i++) {
            float a = p.aim + (i - (n - 1) / 2.0f) * spread + RandF(-0.03f, 0.03f);
            Vector2 muzzle = Vector2Add(p.p, Vector2Scale(Dir(p.aim), 26));
            G.bullets.push_back({muzzle, Vector2Scale(Dir(a), 950), 0.9f, 5, 1.0f, false, p.rapidT > 0 ? C_ACCENT : C_CYAN});
        }
        G.parts.push_back({Vector2Add(p.p, Vector2Scale(Dir(p.aim), 28)), {0, 0}, 0.06f, 0.06f, 12, C_CYAN, true});
        p.v = Vector2Subtract(p.v, Vector2Scale(Dir(p.aim), 18));  // tiny recoil
        Shake(1.5f);
        PlayShoot();
    }
}

static void UpdateEnemies(float dt) {
    Player &pl = G.pl;
    for (auto &e : G.enemies) {
        if (!e.alive) continue;
        e.t += dt; e.flash = std::max(0.0f, e.flash - dt); e.cool -= dt;
        Vector2 to = Vector2Subtract(pl.p, e.p);
        float d = Vector2Length(to);
        Vector2 dir = d > 0.01f ? Vector2Scale(to, 1.0f / d) : Vector2{1, 0};
        Vector2 want{0, 0};
        switch (e.type) {
            case E_CRAWLER: want = Vector2Scale(dir, e.speed); break;
            case E_SWARMER: {
                Vector2 side{-dir.y, dir.x};
                want = Vector2Add(Vector2Scale(dir, e.speed), Vector2Scale(side, sinf(e.t * 7) * e.speed * 0.7f));
            } break;
            case E_SPITTER: {
                float pref = 330;
                Vector2 side{-dir.y, dir.x};
                want = Vector2Add(Vector2Scale(dir, (d - pref) * 0.8f), Vector2Scale(side, 60));
                want = Vector2ClampValue(want, 0, e.speed);
                if (e.cool <= 0 && d < 700) {
                    e.cool = std::max(1.0f, 2.4f - G.wave * 0.08f);
                    float a = atan2f(dir.y, dir.x);
                    EnemyShoot(e, a, 280, 8, C_ENEMY);
                    if (G.wave >= 6) { EnemyShoot(e, a + 0.25f, 280, 8, C_ENEMY); EnemyShoot(e, a - 0.25f, 280, 8, C_ENEMY); }
                    Play(SB.spit);
                }
            } break;
            case E_BRUTE: {
                if (e.phase == 0) {
                    want = Vector2Scale(dir, e.speed);
                    if (e.cool <= 0 && d < 500) { e.phase = 1; e.cool = 0.7f; }  // wind-up
                } else if (e.phase == 1) {
                    want = {0, 0};
                    e.ang = atan2f(dir.y, dir.x);
                    if (e.cool <= 0) { e.phase = 2; e.cool = 0.55f; Shake(4); }
                } else {
                    want = Vector2Scale(Dir(e.ang), 620);
                    if (e.cool <= 0) { e.phase = 0; e.cool = RandF(2.5f, 4); }
                }
            } break;
            case E_BOSS: {
                float hpk = e.hp / e.maxhp;
                Vector2 side{-dir.y, dir.x};
                want = Vector2Add(Vector2Scale(dir, (d - 380) * 0.5f), Vector2Scale(side, 90));
                want = Vector2ClampValue(want, 0, e.speed + (1 - hpk) * 80);
                e.ang += dt * (1.2f + (1 - hpk) * 2);
                if (e.cool <= 0) {
                    e.phase = (e.phase + 1) % 3;
                    if (e.phase == 0) {  // radial burst
                        int n = 18 + (int)((1 - hpk) * 14);
                        for (int i = 0; i < n; i++) EnemyShoot(e, e.ang + i * 2 * PI / n, 230, 10, C_ACCENT);
                        e.cool = 1.6f;
                    } else if (e.phase == 1) {  // aimed spray
                        float a = atan2f(dir.y, dir.x);
                        for (int i = -3; i <= 3; i++) EnemyShoot(e, a + i * 0.12f, 360, 9, C_DANGER);
                        e.cool = 1.2f;
                    } else {  // summon
                        for (int i = 0; i < 3 + G.wave / 5; i++) {
                            SpawnEnemy(E_SWARMER);
                            G.pending.back().p = Vector2Add(e.p, Vector2Scale(Dir(RandF(0, 2 * PI)), 80));
                        }
                        e.cool = 2.0f;
                    }
                    Play(SB.spit);
                }
            } break;
        }
        e.v = Vector2Lerp(e.v, want, Clamp(dt * (e.type == E_BRUTE ? 8 : 4), 0, 1));
        e.p = Vector2Add(e.p, Vector2Scale(e.v, dt));
        e.p.x = Clamp(e.p.x, e.r, WORLD_W - e.r); e.p.y = Clamp(e.p.y, e.r, WORLD_H - e.r);
        // touch damage
        if (d < e.r + pl.r - 4) DamagePlayer(e.type == E_BOSS || e.type == E_BRUTE ? 2 : 1, e.p);
    }
    // separation (cheap)
    for (size_t i = 0; i < G.enemies.size(); i++) {
        Enemy &a = G.enemies[i]; if (!a.alive) continue;
        for (size_t j = i + 1; j < G.enemies.size(); j++) {
            Enemy &b = G.enemies[j]; if (!b.alive) continue;
            Vector2 dd = Vector2Subtract(b.p, a.p);
            float rr = a.r + b.r, L2 = Vector2LengthSqr(dd);
            if (L2 < rr * rr && L2 > 0.001f) {
                float L = sqrtf(L2), push = (rr - L) * 0.5f;
                Vector2 n = Vector2Scale(dd, 1.0f / L);
                float wa = a.type == E_BOSS ? 0.0f : 1.0f, wb = b.type == E_BOSS ? 0.0f : 1.0f;
                if (wa + wb == 0) continue;
                a.p = Vector2Subtract(a.p, Vector2Scale(n, push * 2 * wa / (wa + wb)));
                b.p = Vector2Add(b.p, Vector2Scale(n, push * 2 * wb / (wa + wb)));
            }
        }
    }
}

static void UpdateBullets(float dt) {
    for (auto &b : G.bullets) {
        b.life -= dt;
        b.p = Vector2Add(b.p, Vector2Scale(b.v, dt));
        if (b.p.x < 0 || b.p.y < 0 || b.p.x > WORLD_W || b.p.y > WORLD_H) b.life = 0;
        if (b.life <= 0) continue;
        if (b.enemy) {
            if (G.screen == S_PLAY && Vector2Distance(b.p, G.pl.p) < b.r + G.pl.r - 6) {
                b.life = 0;
                DamagePlayer(1, b.p);
            }
        } else {
            for (auto &e : G.enemies) {
                if (!e.alive) continue;
                if (Vector2DistanceSqr(b.p, e.p) < (b.r + e.r) * (b.r + e.r)) {
                    b.life = 0;
                    e.hp -= b.dmg; e.flash = 0.08f;
                    e.v = Vector2Add(e.v, Vector2Scale(Vector2Normalize(b.v), e.type >= E_BRUTE ? 10 : 90));
                    Burst(b.p, 4, b.c, 60, 220, 0.25f, 3);
                    PlayHit();
                    if (e.hp <= 0) KillEnemy(e);
                    break;
                }
            }
        }
    }
    G.bullets.erase(std::remove_if(G.bullets.begin(), G.bullets.end(), [](const Bullet &b) { return b.life <= 0; }), G.bullets.end());
}

static const char *PickupName(int t) {
    switch (t) { case P_HEAL: return "+HULL"; case P_SPREAD: return "SPREAD SHOT"; case P_RAPID: return "RAPID FIRE"; default: return "SHIELD"; }
}
static Color PickupColor(int t) {
    switch (t) { case P_HEAL: return {120, 255, 140, 255}; case P_SPREAD: return C_ACCENT; case P_RAPID: return {255, 220, 80, 255}; default: return C_CYAN; }
}

static void UpdatePickups(float dt) {
    Player &p = G.pl;
    for (auto &k : G.pickups) {
        k.t += dt;
        float d = Vector2Distance(k.p, p.p);
        if (d < 140) k.p = Vector2MoveTowards(k.p, p.p, dt * (500 - d * 2));  // magnet
        if (d < p.r + 16) {
            k.t = 999;
            switch (k.type) {
                case P_HEAL: p.hp = std::min(p.maxHp, p.hp + 2); break;
                case P_SPREAD: p.spreadT = 10; break;
                case P_RAPID: p.rapidT = 10; break;
                case P_SHIELD: p.shieldT = 15; break;
            }
            AddText(Vector2Add(p.p, {0, -40}), PickupName(k.type), PickupColor(k.type), 26);
            Burst(k.p, 16, PickupColor(k.type), 40, 200, 0.5f, 4);
            Play(SB.pickup);
        }
    }
    G.pickups.erase(std::remove_if(G.pickups.begin(), G.pickups.end(), [](const Pickup &k) { return k.t > 14; }), G.pickups.end());
}

static void UpdateFX(float dt) {
    for (auto &q : G.parts) {
        q.life -= dt;
        q.p = Vector2Add(q.p, Vector2Scale(q.v, dt));
        q.v = Vector2Scale(q.v, 1.0f - Clamp(dt * 3.0f, 0, 1));
    }
    G.parts.erase(std::remove_if(G.parts.begin(), G.parts.end(), [](const Particle &q) { return q.life <= 0; }), G.parts.end());
    if (G.parts.size() > 2500) G.parts.erase(G.parts.begin(), G.parts.begin() + (G.parts.size() - 2500));
    for (auto &t : G.texts) { t.life -= dt * 0.9f; t.p.y -= dt * 50; }
    G.texts.erase(std::remove_if(G.texts.begin(), G.texts.end(), [](const FloatText &t) { return t.life <= 0; }), G.texts.end());
    G.shake = std::max(0.0f, G.shake - dt * 60);
    G.flashRed = std::max(0.0f, G.flashRed - dt);
}

static void FlushPending() {
    for (auto &e : G.pending) G.enemies.push_back(e);
    G.pending.clear();
}

static void UpdateWaves(float dt) {
    FlushPending();
    G.enemies.erase(std::remove_if(G.enemies.begin(), G.enemies.end(), [](const Enemy &e) { return !e.alive; }), G.enemies.end());
    if (G.interT > 0) {
        G.interT -= dt;
        if (G.interT <= 0) StartWave();
        return;
    }
    if (G.toSpawn > 0) {
        G.spawnT -= dt;
        if (G.spawnT <= 0) {
            int group = std::min(G.toSpawn, 1 + G.wave / 3);
            for (int i = 0; i < group; i++) SpawnEnemy(PickEnemyType());
            G.toSpawn -= group;
            G.spawnT = std::max(0.25f, 1.1f - G.wave * 0.06f);
        }
    } else if (G.enemies.empty() && G.pending.empty()) {
        G.interT = 3.0f;
        G.score += G.wave * 250;
        AddText(Vector2Add(G.pl.p, {0, -70}), "WAVE CLEAR +" + std::to_string(G.wave * 250), C_CYAN, 34);
        if (G.pl.hp < G.pl.maxHp) G.pl.hp++;
    }
}

static void UpdateCamera(float dt) {
    float u = std::min(GetScreenWidth(), GetScreenHeight()) / 720.0f;
    G.cam.zoom = u;
    G.cam.offset = {GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f};
    Vector2 look = Vector2Add(G.pl.p, Vector2Scale(Dir(G.pl.aim), CTL.fire ? 60.0f : 20.0f));
    G.cam.target = Vector2Lerp(G.cam.target, look, Clamp(dt * 6, 0, 1));
    float hw = GetScreenWidth() / u / 2, hh = GetScreenHeight() / u / 2;
    if (hw * 2 < WORLD_W) G.cam.target.x = Clamp(G.cam.target.x, hw, WORLD_W - hw);
    if (hh * 2 < WORLD_H) G.cam.target.y = Clamp(G.cam.target.y, hh, WORLD_H - hh);
}

// ---------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------
static void DrawTextC(const char *s, float x, float y, float size, Color c) {
    int w = MeasureText(s, (int)size);
    DrawText(s, (int)(x - w / 2), (int)y, (int)size, c);
}
static void DrawTextShadowC(const char *s, float x, float y, float size, Color c) {
    DrawTextC(s, x + size * 0.06f, y + size * 0.06f, size, Fade(BLACK, 0.6f * c.a / 255.0f));
    DrawTextC(s, x, y, size, c);
}

static void DrawGround() {
    Vector2 tl = GetScreenToWorld2D({0, 0}, G.cam);
    Vector2 br = GetScreenToWorld2D({(float)GetScreenWidth(), (float)GetScreenHeight()}, G.cam);
    DrawRectangleV({0, 0}, {WORLD_W, WORLD_H}, C_GROUND);
    // tile pattern for texture
    const float T = 160;
    int x0 = std::max(0, (int)(tl.x / T) - 1), x1 = std::min((int)(WORLD_W / T), (int)(br.x / T) + 1);
    int y0 = std::max(0, (int)(tl.y / T) - 1), y1 = std::min((int)(WORLD_H / T), (int)(br.y / T) + 1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            unsigned h = (unsigned)(x * 73856093) ^ (unsigned)(y * 19349663);
            if (h % 3 == 0) DrawCircleV({x * T + (h % 53), y * T + (h % 61)}, T * 0.85f, Fade(C_GROUND2, 0.55f));
            else if (h % 5 == 1) DrawCircleV({x * T + (h % 41), y * T + (h % 37)}, T * 0.6f, Fade({140, 66, 40, 255}, 0.35f));
            float px = x * T + (h % 97) * 1.4f, py = y * T + ((h / 97) % 89) * 1.6f;
            DrawCircleV({px, py}, 2 + (h % 4), Fade(C_DUST, 0.35f));
        }
    for (auto &pr : G.props) {
        if (pr.p.x < tl.x - 200 || pr.p.x > br.x + 200 || pr.p.y < tl.y - 200 || pr.p.y > br.y + 200) continue;
        switch (pr.kind) {
            case 0:  // crater
                DrawCircleV(pr.p, pr.r, Fade({70, 28, 18, 255}, 0.55f));
                DrawCircleV(Vector2Add(pr.p, {pr.r * 0.12f, pr.r * 0.12f}), pr.r * 0.82f, Fade({140, 64, 40, 255}, 0.5f));
                DrawRing(pr.p, pr.r * 0.95f, pr.r * 1.08f, 200, 340, 24, Fade(C_DUST, 0.3f));
                break;
            case 1:  // rock
                DrawPoly(Vector2Add(pr.p, {3, 4}), 6, pr.r, pr.ang, Fade(BLACK, 0.3f));
                DrawPoly(pr.p, 6, pr.r, pr.ang, {88, 40, 30, 255});
                DrawPoly(Vector2Add(pr.p, {-pr.r * 0.2f, -pr.r * 0.25f}), 6, pr.r * 0.5f, pr.ang, {150, 80, 55, 255});
                break;
            case 2: {  // abandoned lander
                DrawCircleV(Vector2Add(pr.p, {6, 8}), pr.r * 1.3f, Fade(BLACK, 0.25f));
                for (int i = 0; i < 4; i++) {
                    Vector2 leg = Vector2Add(pr.p, Vector2Scale(Dir(DEG2RAD * (pr.ang + 45 + i * 90)), pr.r * 1.3f));
                    DrawLineEx(pr.p, leg, 4, {150, 150, 160, 255});
                    DrawCircleV(leg, 6, {120, 120, 130, 255});
                }
                DrawPoly(pr.p, 8, pr.r * 0.8f, pr.ang, {200, 170, 60, 255});  // gold foil
                DrawPoly(pr.p, 8, pr.r * 0.5f, pr.ang, {170, 170, 180, 255});
            } break;
            case 3: {  // dead rover
                Rectangle r{pr.p.x, pr.p.y, pr.r * 2.2f, pr.r * 1.4f};
                DrawRectanglePro({r.x + 6, r.y + 8, r.width, r.height}, {r.width / 2, r.height / 2}, pr.ang, Fade(BLACK, 0.25f));
                DrawRectanglePro(r, {r.width / 2, r.height / 2}, pr.ang, {190, 190, 200, 255});
                for (int i = -1; i <= 1; i++)
                    for (int s = -1; s <= 1; s += 2) {
                        Vector2 off = Vector2Rotate({i * pr.r * 0.85f, s * pr.r * 0.85f}, pr.ang * DEG2RAD);
                        DrawCircleV(Vector2Add(pr.p, off), pr.r * 0.28f, {60, 60, 65, 255});
                    }
                DrawCircleV(Vector2Add(pr.p, Vector2Rotate({pr.r * 0.6f, 0}, pr.ang * DEG2RAD)), pr.r * 0.25f, {90, 120, 170, 255});
            } break;
            case 4: {  // broken solar array
                for (int i = 0; i < 3; i++) {
                    Vector2 c = Vector2Add(pr.p, Vector2Rotate({(i - 1) * pr.r * 0.9f, 0}, pr.ang * DEG2RAD));
                    DrawRectanglePro({c.x, c.y, pr.r * 0.8f, pr.r * 1.6f}, {pr.r * 0.4f, pr.r * 0.8f}, pr.ang + (i == 2 ? 25 : 0), {40, 60, 120, 255});
                    DrawRectanglePro({c.x, c.y, pr.r * 0.8f, 3}, {pr.r * 0.4f, 1.5f}, pr.ang + (i == 2 ? 25 : 0), {120, 140, 200, 255});
                }
            } break;
        }
    }
    // world border
    DrawRectangleLinesEx({-6, -6, WORLD_W + 12, WORLD_H + 12}, 12, Fade(C_DANGER, 0.5f + 0.3f * sinf(G.time * 3)));
}

static void DrawPlayer() {
    Player &p = G.pl;
    if (G.screen == S_GAMEOVER) return;
    bool blink = p.inv > 0 && fmodf(p.inv, 0.16f) < 0.08f;
    if (blink) return;
    DrawEllipse((int)p.p.x + 4, (int)p.p.y + 10, p.r * 1.1f, p.r * 0.6f, Fade(BLACK, 0.3f));
    // gun
    Vector2 g0 = Vector2Add(p.p, Vector2Scale(Dir(p.aim), 6));
    Vector2 g1 = Vector2Add(p.p, Vector2Scale(Dir(p.aim), 30));
    DrawLineEx(g0, g1, 9, {60, 70, 85, 255});
    DrawLineEx(Vector2Add(g0, Vector2Scale(Dir(p.aim), 10)), g1, 4, C_CYAN);
    // body (suit + backpack)
    Vector2 back = Vector2Subtract(p.p, Vector2Scale(Dir(p.aim), 13));
    DrawRectanglePro({back.x, back.y, 16, 24}, {8, 12}, p.aim * RAD2DEG, {180, 180, 190, 255});
    DrawCircleV(p.p, p.r, C_PLAYER);
    DrawCircleLinesV(p.p, p.r, {120, 130, 150, 255});
    // visor
    Vector2 vz = Vector2Add(p.p, Vector2Scale(Dir(p.aim), 6));
    DrawCircleV(vz, p.r * 0.55f, {255, 160, 40, 255});
    DrawCircleV(Vector2Add(vz, {-3, -3}), p.r * 0.18f, Fade(WHITE, 0.8f));
    if (p.shieldT > 0) {
        float a = 0.35f + 0.15f * sinf(G.time * 8);
        if (p.shieldT < 3 && fmodf(G.time, 0.3f) < 0.15f) a *= 0.3f;
        DrawCircleV(p.p, p.r + 14, Fade(C_CYAN, a * 0.4f));
        DrawRing(p.p, p.r + 12, p.r + 15, 0, 360, 40, Fade(C_CYAN, a + 0.3f));
    }
}

static void DrawEnemy(const Enemy &e) {
    Color base = C_ENEMY;
    float wob = sinf(e.t * 10) * 0.08f;
    DrawEllipse((int)e.p.x + 4, (int)(e.p.y + e.r * 0.6f), e.r * 1.1f, e.r * 0.5f, Fade(BLACK, 0.3f));
    Vector2 to = Vector2Subtract(G.pl.p, e.p);
    float face = atan2f(to.y, to.x);
    switch (e.type) {
        case E_CRAWLER: {
            base = {150, 220, 90, 255};
            for (int i = 0; i < 6; i++) {  // legs
                float a = face + PI / 2 + (i % 3 - 1) * 0.6f + (i < 3 ? 0 : PI) + sinf(e.t * 18 + i) * 0.25f;
                DrawLineEx(e.p, Vector2Add(e.p, Vector2Scale(Dir(a), e.r * 1.5f)), 3, {70, 110, 40, 255});
            }
            DrawCircleV(e.p, e.r * (1 + wob), base);
            DrawCircleV(Vector2Add(e.p, Vector2Scale(Dir(face), e.r * 0.5f)), e.r * 0.45f, {90, 150, 50, 255});
            DrawCircleV(Vector2Add(e.p, Vector2Scale(Dir(face + 0.4f), e.r * 0.7f)), 3, C_DANGER);
            DrawCircleV(Vector2Add(e.p, Vector2Scale(Dir(face - 0.4f), e.r * 0.7f)), 3, C_DANGER);
        } break;
        case E_SWARMER: {
            base = {255, 230, 90, 255};
            DrawCircleV(e.p, e.r + 6, Fade(base, 0.2f));
            DrawPoly(e.p, 3, e.r * 1.3f, face * RAD2DEG, base);
            DrawCircleV(e.p, 3, C_DANGER);
        } break;
        case E_SPITTER: {
            base = {170, 110, 230, 255};
            DrawCircleV(e.p, e.r * (1.05f + wob), base);
            DrawCircleV(e.p, e.r * 0.6f, {110, 60, 170, 255});
            float glow = e.cool < 0.5f ? 1.0f - e.cool * 2 : 0.0f;
            DrawCircleV(Vector2Add(e.p, Vector2Scale(Dir(face), e.r * 0.4f)), e.r * 0.35f, LerpColor({220, 255, 150, 255}, WHITE, glow));
            if (glow > 0) DrawCircleV(e.p, e.r * (1.3f + glow * 0.3f), Fade(C_ENEMY, glow * 0.25f));
        } break;
        case E_BRUTE: {
            base = {200, 90, 60, 255};
            if (e.phase == 1) base = LerpColor(base, WHITE, 0.5f + 0.5f * sinf(e.t * 40));
            float a = e.phase == 2 ? e.ang : face;
            DrawPoly(e.p, 6, e.r * 1.08f, a * RAD2DEG, {120, 50, 35, 255});
            DrawPoly(e.p, 6, e.r * 0.92f, a * RAD2DEG, base);
            DrawCircleV(Vector2Add(e.p, Vector2Scale(Dir(a), e.r * 0.5f)), e.r * 0.25f, C_ACCENT);
            if (e.phase == 1) DrawLineEx(e.p, Vector2Add(e.p, Vector2Scale(Dir(e.ang), 500)), 2, Fade(C_DANGER, 0.5f));
        } break;
        case E_BOSS: {
            base = {255, 120, 60, 255};
            DrawCircleV(e.p, e.r * 1.35f, Fade(C_ACCENT, 0.12f + 0.06f * sinf(e.t * 4)));
            for (int i = 0; i < 6; i++) {
                Vector2 q = Vector2Add(e.p, Vector2Scale(Dir(e.ang + i * PI / 3), e.r * 0.95f));
                DrawCircleV(q, e.r * 0.25f, {90, 90, 100, 255});
                DrawCircleV(q, e.r * 0.12f, C_DANGER);
            }
            DrawPoly(e.p, 8, e.r, -e.ang * RAD2DEG * 0.5f, {70, 70, 85, 255});
            DrawPoly(e.p, 8, e.r * 0.75f, -e.ang * RAD2DEG * 0.5f, {130, 130, 145, 255});
            DrawCircleV(e.p, e.r * 0.42f, base);
            DrawCircleV(Vector2Add(e.p, Vector2Scale(Dir(face), e.r * 0.15f)), e.r * 0.2f, WHITE);
        } break;
    }
    if (e.flash > 0) DrawCircleV(e.p, e.r * 1.05f, Fade(WHITE, 0.75f));
    if (e.type == E_BRUTE && e.hp < e.maxhp) {
        DrawRectangle((int)(e.p.x - e.r), (int)(e.p.y - e.r - 14), (int)(2 * e.r), 5, Fade(BLACK, 0.5f));
        DrawRectangle((int)(e.p.x - e.r), (int)(e.p.y - e.r - 14), (int)(2 * e.r * e.hp / e.maxhp), 5, C_DANGER);
    }
}

static void DrawWorld() {
    float sx = G.shake > 0 ? RandF(-G.shake, G.shake) * 0.5f : 0;
    float sy = G.shake > 0 ? RandF(-G.shake, G.shake) * 0.5f : 0;
    Camera2D c = G.cam;
    c.target.x += sx; c.target.y += sy;
    G.cam = c;
    BeginMode2D(c);
    DrawGround();
    for (auto &k : G.pickups) {
        float bob = sinf(k.t * 5) * 4;
        bool vis = k.t < 11 || fmodf(k.t, 0.3f) < 0.18f;
        if (!vis) continue;
        Color col = PickupColor(k.type);
        DrawCircleV({k.p.x, k.p.y + bob}, 22, Fade(col, 0.2f));
        DrawPoly({k.p.x, k.p.y + bob}, 4, 14, G.time * 90, col);
        const char *ic = k.type == P_HEAL ? "+" : k.type == P_SPREAD ? "S" : k.type == P_RAPID ? "R" : "O";
        DrawTextC(ic, k.p.x, k.p.y + bob - 9, 18, BLACK);
    }
    for (auto &e : G.enemies) if (e.alive) DrawEnemy(e);
    DrawPlayer();
    BeginBlendMode(BLEND_ADDITIVE);
    for (auto &b : G.bullets) {
        Vector2 tail = Vector2Subtract(b.p, Vector2Scale(Vector2Normalize(b.v), b.enemy ? 10.0f : 22.0f));
        DrawLineEx(tail, b.p, b.r * 1.6f, Fade(b.c, 0.5f));
        DrawCircleV(b.p, b.r * 1.8f, Fade(b.c, 0.25f));
        DrawCircleV(b.p, b.r * 0.8f, WHITE);
    }
    for (auto &q : G.parts) if (q.glow) DrawCircleV(q.p, q.size * (q.life / q.maxlife), Fade2(q.c, q.life / q.maxlife));
    EndBlendMode();
    for (auto &b : G.bullets) {  // solid core so shots read clearly on the red ground
        DrawCircleV(b.p, b.r * 1.15f, b.c);
        DrawCircleV(b.p, b.r * 0.55f, WHITE);
    }
    for (auto &q : G.parts) if (!q.glow) DrawCircleV(q.p, q.size * (0.5f + q.life / q.maxlife), Fade2(q.c, 0.5f * q.life / q.maxlife));
    for (auto &t : G.texts) DrawTextShadowC(t.s.c_str(), t.p.x, t.p.y, t.size, Fade2(t.c, t.life * 1.5f));
    EndMode2D();
    G.cam.target.x -= sx; G.cam.target.y -= sy;
}

static void DrawAtmosphere() {
    int w = GetScreenWidth(), h = GetScreenHeight();
    // dusty vignette
    DrawRectangleGradientV(0, 0, w, h / 4, Fade({60, 20, 10, 255}, 0.35f), BLANK);
    DrawRectangleGradientV(0, h - h / 4, w, h / 4, BLANK, Fade({60, 20, 10, 255}, 0.35f));
    DrawRectangleGradientH(0, 0, w / 6, h, Fade({60, 20, 10, 255}, 0.3f), BLANK);
    DrawRectangleGradientH(w - w / 6, 0, w / 6, h, BLANK, Fade({60, 20, 10, 255}, 0.3f));
    if (G.flashRed > 0) DrawRectangle(0, 0, w, h, Fade(C_DANGER, G.flashRed * 0.5f));
}

static void DrawHeart(float x, float y, float s, Color c) {
    DrawCircleV({x - s * 0.25f, y}, s * 0.3f, c);
    DrawCircleV({x + s * 0.25f, y}, s * 0.3f, c);
    DrawTriangle({x - s * 0.53f, y + s * 0.08f}, {x, y + s * 0.65f}, {x + s * 0.53f, y + s * 0.08f}, c);
}

static void DrawHUD() {
    float u = UIS();
    int w = GetScreenWidth();
    Player &p = G.pl;
    // hull
    for (int i = 0; i < p.maxHp; i++) DrawHeart(34 * u + i * 34 * u, 34 * u, 28 * u, i < p.hp ? C_DANGER : Fade(WHITE, 0.2f));
    // score
    std::string sc = std::to_string(G.score);
    DrawText(sc.c_str(), (int)(22 * u), (int)(62 * u), (int)(38 * u), WHITE);
    std::string bt = "BEST " + std::to_string(std::max(G.best, G.score));
    DrawText(bt.c_str(), (int)(22 * u), (int)(102 * u), (int)(18 * u), Fade(WHITE, 0.6f));
    if (G.combo > 1.05f) {
        char buf[32]; snprintf(buf, sizeof buf, "x%.1f", G.combo);
        DrawText(buf, (int)(22 * u + MeasureText(sc.c_str(), (int)(38 * u)) + 12 * u), (int)(70 * u), (int)(28 * u), C_ACCENT);
    }
    // wave
    std::string wv = "WAVE " + std::to_string(std::max(1, G.wave));
    DrawTextC(wv.c_str(), w / 2.0f, 16 * u, 30 * u, WHITE);
    std::string left = std::to_string(G.toSpawn + (int)G.enemies.size()) + " hostiles";
    DrawTextC(left.c_str(), w / 2.0f, 50 * u, 18 * u, Fade(WHITE, 0.6f));
    // power-ups
    float y = 130 * u;
    auto bar = [&](const char *name, float t, float max, Color c) {
        if (t <= 0) return;
        DrawText(name, (int)(22 * u), (int)y, (int)(16 * u), c);
        DrawRectangle((int)(22 * u), (int)(y + 19 * u), (int)(130 * u), (int)(5 * u), Fade(WHITE, 0.2f));
        DrawRectangle((int)(22 * u), (int)(y + 19 * u), (int)(130 * u * t / max), (int)(5 * u), c);
        y += 34 * u;
    };
    bar("SPREAD", p.spreadT, 10, C_ACCENT);
    bar("RAPID", p.rapidT, 10, {255, 220, 80, 255});
    bar("SHIELD", p.shieldT, 15, C_CYAN);
    // boss bar
    for (auto &e : G.enemies)
        if (e.alive && e.type == E_BOSS) {
            float bw = std::min(w * 0.6f, 700 * u);
            float bx = w / 2.0f - bw / 2, by = 78 * u;
            DrawTextC("SENTINEL", w / 2.0f, by - 2 * u, 16 * u, C_ACCENT);
            DrawRectangle((int)bx, (int)(by + 18 * u), (int)bw, (int)(10 * u), Fade(BLACK, 0.5f));
            DrawRectangle((int)bx, (int)(by + 18 * u), (int)(bw * e.hp / e.maxhp), (int)(10 * u), C_ACCENT);
        }
    // wave banner
    if (G.interT > 0 && G.screen == S_PLAY) {
        float a = std::min(1.0f, G.interT);
        std::string s = "WAVE " + std::to_string(G.wave + 1);
        DrawTextShadowC(s.c_str(), w / 2.0f, GetScreenHeight() * 0.32f, 64 * u, Fade(WHITE, a));
        if ((G.wave + 1) % 5 == 0) DrawTextShadowC("WARNING: SENTINEL APPROACHING", w / 2.0f, GetScreenHeight() * 0.32f + 74 * u, 26 * u, Fade(C_DANGER, a));
        else DrawTextShadowC("get ready", w / 2.0f, GetScreenHeight() * 0.32f + 74 * u, 24 * u, Fade(C_ACCENT, a));
    }
    // dash cooldown (desktop)
    if (!G.touchMode) {
        float k = Clamp(1.0f - p.dashCool / 1.0f, 0, 1);
        DrawText("DASH", (int)(22 * u), (int)(GetScreenHeight() - 40 * u), (int)(16 * u), k >= 1 ? C_CYAN : Fade(WHITE, 0.4f));
        DrawRectangle((int)(70 * u), (int)(GetScreenHeight() - 36 * u), (int)(80 * u * k), (int)(8 * u), k >= 1 ? C_CYAN : Fade(WHITE, 0.4f));
    }
}

static void DrawTouchControls() {
    if (!G.touchMode || G.screen != S_PLAY) return;
    float u = UIS();
    // joystick
    if (CTL.joyId >= 0) {
        DrawCircleV(CTL.joyBase, 85 * u, Fade(WHITE, 0.12f));
        DrawRing(CTL.joyBase, 83 * u, 87 * u, 0, 360, 40, Fade(WHITE, 0.35f));
        Vector2 d = Vector2Subtract(CTL.joyKnob, CTL.joyBase);
        d = Vector2ClampValue(d, 0, 85 * u);
        DrawCircleV(Vector2Add(CTL.joyBase, d), 38 * u, Fade(WHITE, 0.45f));
    } else {
        Vector2 hint{150 * u, GetScreenHeight() - 150 * u};
        DrawCircleV(hint, 85 * u, Fade(WHITE, 0.07f));
        DrawRing(hint, 83 * u, 87 * u, 0, 360, 40, Fade(WHITE, 0.2f));
        DrawCircleV(hint, 38 * u, Fade(WHITE, 0.18f));
        DrawTextC("MOVE", hint.x, hint.y - 9 * u, 18 * u, Fade(WHITE, 0.4f));
    }
    // fire
    Vector2 f = FireBtnPos();
    bool fdown = CTL.fireId >= 0;
    DrawCircleV(f, FireBtnR(), Fade(C_DANGER, fdown ? 0.45f : 0.22f));
    DrawRing(f, FireBtnR() - 3 * u, FireBtnR() + 2 * u, 0, 360, 48, Fade(WHITE, 0.45f));
    DrawTextC("FIRE", f.x, f.y - 12 * u, 26 * u, Fade(WHITE, 0.85f));
    DrawTextC("auto-aim", f.x, f.y + 16 * u, 14 * u, Fade(WHITE, 0.6f));
    // dash
    Vector2 d = DashBtnPos();
    float k = Clamp(1.0f - G.pl.dashCool, 0, 1);
    DrawCircleV(d, DashBtnR(), Fade(C_CYAN, k >= 1 ? 0.3f : 0.1f));
    DrawRing(d, DashBtnR() - 3 * u, DashBtnR() + 2 * u, -90, -90 + 360 * k, 40, Fade(C_CYAN, 0.8f));
    DrawTextC("DASH", d.x, d.y - 10 * u, 20 * u, Fade(WHITE, 0.85f));
    // pause
    Vector2 pz = PauseBtnPos();
    DrawCircleV(pz, 30 * u, Fade(BLACK, 0.35f));
    DrawRectangle((int)(pz.x - 10 * u), (int)(pz.y - 12 * u), (int)(7 * u), (int)(24 * u), WHITE);
    DrawRectangle((int)(pz.x + 3 * u), (int)(pz.y - 12 * u), (int)(7 * u), (int)(24 * u), WHITE);
}

static void DrawPlanet(float cx, float cy, float r) {
    DrawCircleV({cx, cy}, r * 1.15f, Fade(C_ACCENT, 0.08f));
    DrawCircleV({cx, cy}, r, {190, 80, 45, 255});
    DrawCircleV({cx - r * 0.25f, cy - r * 0.2f}, r * 0.75f, {215, 105, 60, 255});
    DrawCircleV({cx + r * 0.35f, cy + r * 0.1f}, r * 0.16f, {160, 65, 40, 255});
    DrawCircleV({cx - r * 0.3f, cy + r * 0.45f}, r * 0.1f, {160, 65, 40, 255});
    DrawCircleV({cx - r * 0.45f, cy - r * 0.35f}, r * 0.08f, {170, 75, 45, 255});
    DrawRing({cx, cy}, r * 0.98f, r * 1.02f, 0, 360, 64, Fade({255, 200, 150, 255}, 0.3f));
}

static void DrawStars() {
    int w = GetScreenWidth(), h = GetScreenHeight();
    for (size_t i = 0; i < G.stars.size(); i++) {
        float tw = 0.5f + 0.5f * sinf(G.time * 2 + i);
        float x = fmodf(G.stars[i].x * w + G.time * (5 + i % 7), (float)w);
        DrawCircleV({x, G.stars[i].y * h}, 1 + (i % 3) * 0.6f, Fade(WHITE, 0.3f + 0.5f * tw));
    }
}

static void DrawTitle() {
    int w = GetScreenWidth(), h = GetScreenHeight();
    float u = UIS();
    ClearBackground(C_SKY);
    DrawStars();
    float pr = std::min(w, h) * 0.55f;
    DrawPlanet(w * 0.5f, h + pr * 0.35f, pr);
    float t = G.titleT;
    DrawTextShadowC("MARS STATION", w / 2.0f, h * 0.16f, 86 * u, C_ACCENT);
    DrawTextC("the last astronaut holds the line", w / 2.0f, h * 0.16f + 96 * u, 24 * u, Fade(WHITE, 0.8f));
    float a = 0.55f + 0.45f * sinf(t * 4);
    DrawTextShadowC(G.touchMode ? "TAP TO START" : "PRESS ENTER OR CLICK TO START", w / 2.0f, h * 0.48f, 34 * u, Fade(WHITE, a));
    float y = h * 0.60f;
    if (G.touchMode) {
        DrawTextC("Left thumb: move    Right thumb: FIRE (auto-aim)    DASH to dodge", w / 2.0f, y, 18 * u, Fade(WHITE, 0.75f));
    } else {
        DrawTextC("WASD / Arrows: move     Mouse: aim     LMB: fire", w / 2.0f, y, 20 * u, Fade(WHITE, 0.75f));
        DrawTextC("SPACE / RMB: dash     P / ESC: pause     F: fullscreen     M: mute", w / 2.0f, y + 28 * u, 20 * u, Fade(WHITE, 0.75f));
    }
    if (G.best > 0) {
        std::string b = "BEST " + std::to_string(G.best);
        DrawTextC(b.c_str(), w / 2.0f, y + 66 * u, 24 * u, C_CYAN);
    }
    DrawTextC("made by Mars  |  C++ & raylib", w / 2.0f, h - 34 * u, 16 * u, Fade(WHITE, 0.45f));
}

static void DrawOverlay(const char *title, Color tc) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    float u = UIS();
    DrawRectangle(0, 0, w, h, Fade(BLACK, 0.6f));
    DrawTextShadowC(title, w / 2.0f, h * 0.22f, 76 * u, tc);
}

// ---------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------
static bool g_muted = false;

static void UpdateDrawFrame() {
    float dt = std::min(GetFrameTime(), 1.0f / 30.0f);
    G.time += dt;
    UpdateControls();

    if (IsKeyPressed(KEY_F)) ToggleFullscreen();
    if (IsKeyPressed(KEY_M)) { g_muted = !g_muted; SetMasterVolume(g_muted ? 0.0f : 1.0f); }

    // music loop
    if (SB.ok && G.screen != S_TITLE && !IsSoundPlaying(SB.music)) PlaySound(SB.music);

    switch (G.screen) {
        case S_TITLE:
            G.titleT += dt;
            if (CTL.confirm && G.titleT > 0.3f) { Play(SB.select); ResetGame(); G.screen = S_PLAY; }
            break;
        case S_PLAY:
            if (CTL.pause) { G.screen = S_PAUSE; Play(SB.select); if (SB.ok) PauseSound(SB.music); break; }
            UpdatePlayer(dt);
            UpdateEnemies(dt);
            UpdateBullets(dt);
            UpdatePickups(dt);
            if (G.screen == S_PLAY) UpdateWaves(dt);
            G.comboT -= dt;
            if (G.comboT <= 0) G.combo = std::max(1.0f, G.combo - dt * 2);
            UpdateFX(dt);
            UpdateCamera(dt);
            break;
        case S_PAUSE:
            if (CTL.pause || CTL.confirm) { G.screen = S_PLAY; if (SB.ok) ResumeSound(SB.music); }
            if (IsKeyPressed(KEY_Q)) { G.screen = S_TITLE; G.titleT = 0; if (SB.ok) StopSound(SB.music); }
            break;
        case S_GAMEOVER:
            G.gameOverT += dt;
            UpdateEnemies(dt * 0.3f);
            UpdateBullets(dt * 0.3f);
            UpdateFX(dt);
            UpdateCamera(dt);
            if (CTL.confirm && G.gameOverT > 1.2f) { Play(SB.select); ResetGame(); G.screen = S_PLAY; }
            break;
    }

    BeginDrawing();
    ClearBackground(C_SKY);
    if (G.screen == S_TITLE) {
        DrawTitle();
    } else {
        DrawWorld();
        DrawAtmosphere();
        DrawHUD();
        DrawTouchControls();
        int w = GetScreenWidth(), h = GetScreenHeight();
        float u = UIS();
        if (G.screen == S_PAUSE) {
            DrawOverlay("PAUSED", WHITE);
            DrawTextC(G.touchMode ? "tap to resume" : "P / ESC / click to resume     Q: quit to title", w / 2.0f, h * 0.45f, 26 * u, Fade(WHITE, 0.85f));
        } else if (G.screen == S_GAMEOVER) {
            DrawOverlay("SIGNAL LOST", C_DANGER);
            std::string s = "SCORE " + std::to_string(G.score);
            DrawTextShadowC(s.c_str(), w / 2.0f, h * 0.22f + 100 * u, 48 * u, WHITE);
            std::string info = "wave " + std::to_string(G.wave) + "   |   " + std::to_string(G.kills) + " kills";
            DrawTextC(info.c_str(), w / 2.0f, h * 0.22f + 160 * u, 24 * u, Fade(WHITE, 0.75f));
            if (G.newBest) DrawTextShadowC("NEW RECORD!", w / 2.0f, h * 0.22f + 200 * u, 34 * u, C_ACCENT);
            else { std::string b = "best " + std::to_string(G.best); DrawTextC(b.c_str(), w / 2.0f, h * 0.22f + 200 * u, 24 * u, C_CYAN); }
            if (G.gameOverT > 1.2f) {
                float a = 0.55f + 0.45f * sinf(G.time * 4);
                DrawTextShadowC(G.touchMode ? "TAP TO RETRY" : "ENTER / CLICK TO RETRY", w / 2.0f, h * 0.68f, 34 * u, Fade(WHITE, a));
            }
        }
    }
    EndDrawing();
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
#if defined(PLATFORM_WEB)
    int iw = EM_ASM_INT({ return window.innerWidth; });
    int ih = EM_ASM_INT({ return window.innerHeight; });
    InitWindow(iw, ih, "Mars Station");
    G.touchMode = EM_ASM_INT({ return (('ontouchstart' in window) || navigator.maxTouchPoints > 0) && matchMedia('(pointer: coarse)').matches ? 1 : 0; });
#else
    InitWindow(1280, 720, "Mars Station");
    SetWindowMinSize(640, 360);
    SetTargetFPS(60);
#endif
    SetExitKey(KEY_NULL);
    InitAudioDevice();
    if (IsAudioDeviceReady()) InitSounds();
    G.best = LoadBest();
    GenerateWorld();
    G.cam.zoom = 1; G.cam.target = G.pl.p;

#if defined(PLATFORM_WEB)
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    while (!WindowShouldClose()) UpdateDrawFrame();
    CloseAudioDevice();
    CloseWindow();
#endif
    return 0;
}
