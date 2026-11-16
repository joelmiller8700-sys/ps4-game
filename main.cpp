#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>

#include <orbis/Pad.h>
#include <orbis/UserService.h>

#include "../../_common/graphics.h"
#include "../../_common/log.h"

#define FRAME_WIDTH  1920
#define FRAME_HEIGHT 1080
#define FRAME_DEPTH     4

#define TILE 60
#define COLS 32
#define ROWS 18
#define PW   44
#define PH   44

#define NUM_LEVELS 55
#define LEVEL_COLS 11

std::stringstream debugLogStream;

static Scene2D *scene;
static char level[ROWS][COLS + 1];

static int padHandle = -1;
static OrbisPadData padData;

// ---------- Game state ----------
static float px, py, vx, vy;
static bool onGround = false;
static int coyote = 0, wallLock = 0, facing = 1;
static int deaths = 0, winTimer = 0, tick = 0, frameID = 0;

struct Splat { int x, y, s; };
static Splat splats[96];
static int splatCount = 0;
static unsigned int rng = 12345;

struct Rect { int x, y, w, h; };
static Rect dirty[2][8];
static int dirtyCount[2] = {0, 0};
static bool needFull[2] = {true, true};
static bool needWin[2] = {false, false};
static int splatShown[2] = {0, 0};
static int hudShown[2] = {0, 0};

static int clipX0 = 0, clipY0 = 0, clipX1 = FRAME_WIDTH, clipY1 = FRAME_HEIGHT;

static const int CS[16] = {256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98,0,98,181,237};
static const int SN[16] = {0,98,181,237,256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98};

// ---------- UI state ----------
enum { S_MENU = 0, S_LEVELS = 1, S_SETTINGS = 2, S_CREDITS = 3, S_PLAY = 4 };
static int state = S_MENU;
static int menuSel = 0, levelSel = 0, settingsSel = 0;
static int controlMode = 1;      // 0 = arrows, 1 = analog
static int unlockedLevels = 1;
static bool uiFull[2] = {true, true};
static bool pend[2][64];

// ---------- Input ----------
struct Input {
    bool up, down, left, right;
    bool dleft, dright;
    bool cross, circle, options, square;
    bool crossNew, circleNew, optionsNew;
    float ana;
};
static Input in;
static bool prevCross = false, prevCircle = false, prevOptions = false;
static int holdCnt[4] = {0, 0, 0, 0};

static int iabs(int a) { return a < 0 ? -a : a; }

static int rnd(int n) {
    rng = rng * 1103515245u + 12345u;
    return (int)((rng >> 16) % (unsigned int)n);
}

static Color mk(int r, int g, int b) {
    Color c = { (unsigned char)r, (unsigned char)g, (unsigned char)b };
    return c;
}

static bool initPad() {
    if (scePadInit() != 0) return false;

    OrbisUserServiceInitializeParams param;
    param.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    sceUserServiceInitialize(&param);

    int userID = 0;
    sceUserServiceGetInitialUser(&userID);

    padHandle = scePadOpen(userID, 0, 0, NULL);
    return padHandle >= 0;
}

static void readInput() {
    scePadReadState(padHandle, &padData);
    unsigned int btn = padData.buttons;

    int lx = (int)padData.leftStick.x - 128;
    int ly = (int)padData.leftStick.y - 128;
    int rx = (int)padData.rightStick.x - 128;
    int ry = (int)padData.rightStick.y - 128;

    in.dleft = (btn & ORBIS_PAD_BUTTON_LEFT) != 0;
    in.dright = (btn & ORBIS_PAD_BUTTON_RIGHT) != 0;
    bool dup = (btn & ORBIS_PAD_BUTTON_UP) != 0;
    bool ddown = (btn & ORBIS_PAD_BUTTON_DOWN) != 0;

    in.up = dup || ly < -60 || ry < -60;
    in.down = ddown || ly > 60 || ry > 60;
    in.left = in.dleft || lx < -60 || rx < -60;
    in.right = in.dright || lx > 60 || rx > 60;

    int ax = iabs(rx) > iabs(lx) ? rx : lx;
    float a = ax / 110.0f;
    if (a > 1.0f) a = 1.0f;
    if (a < -1.0f) a = -1.0f;
    if (a > -0.2f && a < 0.2f) a = 0.0f;
    in.ana = a;

    in.cross = (btn & ORBIS_PAD_BUTTON_CROSS) != 0;
    in.circle = (btn & ORBIS_PAD_BUTTON_CIRCLE) != 0;
    in.options = (btn & ORBIS_PAD_BUTTON_OPTIONS) != 0;
    in.square = (btn & ORBIS_PAD_BUTTON_SQUARE) != 0;

    in.crossNew = in.cross && !prevCross;
    in.circleNew = in.circle && !prevCircle;
    in.optionsNew = in.options && !prevOptions;
    prevCross = in.cross;
    prevCircle = in.circle;
    prevOptions = in.options;
}

