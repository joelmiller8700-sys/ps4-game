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
#define WORLD_WIDTH (COLS * TILE)
#define WORLD_HEIGHT (ROWS * TILE)

#define PW 44
#define PH 44

#define NUM_LEVELS 55
#define LEVEL_COLS 11

std::stringstream debugLogStream;

static Scene2D *scene;
static char level[ROWS][COLS + 1];

static int padHandle = -1;
static OrbisPadData padData;

// ---------- Game state ----------
static float px, py, vx, vy;
static float cameraX = 0.0f;
static float cameraTargetX = 0.0f;

static float squashX = 1.0f;
static float squashY = 1.0f;

static bool onGround = false;
static int coyote = 0;
static int wallLock = 0;
static int facing = 1;

static int deaths = 0;
static int winTimer = 0;
static int tick = 0;
static int frameID = 0;

struct Splat {
    int x, y, s;
};

static Splat splats[96];
static int splatCount = 0;
static unsigned int rng = 12345;

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

static int holdCnt[4] = {0,0,0,0};

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

// ---------- Drawing ----------
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

    // Outer boundaries
    setRow(0, 0, COLS - 1, '#');
    setRow(ROWS - 1, 0, COLS - 1, '#');

    for (int r = 0; r < ROWS; r++) {
        level[r][0] = '#';
        level[r][COLS - 1] = '#';
    }

    // Continuous ground
    setRow(17, 1, COLS - 2, '#');

    // SECTION 1: introduction
    platform(15, 5, 8);
    platform(13, 10, 13);
    platform(11, 15, 18);
    platform(13, 21, 24);
    platform(11, 27, 30);

    spikes(16, 7, 9);
    spikes(16, 17, 20);

    saw(12, 25);

    // SECTION 2: small jumps
    platform(14, 34, 38);
    platform(12, 40, 43);
    platform(10, 45, 49);
    platform(13, 51, 54);

    spikes(16, 33, 37);
    spikes(16, 42, 46);

    saw(11, 53);

    // SECTION 3: elevated platforms
    platform(14, 57, 61);
    platform(11, 63, 67);
    platform(9, 69, 73);
    platform(12, 75, 79);

    spikes(16, 60, 64);
    spikes(16, 70, 74);

    saw(10, 78);

    // SECTION 4: narrow passages
    platform(14, 82, 86);
    platform(12, 88, 91);
    platform(10, 93, 97);
    platform(13, 99, 103);

    spikes(16, 85, 89);
    spikes(16, 95, 99);

    saw(11, 101);

    // SECTION 5: alternating platforms
    platform(15, 106, 109);
    platform(12, 111, 115);
    platform(9, 117, 121);
    platform(12, 123, 127);

    spikes(16, 108, 112);
    spikes(16, 119, 123);

    saw(10, 125);

    // SECTION 6: final challenge
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

// ---------- Player ----------
static void respawn() {
    px = (float)(TILE + 8);
    py = (float)((ROWS - 1) * TILE - PH);

    vx = 0;
    vy = 0;

    cameraX = 0;
    cameraTargetX = 0;

    squashX = 1.0f;
    squashY = 1.0f;

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

// ---------- Camera ----------
static void updateCamera() {
    cameraTargetX = px + PW / 2.0f - FRAME_WIDTH * 0.42f;

    if (cameraTargetX < 0)
        cameraTargetX = 0;

    float maxCamera = WORLD_WIDTH - FRAME_WIDTH;

    if (cameraTargetX > maxCamera)
        cameraTargetX = maxCamera;

    // Smooth camera follow
    cameraX += (cameraTargetX - cameraX) * 0.085f;

    if (cameraX < 0)
        cameraX = 0;

    if (cameraX > maxCamera)
        cameraX = maxCamera;
}

// ---------- Physics ----------
static void update() {
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
        } else if (wallL || wallR) {
            vy = -16.0f;
            vx = wallL ? 8.0f : -8.0f;

            wallLock = 9;
            facing = wallL ? 1 : -1;

            squashX = 0.72f;
            squashY = 1.30f;
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
    }

    updateCamera();
}

// ---------- Background ----------
static void drawBackground() {
    // Purple night sky
    fillRect(0, 0, FRAME_WIDTH, FRAME_HEIGHT,
             mk(13, 8, 32));

    fillRect(0, 0, FRAME_WIDTH, 300,
             mk(24, 12, 55));

    fillRect(0, 300, FRAME_WIDTH, 300,
             mk(31, 15, 66));

    fillRect(0, 600, FRAME_WIDTH, 480,
             mk(17, 10, 42));

    // Moon
    fillRect(1500, 115, 100, 100,
             mk(145, 125, 205));

    fillRect(1515, 100, 70, 130,
             mk(145, 125, 205));

    fillRect(1520, 125, 65, 75,
             mk(185, 170, 220));

    // Stars
    for (int i = 0; i < 75; i++) {
        int sx = (i * 271 + 91) % FRAME_WIDTH;
        int sy = (i * 137 + 31) % 570;

        int twinkle = (tick / 20 + i) % 4;

        Color star = twinkle == 0 ?
            mk(130, 115, 185) :
            mk(220, 210, 255);

        fillRect(sx, sy, 3, 3, star);

        if (i % 7 == 0)
            fillRect(sx - 2, sy - 2, 7, 7,
                     mk(70, 55, 110));
    }

    // Distant mountains - slow parallax
    int farOffset = ((int)(cameraX * 0.12f)) % 700;

    for (int i = -1; i < 5; i++) {
        int x = i * 700 - farOffset;

        fillRect(x, 570, 700, 510,
                 mk(35, 20, 75));

        fillRect(x + 80, 450, 180, 130,
                 mk(35, 20, 75));

        fillRect(x + 220, 390, 180, 190,
                 mk(35, 20, 75));

        fillRect(x + 390, 470, 180, 110,
                 mk(35, 20, 75));
    }

    // Near mountains - faster parallax
    int nearOffset = ((int)(cameraX * 0.28f)) % 560;

    for (int i = -1; i < 6; i++) {
        int x = i * 560 - nearOffset;

        fillRect(x, 700, 560, 380,
                 mk(25, 14, 55));

        fillRect(x + 70, 590, 160, 120,
                 mk(25, 14, 55));

        fillRect(x + 220, 530, 180, 180,
                 mk(25, 14, 55));

        fillRect(x + 380, 630, 130, 80,
                 mk(25, 14, 55));
    }
}

