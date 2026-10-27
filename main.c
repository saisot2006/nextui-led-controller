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
#include <dirent.h>

#define W 1024
#define H 768
#define NZ 6
#define NROW 8
#define NSEL 7   /* 6 vung + ALL */
#ifndef LED_DIR
#define LED_DIR "/sys/class/led_anim/"
#endif

typedef struct {
    const char *name;
    const char *file;
    int r, g, b;          /* color1 */
    unsigned color2;      /* kept as-is for NextUI */
    int effect, speed, bright, trigger, inbright;
    int on;               /* 0 = tắt nhanh (ghi màu đen, giữ nguyên giá trị) */
} zone_t;

static zone_t Z[NZ] = {
    {"Phím FN1",       "f1",   0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100, 1},
    {"Phím FN2",       "f2",   0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100, 1},
    {"Thanh trên",     "m",    0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100, 1},
    {"Joystick trái",  "l",    0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100, 1},
    {"Joystick phải",  "r",    0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100, 1},
    {"Đèn vai sau",    "rear", 0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100, 1},
};
static zone_t ORIG[NZ];

static const int EFF[] = {1, 2, 3, 4, 5, 6, 7};
static const char *EFFN[] = {"Tuyến tính", "Thở", "Sniff", "Đứng yên", "Nháy 1", "Nháy 2", "Nháy 3"};
#define NEFF 7

typedef struct { const char *name; unsigned rgb; } preset_t;
static const preset_t PRE[] = {
    {"Đỏ", 0xFF0000}, {"Cam", 0xFF6400}, {"Vàng", 0xFFC800}, {"Xanh lá", 0x00FF00},
    {"Xanh ngọc", 0x00FFFF}, {"Xanh dương", 0x0000FF}, {"Tím", 0x8000FF}, {"Hồng", 0xFF3C8C},
    {"Trắng", 0xFFFFFF}, {"Trắng ấm", 0xFFB46E}
};
#define NPRE 10

static const char *ROWN[NROW] = {"Vùng đèn", "Trạng thái", "Màu", "Đỏ", "Xanh lá", "Xanh dương", "Độ sáng", "Hiệu ứng"};

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
    (void)buf; (void)n;
    if (!strcmp(z, "m")) return "max_scale";
    if (!strcmp(z, "f1") || !strcmp(z, "f2")) return "max_scale_f1f2";   /* chung 1 thanh ghi */
    if (!strcmp(z, "l") || !strcmp(z, "r")) return "max_scale_lr";        /* chung 1 thanh ghi */
    return "max_scale_rear";
}
static void apply_zone(const zone_t *z) {
    char v[32], sb[8];
    for (int pass = 0; pass < 2; pass++) {
        const char *sf = scale_file(z->file, sb, sizeof sb);
        snprintf(v, sizeof v, "%d", z->bright); wr(sf, v);
        wr_zone("effect_cycles", z->file, "-1");
        snprintf(v, sizeof v, "%d", z->speed); wr_zone("effect_duration", z->file, v);
        if (z->on) snprintf(v, sizeof v, "%02X%02X%02X", z->r, z->g, z->b);
        else snprintf(v, sizeof v, "000000");
        wr_zone("effect_rgb_hex", z->file, v);
        snprintf(v, sizeof v, "%d", z->on ? z->effect : 4); wr_zone("effect", z->file, v);
        if (pass == 0) SDL_Delay(30);
    }
}

/* ---------- settings file (NextUI format) ---------- */
static void settings_path(char *out, size_t n) {
    const char *sd = getenv("SDCARD_PATH");
    snprintf(out, n, "%s/.userdata/shared/ledsettings_brickpro.txt", sd ? sd : "/mnt/SDCARD");
}
static int zone_by_name(const char *s) {
    if (!strcmp(s, "lr")) return 3;   /* NextUI lưu 1 vùng "lr" cho cả 2 joystick -> đọc vào joystick trái */
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
    /* joystick phải lấy cùng giá trị với trái lúc khởi động */
    { zone_t *l = &Z[3], *r = &Z[4];
      r->r = l->r; r->g = l->g; r->b = l->b; r->color2 = l->color2; r->effect = l->effect;
      r->speed = l->speed; r->bright = l->bright; r->trigger = l->trigger; r->inbright = l->inbright; }
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
    static const int map[5] = {0, 1, 2, 3, 5};          /* f1, f2, m, lr(=joystick trái), rear */
    static const char *nm[5] = {"f1", "f2", "m", "lr", "rear"};
    for (int k = 0; k < 5; k++) {
        zone_t *z = &Z[map[k]];
        unsigned c = z->on ? (((unsigned)z->r << 16) | ((unsigned)z->g << 8) | (unsigned)z->b) : 0;
        fprintf(f, "[%s]\neffect=%d\ncolor1=0x%06X\ncolor2=0x%06X\nspeed=%d\nbrightness=%d\ntrigger=%d\nfilename=%s\ninbrightness=%d\n\n",
                nm[k], z->effect, c, z->color2 & 0xFFFFFF, z->speed, z->bright, z->trigger, nm[k], z->inbright);
    }
    fclose(f);
    return 0;
}

