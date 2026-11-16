#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>
#include <math.h>

#include <orbis/Pad.h>
#include <orbis/UserService.h>
#ifndef NO_AUDIO
#include <orbis/AudioOut.h>
#endif

#include "../../_common/graphics.h"
#include "../../_common/log.h"
#include "ar_data.h"

#ifdef NO_ARABIC
#define ARABIC_OK 0
#define AR(x) (-1)
#else
#define ARABIC_OK 1
#define AR(x) AR_##x
#endif

#define FRAME_WIDTH 1920
#define FRAME_HEIGHT 1080
#define FRAME_DEPTH 4

#define TILE 60
#define ROOM_COLS 32
#define TOTAL_ROOMS 6
#define COLS (ROOM_COLS * TOTAL_ROOMS)
#define ROWS 18
#define WORLD_WIDTH (COLS * TILE)
#define WORLD_HEIGHT (ROWS * TILE)
#define LAST_ROOM (TOTAL_ROOMS - 1)

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
static int padUserID = 0;
static OrbisPadData padData;

#ifndef NO_AUDIO
static int audioHandle = -1;
#endif

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
    S_PLAY = 4,
    S_STORY = 5
};

static int state = S_MENU;
static int menuSel = 0;
static int levelSel = 0;
static int settingsSel = 0;

static int controlMode = 1;
static int lang = 0;            // 0 = English, 1 = Arabic
static int unlockedLevels = 1;
static bool introSeen = false;

static bool uiFull[2] = {true, true};
static bool pend[2][64];

// ---------- Story state ----------
static int storyId = 0;
static int storyPage = 0;
static int typed = 0;
static bool storyBgDone[2] = {false, false};
static int storyDrawnPage[2] = {-1, -1};
static int storyShown[2] = {0, 0};

// ---------- Texts (English + Arabic image id) ----------
#define TEXT_LIST \
    X(T_TITLE, "MEAT CUBE", AR(TITLE)) \
    X(T_B_LEVELS, "LEVELS", AR(B_LEVELS)) \
    X(T_B_SETTINGS, "SETTINGS", AR(B_SETTINGS)) \
    X(T_B_CREDITS, "CREDITS", AR(B_CREDITS)) \
    X(T_LEVELS, "LEVELS", AR(LEVELS)) \
    X(T_SETTINGS, "SETTINGS", AR(SETTINGS)) \
    X(T_CREDITS, "CREDITS", AR(CREDITS)) \
    X(T_EXIT, "EXIT", AR(EXIT)) \
    X(T_BACK, "BACK", AR(BACK)) \
    X(T_CTRL_A, "CONTROLS: ANALOG", AR(CTRL_ANALOG)) \
    X(T_CTRL_B, "CONTROLS: ARROWS", AR(CTRL_ARROWS)) \
    X(T_LANG, "LANGUAGE: ENGLISH", AR(LANG)) \
    X(T_H_SELECT, "X: SELECT", AR(SELECT)) \
    X(T_H_LEVELS, "X: PLAY   O: BACK", AR(LEVELS_HINT)) \
    X(T_H_BACK, "O: BACK", AR(BACK_HINT)) \
    X(T_TIP, "LEFT RIGHT OR X: CHANGE", AR(CHANGE_TIP)) \
    X(T_H_STORY, "X: NEXT   OPTIONS: SKIP", AR(STORY_HINT)) \
    X(T_PLAYX, "PRESS X TO PLAY", AR(PLAYX)) \
    X(T_LOCKED, "LOCKED", AR(LOCKED)) \
    X(T_DESIGNERS, "GAME DESIGNERS", AR(DESIGNERS)) \
    X(T_KAREEM, "KAREEM", AR(KAREEM)) \
    X(T_CLAUDE, "CLAUDE", AR(CLAUDE)) \
    X(T_GAME, "MEAT CUBE", AR(GAME_NAME)) \
    X(T_DEATHS, "DEATHS", AR(DEATHS)) \
    X(T_COMPLETE, "LEVEL COMPLETE", AR(COMPLETE)) \
    X(T_GREAT, "GREAT JOB", AR(GREAT))

enum {
#define X(id, en, ar) id,
    TEXT_LIST
#undef X
    T_COUNT
};

