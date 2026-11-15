#include <stdio.h>
#include <string.h>
#include <sstream>

#include "../../_common/graphics.h"
#include "../../_common/log.h"
#include "controller.h"

#define FRAME_WIDTH  1920
#define FRAME_HEIGHT 1080
#define FRAME_DEPTH     4

#define TILE 60
#define COLS 32
#define ROWS 18
#define PW   44
#define PH   44

// القيم الحقيقية لأزرار يد PS4 بنظام Orbis
#define REAL_PAD_UP       0x0010
#define REAL_PAD_RIGHT    0x0020
#define REAL_PAD_DOWN     0x0040
#define REAL_PAD_LEFT     0x0080
#define REAL_PAD_L1       0x0400
#define REAL_PAD_R1       0x0800
#define REAL_PAD_TRIANGLE 0x1000
#define REAL_PAD_CIRCLE   0x2000
#define REAL_PAD_CROSS    0x4000
#define REAL_PAD_SQUARE   0x8000

std::stringstream debugLogStream;

static Scene2D *scene;
static char level[ROWS][COLS + 1];

static float px, py, vx, vy;
static bool onGround = false, prevJump = false;
static int coyote = 0, wallLock = 0, facing = 1;
static int deaths = 0, winTimer = 0, tick = 0, frameID = 0;

struct Splat { int x, y, s; };
static Splat splats[96];
static int splatCount = 0, splatIdx = 0;
static unsigned int rng = 12345;

static const int CS[16] = {256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98,0,98,181,237};
static const int SN[16] = {0,98,181,237,256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98};

static int iabs(int a) { return a < 0 ? -a : a; }

static int rnd(int n) {
    rng = rng * 1103515245u + 12345u;
    return (int)((rng >> 16) % (unsigned int)n);
}

static Color mk(int r, int g, int b) {
    Color c = { (unsigned char)r, (unsigned char)g, (unsigned char)b };
    return c;
}

static void fillRect(int x, int y, int w, int h, Color c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FRAME_WIDTH) w = FRAME_WIDTH - x;
    if (y + h > FRAME_HEIGHT) h = FRAME_HEIGHT - y;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            scene->DrawPixel(x + i, y + j, c);
}

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
        Splat &s = splats[splatIdx % 96];
        splatIdx++;
        s.x = (int)px + rnd(PW + 20) - 10;
        s.y = (int)py + rnd(PH + 20) - 10;
        s.s = 6 + rnd(10);
        if (splatCount < 96) splatCount++;
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

static void update(Controller *ctl) {
    unsigned int btns = ctl->GetRawButtons();
    unsigned char lx  = ctl->GetLeftStickX();

    bool left  = (btns & REAL_PAD_LEFT)  != 0 || (lx < 80);
    bool right = (btns & REAL_PAD_RIGHT) != 0 || (lx > 176);
    bool run   = (btns & (REAL_PAD_SQUARE | REAL_PAD_L1 | REAL_PAD_R1)) != 0;
    bool jumpHeld = (btns & REAL_PAD_CROSS) != 0;
    bool jumpNew = jumpHeld && !prevJump;
    prevJump = jumpHeld;

    bool wallL = rectSolid((int)px - 1, (int)py, 1, PH);
    bool wallR = rectSolid((int)px + PW, (int)py, 1, PH);

    float maxSpeed = run ? 8.0f : 5.5f;
    int dir = (right ? 1 : 0) - (left ? 1 : 0);
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
    if (touchingGoal()) winTimer = 150;
}

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
        for (int dx = -26; dx <= 26; dy++) {
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

static void draw() {
    scene->FrameBufferClear();

    if (winTimer > 0) {
        fillRect(0, 0, FRAME_WIDTH, FRAME_HEIGHT, mk(20, 150, 60));
        fillRect(860, 440, 200, 200, mk(255, 140, 170));
        return;
    }

    for (int i = 0; i < splatCount; i++)
        fillRect(splats[i].x, splats[i].y, splats[i].s, splats[i].s, mk(140, 10, 20));

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
            } else if (ch == 'O') {
                drawSaw(x + 30, y + 30);
            } else if (ch == 'G') {
                int gx = x + 8, gy = y + 16;
                fillRect(gx, gy, PW, PH, mk(255, 140, 170));
                fillRect(gx, gy + 3, PW, 7, mk(240, 230, 200));
                drawFace(gx, gy, -1, mk(255, 255, 255));
            }
        }
    }

    int x = (int)px, y = (int)py;
    fillRect(x, y, PW, PH, mk(140, 0, 15));
    fillRect(x + 3, y + 3, PW - 6, PH - 6, mk(205, 25, 35));
    drawFace(x, y, facing, mk(255, 255, 255));
    fillRect(x + 15, y + 32, 14, 4, mk(120, 0, 12));

    for (int i = 0; i < deaths && i < 40; i++)
        fillRect(70 + i * 22, 70, 14, 14, mk(205, 25, 35));
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
    auto controller = new Controller();
    if (!controller->Init(-1)) {
        DEBUGLOG << "Failed to initialize controller";
        for (;;);
    }

    buildLevel();
    respawn();

    for (;;) {
        if (winTimer > 0) {
            winTimer--;
            if (winTimer == 0) {
                deaths = 0;
                splatCount = 0;
                splatIdx = 0;
                respawn();
            }
        } else {
            update(controller);
        }
        draw();

        scene->SubmitFlip(frameID);
        scene->FrameWait(frameID);
        scene->FrameBufferSwap();
        frameID++;
        tick++;
    }
    return 0;
}
