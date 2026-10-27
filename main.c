/*
 * LED Control for TrimUI Brick Pro (NextUI, tg5040)
 * 5 zones: f1, f2 (FN keys), m (top bar), lr (joysticks), rear (shoulder/trigger lights)
 * LED writes follow NextUI's own order: brightness -> cycles -> duration -> color -> effect.
 */
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#else
#include <SDL.h>
#include <SDL_ttf.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 1024
#define H 768
#define NZ 5
#define NROW 7
#define NSEL 6   /* 5 vung + ALL */
#define LED_DIR "/sys/class/led_anim/"

typedef struct {
    const char *name;
    const char *file;
    int r, g, b;          /* color1 */
    unsigned color2;      /* kept as-is for NextUI */
    int effect, speed, bright, trigger, inbright;
} zone_t;

static zone_t Z[NZ] = {
    {"FN1 key",       "f1",   0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100},
    {"FN2 key",       "f2",   0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100},
    {"Top bar",       "m",    0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100},
    {"Joysticks",     "lr",   0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100},
    {"Rear lights",   "rear", 0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100},
};
static zone_t ORIG[NZ];

static const int EFF[] = {1, 2, 3, 4, 5, 6, 7};
static const char *EFFN[] = {"Linear", "Breathe", "Sniff", "Static", "Blink 1", "Blink 2", "Blink 3"};
#define NEFF 7

typedef struct { const char *name; unsigned rgb; } preset_t;
static const preset_t PRE[] = {
    {"Red", 0xFF0000}, {"Orange", 0xFF6400}, {"Yellow", 0xFFC800}, {"Green", 0x00FF00},
    {"Cyan", 0x00FFFF}, {"Blue", 0x0000FF}, {"Purple", 0x8000FF}, {"Pink", 0xFF3C8C},
    {"White", 0xFFFFFF}, {"Warm white", 0xFFB46E}
};
#define NPRE 10

static const char *ROWN[NROW] = {"Zone", "Color", "Red", "Green", "Blue", "Brightness", "Effect"};

/* ---------- LED sysfs ---------- */
static void wr(const char *name, const char *val) {
    char p[160];
    snprintf(p, sizeof p, LED_DIR "%s", name);
    FILE *f = fopen(p, "w");
    if (f) { fputs(val, f); fclose(f); }
}
static void wr_zone(const char *key, const char *zone, const char *val) {
    char n[96];
    snprintf(n, sizeof n, "%s_%s", key, zone);
    wr(n, val);
}
static const char *scale_file(const char *z, char *buf, size_t n) {
    if (!strcmp(z, "m")) return "max_scale";
    if (!strcmp(z, "f1")) return "max_scale_f1f2";
    if (!strcmp(z, "f2")) return NULL; /* shares f1's brightness */
    snprintf(buf, n, "max_scale_%s", z);
    return buf;
}
static void apply_zone(const zone_t *z) {
    char v[32], sb[48];
    for (int pass = 0; pass < 2; pass++) {
        const char *sf = scale_file(z->file, sb, sizeof sb);
        if (sf) { snprintf(v, sizeof v, "%d", z->bright); wr(sf, v); }
        wr_zone("effect_cycles", z->file, "-1");
        snprintf(v, sizeof v, "%d", z->speed); wr_zone("effect_duration", z->file, v);
        snprintf(v, sizeof v, "%02X%02X%02X", z->r, z->g, z->b); wr_zone("effect_rgb_hex", z->file, v);
        snprintf(v, sizeof v, "%d", z->effect); wr_zone("effect", z->file, v);
        if (pass == 0) SDL_Delay(30);
    }
}

