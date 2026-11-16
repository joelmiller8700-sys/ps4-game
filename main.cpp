#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>
#include <math.h>

#include <orbis/Pad.h>
#include <orbis/UserService.h>

#include "../../_common/graphics.h"
#include "../../_common/log.h"

#define FRAME_WIDTH 1920
#define FRAME_HEIGHT 1080
#define FRAME_DEPTH 4

#define TILE 60
#define COLS 168
#define ROWS 18
#define ROOM_COLS (FRAME_WIDTH / TILE)
#define WORLD_WIDTH (COLS * TILE)
#define WORLD_HEIGHT (ROWS * TILE)
#define LAST_ROOM ((WORLD_WIDTH - 1) / FRAME_WIDTH)

#define PW 44
#define PH 44

#define NUM_LEVELS 55
#define LEVEL_COLS 11

#define NUM_STARS 75
#define MAX_SPLATS 200
#define MAX_PARTS 80
#define MAX_DIRTY 112

std::stringstream debugLogStream;

static Scene2D *scene;
static char level[ROWS][COLS + 1];

static int padHandle = -1;
static OrbisPadData padData;

// ---------- Game state ----------
static float px, py, vx, vy;
static int roomIdx = 0;

static float squashX = 1.0f;
static float squashY = 1.0f;

static bool onGround = false;
static int coyote = 0;
static int wallLock = 0;
static int facing = 1;

static int deaths = 0;
static int winTimer = 0;
static int deadTimer = 0;
static int tick = 0;
static int frameID = 0;

static unsigned int rng = 12345;

struct Splat {
    short x, y, w, h, room;
};

static Splat splats[MAX_SPLATS];
static int splatCount = 0;
static bool splatDrawn[2][MAX_SPLATS];

struct Part {
    float x, y, vx, vy;
    short life, maxLife;
    unsigned char kind, size;
};

static Part parts[MAX_PARTS];

struct Rect {
    int x, y, w, h;
};

static Rect dirty[2][MAX_DIRTY];
static int dirtyCount[2] = {0, 0};

static bool needFull[2] = {true, true};
static bool needWin[2] = {false, false};
static int bufRoom[2] = {0, 0};
static char bufGrid[2][ROWS][ROOM_COLS];
static bool bufHi[2][ROWS][ROOM_COLS];
static int bufStar[2] = {0, 0};
static int hudSq[2] = {0, 0};
static int hudProg[2] = {0, 0};
static bool hudNeed[2] = {false, false};

static int clipX0 = 0;
static int clipY0 = 0;
static int clipX1 = FRAME_WIDTH;
static int clipY1 = FRAME_HEIGHT;

static const int CS[16] = {
    256,237,181,98,0,-98,-181,-237,
    -256,-237,-181,-98,0,98,181,237
};

static const int SN[16] = {
    0,98,181,237,256,237,181,98,
    0,-98,-181,-237,-256,-237,-181,-98
};

// ---------- UI state ----------
enum {
    S_MENU = 0,
    S_LEVELS = 1,
    S_SETTINGS = 2,
    S_CREDITS = 3,
    S_PLAY = 4
};

static int state = S_MENU;
static int menuSel = 0;
static int levelSel = 0;
static int settingsSel = 0;

static int controlMode = 1;
static int unlockedLevels = 1;

static bool uiFull[2] = {true, true};
static bool pend[2][64];

// ---------- Input ----------
struct Input {
    bool up, down, left, right;
    bool dleft, dright;

    bool cross, circle, options, square;

    bool crossNew;
    bool circleNew;
    bool optionsNew;

    float ana;
};

static Input in;

static bool prevCross = false;
static bool prevCircle = false;
static bool prevOptions = false;

static int holdCnt[4] = {0, 0, 0, 0};

static int iabs(int a) {
    return a < 0 ? -a : a;
}

static int rnd(int n) {
    rng = rng * 1103515245u + 12345u;
    return (int)((rng >> 16) % (unsigned int)n);
}

static Color mk(int r, int g, int b) {
    Color c = {
        (unsigned char)r,
        (unsigned char)g,
        (unsigned char)b
    };
    return c;
}

// ---------- Controller ----------
static bool initPad() {
    if (scePadInit() != 0)
        return false;

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

    if (a > -0.2f && a < 0.2f)
        a = 0.0f;

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
    if (!pressed) {
        holdCnt[i] = 0;
        return false;
    }

    holdCnt[i]++;

    if (holdCnt[i] == 1)
        return true;

    if (holdCnt[i] > 20 &&
        ((holdCnt[i] - 20) % 6) == 0)
        return true;

    return false;
}

// ---------- Drawing primitives ----------
static void setClip(int x, int y, int w, int h) {
    clipX0 = x < 0 ? 0 : x;
    clipY0 = y < 0 ? 0 : y;

    clipX1 = (x + w) > FRAME_WIDTH ?
             FRAME_WIDTH : (x + w);

    clipY1 = (y + h) > FRAME_HEIGHT ?
             FRAME_HEIGHT : (y + h);
}

static void resetClip() {
    clipX0 = 0;
    clipY0 = 0;
    clipX1 = FRAME_WIDTH;
    clipY1 = FRAME_HEIGHT;
}

static void fillRect(int x, int y, int w, int h, Color c) {
    if (w <= 0 || h <= 0)
        return;

    int x1 = x + w;
    int y1 = y + h;

    if (x < clipX0) x = clipX0;
    if (y < clipY0) y = clipY0;

    if (x1 > clipX1) x1 = clipX1;
    if (y1 > clipY1) y1 = clipY1;

    if (x >= x1 || y >= y1)
        return;

    for (int j = y; j < y1; j++) {
        for (int i = x; i < x1; i++) {
            scene->DrawPixel(i, j, c);
        }
    }
}