static const char *TXT_EN[T_COUNT] = {
#define X(id, en, ar) en,
    TEXT_LIST
#undef X
};

static const int TXT_AR[T_COUNT] = {
#define X(id, en, ar) ar,
    TEXT_LIST
#undef X
};

struct Page {
    int art;
    int n;
    const char *en[3];
    int ar[3];
};

static const Page PAGES[5] = {
    {0, 2, {"MEAT CUBE AND BANDAGE GIRL", "LIVED IN THE OLD FOREST.", ""},
     {AR(P0L0), AR(P0L1), -1}},
    {1, 2, {"ONE NIGHT THE GRINDER CAME.", "HIS SAWS TOOK HER AWAY.", ""},
     {AR(P1L0), AR(P1L1), -1}},
    {2, 2, {"MEAT CUBE WOKE UP ANGRY.", "I WILL BRING YOU BACK!", ""},
     {AR(P2L0), AR(P2L1), -1}},
    {3, 3, {"MEAT CUBE REACHED THE TOWER.", "BUT SHE WAS NOT THERE.",
            "ONLY A NOTE: FOLLOW THE SAWS."},
     {AR(P3L0), AR(P3L1), AR(P3L2)}},
    {1, 1, {"TO BE CONTINUED.", "", ""},
     {AR(P4L0), -1, -1}}
};

static const int STORY_FIRST[2] = {0, 3};
static const int STORY_LAST[2] = {3, 5};

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

static int iclamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
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