/* ---------- settings file (NextUI format) ---------- */
static void settings_path(char *out, size_t n) {
    const char *sd = getenv("SDCARD_PATH");
    snprintf(out, n, "%s/.userdata/shared/ledsettings_brickpro.txt", sd ? sd : "/mnt/SDCARD");
}
static int zone_by_name(const char *s) {
    for (int i = 0; i < NZ; i++) if (!strcmp(Z[i].file, s)) return i;
    return -1;
}
static void load_settings(void) {
    char p[256], line[256];
    settings_path(p, sizeof p);
    FILE *f = fopen(p, "r");
    if (!f) return;
    int cur = -1;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') {
            char *e = strchr(line, ']');
            if (e) { *e = 0; cur = zone_by_name(line + 1); }
            continue;
        }
        if (cur < 0) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        zone_t *z = &Z[cur];
        if (!strcmp(k, "effect")) z->effect = atoi(v);
        else if (!strcmp(k, "color1")) { unsigned c = (unsigned)strtoul(v, NULL, 0); z->r = (c >> 16) & 255; z->g = (c >> 8) & 255; z->b = c & 255; }
        else if (!strcmp(k, "color2")) z->color2 = (unsigned)strtoul(v, NULL, 0);
        else if (!strcmp(k, "speed")) z->speed = atoi(v);
        else if (!strcmp(k, "brightness")) z->bright = atoi(v);
        else if (!strcmp(k, "trigger")) z->trigger = atoi(v);
        else if (!strcmp(k, "inbrightness")) z->inbright = atoi(v);
    }
    fclose(f);
    for (int i = 0; i < NZ; i++) {
        if (Z[i].bright < 0) Z[i].bright = 0;
        if (Z[i].bright > 100) Z[i].bright = 100;
        if (Z[i].effect < 1 || Z[i].effect > 7) Z[i].effect = 4;
    }
}
static int save_settings(void) {
    char p[256];
    settings_path(p, sizeof p);
    FILE *f = fopen(p, "w");
    if (!f) return -1;
    for (int i = 0; i < NZ; i++) {
        zone_t *z = &Z[i];
        fprintf(f, "[%s]\neffect=%d\ncolor1=0x%02X%02X%02X\ncolor2=0x%06X\nspeed=%d\nbrightness=%d\ntrigger=%d\nfilename=%s\ninbrightness=%d\n\n",
                z->file, z->effect, z->r, z->g, z->b, z->color2 & 0xFFFFFF, z->speed, z->bright, z->trigger, z->file, z->inbright);
    }
    fclose(f);
    return 0;
}

/* ---------- drawing ---------- */
static SDL_Renderer *R;
static TTF_Font *F_BIG, *F_MED, *F_SMALL;

static void text(TTF_Font *f, const char *s, int x, int y, SDL_Color c, int align) {
    if (!s || !*s) return;
    SDL_Surface *su = TTF_RenderUTF8_Blended(f, s, c);
    if (!su) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(R, su);
    SDL_Rect d = { x, y, su->w, su->h };
    if (align == 1) d.x = x - su->w / 2;       /* center */
    else if (align == 2) d.x = x - su->w;      /* right */
    if (t) { SDL_RenderCopy(R, t, NULL, &d); SDL_DestroyTexture(t); }
    SDL_FreeSurface(su);
}
static void rect(int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, 255);
    SDL_Rect r = { x, y, w, h };
    SDL_RenderFillRect(R, &r);
}

static int zi = 0, ri = 0;     /* zi: 0..4 = zone, 5 = ALL */
static Uint32 msg_until = 0;
static char msg[96] = "";

static void set_msg(const char *s) { snprintf(msg, sizeof msg, "%s", s); msg_until = SDL_GetTicks() + 2000; }
static zone_t *cur(void) { return &Z[zi == NSEL - 1 ? 0 : zi]; }
static const char *zone_label(void) { return zi == NSEL - 1 ? "All zones" : Z[zi].name; }
static int preset_of(const zone_t *z) {
    unsigned c = ((unsigned)z->r << 16) | ((unsigned)z->g << 8) | (unsigned)z->b;
    for (int i = 0; i < NPRE; i++) if (PRE[i].rgb == c) return i;
    return -1;
}