// ---------- Font ----------
static const unsigned char FONT[41][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04},
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},

    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},

    {0x00,0x04,0x04,0x00,0x04,0x04,0x00},
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02},
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};

static int glyphIndex(char ch) {
    if (ch >= 'A' && ch <= 'Z')
        return ch - 'A';

    if (ch >= '0' && ch <= '9')
        return 26 + ch - '0';

    if (ch == ':') return 36;
    if (ch == '-') return 37;
    if (ch == '<') return 38;
    if (ch == '>') return 39;

    return 40;
}

static void drawText(int x, int y, const char *s,
                     int scale, Color c) {
    for (; *s; s++) {
        int g = glyphIndex(*s);

        if (g != 40) {
            for (int row = 0; row < 7; row++) {
                unsigned char bits = FONT[g][row];

                for (int col = 0; col < 5; col++) {
                    if (bits & (0x10 >> col)) {
                        fillRect(
                            x + col * scale,
                            y + row * scale,
                            scale,
                            scale,
                            c
                        );
                    }
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

static void drawTextC(int cx, int y, const char *s,
                      int scale, Color c) {
    drawText(cx - textWidth(s, scale) / 2,
             y, s, scale, c);
}

static void intToStr(int v, char *out) {
    if (v >= 10) {
        out[0] = (char)('0' + v / 10);
        out[1] = (char)('0' + v % 10);
        out[2] = 0;
    } else {
        out[0] = (char)('0' + v);
        out[1] = 0;
    }
}

// ---------- Level construction ----------
static void setRow(int row, int c0, int c1, char ch) {
    if (row < 0 || row >= ROWS)
        return;

    if (c0 < 0) c0 = 0;
    if (c1 >= COLS) c1 = COLS - 1;

    for (int c = c0; c <= c1; c++)
        level[row][c] = ch;
}

static void platform(int row, int start, int end) {
    setRow(row, start, end, '#');
}

static void spikes(int row, int start, int end) {
    setRow(row, start, end, '^');
}

static void saw(int row, int col) {
    if (row >= 0 && row < ROWS &&
        col >= 0 && col < COLS) {
        level[row][col] = 'O';
    }
}

static void buildLevel() {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++)
            level[r][c] = '.';

        level[r][COLS] = 0;
    }

    setRow(0, 0, COLS - 1, '#');
    setRow(ROWS - 1, 0, COLS - 1, '#');

    for (int r = 0; r < ROWS; r++) {
        level[r][0] = '#';
        level[r][COLS - 1] = '#';
    }

    setRow(17, 1, COLS - 2, '#');

    // SECTION 1
    platform(15, 5, 8);
    platform(13, 10, 13);
    platform(11, 15, 18);
    platform(13, 21, 24);
    platform(11, 27, 30);

    spikes(16, 7, 9);
    spikes(16, 17, 20);

    saw(12, 25);

    // SECTION 2
    platform(14, 34, 38);
    platform(12, 40, 43);
    platform(10, 45, 49);
    platform(13, 51, 54);

    spikes(16, 33, 37);
    spikes(16, 42, 46);

    saw(11, 53);

    // SECTION 3
    platform(14, 57, 61);
    platform(11, 63, 67);
    platform(9, 69, 73);
    platform(12, 75, 79);

    spikes(16, 60, 64);
    spikes(16, 70, 74);

    saw(10, 78);

    // SECTION 4
    platform(14, 82, 86);
    platform(12, 88, 91);
    platform(10, 93, 97);
    platform(13, 99, 103);

    spikes(16, 85, 89);
    spikes(16, 95, 99);

    saw(11, 101);

    // SECTION 5
    platform(15, 106, 109);
    platform(12, 111, 115);
    platform(9, 117, 121);
    platform(12, 123, 127);

    spikes(16, 108, 112);
    spikes(16, 119, 123);

    saw(10, 125);

    // SECTION 6
    platform(14, 130, 134);
    platform(11, 136, 140);
    platform(9, 142, 146);
    platform(12, 148, 152);
    platform(10, 154, 158);

    spikes(16, 132, 136);
    spikes(16, 144, 148);
    spikes(16, 156, 160);

    saw(10, 151);
    saw(8, 161);

    // Finish area
    platform(14, 162, 165);

    level[15][165] = 'G';
}

// ---------- Collision ----------
static char tileAt(int r, int c) {
    if (r < 0 || r >= ROWS || c < 0 || c >= COLS)
        return '#';

    return level[r][c];
}

static char staticChar(int r, int c) {
    char ch = tileAt(r, c);

    if (ch == 'O')
        return '.';

    return ch;
}

static bool hiAt(int r, int c) {
    return r > 0 &&
           staticChar(r, c) == '#' &&
           staticChar(r - 1, c) != '#';
}

static bool solidAt(int x, int y) {
    if (x < 0 || y < 0)
        return true;

    int c = x / TILE;
    int r = y / TILE;

    if (c >= COLS || r >= ROWS)
        return true;

    return level[r][c] == '#';
}

static bool rectSolid(int x, int y, int w, int h) {
    return solidAt(x, y) ||
           solidAt(x + w - 1, y) ||
           solidAt(x, y + h - 1) ||
           solidAt(x + w - 1, y + h - 1);
}

static bool rectOverlap(
    int ax, int ay, int aw, int ah,
    int bx, int by, int bw, int bh
) {
    return ax < bx + bw &&
           ax + aw > bx &&
           ay < by + bh &&
           ay + ah > by;
}

static bool circleHit(
    int cx, int cy, int rad,
    int rx, int ry, int rw, int rh
) {
    int nx = cx < rx ? rx :
             (cx > rx + rw ? rx + rw : cx);

    int ny = cy < ry ? ry :
             (cy > ry + rh ? ry + rh : cy);

    int dx = cx - nx;
    int dy = cy - ny;

    return dx * dx + dy * dy < rad * rad;
}

// ---------- Blood stains and particles ----------
static void addSplat(int wx, int wy, int w, int h) {
    if (splatCount >= MAX_SPLATS)
        return;

    if (wx < 0 || wy < 0)
        return;

    int room = wx / FRAME_WIDTH;

    if (room > LAST_ROOM)
        return;

    Splat &s = splats[splatCount++];

    s.room = (short)room;
    s.x = (short)(wx - room * FRAME_WIDTH - w / 2);
    s.y = (short)(wy - h / 2);
    s.w = (short)w;
    s.h = (short)h;
}

static void spawnPart(
    float x, float y,
    float pvx, float pvy,
    int life, int size, int kind
) {
    for (int i = 0; i < MAX_PARTS; i++) {
        if (parts[i].life <= 0) {
            Part &p = parts[i];

            p.x = x;
            p.y = y;
            p.vx = pvx;
            p.vy = pvy;
            p.life = (short)life;
            p.maxLife = (short)life;
            p.kind = (unsigned char)kind;
            p.size = (unsigned char)size;

            return;
        }
    }
}

static void spawnDust(float x, float y, int count, float spread) {
    for (int i = 0; i < count; i++) {
        float dvx = ((float)(rnd(100) - 50) / 50.0f) * spread;
        float dvy = -((float)rnd(100) / 100.0f) * 1.6f;

        spawnPart(
            x + (float)(rnd(20) - 10),
            y - (float)rnd(4),
            dvx, dvy,
            16 + rnd(12),
            7 + rnd(5),
            0
        );
    }
}

static void updateParts() {
    for (int i = 0; i < MAX_PARTS; i++) {
        Part &p = parts[i];

        if (p.life <= 0)
            continue;

        p.life--;

        if (p.kind == 0) {
            p.x += p.vx;
            p.y += p.vy;
            p.vx *= 0.9f;
            p.vy *= 0.92f;
        } else {
            float ox = p.x;
            float oy = p.y;

            p.vy += 0.55f;

            if (p.vy > 16.0f)
                p.vy = 16.0f;

            p.x += p.vx;
            p.y += p.vy;

            if (solidAt((int)p.x, (int)p.y)) {
                if (rnd(100) < 45) {
                    int s = p.size;

                    if (solidAt((int)ox, (int)p.y)) {
                        addSplat((int)ox, (int)oy,
                                 s + 4 + rnd(8), s / 2 + 2);
                    } else {
                        addSplat((int)ox, (int)oy,
                                 s / 2 + 2, s + 4 + rnd(8));
                    }
                }

                p.life = 0;
            } else if (p.y > (float)WORLD_HEIGHT) {
                p.life = 0;
            }
        }
    }
}

// ---------- Player ----------
static void respawn() {
    px = (float)(TILE + 8);
    py = (float)((ROWS - 1) * TILE - PH);

    vx = 0;
    vy = 0;

    roomIdx = 0;

    squashX = 1.0f;
    squashY = 1.0f;

    onGround = false;
    wallLock = 0;
    coyote = 0;
}

static void die() {
    deaths++;

    float cx = px + PW / 2.0f;
    float cy = py + PH / 2.0f;

    for (int i = 0; i < 26; i++) {
        float bvx = (float)(rnd(180) - 90) / 10.0f;
        float bvy = (float)(rnd(140) - 120) / 10.0f;

        spawnPart(
            cx + (float)(rnd(20) - 10),
            cy + (float)(rnd(20) - 10),
            bvx, bvy,
            90,
            5 + rnd(5),
            1
        );
    }

    for (int i = 0; i < 4; i++) {
        addSplat(
            (int)cx + rnd(40) - 20,
            (int)cy + rnd(40) - 20,
            8 + rnd(10),
            8 + rnd(10)
        );
    }

    vx = 0;
    vy = 0;
    deadTimer = 28;
}

static bool touchingHazard() {
    int hx = (int)px + 4;
    int hy = (int)py + 4;
    int hw = PW - 8;
    int hh = PH - 8;

    int c0 = ((int)px / TILE) - 2;
    int c1 = ((int)px + PW) / TILE + 2;

    if (c0 < 0) c0 = 0;
    if (c1 >= COLS) c1 = COLS - 1;

    for (int r = 0; r < ROWS; r++) {
        for (int c = c0; c <= c1; c++) {
            char ch = level[r][c];

            if (ch == '^') {
                if (rectOverlap(
                    hx, hy, hw, hh,
                    c * TILE + 14,
                    r * TILE + 26,
                    32, 34
                )) return true;
            }

            if (ch == 'O') {
                if (circleHit(
                    c * TILE + 30,
                    r * TILE + 30,
                    24,
                    hx, hy, hw, hh
                )) return true;
            }
        }
    }

    return false;
}

static bool touchingGoal() {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (level[r][c] == 'G') {
                if (rectOverlap(
                    (int)px, (int)py, PW, PH,
                    c * TILE + 8,
                    r * TILE + 16,
                    PW, PH
                )) return true;
            }
        }
    }

    return false;
}

// ---------- Physics ----------
static void update() {
    updateParts();

    if (deadTimer > 0) {
        deadTimer--;

        if (deadTimer == 0)
            respawn();

        return;
    }

    bool wasGround = onGround;

    {
        float k = 0.0f;

        if (!onGround) {
            k = iabs((int)vy) / 18.0f;
            if (k > 1.0f) k = 1.0f;
        }

        float tx = 1.0f - 0.22f * k;
        float ty = 1.0f + 0.30f * k;

        squashX += (tx - squashX) * 0.3f;
        squashY += (ty - squashY) * 0.3f;
    }

    int dir = 0;
    float scale = 1.0f;

    if (controlMode == 1) {
        if (in.ana != 0.0f) {
            dir = in.ana > 0 ? 1 : -1;
            scale = in.ana > 0 ? in.ana : -in.ana;

            if (scale < 0.5f)
                scale = 0.5f;
        }
    } else {
        dir = (in.dright ? 1 : 0) -
              (in.dleft ? 1 : 0);
    }

    bool left = dir < 0;
    bool right = dir > 0;

    bool run = in.square;
    bool jumpHeld = in.cross;
    bool jumpNew = in.crossNew;

    bool wallL = rectSolid((int)px - 1, (int)py, 1, PH);
    bool wallR = rectSolid((int)px + PW, (int)py, 1, PH);

    float maxSpeed = (run ? 8.0f : 5.5f) * scale;

    if (dir != 0)
        facing = dir;

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

    if (vy > 18.0f)
        vy = 18.0f;

    bool sliding =
        !onGround &&
        vy > 0 &&
        ((wallL && left) || (wallR && right));

    if (sliding && vy > 3.0f)
        vy = 3.0f;

    if (sliding && (tick & 3) == 0)
        spawnDust(wallL ? px : px + PW, py + PH - 6.0f, 1, 1.0f);

    if (onGround)
        coyote = 6;
    else if (coyote > 0)
        coyote--;

    if (jumpNew) {
        if (coyote > 0) {
            vy = -17.0f;
            coyote = 0;

            squashX = 0.72f;
            squashY = 1.30f;

            spawnDust(px + PW / 2.0f, py + PH, 6, 3.0f);
        } else if (wallL || wallR) {
            vy = -16.0f;
            vx = wallL ? 8.0f : -8.0f;

            wallLock = 9;
            facing = wallL ? 1 : -1;

            squashX = 0.72f;
            squashY = 1.30f;

            spawnDust(wallL ? px : px + PW, py + PH / 2.0f, 4, 2.0f);
        }
    }

    if (!jumpHeld && vy < -6.0f)
        vy = -6.0f;

    // Horizontal collision
    px += vx;

    if (rectSolid((int)px, (int)py, PW, PH)) {
        if (vx > 0) {
            px = (float)(
                (((int)px + PW - 1) / TILE) * TILE - PW
            );
        } else if (vx < 0) {
            px = (float)(
                (((int)px) / TILE + 1) * TILE
            );
        }

        vx = 0;
    }

    // Vertical collision
    py += vy;
    onGround = false;

    if (rectSolid((int)px, (int)py, PW, PH)) {
        if (vy > 0) {
            py = (float)(
                (((int)py + PH - 1) / TILE) * TILE - PH
            );

            onGround = true;

            if (!wasGround && vy > 5.0f) {
                float k = vy / 18.0f;

                if (k > 1.0f)
                    k = 1.0f;

                squashY = 1.0f - 0.35f * k;
                squashX = 1.0f + 0.40f * k;

                spawnDust(
                    px + PW / 2.0f,
                    py + PH,
                    4 + (int)(k * 6.0f),
                    4.0f
                );
            }
        } else if (vy < 0) {
            py = (float)(
                (((int)py) / TILE + 1) * TILE
            );
        }

        vy = 0;
    }

    if (touchingHazard()) {
        die();
        return;
    }

    if (touchingGoal()) {
        winTimer = 150;
        needWin[0] = true;
        needWin[1] = true;
    }

    // Room (screen) selection
    int nr = ((int)px + PW / 2) / FRAME_WIDTH;

    if (nr < 0) nr = 0;
    if (nr > LAST_ROOM) nr = LAST_ROOM;

    roomIdx = nr;
}

// ---------- Game drawing ----------
static void drawSpike(int cx, int r) {
    int center = cx * TILE + TILE / 2;
    int top = r * TILE + 20;

    for (int j = 0; j < 40; j++) {
        int half = (j * 26) / 39;

        fillRect(
            center - half,
            top + j,
            2 * half + 1,
            1,
            mk(205, 205, 215)
        );
    }
}

static void drawSaw(int cx, int cy) {
    int a = (tick / 2) & 15;

    int cc = CS[a];
    int ss = SN[a];

    for (int dy = -26; dy <= 26; dy++) {
        for (int dx = -26; dx <= 26; dx++) {
            int d2 = dx * dx + dy * dy;

            if (d2 > 26 * 26)
                continue;

            int u = (dx * cc + dy * ss) >> 8;
            int v = (-dx * ss + dy * cc) >> 8;

            if (d2 < 36) {
                scene->DrawPixel(
                    cx + dx, cy + dy,
                    mk(50, 50, 55)
                );
            } else if (d2 <= 20 * 20) {
                scene->DrawPixel(
                    cx + dx, cy + dy,
                    mk(165, 165, 178)
                );
            } else if (
                iabs(u) < 6 ||
                iabs(v) < 6 ||
                iabs(u - v) < 8 ||
                iabs(u + v) < 8
            ) {
                scene->DrawPixel(
                    cx + dx, cy + dy,
                    mk(215, 215, 225)
                );
            }
        }
    }
}

static void drawFace(
    int x, int y, int dirFace, Color eyeWhite
) {
    fillRect(x + 9, y + 12, 10, 14, eyeWhite);
    fillRect(x + 25, y + 12, 10, 14, eyeWhite);

    int off = dirFace > 0 ? 5 : 0;

    fillRect(x + 9 + off, y + 16, 5, 8,
             mk(0, 0, 0));

    fillRect(x + 25 + off, y + 16, 5, 8,
             mk(0, 0, 0));
}

static void drawPlayerFace(
    int dx, int dy, int dw, int dh, int dirFace
) {
    int ew = dw * 10 / 44;
    if (ew < 4) ew = 4;

    int eh = dh * 14 / 44;
    if (eh < 6) eh = 6;

    int e1 = dx + dw * 9 / 44;
    int e2 = dx + dw * 25 / 44;

    int ey = dy + dh * 12 / 44;

    int pw = dw * 5 / 44;
    if (pw < 2) pw = 2;

    int ph = dh * 8 / 44;
    if (ph < 3) ph = 3;

    int poff = dirFace > 0 ? dw * 5 / 44 : 0;
    int pyy = dy + dh * 16 / 44;

    fillRect(e1, ey, ew, eh, mk(255,255,255));
    fillRect(e2, ey, ew, eh, mk(255,255,255));

    fillRect(e1 + poff, pyy, pw, ph, mk(0,0,0));
    fillRect(e2 + poff, pyy, pw, ph, mk(0,0,0));

    int mw = dw * 14 / 44;
    int mh = dh * 4 / 44;

    if (mh < 2) mh = 2;

    fillRect(
        dx + dw * 15 / 44,
        dy + dh * 32 / 44,
        mw, mh,
        mk(120, 0, 12)
    );
}

static void drawTileCell(int r, int cx, char ch, bool hi) {
    int x = cx * TILE;
    int y = r * TILE;

    if (ch == '#') {
        fillRect(x, y, TILE, TILE, mk(58, 48, 82));

        if (hi)
            fillRect(x, y, TILE, 5, mk(145, 105, 180));

        fillRect(x, y + TILE - 5, TILE, 5, mk(38, 30, 58));
    }

    else if (ch == '^') {
        drawSpike(cx, r);
    }

    else if (ch == 'G') {
        int gx = x + 8;
        int gy = y + 16;

        fillRect(gx, gy, PW, PH, mk(255, 140, 170));
        fillRect(gx, gy + 3, PW, 7, mk(240, 230, 200));

        drawFace(gx, gy, -1, mk(255, 255, 255));
    }
}

static void drawSplat(const Splat &s) {
    fillRect(s.x, s.y, s.w, s.h, mk(140, 10, 20));
    fillRect(s.x + 1, s.y + 1, s.w / 3 + 1, s.h / 3 + 1,
             mk(185, 22, 34));
}

static void starPos(int i, int &sx, int &sy) {
    sx = (i * 271 + 91) % FRAME_WIDTH;
    sy = (i * 137 + 31) % 570;
}

static bool starDim(int i, int phase) {
    return ((phase + i) % 4) == 0;
}

// Static background (sky, moon, stars, mountains).
// Painted without overlap so a full draw is as cheap as possible.
static void paintBackground() {
    Color farC = mk(35, 20, 75);
    Color nearC = mk(25, 14, 55);

    fillRect(0, 0, FRAME_WIDTH, 300, mk(24, 12, 55));
    fillRect(0, 300, FRAME_WIDTH, 270, mk(31, 15, 66));
    fillRect(0, 570, FRAME_WIDTH, 130, farC);
    fillRect(0, 700, FRAME_WIDTH, 380, nearC);

    // Moon
    fillRect(1500, 115, 100, 100, mk(145, 125, 205));
    fillRect(1515, 100, 70, 130, mk(145, 125, 205));
    fillRect(1520, 125, 65, 75, mk(185, 170, 220));

    // Stars
    int phase = tick / 20;

    for (int i = 0; i < NUM_STARS; i++) {
        int sx, sy;
        starPos(i, sx, sy);

        if (i % 7 == 0)
            fillRect(sx - 2, sy - 2, 7, 7, mk(70, 55, 110));

        fillRect(
            sx, sy, 3, 3,
            starDim(i, phase) ? mk(130, 115, 185)
                              : mk(220, 210, 255)
        );
    }

    // Far mountains
    for (int i = 0; i < 3; i++) {
        int x = i * 700;

        fillRect(x + 80, 450, 180, 130, farC);
        fillRect(x + 220, 390, 180, 190, farC);
        fillRect(x + 390, 470, 180, 110, farC);
    }

    // Near mountains
    for (int i = 0; i < 4; i++) {
        int x = i * 560;

        fillRect(x + 70, 590, 160, 120, nearC);
        fillRect(x + 220, 530, 180, 180, nearC);
        fillRect(x + 380, 630, 130, 80, nearC);
    }
}

static void drawSplatsClipped(int b) {
    for (int i = 0; i < splatCount; i++) {
        if (splatDrawn[b][i])
            drawSplat(splats[i]);
    }
}

// Repaint a small area of buffer b from the static layer
static void restoreRect(
    int b, int x, int y, int w, int h, bool withSplats
) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }

    if (x + w > FRAME_WIDTH) w = FRAME_WIDTH - x;
    if (y + h > FRAME_HEIGHT) h = FRAME_HEIGHT - y;

    if (w <= 0 || h <= 0)
        return;

    setClip(x, y, w, h);

    paintBackground();

    int c0 = x / TILE;
    int c1 = (x + w - 1) / TILE;
    int r0 = y / TILE;
    int r1 = (y + h - 1) / TILE;

    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            char ch = bufGrid[b][r][c];

            if (ch != '.')
                drawTileCell(r, c, ch, bufHi[b][r][c]);
        }
    }

    if (withSplats)
        drawSplatsClipped(b);

    resetClip();
}