static bool navEvent(int i, bool pressed) {
    if (!pressed) { holdCnt[i] = 0; return false; }
    holdCnt[i]++;
    if (holdCnt[i] == 1) return true;
    if (holdCnt[i] > 20 && ((holdCnt[i] - 20) % 6) == 0) return true;
    return false;
}

// ---------- Drawing primitives ----------
static void setClip(int x, int y, int w, int h) {
    clipX0 = x < 0 ? 0 : x;
    clipY0 = y < 0 ? 0 : y;
    clipX1 = (x + w) > FRAME_WIDTH ? FRAME_WIDTH : (x + w);
    clipY1 = (y + h) > FRAME_HEIGHT ? FRAME_HEIGHT : (y + h);
}

static void resetClip() {
    clipX0 = 0; clipY0 = 0; clipX1 = FRAME_WIDTH; clipY1 = FRAME_HEIGHT;
}

static void fillRect(int x, int y, int w, int h, Color c) {
    int x1 = x + w, y1 = y + h;
    if (x < clipX0) x = clipX0;
    if (y < clipY0) y = clipY0;
    if (x1 > clipX1) x1 = clipX1;
    if (y1 > clipY1) y1 = clipY1;
    for (int j = y; j < y1; j++)
        for (int i = x; i < x1; i++)
            scene->DrawPixel(i, j, c);
}

// ---------- Bitmap font (5x7) ----------
static const unsigned char FONT[41][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // D
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // G
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // L
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, // N
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // V
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, // W
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04}, // Y
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // 2
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, // 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 6
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 9
    {0x00,0x04,0x04,0x00,0x04,0x04,0x00}, // :
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // -
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // <
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // >
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}  // space
};

static int glyphIndex(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= '0' && ch <= '9') return 26 + (ch - '0');
    if (ch == ':') return 36;
    if (ch == '-') return 37;
    if (ch == '<') return 38;
    if (ch == '>') return 39;
    return 40;
}

static void drawText(int x, int y, const char *s, int scale, Color c) {
    for (; *s; s++) {
        int g = glyphIndex(*s);
        if (g != 40) {
            for (int row = 0; row < 7; row++) {
                unsigned char bits = FONT[g][row];
                for (int col = 0; col < 5; col++) {
                    if (bits & (0x10 >> col))
                        fillRect(x + col * scale, y + row * scale, scale, scale, c);
                }
            }
        }
        x += 6 * scale;
    }
}

static int textWidth(const char *s, int scale) {
    int n = (int)strlen(s);
    return n > 0 ? n * 6 * scale - scale : 0;
}

static void drawTextC(int cx, int y, const char *s, int scale, Color c) {
    drawText(cx - textWidth(s, scale) / 2, y, s, scale, c);
}

static void intToStr(int v, char *out) {
    if (v >= 10) { out[0] = (char)('0' + v / 10); out[1] = (char)('0' + v % 10); out[2] = 0; }
    else { out[0] = (char)('0' + v); out[1] = 0; }
}

// ---------- Level ----------
static void setRow(int row, int c0, int c1, char ch) {
    for (int c = c0; c <= c1; c++) level[row][c] = ch;
}