static void draw(void) {
    SDL_Color bg = {18, 18, 26, 255}, white = {235, 235, 240, 255}, grey = {140, 140, 155, 255};
    SDL_Color sel = {52, 58, 96, 255}, acc = {120, 170, 255, 255};
    rect(0, 0, W, H, bg);
    text(F_BIG, "LED Control - Brick Pro", W / 2, 20, white, 1);

    zone_t *z = cur();
    int y0 = 92, rh = 74;
    for (int i = 0; i < NROW; i++) {
        int y = y0 + i * rh;
        if (i == ri) rect(40, y, 600, rh - 10, sel);
        text(F_MED, ROWN[i], 64, y + 14, i == ri ? white : grey, 0);
        char v[48] = "";
        if (i == 0) snprintf(v, sizeof v, "< %s >", zone_label());
        else if (i == 1) { int p = preset_of(z); snprintf(v, sizeof v, "< %s >", p >= 0 ? PRE[p].name : "Custom"); }
        else if (i == 2) snprintf(v, sizeof v, "%d", z->r);
        else if (i == 3) snprintf(v, sizeof v, "%d", z->g);
        else if (i == 4) snprintf(v, sizeof v, "%d", z->b);
        else if (i == 5) snprintf(v, sizeof v, "%d%%", z->bright);
        else { int k = 3; for (int j = 0; j < NEFF; j++) if (EFF[j] == z->effect) k = j; snprintf(v, sizeof v, "< %s >", EFFN[k]); }
        text(F_MED, v, 620, y + 14, i == ri ? acc : white, 2);
        if (i >= 2 && i <= 5) {
            int val = i == 2 ? z->r : i == 3 ? z->g : i == 4 ? z->b : z->bright;
            int mx = i == 5 ? 100 : 255;
            SDL_Color track = {40, 42, 60, 255};
            SDL_Color fill = i == 2 ? (SDL_Color){220, 70, 70, 255} : i == 3 ? (SDL_Color){70, 200, 90, 255} : i == 4 ? (SDL_Color){80, 110, 240, 255} : (SDL_Color){230, 200, 90, 255};
            rect(280, y + 28, 200, 14, track);
            rect(280, y + 28, 200 * val / mx, 14, fill);
        }
    }

    /* swatch */
    SDL_Color sw = {(Uint8)z->r, (Uint8)z->g, (Uint8)z->b, 255}, frame = {70, 72, 95, 255};
    rect(692, 136, 296, 296, frame);
    rect(696, 140, 288, 288, sw);
    char hex[24];
    snprintf(hex, sizeof hex, "#%02X%02X%02X", z->r, z->g, z->b);
    text(F_MED, hex, 840, 450, white, 1);
    text(F_SMALL, zi == NSEL - 1 ? "all zones" : z->file, 840, 492, grey, 1);

    /* all-zone mini swatches */
    for (int i = 0; i < NZ; i++) {
        SDL_Color c = {(Uint8)Z[i].r, (Uint8)Z[i].g, (Uint8)Z[i].b, 255};
        int on = (zi == NSEL - 1) || (i == zi);
        rect(692 + i * 60, 540, 52, 52, on ? white : frame);
        rect(696 + i * 60, 544, 44, 44, c);
    }

    if (SDL_GetTicks() < msg_until) text(F_MED, msg, W / 2, 640, acc, 1);
    text(F_SMALL, "D-pad: change   L1/R1: big step   A: next color   START: save   B: exit", W / 2, 722, grey, 1);
    SDL_RenderPresent(R);
}