static void addDirty(int b, int x, int y, int w, int h) {
    if (dirtyCount[b] < MAX_DIRTY) {
        Rect &d = dirty[b][dirtyCount[b]++];

        d.x = x;
        d.y = y;
        d.w = w;
        d.h = h;
    }
}

static int progressNow() {
    int p = (int)((px / (float)WORLD_WIDTH) * 520.0f);

    if (p < 0) p = 0;
    if (p > 520) p = 520;

    return p;
}

static void drawHUDFull(int b) {
    int hudN = deaths < 40 ? deaths : 40;

    drawText(70, 35, "DEATHS", 3, mk(220, 210, 240));

    for (int i = 0; i < hudN; i++) {
        fillRect(
            70 + i * 22,
            70,
            14, 14,
            mk(205, 25, 35)
        );
    }

    int prog = progressNow();

    fillRect(700, 45, 520, 14, mk(45, 35, 70));
    fillRect(700, 45, prog, 14, mk(205, 40, 100));

    hudSq[b] = hudN;
    hudProg[b] = prog;
    hudNeed[b] = false;
}

static void drawRoomSplats(int b) {
    for (int i = 0; i < splatCount; i++) {
        if (splats[i].room == bufRoom[b]) {
            drawSplat(splats[i]);
            splatDrawn[b][i] = true;
        }
    }
}