static SDL_Renderer *R;
static TTF_Font *F_BIG, *F_MED, *F_SMALL;

/* ---------- font ---------- */
/* Tìm file kết thúc bằng DejaVuSans-Bold.ttf trong thư mục hiện tại (launch.sh đã cd vào thư mục pak). */
static int find_bundled_font(char *out, size_t n) {
    DIR *d = opendir(".");
    if (!d) return 0;
    struct dirent *e;
    const char *suf = "DejaVuSans-Bold.ttf";
    size_t sl = strlen(suf);
    int ok = 0;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l >= sl && !strcmp(e->d_name + l - sl, suf)) { snprintf(out, n, "%s", e->d_name); ok = 1; break; }
    }
    closedir(d);
    return ok;
}
static int open_fonts(const char *path) {
    F_BIG = TTF_OpenFont(path, 42); F_MED = TTF_OpenFont(path, 30); F_SMALL = TTF_OpenFont(path, 21);
    return F_BIG && F_MED && F_SMALL;
}

/* ---------- drawing ---------- */

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

static int zi = NSEL - 1, ri = 0;   /* zi: 0..5 = vùng, 6 = tất cả (mặc định) */
static Uint32 msg_until = 0;
static char msg[96] = "";

static void set_msg(const char *s) { snprintf(msg, sizeof msg, "%s", s); msg_until = SDL_GetTicks() + 2000; }
static zone_t *cur(void) { return &Z[zi == NSEL - 1 ? 0 : zi]; }
static const char *zone_label(void) { return zi == NSEL - 1 ? "Tất cả đèn" : Z[zi].name; }
static int preset_of(const zone_t *z) {
    unsigned c = ((unsigned)z->r << 16) | ((unsigned)z->g << 8) | (unsigned)z->b;
    for (int i = 0; i < NPRE; i++) if (PRE[i].rgb == c) return i;
    return -1;
}

static void draw(void) {
    SDL_Color bg = {18, 18, 26, 255}, white = {235, 235, 240, 255}, grey = {140, 140, 155, 255};
    SDL_Color sel = {52, 58, 96, 255}, acc = {120, 170, 255, 255};
    rect(0, 0, W, H, bg);
    text(F_BIG, "Điều khiển LED - Brick Pro", W / 2, 16, white, 1);

    zone_t *z = cur();
    int y0 = 84, rh = 68;
    for (int i = 0; i < NROW; i++) {
        int y = y0 + i * rh;
        if (i == ri) rect(40, y, 600, rh - 8, sel);
        text(F_MED, ROWN[i], 64, y + 12, i == ri ? white : grey, 0);
        char v[48] = "";
        if (i == 0) snprintf(v, sizeof v, "< %s >", zone_label());
        else if (i == 1) snprintf(v, sizeof v, "< %s >", z->on ? "Bật" : "Tắt");
        else if (i == 2) { int p = preset_of(z); snprintf(v, sizeof v, "< %s >", p >= 0 ? PRE[p].name : "Tùy chỉnh"); }
        else if (i == 3) snprintf(v, sizeof v, "%d", z->r);
        else if (i == 4) snprintf(v, sizeof v, "%d", z->g);
        else if (i == 5) snprintf(v, sizeof v, "%d", z->b);
        else if (i == 6) snprintf(v, sizeof v, "%d%%", z->bright);
        else { int k = 3; for (int j = 0; j < NEFF; j++) if (EFF[j] == z->effect) k = j; snprintf(v, sizeof v, "< %s >", EFFN[k]); }
        text(F_MED, v, 620, y + 12, i == ri ? acc : white, 2);
        if (i >= 3 && i <= 6) {
            int val = i == 3 ? z->r : i == 4 ? z->g : i == 5 ? z->b : z->bright;
            int mx = i == 6 ? 100 : 255;
            SDL_Color track = {40, 42, 60, 255};
            SDL_Color fill = i == 3 ? (SDL_Color){220, 70, 70, 255} : i == 4 ? (SDL_Color){70, 200, 90, 255} : i == 5 ? (SDL_Color){80, 110, 240, 255} : (SDL_Color){230, 200, 90, 255};
            rect(300, y + 26, 190, 14, track);
            rect(300, y + 26, 190 * val / mx, 14, fill);
        }
    }

    /* swatch */
    SDL_Color sw = {(Uint8)(z->on ? z->r : 20), (Uint8)(z->on ? z->g : 20), (Uint8)(z->on ? z->b : 20), 255}, frame = {70, 72, 95, 255};
    rect(692, 120, 296, 296, frame);
    rect(696, 124, 288, 288, sw);
    char hex[24];
    snprintf(hex, sizeof hex, "#%02X%02X%02X", z->r, z->g, z->b);
    text(F_MED, z->on ? hex : "Tắt", 840, 430, white, 1);
    text(F_SMALL, zi == NSEL - 1 ? "tất cả đèn" : z->file, 840, 470, grey, 1);

    /* ô nhỏ từng vùng (tối = đang tắt) */
    for (int i = 0; i < NZ; i++) {
        SDL_Color c = {(Uint8)(Z[i].on ? Z[i].r : 20), (Uint8)(Z[i].on ? Z[i].g : 20), (Uint8)(Z[i].on ? Z[i].b : 20), 255};
        int on = (zi == NSEL - 1) || (i == zi);
        rect(692 + i * 50, 520, 44, 44, on ? white : frame);
        rect(695 + i * 50, 523, 38, 38, c);
    }

    if (SDL_GetTicks() < msg_until) text(F_SMALL, msg, 840, 600, acc, 1);
    text(F_SMALL, "D-pad: đổi  L1/R1: nhảy  A: màu kế  X: bật/tắt  START: lưu  B: thoát", W / 2, 726, grey, 1);
    SDL_RenderPresent(R);
}