/* ---------- logic ---------- */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* f1/f2 share one brightness register */
static void sync_shared_brightness(int idx) {
    if (idx == 0) Z[1].bright = Z[0].bright;
    else if (idx == 1) Z[0].bright = Z[1].bright;
}
/* push the edited zone (or all zones when ALL is selected) to the LEDs */
static void commit(void) {
    zone_t *src = cur();
    if (zi == NSEL - 1) {
        for (int i = 0; i < NZ; i++) {
            if (&Z[i] != src) { Z[i].r = src->r; Z[i].g = src->g; Z[i].b = src->b; Z[i].bright = src->bright; Z[i].effect = src->effect; }
            apply_zone(&Z[i]);
        }
    } else {
        if (ri == 5) sync_shared_brightness(zi);
        apply_zone(&Z[zi]);
        if (ri == 5 && zi <= 1) apply_zone(&Z[zi == 0 ? 1 : 0]);
    }
}
static void set_preset(int idx) {
    zone_t *z = cur();
    idx = (idx % NPRE + NPRE) % NPRE;
    z->r = (PRE[idx].rgb >> 16) & 255; z->g = (PRE[idx].rgb >> 8) & 255; z->b = PRE[idx].rgb & 255;
}
static void change(int dir, int big) {
    zone_t *z = cur();
    switch (ri) {
    case 0: zi = (zi + dir + NSEL) % NSEL; return;
    case 1: { int p = preset_of(z); set_preset(p < 0 ? (dir > 0 ? 0 : NPRE - 1) : p + dir); break; }
    case 2: z->r = clampi(z->r + dir * (big ? 32 : 8), 0, 255); break;
    case 3: z->g = clampi(z->g + dir * (big ? 32 : 8), 0, 255); break;
    case 4: z->b = clampi(z->b + dir * (big ? 32 : 8), 0, 255); break;
    case 5: z->bright = clampi(z->bright + dir * (big ? 20 : 5), 0, 100); break;
    case 6: { int k = 3; for (int j = 0; j < NEFF; j++) if (EFF[j] == z->effect) k = j;
              k = (k + dir + NEFF) % NEFF; z->effect = EFF[k]; break; }
    }
    commit();
}
static void next_preset(void) {
    int p = preset_of(cur());
    set_preset(p < 0 ? 0 : p + 1);
    commit();
}
static void do_save(void) {
    if (save_settings() == 0) { memcpy(ORIG, Z, sizeof Z); set_msg("Saved to NextUI settings"); }
    else set_msg("Save failed");
}
static void quit_restore(void) { for (int i = 0; i < NZ; i++) apply_zone(&ORIG[i]); }

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    if (TTF_Init() < 0) { fprintf(stderr, "TTF_Init: %s\n", TTF_GetError()); return 1; }
    for (int i = 0; i < SDL_NumJoysticks(); i++) SDL_JoystickOpen(i);

    SDL_Window *win = SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, W, H, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    R = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!R) R = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!R) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(R, W, H);
    SDL_ShowCursor(SDL_DISABLE);

    const char *sd = getenv("SDCARD_PATH");
    char fp[300];
    snprintf(fp, sizeof fp, "%s/.system/res/BPreplayBold-unhinted.otf", sd ? sd : "/mnt/SDCARD");
    F_BIG = TTF_OpenFont(fp, 44); F_MED = TTF_OpenFont(fp, 32); F_SMALL = TTF_OpenFont(fp, 22);
    if (!F_BIG || !F_MED || !F_SMALL) { fprintf(stderr, "font not found: %s\n", fp); return 1; }

    load_settings();
    memcpy(ORIG, Z, sizeof Z);
    for (int i = 0; i < NZ; i++) apply_zone(&Z[i]);

    int running = 1, hx = 0, hy = 0;
    Uint32 next_rep = 0;
    SDL_Event e;
    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_JOYHATMOTION) {
                hx = (e.jhat.value & SDL_HAT_LEFT) ? -1 : (e.jhat.value & SDL_HAT_RIGHT) ? 1 : 0;
                hy = (e.jhat.value & SDL_HAT_UP) ? -1 : (e.jhat.value & SDL_HAT_DOWN) ? 1 : 0;
                if (hy) ri = (ri + hy + NROW) % NROW;
                if (hx) change(hx, 0);
                next_rep = SDL_GetTicks() + 350;
            } else if (e.type == SDL_JOYBUTTONDOWN) {
                switch (e.jbutton.button) {
                case 0: running = 0; break;              /* B: exit */
                case 1: next_preset(); break;           /* A */
                case 4: change(-1, 1); break;            /* L1 */
                case 5: change(1, 1); break;             /* R1 */
                case 7: do_save(); break;               /* START */
                default: break;
                }
            } else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: running = 0; break;
                case SDLK_UP: ri = (ri + NROW - 1) % NROW; break;
                case SDLK_DOWN: ri = (ri + 1) % NROW; break;
                case SDLK_LEFT: change(-1, 0); break;
                case SDLK_RIGHT: change(1, 0); break;
                case SDLK_RETURN: next_preset(); break;
                case SDLK_s: do_save(); break;
                default: break;
                }
            }
        }
        if (hx && SDL_GetTicks() >= next_rep) { change(hx, 0); next_rep = SDL_GetTicks() + 90; }
        draw();
        SDL_Delay(16);
    }
    quit_restore();
    TTF_Quit();
    SDL_Quit();
    return 0;
}