static void renderGame(int b) {
    resetClip();

    // Level complete screen (drawn once per buffer)
    if (winTimer > 0) {
        if (needWin[b]) {
            fillRect(0, 0, FRAME_WIDTH, FRAME_HEIGHT,
                     mk(20, 90, 60));

            drawTextC(960, 400, "LEVEL COMPLETE", 10,
                      mk(255, 255, 255));

            drawTextC(960, 540, "GREAT JOB", 6,
                      mk(255, 220, 180));

            needWin[b] = false;
        }

        return;
    }

    if (needFull[b]) {
        // Full draw (once per buffer at level start)
        paintBackground();

        for (int r = 0; r < ROWS; r++) {
            for (int c = 0; c < ROOM_COLS; c++) {
                int wc = roomIdx * ROOM_COLS + c;

                char ch = staticChar(r, wc);
                bool hi = hiAt(r, wc);

                bufGrid[b][r][c] = ch;
                bufHi[b][r][c] = hi;

                if (ch != '.')
                    drawTileCell(r, c, ch, hi);
            }
        }

        bufRoom[b] = roomIdx;

        for (int i = 0; i < MAX_SPLATS; i++)
            splatDrawn[b][i] = false;

        drawRoomSplats(b);

        bufStar[b] = tick / 20;
        dirtyCount[b] = 0;

        drawHUDFull(b);

        needFull[b] = false;
    }

    else {
        bool trans = (bufRoom[b] != roomIdx);

        // Erase what was drawn two frames ago in this buffer
        for (int i = 0; i < dirtyCount[b]; i++) {
            Rect d = dirty[b][i];

            if (d.y < 100)
                hudNeed[b] = true;

            restoreRect(b, d.x, d.y, d.w, d.h, !trans);
        }

        dirtyCount[b] = 0;

        if (trans) {
            // Erase blood of the old room
            for (int i = 0; i < splatCount; i++) {
                if (splatDrawn[b][i]) {
                    Splat &s = splats[i];

                    restoreRect(
                        b,
                        s.x - 1, s.y - 1,
                        s.w + 2, s.h + 2,
                        false
                    );

                    splatDrawn[b][i] = false;
                }
            }

            // Update only the tiles that differ between the rooms
            for (int r = 0; r < ROWS; r++) {
                for (int c = 0; c < ROOM_COLS; c++) {
                    int wc = roomIdx * ROOM_COLS + c;

                    char nc = staticChar(r, wc);
                    bool nh = hiAt(r, wc);

                    char oc = bufGrid[b][r][c];
                    bool oh = bufHi[b][r][c];

                    if (nc == oc && nh == oh)
                        continue;

                    bool needBg = (oc != '.') && (nc != '#');

                    if (needBg) {
                        setClip(c * TILE, r * TILE, TILE, TILE);
                        paintBackground();
                        resetClip();
                    }

                    bufGrid[b][r][c] = nc;
                    bufHi[b][r][c] = nh;

                    if (nc != '.')
                        drawTileCell(r, c, nc, nh);

                    if (r <= 1)
                        hudNeed[b] = true;
                }
            }

            bufRoom[b] = roomIdx;

            drawRoomSplats(b);
        }

        // New blood stains
        for (int i = 0; i < splatCount; i++) {
            if (!splatDrawn[b][i] &&
                splats[i].room == bufRoom[b]) {
                drawSplat(splats[i]);
                splatDrawn[b][i] = true;
            }
        }

        // Star twinkle
        int ph = tick / 20;

        if (bufStar[b] != ph) {
            for (int i = 0; i < NUM_STARS; i++) {
                if (starDim(i, bufStar[b]) != starDim(i, ph)) {
                    int sx, sy;
                    starPos(i, sx, sy);

                    restoreRect(b, sx - 3, sy - 3, 9, 9, true);
                }
            }

            bufStar[b] = ph;
        }

        // HUD
        int hudN = deaths < 40 ? deaths : 40;
        int prog = progressNow();

        if (hudNeed[b]) {
            drawHUDFull(b);
        } else {
            for (int i = hudSq[b]; i < hudN; i++) {
                fillRect(
                    70 + i * 22, 70,
                    14, 14,
                    mk(205, 25, 35)
                );
            }

            hudSq[b] = hudN;

            if (prog > hudProg[b]) {
                fillRect(
                    700 + hudProg[b], 45,
                    prog - hudProg[b], 14,
                    mk(205, 40, 100)
                );
            } else if (prog < hudProg[b]) {
                fillRect(
                    700 + prog, 45,
                    hudProg[b] - prog, 14,
                    mk(45, 35, 70)
                );
            }

            hudProg[b] = prog;
        }
    }

    // ----- Moving objects -----
    resetClip();

    int roomX = roomIdx * FRAME_WIDTH;

    // Saws in this room
    for (int r = 0; r < ROWS; r++) {
        for (int c = roomIdx * ROOM_COLS;
             c < roomIdx * ROOM_COLS + ROOM_COLS && c < COLS;
             c++) {
            if (level[r][c] == 'O') {
                int cx = c * TILE + 30 - roomX;
                int cy = r * TILE + 30;

                drawSaw(cx, cy);
                addDirty(b, cx - 27, cy - 27, 55, 55);
            }
        }
    }

    // Blood and dust particles
    for (int i = 0; i < MAX_PARTS; i++) {
        Part &p = parts[i];

        if (p.life <= 0)
            continue;

        int sx = (int)p.x - roomX;
        int sy = (int)p.y;

        if (sx < -20 || sx > FRAME_WIDTH + 20 ||
            sy < -20 || sy > FRAME_HEIGHT + 20)
            continue;

        int sz;
        Color col = mk(0, 0, 0);

        if (p.kind == 0) {
            sz = 2 + ((int)p.size - 2) * p.life / p.maxLife;

            col = (p.life * 2 > p.maxLife) ?
                  mk(205, 195, 230) :
                  mk(150, 140, 185);
        } else {
            sz = p.size;
            col = mk(200, 20, 35);
        }

        fillRect(sx, sy, sz, sz, col);
        addDirty(b, sx - 1, sy - 1, sz + 2, sz + 2);
    }

    // Player (hidden while dying)
    if (deadTimer == 0) {
        int x = (int)px - roomX;
        int y = (int)py;

        int dw = (int)(PW * squashX);
        int dh = (int)(PH * squashY);

        if (dw < 20) dw = 20;
        if (dh < 20) dh = 20;

        int dx = x + (PW - dw) / 2;
        int dy = y + (PH - dh);

        fillRect(dx, dy, dw, dh, mk(140, 0, 15));

        fillRect(dx + 3, dy + 3, dw - 6, dh - 6,
                 mk(205, 25, 35));

        drawPlayerFace(dx, dy, dw, dh, facing);

        addDirty(b, x - 14, y - 18, PW + 28, PH + 20);
    }
}

