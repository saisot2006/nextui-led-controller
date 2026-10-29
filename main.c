/*
 * Điều khiển LED cho TrimUI Brick Pro (NextUI, tg5040)
 * 6 vùng: f1, f2 (phím FN), m (thanh trên), l, r (joystick trái/phải), rear (đèn vai sau)
 * Ghi LED theo thứ tự của NextUI: độ sáng -> cycles -> duration -> màu -> effect (effect ghi cuối để kích hoạt).
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
#include <sys/stat.h>

#define W 1024
#define H 768
#define NZ 6
#define NROW 8
#define NSEL 7   /* 6 vùng + "Tất cả đèn" */
#ifndef LED_DIR
#define LED_DIR "/sys/class/led_anim/"
#endif

/* chỉ số vùng */
enum { Z_F1, Z_F2, Z_M, Z_L, Z_R, Z_REAR };
/* mặt nạ trường được sửa (để "Tất cả đèn" chỉ ghi đúng trường vừa đổi) */
enum { F_ON = 1, F_R = 2, F_G = 4, F_B = 8, F_BR = 16, F_EF = 32, F_COLOR = 14 };

typedef struct {
    const char *name;
    const char *file;
    int r, g, b;          /* color1 */
    unsigned color2;      /* giữ nguyên cho NextUI */
    int effect, speed, bright, trigger, inbright;
    int on;               /* 0 = tắt nhanh (ghi màu đen, vẫn giữ màu cũ) */
    int primed;           /* đã ghi đủ cycles/duration cho vùng này chưa (không lưu) */
} zone_t;

static zone_t Z[NZ] = {
    {"Phím FN1",       "f1",   0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100, 1, 0},
    {"Phím FN2",       "f2",   0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100, 1, 0},
    {"Thanh trên",     "m",    0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100, 1, 0},
    {"Joystick trái",  "l",    0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100, 1, 0},
    {"Joystick phải",  "r",    0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100, 1, 0},
    {"Đèn vai sau",    "rear", 0x44, 0x00, 0x44, 0x440044, 4, 1000, 100, 1, 100, 1, 0},
};
static zone_t ORIG[NZ];      /* trạng thái lúc mở app / lần lưu gần nhất, dùng để hoàn tác khi thoát */
static int touched[NZ];      /* vùng đã bị app thay đổi */

/* ---------- chế độ từng đèn (frame_hex, 22 vị trí) ---------- */
#define NLED 22
typedef struct { int r, g, b, on; } led_t;
static led_t LED[NLED], LED_ORIG[NLED];
static int fmode = 0, orig_mode = 0;          /* 0 = theo vùng, 1 = từng đèn */
static int led_loaded = 0, frame_ready = 0, frame_used = 0;
static int fgrp = 5, fidx = 0, fquick = 0;    /* nhóm (5 = tất cả), đèn trong nhóm (0 = cả nhóm), tạo nhanh */
/* bảng vị trí đã đo trên Brick Pro: thanh trên 1-8, FN 9-10, joystick trái 11-14, joystick phải 15-18, mặt sau 19-22 */
static const int GSTART[5] = {0, 8, 10, 14, 18};
static const int GCOUNT[5] = {8, 2, 4, 4, 4};
static const char *GNAME[6] = {"Thanh trên", "Phím FN", "Joystick trái", "Joystick phải", "Mặt sau", "Tất cả đèn"};
static const char *FROWN[8] = {"Nhóm", "Đèn", "Màu", "Đỏ", "Xanh lá", "Xanh dương", "Độ sáng", "Tạo nhanh"};
static const char *QNAME[3] = {"Không", "Cầu vồng", "Một màu"};

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
    if (f) { fputs(val, f); fputc('\n', f); fclose(f); }   /* có xuống dòng, giống NextUI */
}
static void wr_zone(const char *key, const char *zone, const char *val) {
    char n[96];
    snprintf(n, sizeof n, "%s_%s", key, zone);
    wr(n, val);
}
static int rd_file(const char *name, char *buf, size_t n) {
    char p[160];
    snprintf(p, sizeof p, LED_DIR "%s", name);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    buf[0] = 0;
    if (!fgets(buf, (int)n, f)) buf[0] = 0;
    fclose(f);
    buf[strcspn(buf, "\r\n")] = 0;
    return buf[0] != 0;
}
/* thanh ghi độ sáng: chỉ vùng "chính" của mỗi cặp ghi (NextUI bỏ qua f2). NULL = không ghi */
static const char *scale_file(int i) {
    switch (i) {
    case Z_F1: return "max_scale_f1f2";
    case Z_M: return "max_scale";
    case Z_L: return "max_scale_lr";
    case Z_REAR: return "max_scale_rear";
    default: return NULL;   /* f2 và r dùng chung thanh ghi với f1 và l */
    }
}
/* cặp dùng chung độ sáng: f1<->f2, l<->r */
static int partner_of(int i) { return i == Z_F1 ? Z_F2 : i == Z_F2 ? Z_F1 : i == Z_L ? Z_R : i == Z_R ? Z_L : -1; }
static int is_primary(int i) { return scale_file(i) != NULL; }

/* ghi 2 lượt liên tiếp vào cùng một vùng (cách đã kiểm chứng trên máy).
 * Lần đầu cho vùng đó ghi đủ cycles/duration, các lần sau chỉ ghi độ sáng + màu + effect. */