static void buildLevel() {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) level[r][c] = '.';
        level[r][COLS] = 0;
    }
    setRow(0, 0, COLS - 1, '#');
    setRow(ROWS - 1, 0, COLS - 1, '#');
    for (int r = 0; r < ROWS; r++) { level[r][0] = '#'; level[r][COLS - 1] = '#'; }

    setRow(15, 5, 8, '#');
    setRow(13, 10, 13, '#');
    setRow(11, 15, 18, '#');
    setRow(9, 20, 23, '#');
    setRow(7, 25, 30, '#');

    level[16][3] = '^';
    setRow(16, 9, 30, '^');
    level[12][12] = '^';
    level[8][22] = '^';

    level[8][14] = 'O';
    level[5][27] = 'O';

    level[6][28] = 'G';
}

static bool solidAt(int x, int y) {
    if (x < 0 || y < 0) return true;
    int c = x / TILE, r = y / TILE;
    if (c >= COLS || r >= ROWS) return true;
    return level[r][c] == '#';
}

static bool rectSolid(int x, int y, int w, int h) {
    return solidAt(x, y) || solidAt(x + w - 1, y) ||
           solidAt(x, y + h - 1) || solidAt(x + w - 1, y + h - 1);
}

static bool rectOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static bool circleHit(int cx, int cy, int rad, int rx, int ry, int rw, int rh) {
    int nx = cx < rx ? rx : (cx > rx + rw ? rx + rw : cx);
    int ny = cy < ry ? ry : (cy > ry + rh ? ry + rh : cy);
    int dx = cx - nx, dy = cy - ny;
    return dx * dx + dy * dy < rad * rad;
}

static void respawn() {
    px = (float)(TILE + 8);
    py = (float)((ROWS - 1) * TILE - PH);
    vx = 0; vy = 0;
    onGround = false;
    wallLock = 0;
    coyote = 0;
}

static void die() {
    deaths++;
    for (int i = 0; i < 12; i++) {
        if (splatCount < 96) {
            Splat &s = splats[splatCount++];
            s.x = (int)px + rnd(PW + 20) - 10;
            s.y = (int)py + rnd(PH + 20) - 10;
            s.s = 6 + rnd(10);
        }
    }
    respawn();
}

static bool touchingHazard() {
    int hx = (int)px + 4, hy = (int)py + 4, hw = PW - 8, hh = PH - 8;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            char ch = level[r][c];
            if (ch == '^') {
                if (rectOverlap(hx, hy, hw, hh, c * TILE + 14, r * TILE + 26, 32, 34)) return true;
            } else if (ch == 'O') {
                if (circleHit(c * TILE + 30, r * TILE + 30, 24, hx, hy, hw, hh)) return true;
            }
        }
    }
    return false;
}

static bool touchingGoal() {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (level[r][c] == 'G')
                if (rectOverlap((int)px, (int)py, PW, PH, c * TILE + 8, r * TILE + 16, PW, PH)) return true;
    return false;
}