// ---------- Game drawing ----------
static void drawSpike(int c, int r) {
    int worldX = c * TILE + TILE / 2;
    int screenX = worldX - (int)cameraX;

    int top = r * TILE + 20;

    for (int j = 0; j < 40; j++) {
        int half = (j * 26) / 39;

        fillRect(
            screenX - half,
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

static void drawTiles() {
    int firstCol = (int)cameraX / TILE;
    int lastCol = firstCol + FRAME_WIDTH / TILE + 2;

    if (firstCol < 0) firstCol = 0;
    if (lastCol >= COLS) lastCol = COLS - 1;

    for (int r = 0; r < ROWS; r++) {
        for (int c = firstCol; c <= lastCol; c++) {
            char ch = level[r][c];

            int x = c * TILE - (int)cameraX;
            int y = r * TILE;

            if (ch == '#') {
                fillRect(x, y, TILE, TILE,
                         mk(58, 48, 82));

                if (r > 0 && level[r - 1][c] != '#') {
                    fillRect(x, y, TILE, 5,
                             mk(145, 105, 180));
                }

                fillRect(x, y + TILE - 5, TILE, 5,
                         mk(38, 30, 58));
            }

            else if (ch == '^') {
                drawSpike(c, r);
            }

            else if (ch == 'G') {
                int gx = x + 8;
                int gy = y + 16;

                fillRect(gx, gy, PW, PH,
                         mk(255, 140, 170));

                fillRect(gx, gy + 3, PW, 7,
                         mk(240, 230, 200));

                drawFace(
                    gx, gy, -1,
                    mk(255,255,255)
                );
            }
        }
    }
}

static void drawSplat(const Splat &s) {
    int x = s.x - (int)cameraX;

    fillRect(x, s.y, s.s, s.s,
             mk(140, 10, 20));
}

static void drawPlayer() {
    int x = (int)px - (int)cameraX;
    int y = (int)py;

    int dw = (int)(PW * squashX);
    int dh = (int)(PH * squashY);

    if (dw < 20) dw = 20;
    if (dh < 20) dh = 20;

    int dx = x + (PW - dw) / 2;
    int dy = y + (PH - dh);

    fillRect(dx, dy, dw, dh,
             mk(140, 0, 15));

    fillRect(dx + 3, dy + 3, dw - 6, dh - 6,
             mk(205, 25, 35));

    drawPlayerFace(dx, dy, dw, dh, facing);
}

static void drawHUD() {
    int hudN = deaths < 40 ? deaths : 40;

    for (int i = 0; i < hudN; i++) {
        fillRect(
            70 + i * 22,
            70,
            14, 14,
            mk(205, 25, 35)
        );
    }

    drawText(70, 35, "DEATHS", 3,
             mk(220, 210, 240));

    // Progress bar
    fillRect(700, 45, 520, 14,
             mk(45, 35, 70));

    int progress = (int)(
        (px / (float)WORLD_WIDTH) * 520.0f
    );

    if (progress < 0) progress = 0;
    if (progress > 520) progress = 520;

    fillRect(700, 45, progress, 14,
             mk(205, 40, 100));
}

static void renderGame() {
    resetClip();

    drawBackground();
    drawTiles();

    for (int i = 0; i < splatCount; i++)
        drawSplat(splats[i]);

    // Only draw saws near the camera
    int firstCol = (int)cameraX / TILE;
    int lastCol = firstCol + FRAME_WIDTH / TILE + 2;

    if (firstCol < 0) firstCol = 0;
    if (lastCol >= COLS) lastCol = COLS - 1;

    for (int r = 0; r < ROWS; r++) {
        for (int c = firstCol; c <= lastCol; c++) {
            if (level[r][c] == 'O') {
                int cx = c * TILE + 30 - (int)cameraX;
                int cy = r * TILE + 30;

                drawSaw(cx, cy);
            }
        }
    }

    drawPlayer();
    drawHUD();

    if (winTimer > 0) {
        fillRect(0, 0, FRAME_WIDTH, FRAME_HEIGHT,
                 mk(20, 90, 60));

        drawTextC(
            960, 400,
            "LEVEL COMPLETE",
            10,
            mk(255,255,255)
        );

        drawTextC(
            960, 540,
            "GREAT JOB",
            6,
            mk(255,220,180)
        );
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
            renderGame();
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