static void apply_zone(int idx, const zone_t *z, int force_full) {
    char v[32];
    int full = force_full || !Z[idx].primed;
    const char *sf = scale_file(idx);
    for (int pass = 0; pass < 2; pass++) {
        if (sf) { snprintf(v, sizeof v, "%d", z->bright); wr(sf, v); }
        if (full) {
            wr_zone("effect_cycles", z->file, "-1");
            snprintf(v, sizeof v, "%d", z->speed); wr_zone("effect_duration", z->file, v);
        }
        if (z->on) snprintf(v, sizeof v, "%02X%02X%02X", z->r, z->g, z->b);
        else snprintf(v, sizeof v, "000000");
        wr_zone("effect_rgb_hex", z->file, v);
        snprintf(v, sizeof v, "%d", z->on ? z->effect : 4); wr_zone("effect", z->file, v);
        if (pass == 0) SDL_Delay(10);
    }
    Z[idx].primed = 1;
}

/* ---- chế độ từng đèn: ghi frame_hex đúng 1 lần là giữ màu (đã thử trên máy) ---- */
static const char *ZALL[7] = {"m", "f1", "f2", "lr", "rear", "l", "r"};
static void frame_setup(void) {        /* tắt hệ effect theo vùng để frame_hex điều khiển */
    for (int i = 0; i < 7; i++) { wr_zone("effect_rgb_hex", ZALL[i], "000000"); wr_zone("effect", ZALL[i], "0"); }
    wr("effect_enable", "0");
}
static void frame_build(char *out, size_t n) {
    size_t o = 0;
    out[0] = 0;
    for (int i = 0; i < NLED && o + 8 < n; i++)
        o += (size_t)snprintf(out + o, n - o, "%02X%02X%02X ",
                              (unsigned)(LED[i].on ? LED[i].r : 0), (unsigned)(LED[i].on ? LED[i].g : 0), (unsigned)(LED[i].on ? LED[i].b : 0));
}
static void frame_write(void) {
    char v[16], buf[NLED * 8 + 8];
    snprintf(v, sizeof v, "%d", Z[Z_M].bright);    wr("max_scale", v);
    snprintf(v, sizeof v, "%d", Z[Z_F1].bright);   wr("max_scale_f1f2", v);
    snprintf(v, sizeof v, "%d", Z[Z_L].bright);    wr("max_scale_lr", v);
    snprintf(v, sizeof v, "%d", Z[Z_REAR].bright); wr("max_scale_rear", v);
    frame_build(buf, sizeof buf);
    wr("frame_hex", buf);
    wr("effect_enable", "1"); wr("effect_enable", "0");
}
static void ensure_frame(void) { if (!frame_ready) { frame_setup(); frame_ready = 1; } }
static void frame_leave(void) {        /* về chế độ theo vùng: bật lại hệ effect và ghi đủ 6 vùng */
    wr("effect_enable", "1");
    for (int i = 0; i < NZ; i++) apply_zone(i, &Z[i], 1);
    frame_ready = 0;
}

/* ---------- cài đặt ---------- */
static void shared_dir(char *out, size_t n) {
    const char *sd = getenv("SDCARD_PATH");
    snprintf(out, n, "%s/.userdata/shared", sd ? sd : "/mnt/SDCARD");
}
static void settings_path(char *out, size_t n) { char d[200]; shared_dir(d, sizeof d); snprintf(out, n, "%s/ledsettings_brickpro.txt", d); }
static void state_path(char *out, size_t n) { char d[200]; shared_dir(d, sizeof d); snprintf(out, n, "%s/ledbp_state.txt", d); }