static void update() {
    bool useAnalog = (controlMode == 1);
    int dir = 0;
    float scale = 1.0f;
    if (useAnalog) {
        if (in.ana != 0.0f) {
            dir = in.ana > 0.0f ? 1 : -1;
            scale = in.ana > 0.0f ? in.ana : -in.ana;
            if (scale < 0.5f) scale = 0.5f;
        }
    } else {
        dir = (in.dright ? 1 : 0) - (in.dleft ? 1 : 0);
    }
    bool left = dir < 0;
    bool right = dir > 0;

    bool run = in.square;
    bool jumpHeld = in.cross;
    bool jumpNew = in.crossNew;

    bool wallL = rectSolid((int)px - 1, (int)py, 1, PH);
    bool wallR = rectSolid((int)px + PW, (int)py, 1, PH);

    float maxSpeed = (run ? 8.0f : 5.5f) * scale;
    if (dir != 0) facing = dir;

    if (wallLock > 0) {
        wallLock--;
    } else if (dir != 0) {
        float acc = onGround ? 1.2f : 0.7f;
        vx += dir * acc;
        if (vx > maxSpeed) vx = maxSpeed;
        if (vx < -maxSpeed) vx = -maxSpeed;
    } else {
        float fr = onGround ? 1.0f : 0.3f;
        if (vx > fr) vx -= fr;
        else if (vx < -fr) vx += fr;
        else vx = 0;
    }

    vy += 0.9f;
    if (vy > 18.0f) vy = 18.0f;
    bool sliding = !onGround && vy > 0 && ((wallL && left) || (wallR && right));
    if (sliding && vy > 3.0f) vy = 3.0f;

    if (onGround) coyote = 6; else if (coyote > 0) coyote--;

    if (jumpNew) {
        if (coyote > 0) {
            vy = -17.0f;
            coyote = 0;
        } else if (wallL || wallR) {
            vy = -16.0f;
            vx = wallL ? 8.0f : -8.0f;
            wallLock = 9;
            facing = wallL ? 1 : -1;
        }
    }
    if (!jumpHeld && vy < -6.0f) vy = -6.0f;

    px += vx;
    if (rectSolid((int)px, (int)py, PW, PH)) {
        if (vx > 0) px = (float)((((int)px + PW - 1) / TILE) * TILE - PW);
        else if (vx < 0) px = (float)((((int)px) / TILE + 1) * TILE);
        vx = 0;
    }

    py += vy;
    onGround = false;
    if (rectSolid((int)px, (int)py, PW, PH)) {
        if (vy > 0) {
            py = (float)((((int)py + PH - 1) / TILE) * TILE - PH);
            onGround = true;
        } else if (vy < 0) {
            py = (float)((((int)py) / TILE + 1) * TILE);
        }
        vy = 0;
    }

    if (touchingHazard()) { die(); return; }
    if (touchingGoal()) {
        winTimer = 150;
        needWin[0] = true;
        needWin[1] = true;
    }
}

// ---------- Game drawing ----------
static void drawSpike(int c, int r) {
    int cx = c * TILE + TILE / 2;
    int top = r * TILE + 20;
    for (int j = 0; j < 40; j++) {
        int half = (j * 26) / 39;
        fillRect(cx - half, top + j, 2 * half + 1, 1, mk(205, 205, 215));
    }
}

static void drawSaw(int cx, int cy) {
    int a = (tick / 2) & 15;
    int cc = CS[a], ss = SN[a];
    for (int dy = -26; dy <= 26; dy++) {
        for (int dx = -26; dx <= 26; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > 26 * 26) continue;
            int u = (dx * cc + dy * ss) >> 8;
            int v = (-dx * ss + dy * cc) >> 8;
            if (d2 < 36) {
                scene->DrawPixel(cx + dx, cy + dy, mk(50, 50, 55));
            } else if (d2 <= 20 * 20) {
                scene->DrawPixel(cx + dx, cy + dy, mk(165, 165, 178));
            } else if (iabs(u) < 6 || iabs(v) < 6 || iabs(u - v) < 8 || iabs(u + v) < 8) {
                scene->DrawPixel(cx + dx, cy + dy, mk(215, 215, 225));
            }
        }
    }
}

static void drawFace(int x, int y, int dirFace, Color eyeWhite) {
    fillRect(x + 9, y + 12, 10, 14, eyeWhite);
    fillRect(x + 25, y + 12, 10, 14, eyeWhite);
    int off = dirFace > 0 ? 5 : 0;
    fillRect(x + 9 + off, y + 16, 5, 8, mk(0, 0, 0));
    fillRect(x + 25 + off, y + 16, 5, 8, mk(0, 0, 0));
}

static void drawTiles() {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            char ch = level[r][c];
            int x = c * TILE, y = r * TILE;
            if (ch == '#') {
                fillRect(x, y, TILE, TILE, mk(58, 58, 70));
                if (r > 0 && level[r - 1][c] != '#')
                    fillRect(x, y, TILE, 5, mk(120, 120, 140));
            } else if (ch == '^') {
                drawSpike(c, r);
            } else if (ch == 'G') {
                int gx = x + 8, gy = y + 16;
                fillRect(gx, gy, PW, PH, mk(255, 140, 170));
                fillRect(gx, gy + 3, PW, 7, mk(240, 230, 200));
                drawFace(gx, gy, -1, mk(255, 255, 255));
            }
        }
    }
}

static void drawSplat(const Splat &s) {
    fillRect(s.x, s.y, s.s, s.s, mk(140, 10, 20));
}

