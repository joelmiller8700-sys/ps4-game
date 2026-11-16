#include <stdio.h>
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

std::stringstream debugLogStream;

static Scene2D *scene;
static char level[ROWS][COLS + 1];

static int padHandle = -1;
static OrbisPadData padData;

static float px, py, vx, vy;
static bool onGround = false, prevJump = false;
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
    scePadReadState(padHandle, &padData);
    unsigned int btn = padData.buttons;

    // Analog sticks (center is about 128): use whichever stick is pushed more
    int lx = (int)padData.leftStick.x - 128;
    int rxs = (int)padData.rightStick.x - 128;
    int ax = iabs(rxs) > iabs(lx) ? rxs : lx;
    float ana = ax / 110.0f;
    if (ana > 1.0f) ana = 1.0f;
    if (ana < -1.0f) ana = -1.0f;
    if (ana > -0.2f && ana < 0.2f) ana = 0.0f;

    bool dleft = (btn & ORBIS_PAD_BUTTON_LEFT) != 0;
    bool dright = (btn & ORBIS_PAD_BUTTON_RIGHT) != 0;

    int dir = 0;
    float scale = 1.0f;
    if (ana != 0.0f) {
        dir = ana > 0.0f ? 1 : -1;
        scale = ana > 0.0f ? ana : -ana;
        if (scale < 0.5f) scale = 0.5f;
    } else {
        dir = (dright ? 1 : 0) - (dleft ? 1 : 0);
    }
    bool left = dir < 0;
    bool right = dir > 0;

    bool run = (btn & ORBIS_PAD_BUTTON_SQUARE) != 0;
    bool jumpHeld = (btn & ORBIS_PAD_BUTTON_CROSS) != 0;
    bool jumpNew = jumpHeld && !prevJump;
    prevJump = jumpHeld;

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

    for (;;) {
        int b = frameID & 1;

        if (winTimer > 0) {
            winTimer--;
            if (winTimer == 0) {
                deaths = 0;
                splatCount = 0;
                respawn();
                needFull[0] = true;
                needFull[1] = true;
            }
        } else {
            update();
        }

        render(b);

        scene->SubmitFlip(frameID);
        scene->FrameWait(frameID);
        scene->FrameBufferSwap();
        frameID++;
        tick++;
    }
    return 0;
}