/* ---------- logic ---------- */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* vùng dùng chung thanh ghi độ sáng: f1<->f2, l<->r */
static int partner_of(int i) { return i == 0 ? 1 : i == 1 ? 0 : i == 3 ? 4 : i == 4 ? 3 : -1; }

/* đẩy vùng vừa sửa (hoặc tất cả khi chọn "Tất cả đèn") xuống LED */
static void commit(void) {
    zone_t *src = cur();
    if (zi == NSEL - 1) {
        for (int i = 0; i < NZ; i++) {
            if (&Z[i] != src) {
                Z[i].r = src->r; Z[i].g = src->g; Z[i].b = src->b;
                Z[i].bright = src->bright; Z[i].effect = src->effect; Z[i].on = src->on;
            }
            apply_zone(&Z[i]);
        }
    } else {
        int p = partner_of(zi);
        if (ri == 6 && p >= 0) { Z[p].bright = Z[zi].bright; apply_zone(&Z[p]); }
        apply_zone(&Z[zi]);
    }
}
static void set_preset(int idx) {
    zone_t *z = cur();
    idx = (idx % NPRE + NPRE) % NPRE;
    z->r = (PRE[idx].rgb >> 16) & 255; z->g = (PRE[idx].rgb >> 8) & 255; z->b = PRE[idx].rgb & 255;
    z->on = 1;
}
static void toggle_on(void) {
    zone_t *z = cur();
    z->on = !z->on;
    commit();
    set_msg(z->on ? "Đã bật" : "Đã tắt");
}
static void change(int dir, int big) {
    zone_t *z = cur();
    switch (ri) {
    case 0: zi = (zi + dir + NSEL) % NSEL; return;
    case 1: z->on = !z->on; break;
    case 2: { int p = preset_of(z); set_preset(p < 0 ? (dir > 0 ? 0 : NPRE - 1) : p + dir); break; }
    case 3: z->r = clampi(z->r + dir * (big ? 32 : 8), 0, 255); break;
    case 4: z->g = clampi(z->g + dir * (big ? 32 : 8), 0, 255); break;
    case 5: z->b = clampi(z->b + dir * (big ? 32 : 8), 0, 255); break;
    case 6: z->bright = clampi(z->bright + dir * (big ? 20 : 5), 0, 100); break;
    case 7: { int k = 3; for (int j = 0; j < NEFF; j++) if (EFF[j] == z->effect) k = j;
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
    int diff = memcmp(&Z[3].r, &Z[4].r, 3 * sizeof(int)) != 0 || Z[3].on != Z[4].on;
    if (save_settings() == 0) {
        memcpy(ORIG, Z, sizeof Z);
        set_msg(diff ? "Đã lưu (NextUI chỉ nhớ 1 màu cho 2 joystick)" : "Đã lưu vào cài đặt NextUI");
    } else set_msg("Lưu thất bại");
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
    int font_ok = 0;
    if (find_bundled_font(fp, sizeof fp)) font_ok = open_fonts(fp);                 /* 1. font đi kèm pak (có đủ tiếng Việt) */
    if (!font_ok) { snprintf(fp, sizeof fp, "%s/.system/res/font1.ttf", sd ? sd : "/mnt/SDCARD"); font_ok = open_fonts(fp); }
    if (!font_ok) { snprintf(fp, sizeof fp, "%s/.system/res/BPreplayBold-unhinted.otf", sd ? sd : "/mnt/SDCARD"); font_ok = open_fonts(fp); }
    if (!font_ok) { fprintf(stderr, "font not found: %s\n", fp); return 1; }
    fprintf(stderr, "font: %s\n", fp);

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
                case 3: toggle_on(); break;              /* X */
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
                case SDLK_x: toggle_on(); break;
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