// ---------- UI ----------
static void markPend(int i) {
    if (i < 0 || i >= 64)
        return;

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
    deadTimer = 0;

    for (int i = 0; i < MAX_PARTS; i++)
        parts[i].life = 0;

    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < MAX_SPLATS; i++)
            splatDrawn[b][i] = false;

        needFull[b] = true;
        needWin[b] = false;
        hudNeed[b] = false;
        dirtyCount[b] = 0;
    }

    state = S_PLAY;
}

static int itemCount() {
    if (state == S_MENU) return 4;
    if (state == S_LEVELS) return NUM_LEVELS + 1;
    if (state == S_SETTINGS) return 2;

    return 0;
}

static void drawBigCube(int x, int y) {
    fillRect(x, y, 88, 88, mk(140,0,15));
    fillRect(x + 6, y + 6, 76, 76, mk(205,25,35));

    fillRect(x + 18, y + 24, 20, 28,
             mk(255,255,255));

    fillRect(x + 50, y + 24, 20, 28,
             mk(255,255,255));

    fillRect(x + 28, y + 32, 10, 16,
             mk(0,0,0));

    fillRect(x + 60, y + 32, 10, 16,
             mk(0,0,0));

    fillRect(x + 30, y + 64, 28, 8,
             mk(120,0,12));
}