static bool useAr() {
    return lang == 1 && ARABIC_OK;
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
    padUserID = userID;

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

// ---------- Sound (menus only) ----------
static void initAudio() {
#ifndef NO_AUDIO
    sceAudioOutInit();
    audioHandle = sceAudioOutOpen(padUserID, 0, 0, 256, 48000, 1);
#endif
}

static void playTone(int freq, int ms, int vol) {
#ifndef NO_AUDIO
    if (audioHandle < 0)
        return;

    static short buf[256 * 2];

    int total = 48 * ms;
    int phase = 0;
    int done = 0;

    while (done < total) {
        for (int i = 0; i < 256; i++) {
            int idx = done + i;
            int s = 0;

            if (idx < total) {
                phase += freq;

                if (phase >= 48000)
                    phase -= 48000;

                int tri = phase < 24000 ? phase : 48000 - phase;

                s = ((tri - 12000) * vol) / 12000;

                int remain = total - idx;

                if (remain < 1500)
                    s = s * remain / 1500;
            }

            buf[i * 2] = (short)s;
            buf[i * 2 + 1] = (short)s;
        }

        sceAudioOutOutput(audioHandle, buf);
        done += 256;
    }
#else
    (void)freq;
    (void)ms;
    (void)vol;
#endif
}

static void snd(int k) {
    if (k == 0) {
        playTone(880, 25, 3500);
    } else if (k == 1) {
        playTone(660, 35, 4500);
        playTone(990, 55, 4500);
    } else if (k == 2) {
        playTone(440, 50, 4000);
    } else {
        playTone(220, 90, 4000);
    }
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

// ---------- Latin font ----------
static const unsigned char FONT[45][7] = {
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
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},

    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    {0x00,0x00,0x00,0x00,0x0C,0x04,0x08},
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}
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
    if (ch == '.') return 41;
    if (ch == '!') return 42;
    if (ch == ',') return 43;
    if (ch == '?') return 44;

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

// ---------- Arabic text images ----------
static int arW(int id) {
#if ARABIC_OK
    return id >= 0 ? AR_IMGS[id].w : 0;
#else
    (void)id;
    return 0;
#endif
}

static int arH(int id) {
#if ARABIC_OK
    return id >= 0 ? AR_IMGS[id].h : 0;
#else
    (void)id;
    return 0;
#endif
}

static void drawArCols(int id, int x, int y, int c0, int c1, Color col) {
#if ARABIC_OK
    if (id < 0)
        return;

    const ArImg &im = AR_IMGS[id];
    int rb = (im.w + 7) / 8;

    if (c0 < 0) c0 = 0;
    if (c1 > im.w) c1 = im.w;

    for (int cx = c0; cx < c1; cx++) {
        for (int ry = 0; ry < im.h; ry++) {
            if (im.bits[ry * rb + (cx >> 3)] & (0x80 >> (cx & 7)))
                scene->DrawPixel(x + cx, y + ry, col);
        }
    }
#else
    (void)id; (void)x; (void)y; (void)c0; (void)c1; (void)col;
#endif
}

// Draws a text id centered in a box, in the selected language
static void drawLabel(int tid, int cx, int boxY, int boxH,
                      int sc, Color c) {
    if (useAr() && TXT_AR[tid] >= 0) {
        int id = TXT_AR[tid];
        int w = arW(id);
        int h = arH(id);

        drawArCols(id, cx - w / 2, boxY + (boxH - h) / 2, 0, w, c);
    } else {
        drawTextC(cx, boxY + (boxH - 7 * sc) / 2,
                  TXT_EN[tid], sc, c);
    }
}

static int labelWidth(int tid, int sc) {
    if (useAr() && TXT_AR[tid] >= 0)
        return arW(TXT_AR[tid]);

    return textWidth(TXT_EN[tid], sc);
}

// ---------- Level construction ----------
static int baseCol = 0;

static void put(int row, int c, char ch) {
    int g = baseCol + c;

    if (row >= 0 && row < ROWS && g >= 0 && g < COLS)
        level[row][g] = ch;
}

static void plat(int row, int a, int b) {
    for (int c = a; c <= b; c++)
        put(row, c, '#');
}

static void wall(int col, int r0, int r1) {
    for (int r = r0; r <= r1; r++)
        put(r, col, '#');
}

static void spk(int row, int a, int b) {
    for (int c = a; c <= b; c++)
        put(row, c, '^');
}

static void sawAt(int row, int col) {
    put(row, col, 'O');
}

static void buildLevel() {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++)
            level[r][c] = '.';

        level[r][COLS] = 0;
    }

    for (int c = 0; c < COLS; c++) {
        level[0][c] = '#';
        level[ROWS - 1][c] = '#';
    }

    for (int r = 0; r < ROWS; r++) {
        level[r][0] = '#';
        level[r][COLS - 1] = '#';
    }

    // Screen 1: first steps
    baseCol = 0 * ROOM_COLS;
    spk(16, 9, 10);
    plat(15, 14, 17);
    plat(13, 19, 22);
    plat(11, 24, 27);
    spk(16, 13, 28);

    // Screen 2: wall jump shaft
    baseCol = 1 * ROOM_COLS;
    spk(16, 5, 6);
    wall(12, 4, 14);
    wall(15, 4, 16);
    plat(4, 15, 25);
    spk(16, 16, 23);
    sawAt(2, 20);

    // Screen 3: pillars over spikes with saws
    baseCol = 2 * ROOM_COLS;
    spk(16, 2, 29);
    plat(15, 3, 4);
    plat(15, 7, 8);
    plat(15, 11, 12);
    plat(15, 15, 16);
    plat(15, 19, 20);
    plat(15, 23, 24);
    plat(15, 27, 28);
    sawAt(14, 9);
    sawAt(14, 17);
    sawAt(14, 25);

    // Screen 4: the climb
    baseCol = 3 * ROOM_COLS;
    spk(16, 4, 27);
    plat(15, 5, 7);
    plat(13, 10, 12);
    plat(11, 15, 17);
    plat(9, 20, 22);
    plat(7, 25, 27);
    sawAt(10, 18);
    sawAt(8, 23);

    // Screen 5: up and down
    baseCol = 4 * ROOM_COLS;
    spk(16, 2, 29);
    plat(15, 3, 5);
    plat(13, 8, 10);
    plat(11, 13, 15);
    plat(9, 18, 20);
    plat(11, 23, 25);
    plat(13, 28, 30);
    sawAt(8, 21);

    // Screen 6: the tower
    baseCol = 5 * ROOM_COLS;
    spk(16, 3, 28);
    plat(15, 4, 6);
    plat(13, 9, 11);
    plat(15, 14, 16);
    plat(13, 19, 21);
    plat(11, 24, 29);
    sawAt(14, 7);
    sawAt(12, 22);
    spk(10, 26, 26);
    put(10, 28, 'G');

    baseCol = 0;
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
// Respawns at the start of the current screen (checkpoint)
static void respawn() {
    px = (float)(roomIdx * FRAME_WIDTH + TILE + 8);
    py = (float)((ROWS - 1) * TILE - PH);

    vx = 0;
    vy = 0;

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

    // Screen (room) selection
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

// Static background (sky, moon, stars, mountains)
static void paintBackground() {
    Color farC = mk(35, 20, 75);
    Color nearC = mk(25, 14, 55);

    fillRect(0, 0, FRAME_WIDTH, 300, mk(24, 12, 55));
    fillRect(0, 300, FRAME_WIDTH, 270, mk(31, 15, 66));
    fillRect(0, 570, FRAME_WIDTH, 130, farC);
    fillRect(0, 700, FRAME_WIDTH, 380, nearC);

    fillRect(1500, 115, 100, 100, mk(145, 125, 205));
    fillRect(1515, 100, 70, 130, mk(145, 125, 205));
    fillRect(1520, 125, 65, 75, mk(185, 170, 220));

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

    for (int i = 0; i < 3; i++) {
        int x = i * 700;

        fillRect(x + 80, 450, 180, 130, farC);
        fillRect(x + 220, 390, 180, 190, farC);
        fillRect(x + 390, 470, 180, 110, farC);
    }

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

    if (useAr() && TXT_AR[T_DEATHS] >= 0) {
        int id = TXT_AR[T_DEATHS];
        drawArCols(id, 70, 28, 0, arW(id), mk(220, 210, 240));
    } else {
        drawText(70, 35, "DEATHS", 3, mk(220, 210, 240));
    }

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

            drawLabel(T_COMPLETE, 960, 370, 110, 10,
                      mk(255, 255, 255));

            drawLabel(T_GREAT, 960, 520, 80, 6,
                      mk(255, 220, 180));

            needWin[b] = false;
        }

        return;
    }

    if (needFull[b]) {
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

        for (int i = 0; i < dirtyCount[b]; i++) {
            Rect d = dirty[b][i];

            if (d.y < 100)
                hudNeed[b] = true;

            restoreRect(b, d.x, d.y, d.w, d.h, !trans);
        }

        dirtyCount[b] = 0;

        if (trans) {
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

        for (int i = 0; i < splatCount; i++) {
            if (!splatDrawn[b][i] &&
                splats[i].room == bufRoom[b]) {
                drawSplat(splats[i]);
                splatDrawn[b][i] = true;
            }
        }

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

// ---------- Cutscene art ----------
static void drawCubeK(int x, int y, int k) {
    fillRect(x, y, 88 * k, 88 * k, mk(140, 0, 15));
    fillRect(x + 6 * k, y + 6 * k, 76 * k, 76 * k, mk(205, 25, 35));

    fillRect(x + 18 * k, y + 24 * k, 20 * k, 28 * k, mk(255, 255, 255));
    fillRect(x + 50 * k, y + 24 * k, 20 * k, 28 * k, mk(255, 255, 255));

    fillRect(x + 28 * k, y + 32 * k, 10 * k, 16 * k, mk(0, 0, 0));
    fillRect(x + 60 * k, y + 32 * k, 10 * k, 16 * k, mk(0, 0, 0));

    fillRect(x + 30 * k, y + 64 * k, 28 * k, 8 * k, mk(120, 0, 12));
}

static void drawBigCube(int x, int y) {
    drawCubeK(x, y, 1);
}

static void drawGirlK(int x, int y, int k) {
    fillRect(x, y, 88 * k, 88 * k, mk(255, 140, 170));
    fillRect(x, y + 8 * k, 88 * k, 12 * k, mk(240, 230, 200));

    fillRect(x + 18 * k, y + 30 * k, 20 * k, 26 * k, mk(255, 255, 255));
    fillRect(x + 50 * k, y + 30 * k, 20 * k, 26 * k, mk(255, 255, 255));

    fillRect(x + 26 * k, y + 38 * k, 10 * k, 16 * k, mk(0, 0, 0));
    fillRect(x + 58 * k, y + 38 * k, 10 * k, 16 * k, mk(0, 0, 0));

    fillRect(x + 34 * k, y + 66 * k, 20 * k, 6 * k, mk(170, 60, 90));
}

static void drawBigSaw(int cx, int cy, int R) {
    int R2 = R * R;
    int inner = (R * 78) / 100;
    int in2 = inner * inner;
    int hub = (R / 5) * (R / 5);
    int t1 = R / 8;
    int t2 = R / 6;

    for (int dy = -R; dy <= R; dy++) {
        for (int dx = -R; dx <= R; dx++) {
            int d2 = dx * dx + dy * dy;

            if (d2 > R2)
                continue;

            if (d2 < hub) {
                scene->DrawPixel(cx + dx, cy + dy, mk(50, 50, 55));
            } else if (d2 <= in2) {
                scene->DrawPixel(cx + dx, cy + dy, mk(165, 165, 178));
            } else if (iabs(dx) < t1 || iabs(dy) < t1 ||
                       iabs(dx - dy) < t2 || iabs(dx + dy) < t2) {
                scene->DrawPixel(cx + dx, cy + dy, mk(215, 215, 225));
            }
        }
    }
}

static void drawStoryArt(int art) {
    int ax = 460, ay = 90, aw = 1000, ah = 500;

    if (art == 0) {
        fillRect(ax, ay, aw, ah, mk(20, 25, 60));
        fillRect(ax + 780, ay + 60, 80, 80, mk(230, 225, 190));
        fillRect(ax, ay + 380, aw, 120, mk(25, 70, 45));

        for (int i = 0; i < 4; i++) {
            int x = ax + 60 + i * 240;
            fillRect(x + 30, ay + 300, 24, 90, mk(70, 45, 30));
            fillRect(x, ay + 210, 84, 100, mk(15, 55, 35));
        }

        drawBigCube(ax + 300, ay + 292);
        drawGirlK(ax + 560, ay + 292, 1);
    }

    else if (art == 1) {
        fillRect(ax, ay, aw, ah, mk(45, 10, 25));
        fillRect(ax, ay + 420, aw, 80, mk(30, 12, 22));

        drawBigSaw(ax + 500, ay + 230, 150);
        drawBigCube(ax + 120, ay + 332);
        drawGirlK(ax + 800, ay + 100, 1);
    }

    else if (art == 2) {
        fillRect(ax, ay, aw, ah, mk(35, 15, 55));
        fillRect(ax, ay + 420, aw, 80, mk(25, 12, 40));

        drawCubeK(ax + 368, ay + 150, 3);
        fillRect(ax + 368 + 42, ay + 150 + 40, 84, 14, mk(60, 0, 10));
        fillRect(ax + 368 + 150, ay + 150 + 40, 84, 14, mk(60, 0, 10));
    }

    else {
        fillRect(ax, ay, aw, ah, mk(20, 18, 40));
        fillRect(ax, ay + 420, aw, 80, mk(25, 25, 35));

        fillRect(ax + 400, ay + 40, 200, 400, mk(55, 45, 80));
        fillRect(ax + 470, ay + 90, 60, 60, mk(240, 200, 90));
        fillRect(ax + 470, ay + 190, 60, 60, mk(240, 200, 90));
        fillRect(ax + 440, ay + 330, 120, 110, mk(30, 25, 50));

        drawBigCube(ax + 160, ay + 332);

        fillRect(ax + 720, ay + 340, 70, 90, mk(235, 230, 215));
        fillRect(ax + 732, ay + 360, 46, 6, mk(120, 110, 100));
        fillRect(ax + 732, ay + 378, 46, 6, mk(120, 110, 100));
        fillRect(ax + 732, ay + 396, 30, 6, mk(120, 110, 100));
    }
}

// ---------- Cutscenes ----------
static int storyTotal() {
    const Page &p = PAGES[storyPage];
    int total = 0;

    if (useAr()) {
        int cols = 0;

        for (int i = 0; i < p.n; i++)
            if (p.ar[i] >= 0)
                cols += arW(p.ar[i]);

        total = cols / 14 + 1;
    } else {
        int ch = 0;

        for (int i = 0; i < p.n; i++)
            ch += (int)strlen(p.en[i]);

        total = ch * 2;
    }

    return total;
}

static void storyDrawText(int fromT, int toT) {
    const Page &p = PAGES[storyPage];
    bool ar = useAr();

    int f = ar ? fromT * 14 : fromT / 2;
    int t = ar ? toT * 14 : toT / 2;
    int before = 0;

    for (int i = 0; i < p.n; i++) {
        int y = 660 + i * 80;

        if (ar) {
            int id = p.ar[i];

            if (id < 0)
                continue;

            int w = arW(id);
            int h = arH(id);

            int a = iclamp(f - before, 0, w);
            int b2 = iclamp(t - before, 0, w);

            if (b2 > a) {
                drawArCols(id, 960 - w / 2, y + (70 - h) / 2,
                           w - b2, w - a, mk(255, 255, 255));
            }

            before += w;
        } else {
            const char *s = p.en[i];
            int len = (int)strlen(s);

            int a = iclamp(f - before, 0, len);
            int b2 = iclamp(t - before, 0, len);

            int x0 = 960 - textWidth(s, 4) / 2;

            for (int k = a; k < b2; k++) {
                char tmp[2] = { s[k], 0 };

                drawText(x0 + k * 24, y + 21, tmp, 4,
                         mk(255, 255, 255));
            }

            before += len;
        }
    }
}

static void renderStory(int b) {
    resetClip();

    if (!storyBgDone[b]) {
        fillRect(0, 0, FRAME_WIDTH, FRAME_HEIGHT, mk(12, 8, 24));
        storyBgDone[b] = true;
        storyDrawnPage[b] = -1;
    }

    if (storyDrawnPage[b] != storyPage) {
        drawStoryArt(PAGES[storyPage].art);

        fillRect(460, 630, 1000, 300, mk(30, 22, 48));

        fillRect(460, 950, 1000, 60, mk(12, 8, 24));
        drawLabel(T_H_STORY, 960, 950, 60, 3, mk(200, 190, 220));

        storyDrawnPage[b] = storyPage;
        storyShown[b] = 0;
    }

    if (typed > storyShown[b]) {
        storyDrawText(storyShown[b], typed);
        storyShown[b] = typed;
    }
}

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

    roomIdx = 0;
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

static void startStory(int id) {
    storyId = id;
    storyPage = STORY_FIRST[id];
    typed = 0;

    storyBgDone[0] = false;
    storyBgDone[1] = false;

    state = S_STORY;
}

static void endStory() {
    if (storyId == 0)
        startLevel();
    else
        setState(S_LEVELS);
}

// ---------- Menus ----------
static const int MENU_T[4] = { T_LEVELS, T_SETTINGS, T_CREDITS, T_EXIT };

static int itemCount() {
    if (state == S_MENU) return 4;
    if (state == S_LEVELS) return NUM_LEVELS + 1;
    if (state == S_SETTINGS) return 3;

    return 0;
}

static void drawBanner(
    int tid,
    int scale,
    int y,
    int h,
    bool cubes
) {
    fillRect(460, y, 1000, h,
             mk(35,25,55));

    drawLabel(tid, 960, y, h, scale, mk(225,40,100));

    if (cubes) {
        drawBigCube(500, y + (h - 88) / 2);
        drawBigCube(1332, y + (h - 88) / 2);
    }
}

static void drawHint(int tid) {
    int w = labelWidth(tid, 3) + 80;

    fillRect(960 - w / 2, 980, w, 60,
             mk(35,25,55));

    drawLabel(tid, 960, 980, 60, 3, mk(200,190,220));
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
        drawBanner(T_TITLE, 12, 140, 150, true);
        drawHint(T_H_SELECT);
    }

    else if (state == S_LEVELS) {
        drawBanner(T_B_LEVELS, 8, 130, 110, false);
        drawHint(T_H_LEVELS);
    }

    else if (state == S_SETTINGS) {
        drawBanner(T_B_SETTINGS, 8, 130, 110, false);

        fillRect(610, 740, 700, 60,
                 mk(35,25,55));

        drawLabel(T_TIP, 960, 740, 60, 3, mk(200,190,220));

        drawHint(T_H_BACK);
    }

    else if (state == S_CREDITS) {
        drawBanner(T_B_CREDITS, 8, 130, 110, false);

        fillRect(460, 300, 1000, 540,
                 mk(35,25,55));

        drawLabel(T_DESIGNERS, 960, 340, 60, 5, mk(200,190,220));
        drawLabel(T_KAREEM, 960, 450, 110, 10, mk(225,40,100));
        drawLabel(T_CLAUDE, 960, 580, 110, 10, mk(225,40,100));
        drawLabel(T_GAME, 960, 740, 60, 4, mk(140,140,155));

        drawHint(T_H_BACK);
    }
}

static void drawItem(int i) {
    if (state == S_MENU) {
        if (i < 0 || i > 3)
            return;

        int x = 610;
        int y = 420 + i * 120;

        bool sel = (i == menuSel);

        fillRect(
            x, y, 700, 90,
            sel ? mk(205,25,35) : mk(45,35,62)
        );

        drawLabel(
            MENU_T[i], 960, y, 90, 6,
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

            bool ok = (levelSel < unlockedLevels);
            Color c = ok ? mk(120,230,140) : mk(255,170,60);

            if (useAr()) {
                drawLabel(ok ? T_PLAYX : T_LOCKED,
                          960, 880, 60, 4, c);
            } else {
                char buf[64];
                char n[4];

                strcpy(buf, "LEVEL ");
                intToStr(levelSel + 1, n);
                strcat(buf, n);
                strcat(buf, ok ? ": PRESS X TO PLAY" : ": LOCKED");

                drawTextC(960, 896, buf, 4, c);
            }
        }
    }

    else if (state == S_SETTINGS) {
        if (i < 0 || i > 2)
            return;

        int x = 510;
        int y = 380 + i * 110;

        bool sel = (i == settingsSel);

        fillRect(
            x, y, 900, 90,
            sel ? mk(205,25,35) : mk(45,35,62)
        );

        int tid;

        if (i == 0)
            tid = controlMode == 1 ? T_CTRL_A : T_CTRL_B;
        else if (i == 1)
            tid = T_LANG;
        else
            tid = T_BACK;

        drawLabel(
            tid, 960, y, 90, 6,
            sel ? mk(255,255,255) : mk(200,190,215)
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
            snd(0);
        }

        if (ok) {
            if (menuSel == 0) {
                snd(1);
                setState(S_LEVELS);
            }

            else if (menuSel == 1) {
                snd(1);
                settingsSel = 0;
                setState(S_SETTINGS);
            }

            else if (menuSel == 2) {
                snd(1);
                setState(S_CREDITS);
            }

            else {
                snd(2);
                exit(0);
            }
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
            snd(0);
        }

        if (ok && levelSel < unlockedLevels) {
            snd(1);

            if (!introSeen) {
                introSeen = true;
                startStory(0);
            } else {
                startLevel();
            }
        }

        else if (ok) {
            snd(3);
        }

        else if (back) {
            snd(2);
            setState(S_MENU);
        }
    }

    else if (state == S_SETTINGS) {
        int old = settingsSel;

        if (nu) settingsSel = (settingsSel + 2) % 3;
        if (nd) settingsSel = (settingsSel + 1) % 3;

        if (settingsSel != old) {
            markPend(old);
            markPend(settingsSel);
            snd(0);
        }

        if (settingsSel == 0) {
            if (nl || nr || ok) {
                controlMode = 1 - controlMode;
                markPend(0);
                snd(0);
            }
        }

        else if (settingsSel == 1) {
            if (nl || nr || ok) {
                if (ARABIC_OK) {
                    lang = 1 - lang;
                    snd(1);
                    setState(S_SETTINGS);
                } else {
                    snd(3);
                }
            }
        }

        else if (ok) {
            snd(2);
            setState(S_MENU);
        }

        if (back) {
            snd(2);
            setState(S_MENU);
        }
    }

    else if (state == S_CREDITS) {
        if (ok || back) {
            snd(2);
            setState(S_MENU);
        }
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

    initAudio();

    buildLevel();
    roomIdx = 0;
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
                    startStory(1);
            }

            else {
                update();
            }
        }

        else if (state == S_STORY) {
            int total = storyTotal();

            if (typed < total)
                typed++;

            if (in.crossNew) {
                if (typed < total) {
                    typed = total;
                } else {
                    snd(0);
                    storyPage++;
                    typed = 0;

                    if (storyPage >= STORY_LAST[storyId])
                        endStory();
                }
            }

            if (state == S_STORY && in.optionsNew)
                endStory();
        }

        else {
            updateUI();
        }

        if (state == S_PLAY)
            renderGame(b);
        else if (state == S_STORY)
            renderStory(b);
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