static void addDirty(int b, int x, int y, int w, int h) {
    if (dirtyCount[b] < 8) {
        Rect &d = dirty[b][dirtyCount[b]++];
        d.x = x; d.y = y; d.w = w; d.h = h;
    }
}

static void render(int b) {
    Color bg = mk(22, 22, 30);

    if (winTimer > 0) {
        if (needWin[b]) {
            resetClip();
            fillRect(0, 0, FRAME_WIDTH, FRAME_HEIGHT, mk(20, 150, 60));
            fillRect(860, 440, 200, 200, mk(255, 140, 170));
            needWin[b] = false;
        }
        return;
    }

    if (needFull[b]) {
        resetClip();
        fillRect(0, 0, FRAME_WIDTH, FRAME_HEIGHT, bg);
        drawTiles();
        for (int i = 0; i < splatCount; i++) drawSplat(splats[i]);
        splatShown[b] = splatCount;
        hudShown[b] = 0;
        dirtyCount[b] = 0;
        needFull[b] = false;
    } else {
        for (int i = 0; i < dirtyCount[b]; i++) {
            Rect d = dirty[b][i];
            setClip(d.x, d.y, d.w, d.h);
            fillRect(d.x, d.y, d.w, d.h, bg);
            drawTiles();
            for (int k = 0; k < splatCount; k++) drawSplat(splats[k]);
        }
        resetClip();
        dirtyCount[b] = 0;
    }

    for (int i = splatShown[b]; i < splatCount; i++) drawSplat(splats[i]);
    splatShown[b] = splatCount;

    int hudN = deaths < 40 ? deaths : 40;
    for (int i = hudShown[b]; i < hudN; i++)
        fillRect(70 + i * 22, 70, 14, 14, mk(205, 25, 35));
    hudShown[b] = hudN;

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (level[r][c] == 'O') {
                int cx = c * TILE + 30, cy = r * TILE + 30;
                drawSaw(cx, cy);
                addDirty(b, cx - 27, cy - 27, 55, 55);
            }
        }
    }

    int x = (int)px, y = (int)py;
    fillRect(x, y, PW, PH, mk(140, 0, 15));
    fillRect(x + 3, y + 3, PW - 6, PH - 6, mk(205, 25, 35));
    drawFace(x, y, facing, mk(255, 255, 255));
    fillRect(x + 15, y + 32, 14, 4, mk(120, 0, 12));
    addDirty(b, x - 2, y - 2, PW + 4, PH + 4);
}

// ---------- UI ----------
static void markPend(int i) {
    if (i < 0 || i >= 64) return;
    pend[0][i] = true;
    pend[1][i] = true;
}

static void setState(int s) {
    state = s;
    uiFull[0] = true;
    uiFull[1] = true;
}

static void startLevel() {
    buildLevel();
    respawn();
    deaths = 0;
    splatCount = 0;
    winTimer = 0;
    needFull[0] = true;
    needFull[1] = true;
    needWin[0] = false;
    needWin[1] = false;
    state = S_PLAY;
}

static int itemCount() {
    if (state == S_MENU) return 4;
    if (state == S_LEVELS) return NUM_LEVELS + 1;
    if (state == S_SETTINGS) return 2;
    return 0;
}

static void drawBigCube(int x, int y) {
    fillRect(x, y, 88, 88, mk(140, 0, 15));
    fillRect(x + 6, y + 6, 76, 76, mk(205, 25, 35));
    fillRect(x + 18, y + 24, 20, 28, mk(255, 255, 255));
    fillRect(x + 50, y + 24, 20, 28, mk(255, 255, 255));
    fillRect(x + 28, y + 32, 10, 16, mk(0, 0, 0));
    fillRect(x + 60, y + 32, 10, 16, mk(0, 0, 0));
    fillRect(x + 30, y + 64, 28, 8, mk(120, 0, 12));
}

static void drawBanner(const char *title, int scale, int y, int h, bool cubes) {
    fillRect(460, y, 1000, h, mk(35, 35, 50));
    drawTextC(960, y + (h - 7 * scale) / 2, title, scale, mk(225, 40, 50));
    if (cubes) {
        drawBigCube(500, y + (h - 88) / 2);
        drawBigCube(1332, y + (h - 88) / 2);
    }
}