static int zone_by_name(const char *s) {
    if (!strcmp(s, "lr")) return Z_L;   /* NextUI lưu 1 vùng "lr" cho cả 2 joystick -> đọc vào joystick trái */
    for (int i = 0; i < NZ; i++) if (!strcmp(Z[i].file, s)) return i;
    return -1;
}
static void load_nextui_file(void) {
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
    /* joystick phải lấy cùng giá trị với trái */
    { zone_t *l = &Z[Z_L], *r = &Z[Z_R];
      r->r = l->r; r->g = l->g; r->b = l->b; r->color2 = l->color2; r->effect = l->effect;
      r->speed = l->speed; r->bright = l->bright; r->trigger = l->trigger; r->inbright = l->inbright; }
    /* f2 dùng độ sáng của f1, giống NextUI (NextUI bỏ qua độ sáng của f2) */
    Z[Z_F2].bright = Z[Z_F1].bright;
}
/* trạng thái thật đang có trên đèn (nếu đọc được) là nguồn đúng nhất */
static void read_hardware(void) {
    char b[64];
    for (int i = 0; i < NZ; i++) {
        zone_t *z = &Z[i];
        char key[64];
        snprintf(key, sizeof key, "effect_rgb_hex_%s", z->file);
        if (rd_file(key, b, sizeof b) && strlen(b) >= 6) {
            char hex[7]; memcpy(hex, b, 6); hex[6] = 0;
            char *end; unsigned long c = strtoul(hex, &end, 16);
            if (*end == 0) { z->r = (c >> 16) & 255; z->g = (c >> 8) & 255; z->b = c & 255; }
        }
        snprintf(key, sizeof key, "effect_%s", z->file);
        if (rd_file(key, b, sizeof b)) { int e = atoi(b); if (e >= 1 && e <= 7) z->effect = e; }
        const char *sf = scale_file(i);
        if (sf && rd_file(sf, b, sizeof b)) { int v = atoi(b); if (v >= 0 && v <= 100) z->bright = v; }
    }
    Z[Z_F2].bright = Z[Z_F1].bright;
    Z[Z_R].bright = Z[Z_L].bright;
}
/* tệp trạng thái riêng của app: nhớ màu gốc của vùng đang tắt (NextUI chỉ lưu được màu đen) */
static void load_state_file(void) {
    char p[256], line[512];
    state_path(p, sizeof p);
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        if (!strcmp(line, "mode")) { fmode = atoi(eq + 1) == 1; continue; }
        if (!strcmp(line, "fleds")) {               /* "RRGGBB:bật" cho 22 đèn */
            const char *q = eq + 1;
            int k = 0;
            while (k < NLED && *q) {
                unsigned c; int o, used = 0;
                if (sscanf(q, "%6x:%d%n", &c, &o, &used) < 2) break;
                LED[k].r = (int)((c >> 16) & 255); LED[k].g = (int)((c >> 8) & 255); LED[k].b = (int)(c & 255); LED[k].on = o != 0;
                k++; q += used;
                while (*q == ' ') q++;
            }
            if (k == NLED) led_loaded = 1;
            continue;
        }
        int i = -1;
        for (int k = 0; k < NZ; k++) if (!strcmp(Z[k].file, line)) i = k;
        if (i < 0) continue;
        int on, r, g, b;
        if (sscanf(eq + 1, "%d,%d,%d,%d", &on, &r, &g, &b) != 4) continue;
        if (!on && Z[i].r == 0 && Z[i].g == 0 && Z[i].b == 0) {   /* đèn đang đen đúng như lúc lưu "tắt" */
            Z[i].on = 0; Z[i].r = r & 255; Z[i].g = g & 255; Z[i].b = b & 255;
        }
    }
    fclose(f);
}
static void load_all(void) {
    load_nextui_file();
    read_hardware();
    load_state_file();
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
    static const int map[5] = {Z_F1, Z_F2, Z_M, Z_L, Z_REAR};   /* lr lấy theo joystick trái */
    static const char *nm[5] = {"f1", "f2", "m", "lr", "rear"};
    for (int k = 0; k < 5; k++) {
        zone_t *z = &Z[map[k]];
        unsigned c = z->on ? (((unsigned)z->r << 16) | ((unsigned)z->g << 8) | (unsigned)z->b) : 0;
        fprintf(f, "[%s]\neffect=%d\ncolor1=0x%06X\ncolor2=0x%06X\nspeed=%d\nbrightness=%d\ntrigger=%d\nfilename=%s\ninbrightness=%d\n\n",
                nm[k], z->effect, c, z->color2 & 0xFFFFFF, z->speed, z->bright, z->trigger, nm[k], z->inbright);
    }
    fclose(f);
    state_path(p, sizeof p);
    f = fopen(p, "w");
    if (f) {
        for (int i = 0; i < NZ; i++) fprintf(f, "%s=%d,%d,%d,%d,%d\n", Z[i].file, Z[i].on, Z[i].r, Z[i].g, Z[i].b, Z[i].effect);   /* effect dùng cho hook giữ màu joystick */
        fprintf(f, "mode=%d\n", fmode);
        if (led_loaded) {
            char buf[NLED * 8 + 8];
            fputs("fleds=", f);
            for (int i = 0; i < NLED; i++) fprintf(f, "%02X%02X%02X:%d%s", (unsigned)LED[i].r, (unsigned)LED[i].g, (unsigned)LED[i].b, LED[i].on, i + 1 < NLED ? " " : "");
            fputc('\n', f);
            frame_build(buf, sizeof buf);          /* màu thực tế (đèn tắt = 000000) cho hook */
            fprintf(f, "frame=%s\n", buf);
            fprintf(f, "fscale=%d,%d,%d,%d\n", Z[Z_M].bright, Z[Z_F1].bright, Z[Z_L].bright, Z[Z_REAR].bright);
        }
        fclose(f);
    }
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

/* ---------- vẽ ---------- */
static void text(TTF_Font *f, const char *s, int x, int y, SDL_Color c, int align) {
    if (!s || !*s) return;
    SDL_Surface *su = TTF_RenderUTF8_Blended(f, s, c);
    if (!su) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(R, su);
    SDL_Rect d = { x, y, su->w, su->h };
    if (align == 1) d.x = x - su->w / 2;       /* giữa */
    else if (align == 2) d.x = x - su->w;      /* phải */
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
static int all_mode(void) { return zi == NSEL - 1; }
static zone_t *cur(void) { return &Z[all_mode() ? 0 : zi]; }
static const char *zone_label(void) { return all_mode() ? "Tất cả đèn" : Z[zi].name; }
static int preset_of(const zone_t *z) {
    unsigned c = ((unsigned)z->r << 16) | ((unsigned)z->g << 8) | (unsigned)z->b;
    for (int i = 0; i < NPRE; i++) if (PRE[i].rgb == c) return i;
    return -1;
}
/* ở "Tất cả đèn": các vùng có giá trị khác nhau ở các trường trong mặt nạ không? */
static int mixed(int mask) {
    if (!all_mode()) return 0;
    for (int i = 1; i < NZ; i++) {
        if ((mask & F_ON) && Z[i].on != Z[0].on) return 1;
        if ((mask & F_R) && Z[i].r != Z[0].r) return 1;
        if ((mask & F_G) && Z[i].g != Z[0].g) return 1;
        if ((mask & F_B) && Z[i].b != Z[0].b) return 1;
        if ((mask & F_BR) && Z[i].bright != Z[0].bright) return 1;
        if ((mask & F_EF) && Z[i].effect != Z[0].effect) return 1;
    }
    return 0;
}

static void fdraw(void);
static void draw(void) {
    if (fmode) { fdraw(); return; }
    SDL_Color bg = {18, 18, 26, 255}, white = {235, 235, 240, 255}, grey = {140, 140, 155, 255};
    SDL_Color sel = {52, 58, 96, 255}, acc = {120, 170, 255, 255};
    rect(0, 0, W, H, bg);
    text(F_BIG, "Điều khiển LED - Brick Pro", W / 2, 16, white, 1);

    zone_t *z = cur();
    int m_on = mixed(F_ON), m_col = mixed(F_COLOR), m_r = mixed(F_R), m_g = mixed(F_G), m_b = mixed(F_B), m_br = mixed(F_BR), m_ef = mixed(F_EF);
    int y0 = 84, rh = 68;
    for (int i = 0; i < NROW; i++) {
        int y = y0 + i * rh;
        if (i == ri) rect(40, y, 600, rh - 8, sel);
        text(F_MED, ROWN[i], 64, y + 12, i == ri ? white : grey, 0);
        char v[48] = "";
        if (i == 0) snprintf(v, sizeof v, "< %s >", zone_label());
        else if (i == 1) snprintf(v, sizeof v, "< %s >", m_on ? "Một phần" : z->on ? "Bật" : "Tắt");
        else if (i == 2) { int p = preset_of(z); snprintf(v, sizeof v, "< %s >", m_col ? "Nhiều màu" : p >= 0 ? PRE[p].name : "Tùy chỉnh"); }
        else if (i == 3) snprintf(v, sizeof v, m_r ? "—" : "%d", z->r);
        else if (i == 4) snprintf(v, sizeof v, m_g ? "—" : "%d", z->g);
        else if (i == 5) snprintf(v, sizeof v, m_b ? "—" : "%d", z->b);
        else if (i == 6) snprintf(v, sizeof v, m_br ? "—" : "%d%%", z->bright);
        else { int k = 3; for (int j = 0; j < NEFF; j++) if (EFF[j] == z->effect) k = j; snprintf(v, sizeof v, "< %s >", m_ef ? "—" : EFFN[k]); }
        text(F_MED, v, 620, y + 12, i == ri ? acc : white, 2);
        int mm = i == 3 ? m_r : i == 4 ? m_g : i == 5 ? m_b : m_br;
        if (i >= 3 && i <= 6 && !mm) {
            int val = i == 3 ? z->r : i == 4 ? z->g : i == 5 ? z->b : z->bright;
            int mx = i == 6 ? 100 : 255;
            SDL_Color track = {40, 42, 60, 255};
            SDL_Color fill = i == 3 ? (SDL_Color){220, 70, 70, 255} : i == 4 ? (SDL_Color){70, 200, 90, 255} : i == 5 ? (SDL_Color){80, 110, 240, 255} : (SDL_Color){230, 200, 90, 255};
            rect(300, y + 26, 190, 14, track);
            rect(300, y + 26, 190 * val / mx, 14, fill);
        }
    }

    /* ô màu lớn: nếu các vùng khác màu thì chia sọc theo từng vùng */
    SDL_Color frame = {70, 72, 95, 255};
    rect(692, 120, 296, 296, frame);
    if (m_col) {
        for (int i = 0; i < NZ; i++) {
            SDL_Color c = {(Uint8)(Z[i].on ? Z[i].r : 20), (Uint8)(Z[i].on ? Z[i].g : 20), (Uint8)(Z[i].on ? Z[i].b : 20), 255};
            rect(696, 124 + i * 48, 288, 48, c);
        }
    } else {
        SDL_Color sw = {(Uint8)(z->on ? z->r : 20), (Uint8)(z->on ? z->g : 20), (Uint8)(z->on ? z->b : 20), 255};
        rect(696, 124, 288, 288, sw);
    }
    char hex[24];
    snprintf(hex, sizeof hex, "#%02X%02X%02X", z->r, z->g, z->b);
    text(F_MED, m_col ? "nhiều màu" : z->on ? hex : "Tắt", 840, 430, white, 1);
    text(F_SMALL, all_mode() ? "tất cả đèn" : z->file, 840, 470, grey, 1);

    /* ô nhỏ từng vùng (tối = đang tắt) */
    for (int i = 0; i < NZ; i++) {
        SDL_Color c = {(Uint8)(Z[i].on ? Z[i].r : 20), (Uint8)(Z[i].on ? Z[i].g : 20), (Uint8)(Z[i].on ? Z[i].b : 20), 255};
        int on = all_mode() || (i == zi);
        rect(692 + i * 50, 520, 44, 44, on ? white : frame);
        rect(695 + i * 50, 523, 38, 38, c);
    }

    if (SDL_GetTicks() < msg_until) text(F_SMALL, msg, 840, 600, acc, 1);
    text(F_SMALL, "D-pad: đổi  L1/R1: nhảy  A: màu kế  X: bật/tắt  Y: từng đèn  START: lưu  B: thoát", W / 2, 726, grey, 1);
    SDL_RenderPresent(R);
}

/* ---------- logic ---------- */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void copy_fields(zone_t *d, const zone_t *s, int mask) {
    if (mask & F_ON) d->on = s->on;
    if (mask & F_R) d->r = s->r;
    if (mask & F_G) d->g = s->g;
    if (mask & F_B) d->b = s->b;
    if (mask & F_BR) d->bright = s->bright;
    if (mask & F_EF) d->effect = s->effect;
}
/* đẩy thay đổi xuống LED. Ở "Tất cả đèn" chỉ chép đúng các trường vừa đổi sang các vùng khác. */
static void commit(int mask) {
    zone_t *src = cur();
    if (all_mode()) {
        for (int i = 0; i < NZ; i++) {
            if (&Z[i] != src) copy_fields(&Z[i], src, mask);
            touched[i] = 1;
            apply_zone(i, &Z[i], 0);
        }
        return;
    }
    touched[zi] = 1;
    int p = partner_of(zi);
    if ((mask & F_BR) && p >= 0) {          /* độ sáng dùng chung thanh ghi với vùng cặp */
        Z[p].bright = Z[zi].bright;
        touched[p] = 1;
        if (is_primary(p)) apply_zone(p, &Z[p], 0);   /* vùng chính mới là vùng ghi thanh ghi */
    }
    apply_zone(zi, &Z[zi], 0);
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
    commit(F_ON);
    set_msg(z->on ? "Đã bật" : "Đã tắt");
}
static void change(int dir, int big) {
    zone_t *z = cur();
    int mask = 0;
    switch (ri) {
    case 0: zi = (zi + dir + NSEL) % NSEL; return;
    case 1: z->on = !z->on; mask = F_ON; break;
    case 2: { int p = preset_of(z); set_preset(p < 0 ? (dir > 0 ? 0 : NPRE - 1) : p + dir); mask = F_COLOR | F_ON; break; }
    case 3: z->r = clampi(z->r + dir * (big ? 32 : 8), 0, 255); mask = F_R; break;
    case 4: z->g = clampi(z->g + dir * (big ? 32 : 8), 0, 255); mask = F_G; break;
    case 5: z->b = clampi(z->b + dir * (big ? 32 : 8), 0, 255); mask = F_B; break;
    case 6: z->bright = clampi(z->bright + dir * (big ? 20 : 5), 0, 100); mask = F_BR; break;
    case 7: { int k = 3; for (int j = 0; j < NEFF; j++) if (EFF[j] == z->effect) k = j;
              k = (k + dir + NEFF) % NEFF; z->effect = EFF[k]; mask = F_EF; break; }
    }
    commit(mask);
}
static void next_preset(void) {
    int p = preset_of(cur());
    set_preset(p < 0 ? 0 : p + 1);
    commit(F_COLOR | F_ON);
}
static void do_save(void) {
    int diff = Z[Z_L].r != Z[Z_R].r || Z[Z_L].g != Z[Z_R].g || Z[Z_L].b != Z[Z_R].b || Z[Z_L].on != Z[Z_R].on;
    if (save_settings() == 0) {
        memcpy(ORIG, Z, sizeof Z);
        memcpy(LED_ORIG, LED, sizeof LED);
        orig_mode = fmode;
        memset(touched, 0, sizeof touched);
        set_msg(diff ? "Đã lưu (NextUI chỉ nhớ 1 màu cho 2 joystick)" : "Đã lưu vào cài đặt NextUI");
    } else set_msg("Lưu thất bại");
}
/* thoát mà chưa lưu: chỉ hoàn tác các vùng app đã đổi */
static void quit_restore(void) {
    for (int i = 0; i < NZ; i++) if (touched[i]) apply_zone(i, &ORIG[i], 1);
}


/* ---------- chế độ từng đèn: giao diện và logic ---------- */
static void led_from(led_t *l, const zone_t *z) { l->r = z->r; l->g = z->g; l->b = z->b; l->on = z->on; }
static void led_init_from_zones(void) {          /* lần đầu vào chế độ này: lấy màu từ các vùng hiện tại */
    for (int i = 0; i < 8; i++) led_from(&LED[i], &Z[Z_M]);
    led_from(&LED[8], &Z[Z_F2]);                 /* vị trí 9 = phím FN phải */
    led_from(&LED[9], &Z[Z_F1]);                 /* vị trí 10 = phím FN trái */
    for (int i = 10; i < 14; i++) led_from(&LED[i], &Z[Z_L]);
    for (int i = 14; i < 18; i++) led_from(&LED[i], &Z[Z_R]);
    for (int i = 18; i < 22; i++) led_from(&LED[i], &Z[Z_REAR]);
    led_loaded = 1;
}
static void fsel(int *st, int *cnt) {            /* các đèn đang chọn */
    if (fgrp == 5) { *st = 0; *cnt = NLED; }
    else if (fidx == 0) { *st = GSTART[fgrp]; *cnt = GCOUNT[fgrp]; }
    else { *st = GSTART[fgrp] + fidx - 1; *cnt = 1; }
}
static int fidx_max(void) { return fgrp == 5 ? 0 : GCOUNT[fgrp]; }
static int fmixed(int mask) {
    int st, cnt;
    fsel(&st, &cnt);
    for (int k = 1; k < cnt; k++) {
        if ((mask & F_ON) && LED[st + k].on != LED[st].on) return 1;
        if ((mask & F_R) && LED[st + k].r != LED[st].r) return 1;
        if ((mask & F_G) && LED[st + k].g != LED[st].g) return 1;
        if ((mask & F_B) && LED[st + k].b != LED[st].b) return 1;
    }
    return 0;
}
static int preset_of_rgb(int r, int g, int b) {
    unsigned c = ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
    for (int i = 0; i < NPRE; i++) if (PRE[i].rgb == c) return i;
    return -1;
}
/* độ sáng theo thanh ghi: thanh trên = max_scale, FN = f1f2, hai vòng joystick = lr, mặt sau = rear */
static int fbright_cur(void) {
    switch (fgrp) {
    case 1: return Z[Z_F1].bright;
    case 2: case 3: return Z[Z_L].bright;
    case 4: return Z[Z_REAR].bright;
    default: return Z[Z_M].bright;
    }
}
static int fbright_mixed(void) {
    return fgrp == 5 && (Z[Z_M].bright != Z[Z_F1].bright || Z[Z_M].bright != Z[Z_L].bright || Z[Z_M].bright != Z[Z_REAR].bright);
}
static void fbright_set(int v) {
    if (fgrp == 5 || fgrp == 0) Z[Z_M].bright = v;
    if (fgrp == 5 || fgrp == 1) Z[Z_F1].bright = Z[Z_F2].bright = v;
    if (fgrp == 5 || fgrp == 2 || fgrp == 3) Z[Z_L].bright = Z[Z_R].bright = v;
    if (fgrp == 5 || fgrp == 4) Z[Z_REAR].bright = v;
}
static void fapply(void) { ensure_frame(); frame_write(); }

static void fset_preset(int idx) {
    int st, cnt;
    fsel(&st, &cnt);
    idx = (idx % NPRE + NPRE) % NPRE;
    for (int k = 0; k < cnt; k++) {
        LED[st + k].r = (int)((PRE[idx].rgb >> 16) & 255); LED[st + k].g = (int)((PRE[idx].rgb >> 8) & 255); LED[st + k].b = (int)(PRE[idx].rgb & 255);
        LED[st + k].on = 1;
    }
}
static void fchange(int dir, int big) {
    int st, cnt;
    fsel(&st, &cnt);
    led_t *c = &LED[st];
    int step = big ? 32 : 8;
    switch (ri) {
    case 0: fgrp = (fgrp + dir + 6) % 6; fidx = 0; return;
    case 1: { int m = fidx_max(); if (m == 0) return; fidx = (fidx + dir + m + 1) % (m + 1); return; }
    case 2: { int p = preset_of_rgb(c->r, c->g, c->b); fset_preset(p < 0 ? (dir > 0 ? 0 : NPRE - 1) : p + dir); break; }
    case 3: { int v = clampi(c->r + dir * step, 0, 255); for (int k = 0; k < cnt; k++) { LED[st + k].r = v; LED[st + k].on = 1; } break; }
    case 4: { int v = clampi(c->g + dir * step, 0, 255); for (int k = 0; k < cnt; k++) { LED[st + k].g = v; LED[st + k].on = 1; } break; }
    case 5: { int v = clampi(c->b + dir * step, 0, 255); for (int k = 0; k < cnt; k++) { LED[st + k].b = v; LED[st + k].on = 1; } break; }
    case 6: fbright_set(clampi(fbright_cur() + dir * (big ? 20 : 5), 0, 100)); break;
    case 7: {
        fquick = (fquick + dir + 3) % 3;
        if (fquick == 1) {                       /* cầu vồng: mỗi đèn một màu theo thứ tự */
            for (int k = 0; k < cnt; k++) {
                unsigned col = PRE[k % 8].rgb;
                LED[st + k].r = (int)((col >> 16) & 255); LED[st + k].g = (int)((col >> 8) & 255); LED[st + k].b = (int)(col & 255); LED[st + k].on = 1;
            }
        } else if (fquick == 2) {                /* một màu: mọi đèn đang chọn cùng màu của đèn đầu tiên */
            led_t first = LED[st];
            for (int k = 0; k < cnt; k++) { LED[st + k] = first; LED[st + k].on = 1; }
        }
        break;
    }
    }
    fapply();
}
static void fnext_preset(void) {
    int st, cnt;
    fsel(&st, &cnt);
    int p = preset_of_rgb(LED[st].r, LED[st].g, LED[st].b);
    fset_preset(p < 0 ? 0 : p + 1);
    fapply();
}
static void ftoggle(void) {
    int st, cnt, any = 0;
    fsel(&st, &cnt);
    for (int k = 0; k < cnt; k++) if (LED[st + k].on) any = 1;
    for (int k = 0; k < cnt; k++) LED[st + k].on = !any;
    fapply();
    set_msg(any ? "Đã tắt" : "Đã bật");
}
static void toggle_mode(void) {
    if (!fmode) {                                  /* theo vùng -> từng đèn */
        if (!led_loaded) led_init_from_zones();
        fmode = 1; frame_used = 1;
        fapply();
        set_msg("Chế độ từng đèn");
    } else {                                       /* từng đèn -> theo vùng */
        fmode = 0;
        frame_leave();
        set_msg("Chế độ theo vùng");
    }
}
static void fdraw(void) {
    SDL_Color bg = {18, 18, 26, 255}, white = {235, 235, 240, 255}, grey = {140, 140, 155, 255};
    SDL_Color sel = {52, 58, 96, 255}, acc = {120, 170, 255, 255}, frame = {70, 72, 95, 255}, dark = {30, 30, 42, 255};
    rect(0, 0, W, H, bg);
    text(F_BIG, "Điều khiển LED - Từng đèn", W / 2, 16, white, 1);
    int st, cnt;
    fsel(&st, &cnt);
    led_t *c = &LED[st];
    int m_on = fmixed(F_ON), m_col = fmixed(F_R | F_G | F_B), m_r = fmixed(F_R), m_g = fmixed(F_G), m_b = fmixed(F_B), m_br = fbright_mixed();
    int y0 = 84, rh = 68;
    for (int i = 0; i < NROW; i++) {
        int y = y0 + i * rh;
        if (i == ri) rect(40, y, 600, rh - 8, sel);
        text(F_MED, FROWN[i], 64, y + 12, i == ri ? white : grey, 0);
        char v[48] = "";
        if (i == 0) snprintf(v, sizeof v, "< %s >", GNAME[fgrp]);
        else if (i == 1) { if (fgrp == 5) snprintf(v, sizeof v, "Tất cả"); else if (fidx == 0) snprintf(v, sizeof v, "< Cả nhóm >"); else snprintf(v, sizeof v, "< Đèn %d >", fidx); }
        else if (i == 2) { int p = preset_of_rgb(c->r, c->g, c->b); snprintf(v, sizeof v, "< %s >", m_col ? "Nhiều màu" : m_on ? "Một phần tắt" : p >= 0 ? PRE[p].name : "Tùy chỉnh"); }
        else if (i == 3) snprintf(v, sizeof v, m_r ? "—" : "%d", c->r);
        else if (i == 4) snprintf(v, sizeof v, m_g ? "—" : "%d", c->g);
        else if (i == 5) snprintf(v, sizeof v, m_b ? "—" : "%d", c->b);
        else if (i == 6) snprintf(v, sizeof v, m_br ? "—" : "%d%%", fbright_cur());
        else snprintf(v, sizeof v, "< %s >", QNAME[fquick]);
        text(F_MED, v, 620, y + 12, i == ri ? acc : white, 2);
        int mm = i == 3 ? m_r : i == 4 ? m_g : i == 5 ? m_b : m_br;
        if (i >= 3 && i <= 6 && !mm) {
            int val = i == 3 ? c->r : i == 4 ? c->g : i == 5 ? c->b : fbright_cur();
            int mx = i == 6 ? 100 : 255;
            SDL_Color track = {40, 42, 60, 255};
            SDL_Color fill = i == 3 ? (SDL_Color){220, 70, 70, 255} : i == 4 ? (SDL_Color){70, 200, 90, 255} : i == 5 ? (SDL_Color){80, 110, 240, 255} : (SDL_Color){230, 200, 90, 255};
            rect(300, y + 26, 190, 14, track);
            rect(300, y + 26, 190 * val / mx, 14, fill);
        }
    }

    /* sơ đồ máy: mỗi ô là một đèn, viền trắng = đang chọn */
    rect(692, 110, 296, 330, frame);
    rect(696, 114, 288, 322, bg);
    int px[NLED], py[NLED], pw[NLED], ph[NLED];
    for (int i = 0; i < 8; i++) { px[i] = 704 + i * 35; py[i] = 126; pw[i] = 30; ph[i] = 16; }       /* thanh trên */
    px[9] = 768; py[9] = 172; pw[9] = 48; ph[9] = 18;                                                  /* FN trái (vị trí 10) */
    px[8] = 860; py[8] = 172; pw[8] = 48; ph[8] = 18;                                                  /* FN phải (vị trí 9) */
    static const int RX[4] = {0, 34, 0, -34}, RY[4] = {-34, 0, 34, 0};
    for (int k = 0; k < 4; k++) {
        px[10 + k] = 770 + RX[k] - 10; py[10 + k] = 280 + RY[k] - 10; pw[10 + k] = 20; ph[10 + k] = 20;   /* vòng trái */
        px[14 + k] = 910 + RX[k] - 10; py[14 + k] = 280 + RY[k] - 10; pw[14 + k] = 20; ph[14 + k] = 20;   /* vòng phải */
        px[18 + k] = 710 + k * 68; py[18 + k] = 380; pw[18 + k] = 56; ph[18 + k] = 22;                    /* mặt sau */
    }
    for (int i = 0; i < NLED; i++) {
        int inside = i >= st && i < st + cnt;
        if (inside) rect(px[i] - 3, py[i] - 3, pw[i] + 6, ph[i] + 6, white);
        SDL_Color col = LED[i].on ? (SDL_Color){(Uint8)LED[i].r, (Uint8)LED[i].g, (Uint8)LED[i].b, 255} : dark;
        rect(px[i], py[i], pw[i], ph[i], col);
    }
    char cap[64];
    if (cnt == 1) snprintf(cap, sizeof cap, "đèn số %d  #%02X%02X%02X", st + 1, c->r, c->g, c->b);
    else snprintf(cap, sizeof cap, "%d đèn đang chọn", cnt);
    text(F_SMALL, cap, 840, 455, grey, 1);

    if (SDL_GetTicks() < msg_until) text(F_SMALL, msg, 840, 600, acc, 1);
    text(F_SMALL, "D-pad: đổi  L1/R1: nhảy  A: màu kế  X: bật/tắt  Y: theo vùng  START: lưu  B: thoát", W / 2, 726, grey, 1);
    SDL_RenderPresent(R);
}

/* chuyển lệnh theo chế độ hiện tại */
static void dispatch_change(int dir, int big) { if (fmode) fchange(dir, big); else change(dir, big); }
static void dispatch_toggle(void) { if (fmode) ftoggle(); else toggle_on(); }
static void dispatch_preset(void) { if (fmode) fnext_preset(); else next_preset(); }

/* thoát mà chưa lưu: trả đèn về đúng trạng thái lúc mở app / lần lưu gần nhất */
static void quit_all(void) {
    if (orig_mode == 0) {
        if (frame_used) { wr("effect_enable", "1"); for (int i = 0; i < NZ; i++) apply_zone(i, &ORIG[i], 1); }
        else quit_restore();
    } else {
        memcpy(LED, LED_ORIG, sizeof LED);
        for (int i = 0; i < NZ; i++) Z[i].bright = ORIG[i].bright;
        fapply();
    }
}

/* ---------- vòng lặp ---------- */
static int hx = 0;
static Uint32 next_rep = 0;
static int running = 1;

static void handle_event(const SDL_Event *e) {
    if (e->type == SDL_QUIT) running = 0;
    else if (e->type == SDL_JOYHATMOTION) {
        int hy;
        hx = (e->jhat.value & SDL_HAT_LEFT) ? -1 : (e->jhat.value & SDL_HAT_RIGHT) ? 1 : 0;
        hy = (e->jhat.value & SDL_HAT_UP) ? -1 : (e->jhat.value & SDL_HAT_DOWN) ? 1 : 0;
        if (hy) ri = (ri + hy + NROW) % NROW;
        if (hx) dispatch_change(hx, 0);
        next_rep = SDL_GetTicks() + 350;
    } else if (e->type == SDL_JOYBUTTONDOWN) {
        switch (e->jbutton.button) {
        case 0: running = 0; break;              /* B: thoát */
        case 1: dispatch_preset(); break;        /* A */
        case 2: toggle_mode(); break;            /* Y: đổi chế độ */
        case 3: dispatch_toggle(); break;        /* X */
        case 4: dispatch_change(-1, 1); break;   /* L1 */
        case 5: dispatch_change(1, 1); break;    /* R1 */
        case 7: do_save(); break;                /* START */
        default: break;
        }
    } else if (e->type == SDL_KEYDOWN) {
        switch (e->key.keysym.sym) {
        case SDLK_ESCAPE: running = 0; break;
        case SDLK_UP: ri = (ri + NROW - 1) % NROW; break;
        case SDLK_DOWN: ri = (ri + 1) % NROW; break;
        case SDLK_LEFT: dispatch_change(-1, 0); break;
        case SDLK_RIGHT: dispatch_change(1, 0); break;
        case SDLK_RETURN: dispatch_preset(); break;
        case SDLK_x: dispatch_toggle(); break;
        case SDLK_y: toggle_mode(); break;
        case SDLK_s: do_save(); break;
        default: break;
        }
    }
}

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

    /* nạp trạng thái; KHÔNG ghi gì xuống đèn lúc mở app */
    load_all();
    if (fmode && !led_loaded) fmode = 0;
    memcpy(ORIG, Z, sizeof Z);
    memcpy(LED_ORIG, LED, sizeof LED);
    orig_mode = fmode;

    int msg_shown = 0, redraw = 1;
    SDL_Event e;
    while (running) {
        if (redraw || SDL_GetTicks() < msg_until || msg_shown) {
            msg_shown = SDL_GetTicks() < msg_until;
            draw();
            redraw = 0;
        }
        /* ngủ đến khi có sự kiện; thức đúng lúc cần lặp phím giữ hoặc cần xóa thông báo */
        Uint32 now = SDL_GetTicks();
        int timeout = 1000;
        if (hx) timeout = next_rep > now ? (int)(next_rep - now) : 0;
        else if (msg_until > now) timeout = 100;
        if (SDL_WaitEventTimeout(&e, timeout)) {
            do { handle_event(&e); } while (SDL_PollEvent(&e));
            redraw = 1;
        }
        if (hx && SDL_GetTicks() >= next_rep) { dispatch_change(hx, 0); next_rep = SDL_GetTicks() + 90; redraw = 1; }
    }
    quit_all();
    TTF_Quit();
    SDL_Quit();
    return 0;
}