static void drawBanner(
    const char *title,
    int scale,
    int y,
    int h,
    bool cubes
) {
    fillRect(460, y, 1000, h,
             mk(35,25,55));

    drawTextC(
        960,
        y + (h - 7 * scale) / 2,
        title,
        scale,
        mk(225,40,100)
    );

    if (cubes) {
        drawBigCube(500, y + (h - 88) / 2);
        drawBigCube(1332, y + (h - 88) / 2);
    }
}

static void drawHint(const char *s) {
    int w = textWidth(s, 3) + 80;

    fillRect(960 - w / 2, 980, w, 60,
             mk(35,25,55));

    drawTextC(960, 999, s, 3,
              mk(200,190,220));
}

static void drawLock(int cx, int cy) {
    Color g = mk(140,140,155);

    fillRect(cx - 18, cy - 2, 36, 28, g);
    fillRect(cx - 12, cy - 26, 24, 6, g);
    fillRect(cx - 12, cy - 22, 6, 20, g);
    fillRect(cx + 6, cy - 22, 6, 20, g);

    fillRect(cx - 3, cy + 6, 6, 10,
             mk(40,40,50));
}

static void drawStatic() {
    if (state == S_MENU) {
        drawBanner("MEAT CUBE", 12, 140, 150, true);
        drawHint("X: SELECT");
    }

    else if (state == S_LEVELS) {
        drawBanner("LEVELS", 8, 130, 110, false);
        drawHint("X: PLAY   O: BACK");
    }

    else if (state == S_SETTINGS) {
        drawBanner("SETTINGS", 8, 130, 110, false);

        fillRect(610, 700, 700, 60,
                 mk(35,25,55));

        drawTextC(
            960, 719,
            "LEFT RIGHT OR X: CHANGE",
            3,
            mk(200,190,220)
        );

        drawHint("O: BACK");
    }

    else if (state == S_CREDITS) {
        drawBanner("CREDITS", 8, 130, 110, false);

        fillRect(460, 300, 1000, 540,
                 mk(35,25,55));

        drawTextC(960, 350, "GAME DESIGNERS",
                  5, mk(200,190,220));

        drawTextC(960, 470, "KAREEM",
                  10, mk(225,40,100));

        drawTextC(960, 600, "CLAUDE",
                  10, mk(225,40,100));

        drawTextC(960, 760, "MEAT CUBE",
                  4, mk(140,140,155));

        drawHint("O: BACK");
    }
}