static void drawHint(const char *s) {
    int w = textWidth(s, 3) + 80;
    fillRect(960 - w / 2, 980, w, 60, mk(35, 35, 50));
    drawTextC(960, 980 + 19, s, 3, mk(200, 200, 215));
}

static void drawLock(int cx, int cy) {
    Color g = mk(140, 140, 155);
    fillRect(cx - 18, cy - 2, 36, 28, g);
    fillRect(cx - 12, cy - 26, 24, 6, g);
    fillRect(cx - 12, cy - 22, 6, 20, g);
    fillRect(cx + 6, cy - 22, 6, 20, g);
    fillRect(cx - 3, cy + 6, 6, 10, mk(40, 40, 50));
}

static void drawStatic() {
    if (state == S_MENU) {
        drawBanner("MEAT CUBE", 12, 140, 150, true);
        drawHint("X: SELECT");
    } else if (state == S_LEVELS) {
        drawBanner("LEVELS", 8, 130, 110, false);
        drawHint("X: PLAY   O: BACK");
    } else if (state == S_SETTINGS) {
        drawBanner("SETTINGS", 8, 130, 110, false);
        fillRect(610, 700, 700, 60, mk(35, 35, 50));
        drawTextC(960, 700 + 19, "LEFT RIGHT OR X: CHANGE", 3, mk(200, 200, 215));
        drawHint("O: BACK");
    } else if (state == S_CREDITS) {
        drawBanner("CREDITS", 8, 130, 110, false);
        fillRect(460, 300, 1000, 540, mk(35, 35, 50));
        drawTextC(960, 350, "GAME DESIGNERS", 5, mk(200, 200, 215));
        drawTextC(960, 470, "KAREEM", 10, mk(225, 40, 50));
        drawTextC(960, 600, "CLAUDE", 10, mk(225, 40, 50));
        drawTextC(960, 760, "MEAT CUBE", 4, mk(140, 140, 155));
        drawHint("O: BACK");
    }
}

static void drawItem(int i) {
    if (state == S_MENU) {
        if (i < 0 || i > 3) return;
        const char *labels[4] = { "LEVELS", "SETTINGS", "CREDITS", "EXIT" };
        int x = 610, y = 420 + i * 120;
        bool sel = (i == menuSel);
        fillRect(x, y, 700, 90, sel ? mk(205, 25, 35) : mk(45, 45, 62));
        drawTextC(960, y + 24, labels[i], 6, sel ? mk(255, 255, 255) : mk(200, 200, 215));
    } else if (state == S_LEVELS) {
        if (i >= 0 && i < NUM_LEVELS) {
            int col = i % LEVEL_COLS, row = i / LEVEL_COLS;
            int x = 375 + col * 108, y = 330 + row * 108;
            bool sel = (i == levelSel);
            bool unlocked = (i < unlockedLevels);
            if (sel) fillRect(x, y, 90, 90, mk(245, 245, 255));
            int inset = sel ? 6 : 0;
            fillRect(x + inset, y + inset, 90 - 2 * inset, 90 - 2 * inset,
                     unlocked ? mk(205, 25, 35) : mk(50, 50, 62));
            if (unlocked) {
                char buf[4];
                intToStr(i + 1, buf);
                drawTextC(x + 45, y + 24, buf, 6, mk(255, 255, 255));
            } else {
                drawLock(x + 45, y + 45);
            }
        } else if (i == NUM_LEVELS) {
            fillRect(610, 880, 700, 60, mk(35, 35, 50));
            char buf[64];
            char n[4];
            strcpy(buf, "LEVEL ");
            intToStr(levelSel + 1, n);
            strcat(buf, n);
            Color c;
            if (levelSel < unlockedLevels) { strcat(buf, ": PRESS X TO PLAY"); c = mk(120, 230, 140); }
            else { strcat(buf, ": LOCKED"); c = mk(255, 170, 60); }
            drawTextC(960, 880 + 16, buf, 4, c);
        }
    } else if (state == S_SETTINGS) {
        if (i < 0 || i > 1) return;
        int x = 510, y = 420 + i * 120;
        bool sel = (i == settingsSel);
        fillRect(x, y, 900, 90, sel ? mk(205, 25, 35) : mk(45, 45, 62));
        const char *label;
        if (i == 0) label = controlMode == 1 ? "CONTROLS: ANALOG" : "CONTROLS: ARROWS";
        else label = "BACK";
        drawTextC(960, y + 24, label, 6, sel ? mk(255, 255, 255) : mk(200, 200, 215));
    }
}

static void drawUI(int b) {
    resetClip();
    if (uiFull[b]) {
        scene->FrameBufferClear();
        drawStatic();
        int n = itemCount();
        for (int i = 0; i < n; i++) drawItem(i);
        uiFull[b] = false;
        for (int i = 0; i < 64; i++) pend[b][i] = false;
    } else {
        for (int i = 0; i < 64; i++) {
            if (pend[b][i]) {
                drawItem(i);
                pend[b][i] = false;
            }
        }
    }
}

static void updateUI() {
    bool nu = navEvent(0, in.up);
    bool nd = navEvent(1, in.down);
    bool nl = navEvent(2, in.left);
    bool nr = navEvent(3, in.right);
    bool ok = in.crossNew;
    bool back = in.circleNew;

    if (state == S_MENU) {
        int old = menuSel;
        if (nu) menuSel = (menuSel + 3) % 4;
        if (nd) menuSel = (menuSel + 1) % 4;
        if (menuSel != old) { markPend(old); markPend(menuSel); }
        if (ok) {
            if (menuSel == 0) setState(S_LEVELS);
            else if (menuSel == 1) { settingsSel = 0; setState(S_SETTINGS); }
            else if (menuSel == 2) setState(S_CREDITS);
            else exit(0);
        }
    } else if (state == S_LEVELS) {
        int old = levelSel, n = levelSel;
        if (nl && n > 0) n--;
        if (nr && n < NUM_LEVELS - 1) n++;
        if (nu && n >= LEVEL_COLS) n -= LEVEL_COLS;
        if (nd && n + LEVEL_COLS <= NUM_LEVELS - 1) n += LEVEL_COLS;
        if (n != old) {
            levelSel = n;
            markPend(old);
            markPend(n);
            markPend(NUM_LEVELS);
        }
        if (ok && levelSel < unlockedLevels) startLevel();
        else if (back) setState(S_MENU);
    } else if (state == S_SETTINGS) {
        int old = settingsSel;
        if (nu || nd) settingsSel = 1 - settingsSel;
        if (settingsSel != old) { markPend(old); markPend(settingsSel); }
        if (settingsSel == 0) {
            if (nl || nr || ok) {
                controlMode = 1 - controlMode;
                markPend(0);
            }
        } else if (ok) {
            setState(S_MENU);
        }
        if (back) setState(S_MENU);
    } else if (state == S_CREDITS) {
        if (ok || back) setState(S_MENU);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    DEBUGLOG << "Creating a scene";
    scene = new Scene2D(FRAME_WIDTH, FRAME_HEIGHT, FRAME_DEPTH);
    if (!scene->Init(0xC000000, 2)) {
        DEBUGLOG << "Failed to initialize 2D scene";
        for (;;);
    }

    DEBUGLOG << "Initializing controller";
    if (!initPad()) {
        DEBUGLOG << "Failed to initialize controller";
        for (;;);
    }

    buildLevel();
    respawn();
    setState(S_MENU);

    for (;;) {
        int b = frameID & 1;

        readInput();

        if (state == S_PLAY) {
            if (in.optionsNew) {
                setState(S_LEVELS);
            } else if (winTimer > 0) {
                winTimer--;
                if (winTimer == 0) setState(S_LEVELS);
            } else {
                update();
            }
        } else {
            updateUI();
        }

        if (state == S_PLAY) render(b);
        else drawUI(b);

        scene->SubmitFlip(frameID);
        scene->FrameWait(frameID);
        scene->FrameBufferSwap();
        frameID++;
        tick++;
    }
    return 0;
}