static void drawItem(int i) {
    if (state == S_MENU) {
        if (i < 0 || i > 3)
            return;

        const char *labels[4] = {
            "LEVELS",
            "SETTINGS",
            "CREDITS",
            "EXIT"
        };

        int x = 610;
        int y = 420 + i * 120;

        bool sel = (i == menuSel);

        fillRect(
            x, y, 700, 90,
            sel ? mk(205,25,35) : mk(45,35,62)
        );

        drawTextC(
            960, y + 24,
            labels[i],
            6,
            sel ? mk(255,255,255) : mk(200,190,215)
        );
    }

    else if (state == S_LEVELS) {
        if (i >= 0 && i < NUM_LEVELS) {
            int col = i % LEVEL_COLS;
            int row = i / LEVEL_COLS;

            int x = 375 + col * 108;
            int y = 330 + row * 108;

            bool sel = (i == levelSel);
            bool unlocked = (i < unlockedLevels);

            if (sel)
                fillRect(x, y, 90, 90,
                         mk(245,245,255));

            int inset = sel ? 6 : 0;

            fillRect(
                x + inset,
                y + inset,
                90 - 2 * inset,
                90 - 2 * inset,
                unlocked ? mk(205,25,35) :
                           mk(50,40,62)
            );

            if (unlocked) {
                char buf[4];

                intToStr(i + 1, buf);

                drawTextC(
                    x + 45,
                    y + 24,
                    buf,
                    6,
                    mk(255,255,255)
                );
            } else {
                drawLock(x + 45, y + 45);
            }
        }

        else if (i == NUM_LEVELS) {
            fillRect(610, 880, 700, 60,
                     mk(35,25,55));

            char buf[64];
            char n[4];

            strcpy(buf, "LEVEL ");
            intToStr(levelSel + 1, n);
            strcat(buf, n);

            Color c;

            if (levelSel < unlockedLevels) {
                strcat(buf, ": PRESS X TO PLAY");
                c = mk(120,230,140);
            } else {
                strcat(buf, ": LOCKED");
                c = mk(255,170,60);
            }

            drawTextC(960, 896, buf, 4, c);
        }
    }

    else if (state == S_SETTINGS) {
        if (i < 0 || i > 1)
            return;

        int x = 510;
        int y = 420 + i * 120;

        bool sel = (i == settingsSel);

        fillRect(
            x, y, 900, 90,
            sel ? mk(205,25,35) : mk(45,35,62)
        );

        const char *label;

        if (i == 0) {
            label = controlMode == 1 ?
                "CONTROLS: ANALOG" :
                "CONTROLS: ARROWS";
        } else {
            label = "BACK";
        }

        drawTextC(
            960, y + 24,
            label,
            6,
            sel ? mk(255,255,255) :
                  mk(200,190,215)
        );
    }
}

static void drawUI(int b) {
    resetClip();

    if (uiFull[b]) {
        scene->FrameBufferClear();

        drawStatic();

        int n = itemCount();

        for (int i = 0; i < n; i++)
            drawItem(i);

        uiFull[b] = false;

        for (int i = 0; i < 64; i++)
            pend[b][i] = false;
    }

    else {
        for (int i = 0; i < 64; i++) {
            if (pend[b][i]) {
                drawItem(i);
                pend[b][i] = false;
            }
        }
    }
}

// ---------- UI input ----------
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

        if (menuSel != old) {
            markPend(old);
            markPend(menuSel);
        }

        if (ok) {
            if (menuSel == 0)
                setState(S_LEVELS);

            else if (menuSel == 1) {
                settingsSel = 0;
                setState(S_SETTINGS);
            }

            else if (menuSel == 2)
                setState(S_CREDITS);

            else
                exit(0);
        }
    }

    else if (state == S_LEVELS) {
        int old = levelSel;
        int n = levelSel;

        if (nl && n > 0) n--;
        if (nr && n < NUM_LEVELS - 1) n++;

        if (nu && n >= LEVEL_COLS)
            n -= LEVEL_COLS;

        if (nd && n + LEVEL_COLS <= NUM_LEVELS - 1)
            n += LEVEL_COLS;

        if (n != old) {
            levelSel = n;

            markPend(old);
            markPend(n);
            markPend(NUM_LEVELS);
        }

        if (ok && levelSel < unlockedLevels)
            startLevel();

        else if (back)
            setState(S_MENU);
    }

    else if (state == S_SETTINGS) {
        int old = settingsSel;

        if (nu || nd)
            settingsSel = 1 - settingsSel;

        if (settingsSel != old) {
            markPend(old);
            markPend(settingsSel);
        }

        if (settingsSel == 0) {
            if (nl || nr || ok) {
                controlMode = 1 - controlMode;
                markPend(0);
            }
        }

        else if (ok) {
            setState(S_MENU);
        }

        if (back)
            setState(S_MENU);
    }

    else if (state == S_CREDITS) {
        if (ok || back)
            setState(S_MENU);
    }
}

// ---------- Main ----------
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    DEBUGLOG << "Creating a scene";

    scene = new Scene2D(
        FRAME_WIDTH,
        FRAME_HEIGHT,
        FRAME_DEPTH
    );

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
            }

            else if (winTimer > 0) {
                winTimer--;

                if (winTimer == 0)
                    setState(S_LEVELS);
            }

            else {
                update();
            }
        }

        else {
            updateUI();
        }

        if (state == S_PLAY)
            renderGame(b);
        else
            drawUI(b);

        scene->SubmitFlip(frameID);
        scene->FrameWait(frameID);
        scene->FrameBufferSwap();

        frameID++;
        tick++;
    }

    return 0;
}
