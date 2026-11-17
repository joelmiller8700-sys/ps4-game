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
#define MAX_PLANTS 260
#define MAX_AMB 16
#define MAX_DIRTY 256

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
static int currentLevel = 0;
static int currentBiome = 0;

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

// Interactive world plants (grass, wheat, desert scrub, snow reeds, lava ferns)
struct Plant {
    short room;
    short x, y;
    float angle;
    float vel;
    unsigned char height;
    unsigned char style;
    unsigned char seed;
};

static Plant plants[MAX_PLANTS];
static int plantCount = 0;

// Ambient living world particles (leaves, pollen, sand, snow, embers)
struct AmbPart {
    float x, y, vx, vy;
    unsigned char sz, seed;
};

static AmbPart ambParts[MAX_AMB];

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
static int unlockedLevels = 6;  // First 6 levels unlocked (5 new biome tutorials + original tower)
static bool introSeen = false;

static bool uiFull[2] = {true, true};
static bool pend[2][64];

// ---------- Story state ----------
static int storyId = 0;
static int storyPage = 0;
static int typed = 0;
static int storyAnimTick = 0;
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

static const Page PAGES[10] = {
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
     {AR(P4L0), -1, -1}},

    // 5 Animated Gameplay Tutorial Cutscenes (Pages 5..9)
    {10, 3, {"STAGE 1: GREEN FOREST - JUMPING",
             "TAP X FOR SHORT JUMP, HOLD X FOR HIGH JUMP.",
             "MOVE THROUGH THE LIVING GRASS AND CLEAR SPIKES!"},
     {AR(T1L0), AR(T1L1), AR(T1L2)}},
    {11, 3, {"STAGE 2: COUNTRYSIDE - SPRINTING",
             "HOLD SQUARE TO SPRINT AT MAXIMUM SPEED.",
             "SPRINT AND PRESS X TO LEAP ACROSS WIDE GAPS!"},
     {AR(T2L0), AR(T2L1), AR(T2L2)}},
    {12, 3, {"STAGE 3: GOLDEN DESERT - WALL JUMP",
             "PUSH AGAINST A WALL TO SLIDE DOWN SLOWLY.",
             "PRESS X WHILE SLIDING TO JUMP BETWEEN WALLS!"},
     {AR(T3L0), AR(T3L1), AR(T3L2)}},
    {13, 3, {"STAGE 4: SNOWY PEAKS - SAW TIMING",
             "WATCH THE SPINNING SAWS AND TIME YOUR JUMPS.",
             "JUMP AT THE VERY EDGE OF LEDGES TO CROSS FAR!"},
     {AR(T4L0), AR(T4L1), AR(T4L2)}},
    {14, 3, {"STAGE 5: LAVA VALLEY - MASTER COMBO",
             "COMBINE SPRINTING WITH WALL JUMPS AND TIMING.",
             "OVERCOME EVERY HAZARD TO REACH THE TOWER!"},
     {AR(T5L0), AR(T5L1), AR(T5L2)}}
};

static const int STORY_FIRST[7] = {0, 3, 5, 6, 7, 8, 9};
static const int STORY_LAST[7]  = {3, 5, 6, 7, 8, 9, 10};

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

static void initAmbientParticles() {
    for (int i = 0; i < MAX_AMB; i++) {
        AmbPart &a = ambParts[i];
        a.x = (float)((i * 173 + 47) % FRAME_WIDTH);
        a.y = (float)((i * 113 + 80) % (FRAME_HEIGHT - 160));
        a.seed = (unsigned char)i;

        if (currentBiome == 0) {
            a.vx = 0.6f + (i % 3) * 0.25f;
            a.vy = 0.8f + (i % 2) * 0.35f;
            a.sz = 6;
        } else if (currentBiome == 1) {
            a.vx = 0.9f + (i % 3) * 0.3f;
            a.vy = 0.3f + (i % 2) * 0.2f;
            a.sz = 5;
        } else if (currentBiome == 2) {
            a.vx = 3.2f + (i % 4) * 0.6f;
            a.vy = 0.4f;
            a.sz = 4;
        } else if (currentBiome == 3) {
            a.vx = -0.5f + (i % 3) * 0.4f;
            a.vy = 1.4f + (i % 3) * 0.4f;
            a.sz = 5;
        } else if (currentBiome == 4) {
            a.vx = -0.3f + (i % 3) * 0.3f;
            a.vy = -1.3f - (i % 3) * 0.4f;
            a.sz = 5;
        } else {
            a.vx = 0.3f;
            a.vy = -0.4f;
            a.sz = 4;
        }
    }
}

static void populateLevelPlants() {
    plantCount = 0;

    for (int r = 2; r < ROWS; r++) {
        for (int c = 1; c < COLS - 1; c++) {
            if (level[r][c] != '#')
                continue;
            if (level[r - 1][c] != '.')
                continue;

            // Avoid placing plants right next to saws or goal
            bool nearSaw = false;
            for (int dr = -2; dr <= 0; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    int nr = r + dr;
                    int nc = c + dc;
                    if (nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS) {
                        if (level[nr][nc] == 'O' || level[nr][nc] == 'G')
                            nearSaw = true;
                    }
                }
            }
            if (nearSaw)
                continue;

            // Place natural clusters of interactive plants on platforms and ground
            if (((c * 7 + r * 13) % 3) != 0) {
                if (plantCount < MAX_PLANTS) {
                    int rm = c / ROOM_COLS;
                    int lc = c % ROOM_COLS;

                    Plant &p = plants[plantCount++];
                    p.room = (short)rm;
                    p.x = (short)(lc * TILE + 18 + ((c * 11 + r * 5) % 24));
                    p.y = (short)(r * TILE);
                    p.angle = 0.0f;
                    p.vel = 0.0f;
                    p.height = (unsigned char)(16 + ((c * 3 + r * 7) % 10));
                    p.style = (unsigned char)((c + r) % 3);
                    p.seed = (unsigned char)((c * 5 + r * 9) & 15);
                }
            }
        }
    }
}

static void clearLevelGrid() {
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
}

// Original classic level (preserved 100% as Level 6)
static void buildClassicLevel() {
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

// Level 1: Green Forest (Tutorial: Short Jump vs High Jump)
static void buildForestLevel() {
    baseCol = 0 * ROOM_COLS;
    plat(15, 6, 9);
    spk(16, 12, 12);
    plat(14, 15, 18);
    spk(16, 21, 22);
    plat(15, 24, 28);

    baseCol = 1 * ROOM_COLS;
    plat(15, 5, 9);
    plat(13, 12, 16);
    plat(11, 19, 23);
    plat(14, 25, 29);
    spk(16, 10, 24);

    baseCol = 2 * ROOM_COLS;
    plat(12, 8, 18);
    spk(16, 10, 10);
    spk(16, 15, 15);
    plat(14, 21, 24);
    plat(12, 26, 29);
    spk(16, 21, 28);

    baseCol = 3 * ROOM_COLS;
    spk(16, 5, 27);
    plat(15, 4, 8);
    plat(13, 10, 14);
    plat(11, 16, 20);
    plat(13, 22, 26);
    plat(15, 27, 30);

    baseCol = 4 * ROOM_COLS;
    spk(16, 5, 27);
    plat(15, 4, 7);
    plat(14, 9, 12);
    plat(12, 14, 17);
    plat(14, 19, 22);
    plat(15, 24, 28);

    baseCol = 5 * ROOM_COLS;
    spk(16, 6, 27);
    plat(15, 5, 9);
    plat(13, 11, 15);
    plat(11, 17, 21);
    plat(10, 23, 29);
    put(9, 27, 'G');

    baseCol = 0;
}

// Level 2: Golden Countryside (Tutorial: Sprinting with Square + Long Jumps)
static void buildCountryLevel() {
    baseCol = 0 * ROOM_COLS;
    spk(16, 11, 14);
    spk(16, 22, 25);

    baseCol = 1 * ROOM_COLS;
    plat(15, 4, 10);
    plat(15, 15, 21);
    plat(14, 25, 29);
    spk(16, 8, 28);

    baseCol = 2 * ROOM_COLS;
    plat(15, 4, 8);
    plat(13, 10, 15);
    plat(14, 20, 27);
    spk(16, 8, 26);

    baseCol = 3 * ROOM_COLS;
    spk(16, 5, 28);
    plat(15, 4, 9);
    plat(13, 14, 19);
    plat(11, 24, 29);

    baseCol = 4 * ROOM_COLS;
    spk(16, 5, 28);
    plat(15, 4, 8);
    plat(14, 13, 17);
    plat(14, 22, 28);

    baseCol = 5 * ROOM_COLS;
    spk(16, 5, 28);
    plat(15, 4, 9);
    plat(13, 14, 19);
    plat(11, 24, 29);
    put(10, 27, 'G');

    baseCol = 0;
}

// Level 3: Sandy Desert (Tutorial: Wall Slide & Wall Jump)
static void buildDesertLevel() {
    baseCol = 0 * ROOM_COLS;
    wall(12, 11, 16);
    plat(11, 12, 21);
    spk(16, 13, 21);
    plat(14, 24, 28);

    baseCol = 1 * ROOM_COLS;
    wall(10, 6, 14);
    wall(13, 6, 16);
    plat(6, 13, 23);
    spk(16, 14, 23);
    plat(13, 25, 29);

    baseCol = 2 * ROOM_COLS;
    plat(14, 4, 7);
    plat(11, 9, 13);
    wall(17, 7, 14);
    plat(15, 17, 22);
    plat(13, 24, 28);
    spk(16, 8, 27);

    baseCol = 3 * ROOM_COLS;
    wall(9, 8, 14);
    wall(12, 6, 16);
    plat(6, 12, 18);
    plat(12, 21, 27);
    spk(16, 10, 26);

    baseCol = 4 * ROOM_COLS;
    spk(16, 5, 27);
    plat(15, 4, 7);
    wall(10, 9, 15);
    wall(13, 8, 15);
    plat(9, 15, 19);
    plat(13, 22, 28);

    baseCol = 5 * ROOM_COLS;
    spk(16, 6, 28);
    plat(15, 4, 7);
    wall(10, 7, 14);
    wall(13, 6, 16);
    plat(6, 13, 19);
    plat(9, 22, 29);
    put(8, 27, 'G');

    baseCol = 0;
}

// Level 4: Snowy Peaks (Tutorial: Spinning Saws & Ledge Timing)
static void buildSnowLevel() {
    baseCol = 0 * ROOM_COLS;
    plat(13, 10, 16);
    sawAt(16, 13);
    plat(13, 20, 26);
    sawAt(16, 23);

    baseCol = 1 * ROOM_COLS;
    spk(16, 5, 27);
    plat(15, 4, 9);
    sawAt(14, 11);
    plat(15, 13, 18);
    sawAt(14, 20);
    plat(15, 22, 28);

    baseCol = 2 * ROOM_COLS;
    spk(16, 5, 27);
    plat(15, 4, 9);
    plat(12, 11, 19);
    sawAt(11, 15);
    plat(14, 21, 28);

    baseCol = 3 * ROOM_COLS;
    spk(16, 5, 27);
    plat(15, 4, 8);
    plat(13, 10, 14);
    sawAt(12, 16);
    plat(11, 18, 22);
    plat(13, 24, 28);

    baseCol = 4 * ROOM_COLS;
    spk(16, 4, 28);
    plat(15, 4, 7);
    plat(14, 9, 12);
    sawAt(13, 14);
    plat(14, 16, 19);
    sawAt(13, 21);
    plat(15, 23, 28);

    baseCol = 5 * ROOM_COLS;
    spk(16, 5, 28);
    plat(15, 4, 8);
    plat(13, 10, 14);
    sawAt(12, 16);
    plat(11, 18, 22);
    plat(10, 24, 29);
    put(9, 27, 'G');

    baseCol = 0;
}

// Level 5: Lava Valley (Tutorial: Combining Sprinting, Wall Jumps, and Saws)
static void buildLavaLevel() {
    baseCol = 0 * ROOM_COLS;
    spk(16, 5, 27);
    plat(15, 4, 9);
    sawAt(14, 12);
    plat(15, 15, 20);
    plat(13, 23, 28);

    baseCol = 1 * ROOM_COLS;
    wall(10, 5, 14);
    wall(13, 5, 16);
    plat(5, 13, 25);
    sawAt(4, 19);
    spk(16, 14, 25);
    plat(13, 26, 29);

    baseCol = 2 * ROOM_COLS;
    spk(16, 4, 28);
    plat(15, 4, 8);
    plat(13, 11, 15);
    sawAt(12, 17);
    plat(13, 19, 23);
    plat(14, 25, 29);

    baseCol = 3 * ROOM_COLS;
    spk(16, 4, 28);
    plat(15, 4, 8);
    wall(11, 8, 14);
    plat(12, 14, 18);
    wall(21, 7, 13);
    plat(11, 24, 29);

    baseCol = 4 * ROOM_COLS;
    spk(16, 4, 28);
    plat(15, 4, 7);
    plat(13, 9, 13);
    sawAt(12, 15);
    plat(11, 17, 21);
    sawAt(10, 23);
    plat(13, 25, 29);

    baseCol = 5 * ROOM_COLS;
    spk(16, 4, 28);
    plat(15, 4, 8);
    plat(13, 10, 13);
    wall(16, 8, 13);
    plat(9, 19, 23);
    plat(11, 25, 29);
    put(10, 28, 'G');

    baseCol = 0;
}

static void buildLevel() {
    clearLevelGrid();

    int kind = currentLevel % 6;
    currentBiome = kind;

    if (kind == 0)
        buildForestLevel();
    else if (kind == 1)
        buildCountryLevel();
    else if (kind == 2)
        buildDesertLevel();
    else if (kind == 3)
        buildSnowLevel();
    else if (kind == 4)
        buildLavaLevel();
    else
        buildClassicLevel();

    populateLevelPlants();
    initAmbientParticles();
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

static void updatePlantsAndAmbient(bool landedOrJumped) {
    float pcx = (px - (float)(roomIdx * FRAME_WIDTH)) + PW * 0.5f;
    float pby = py + PH;

    for (int i = 0; i < plantCount; i++) {
        Plant &p = plants[i];
        if (p.room != roomIdx)
            continue;

        // Gentle wind wave across the room
        int wIdx = ((tick / 5) + (p.x / 48) + p.seed) & 15;
        float wind = (float)SN[wIdx] / 256.0f * 2.6f;

        if (deadTimer == 0) {
            float dx = pcx - (float)p.x;
            float dy = pby - (float)p.y;
            float adx = dx < 0 ? -dx : dx;

            if (adx < 44.0f && dy > -22.0f && dy < 32.0f) {
                float prox = 1.0f - (adx / 44.0f);
                p.vel += vx * 0.36f * prox;

                float push = (dx < 0.0f ? 6.5f : -6.5f) * prox;
                wind += push;

                if (landedOrJumped)
                    p.vel += (dx < 0.0f ? 4.5f : -4.5f) * prox;
            }
        }

        float force = (wind - p.angle) * 0.16f;
        p.vel = (p.vel + force) * 0.80f;
        p.angle += p.vel;

        if (p.angle > 14.0f) p.angle = 14.0f;
        if (p.angle < -14.0f) p.angle = -14.0f;
    }

    for (int i = 0; i < MAX_AMB; i++) {
        AmbPart &a = ambParts[i];
        int swayIdx = ((tick / 8) + a.seed * 3) & 15;
        float sway = (float)CS[swayIdx] / 256.0f * 0.4f;

        a.x += a.vx + sway;
        a.y += a.vy;

        if (a.x > (float)(FRAME_WIDTH - 20)) a.x = 20.0f;
        if (a.x < 20.0f) a.x = (float)(FRAME_WIDTH - 25);

        if (a.y > (float)(FRAME_HEIGHT - 130)) a.y = 70.0f;
        if (a.y < 65.0f) a.y = (float)(FRAME_HEIGHT - 140);
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
        updatePlantsAndAmbient(false);
        deadTimer--;

        if (deadTimer == 0)
            respawn();

        return;
    }

    bool wasGround = onGround;
    bool didJumpOrLand = false;

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
            didJumpOrLand = true;
        } else if (wallL || wallR) {
            vy = -16.0f;
            vx = wallL ? 8.0f : -8.0f;

            wallLock = 9;
            facing = wallL ? 1 : -1;

            squashX = 0.72f;
            squashY = 1.30f;

            spawnDust(wallL ? px : px + PW, py + PH / 2.0f, 4, 2.0f);
            didJumpOrLand = true;
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
                didJumpOrLand = true;
            }
        } else if (vy < 0) {
            py = (float)(
                (((int)py) / TILE + 1) * TILE
            );
        }

        vy = 0;
    }

    updatePlantsAndAmbient(didJumpOrLand);

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

    Color spCol = mk(205, 205, 215);
    if (currentBiome == 2)
        spCol = mk(230, 195, 145);
    else if (currentBiome == 3)
        spCol = mk(195, 235, 255);
    else if (currentBiome == 4)
        spCol = mk(255, 130, 70);

    for (int j = 0; j < 40; j++) {
        int half = (j * 26) / 39;

        fillRect(
            center - half,
            top + j,
            2 * half + 1,
            1,
            spCol
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
        Color mainC = mk(58, 48, 82);
        Color topC  = mk(145, 105, 180);
        Color botC  = mk(38, 30, 58);

        if (currentBiome == 0) {
            // Green Forest: rich earth & lush green grass cap
            mainC = mk(82, 54, 38);
            topC  = mk(65, 190, 75);
            botC  = mk(55, 36, 24);
        } else if (currentBiome == 1) {
            // Golden Countryside: warm country wood/soil & golden turf
            mainC = mk(102, 62, 40);
            topC  = mk(225, 175, 55);
            botC  = mk(70, 42, 26);
        } else if (currentBiome == 2) {
            // Sandy Desert: sandstone blocks & bright sand top
            mainC = mk(165, 118, 68);
            topC  = mk(240, 200, 115);
            botC  = mk(120, 82, 46);
        } else if (currentBiome == 3) {
            // Snowy Peaks: frozen slate rock & thick snow cap
            mainC = mk(65, 88, 118);
            topC  = mk(230, 245, 255);
            botC  = mk(42, 58, 82);
        } else if (currentBiome == 4) {
            // Lava Valley: dark basalt & glowing magma crust
            mainC = mk(55, 36, 46);
            topC  = mk(235, 90, 35);
            botC  = mk(34, 20, 28);
        }

        fillRect(x, y, TILE, TILE, mainC);

        if (hi)
            fillRect(x, y, TILE, 7, topC);

        fillRect(x, y + TILE - 5, TILE, 5, botC);
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

// Draws an interactive plant tuft that bends smoothly with player movement
static void drawPlantTuft(int bx, int by, int h, int bend, int biome, int style) {
    Color stemDark = mk(40, 145, 55);
    Color stemBright = mk(75, 205, 85);
    Color tipCol = mk(255, 220, 75);

    if (biome == 0) {
        stemDark = mk(38, 145, 55);
        stemBright = mk(80, 215, 90);
        tipCol = (style == 0) ? mk(255, 95, 125) : mk(255, 225, 75);
    } else if (biome == 1) {
        stemDark = mk(185, 135, 40);
        stemBright = mk(240, 195, 70);
        tipCol = mk(255, 225, 110);
    } else if (biome == 2) {
        stemDark = mk(105, 145, 60);
        stemBright = mk(155, 195, 85);
        tipCol = mk(255, 130, 60);
    } else if (biome == 3) {
        stemDark = mk(120, 185, 225);
        stemBright = mk(205, 240, 255);
        tipCol = mk(255, 255, 255);
    } else if (biome == 4) {
        stemDark = mk(175, 50, 35);
        stemBright = mk(245, 120, 40);
        tipCol = mk(255, 220, 80);
    } else {
        stemDark = mk(85, 60, 135);
        stemBright = mk(145, 110, 205);
        tipCol = mk(210, 180, 255);
    }

    for (int b = -1; b <= 1; b++) {
        int ox = bx + b * 7;
        int bh = h - (b == 0 ? 0 : 4);
        if (bh < 8) bh = 8;

        int segH = bh / 3;
        if (segH < 3) segH = 3;

        int b1 = (bend * 2) / 7 + b;
        int b2 = (bend * 4) / 7 + b * 2;
        int b3 = bend + b * 2;

        // Base segment
        fillRect(ox - 2, by - segH, 4, segH, stemDark);
        // Middle curved segment
        fillRect(ox - 2 + b1, by - segH * 2, 4, segH, stemBright);
        // Upper curved segment
        fillRect(ox - 2 + b2, by - segH * 3, 3, segH, stemBright);

        // Flower bud / wheat ear / glowing tip on center or side blades
        if (biome == 1) {
            fillRect(ox - 2 + b3, by - segH * 3 - 5, 5, 6, tipCol);
        } else if (b == 0 && style != 2) {
            fillRect(ox - 3 + b3, by - segH * 3 - 4, 6, 5, tipCol);
        }
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

// Static background themed for each of the 6 biomes
static void paintBackground() {
    if (currentBiome == 0) {
        // Biome 0: Lush Green Forest (Sunny Day + Forest Canopy)
        Color sky1 = mk(75, 170, 235);
        Color sky2 = mk(115, 200, 245);
        Color farC = mk(42, 110, 75);
        Color nearC = mk(26, 78, 52);

        fillRect(0, 0, FRAME_WIDTH, 300, sky1);
        fillRect(0, 300, FRAME_WIDTH, 270, sky2);
        fillRect(0, 570, FRAME_WIDTH, 130, farC);
        fillRect(0, 700, FRAME_WIDTH, 380, nearC);

        // Warm sun
        fillRect(1480, 95, 110, 110, mk(255, 235, 120));
        fillRect(1495, 80, 80, 140, mk(255, 245, 160));

        // Soft clouds
        for (int i = 0; i < 4; i++) {
            int cx = 180 + i * 440;
            int cy = 130 + (i & 1) * 55;
            fillRect(cx, cy, 170, 38, mk(235, 248, 255));
            fillRect(cx + 30, cy - 20, 110, 24, mk(245, 252, 255));
        }

        // Distant forest trees
        for (int i = 0; i < 5; i++) {
            int x = i * 400 + 60;
            fillRect(x + 60, 430, 140, 140, farC);
            fillRect(x + 90, 380, 80, 60, farC);
            fillRect(x + 220, 460, 130, 110, farC);
        }

        // Near forest trees
        for (int i = 0; i < 6; i++) {
            int x = i * 330 + 40;
            fillRect(x + 70, 620, 36, 90, mk(68, 44, 28));
            fillRect(x + 20, 510, 136, 120, nearC);
            fillRect(x + 44, 465, 88, 55, nearC);
        }
        return;
    }

    if (currentBiome == 1) {
        // Biome 1: Golden Countryside (Warm Sunset + Hills + Windmill)
        Color sky1 = mk(195, 95, 75);
        Color sky2 = mk(240, 155, 85);
        Color farC = mk(135, 82, 52);
        Color nearC = mk(92, 55, 36);

        fillRect(0, 0, FRAME_WIDTH, 300, sky1);
        fillRect(0, 300, FRAME_WIDTH, 270, sky2);
        fillRect(0, 570, FRAME_WIDTH, 130, farC);
        fillRect(0, 700, FRAME_WIDTH, 380, nearC);

        // Setting golden sun
        fillRect(1420, 210, 130, 130, mk(255, 215, 95));
        fillRect(1440, 195, 90, 160, mk(255, 230, 130));

        // Rolling countryside hills
        for (int i = 0; i < 4; i++) {
            int x = i * 500;
            fillRect(x + 40, 470, 260, 100, farC);
            fillRect(x + 90, 440, 160, 40, farC);
        }

        // Country barn & windmill silhouettes
        fillRect(420, 450, 140, 120, nearC);
        fillRect(445, 415, 90, 40, nearC);
        fillRect(1180, 390, 60, 180, nearC);
        fillRect(1120, 410, 180, 14, mk(75, 42, 28));
        fillRect(1203, 330, 14, 180, mk(75, 42, 28));
        return;
    }

    if (currentBiome == 2) {
        // Biome 2: Sandy Desert (Scorching Sun + Pyramids + Dunes)
        Color sky1 = mk(230, 145, 65);
        Color sky2 = mk(248, 198, 115);
        Color farC = mk(195, 135, 68);
        Color nearC = mk(158, 102, 48);

        fillRect(0, 0, FRAME_WIDTH, 300, sky1);
        fillRect(0, 300, FRAME_WIDTH, 270, sky2);
        fillRect(0, 570, FRAME_WIDTH, 130, farC);
        fillRect(0, 700, FRAME_WIDTH, 380, nearC);

        // Blazing desert sun
        fillRect(1490, 105, 120, 120, mk(255, 245, 175));
        fillRect(1510, 90, 80, 150, mk(255, 250, 205));

        // Distant pyramids
        for (int i = 0; i < 3; i++) {
            int px0 = 260 + i * 560;
            for (int step = 0; step < 8; step++) {
                int w = (step + 1) * 32;
                fillRect(px0 - w / 2, 410 + step * 20, w, 20, farC);
            }
        }

        // Sand dunes
        for (int i = 0; i < 4; i++) {
            int x = i * 500;
            fillRect(x + 60, 610, 280, 90, nearC);
            fillRect(x + 120, 580, 160, 40, nearC);
        }
        return;
    }

    if (currentBiome == 3) {
        // Biome 3: Snowy Mountain Peaks (Arctic Sky + Snow-Capped Mountains)
        Color sky1 = mk(38, 62, 102);
        Color sky2 = mk(68, 105, 152);
        Color farC = mk(95, 135, 178);
        Color nearC = mk(62, 95, 135);

        fillRect(0, 0, FRAME_WIDTH, 300, sky1);
        fillRect(0, 300, FRAME_WIDTH, 270, sky2);
        fillRect(0, 570, FRAME_WIDTH, 130, farC);
        fillRect(0, 700, FRAME_WIDTH, 380, nearC);

        // Pale ice moon
        fillRect(1500, 110, 96, 96, mk(220, 240, 255));

        for (int i = 0; i < 3; i++) {
            int x = i * 660;
            fillRect(x + 80, 410, 200, 160, farC);
            fillRect(x + 110, 370, 140, 50, mk(230, 245, 255));
            fillRect(x + 340, 440, 180, 130, farC);
            fillRect(x + 365, 410, 130, 40, mk(230, 245, 255));
        }

        for (int i = 0; i < 4; i++) {
            int x = i * 520;
            fillRect(x + 70, 550, 200, 150, nearC);
            fillRect(x + 105, 520, 130, 40, mk(210, 232, 250));
        }
        return;
    }

    if (currentBiome == 4) {
        // Biome 4: Volcanic Lava Valley
        Color sky1 = mk(42, 14, 24);
        Color sky2 = mk(78, 24, 32);
        Color farC = mk(58, 22, 30);
        Color nearC = mk(38, 15, 22);

        fillRect(0, 0, FRAME_WIDTH, 300, sky1);
        fillRect(0, 300, FRAME_WIDTH, 270, sky2);
        fillRect(0, 570, FRAME_WIDTH, 130, farC);
        fillRect(0, 700, FRAME_WIDTH, 380, nearC);

        // Glowing crimson moon
        fillRect(1500, 115, 100, 100, mk(235, 95, 50));
        fillRect(1520, 130, 65, 70, mk(255, 155, 70));

        // Volcano silhouettes with glowing craters
        for (int i = 0; i < 3; i++) {
            int x = i * 680;
            fillRect(x + 120, 400, 220, 170, farC);
            fillRect(x + 180, 395, 100, 18, mk(245, 95, 35));
        }

        for (int i = 0; i < 4; i++) {
            int x = i * 540;
            fillRect(x + 80, 540, 200, 160, nearC);
        }
        return;
    }

    // Biome 5: Original Classic Night Sky (100% preserved)
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

        if (currentBiome == 5 && bufStar[b] != ph) {
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

    // ----- Moving & Living objects -----
    resetClip();

    int roomX = roomIdx * FRAME_WIDTH;

    // Ambient biome particles (leaves, pollen, sand, snow, embers)
    Color ambCol = mk(235, 245, 255);
    if (currentBiome == 0) ambCol = mk(140, 235, 110);
    else if (currentBiome == 1) ambCol = mk(255, 230, 130);
    else if (currentBiome == 2) ambCol = mk(245, 215, 145);
    else if (currentBiome == 3) ambCol = mk(240, 250, 255);
    else if (currentBiome == 4) ambCol = mk(255, 145, 45);
    else ambCol = mk(175, 155, 235);

    for (int i = 0; i < MAX_AMB; i++) {
        AmbPart &a = ambParts[i];
        int ax = (int)a.x;
        int ay = (int)a.y;
        int sz = a.sz;

        fillRect(ax, ay, sz, sz, ambCol);
        addDirty(b, ax - 1, ay - 1, sz + 2, sz + 2);
    }

    // Interactive world plants (grass, wheat, desert scrub, frost reeds, lava ferns)
    for (int i = 0; i < plantCount; i++) {
        Plant &p = plants[i];
        if (p.room != roomIdx)
            continue;

        int bend = (int)p.angle;
        drawPlantTuft(p.x, p.y, p.height, bend, currentBiome, p.style);
        addDirty(b, p.x - 24, p.y - p.height - 8, 48, p.height + 10);
    }

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

// ---------- Cutscene art & Real-Time Animated Gameplay Tutorials ----------
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

static void drawMiniDemoCube(int x, int y, int dw, int dh, int dir) {
    fillRect(x, y, dw, dh, mk(140, 0, 15));
    fillRect(x + 3, y + 3, dw - 6, dh - 6, mk(205, 25, 35));
    drawPlayerFace(x, y, dw, dh, dir);
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

// Draws a controller button prompt badge inside tutorial cutscenes
static void drawPadPrompt(int x, int y, bool dirL, bool dirR, bool sqOn, bool xOn, const char *label) {
    fillRect(x, y, 420, 74, mk(22, 16, 38));
    fillRect(x + 3, y + 3, 414, 68, mk(38, 28, 62));

    // Direction arrows badge
    fillRect(x + 18, y + 16, 42, 42, dirL ? mk(90, 200, 255) : mk(55, 45, 80));
    drawTextC(x + 39, y + 27, "<", 3, mk(255, 255, 255));

    fillRect(x + 68, y + 16, 42, 42, dirR ? mk(90, 200, 255) : mk(55, 45, 80));
    drawTextC(x + 89, y + 27, ">", 3, mk(255, 255, 255));

    // Square button badge (Sprint)
    fillRect(x + 130, y + 16, 54, 42, sqOn ? mk(235, 65, 145) : mk(55, 45, 80));
    fillRect(x + 147, y + 25, 20, 20, mk(255, 255, 255));
    fillRect(x + 151, y + 29, 12, 12, sqOn ? mk(235, 65, 145) : mk(55, 45, 80));

    // Cross (X) button badge (Jump)
    fillRect(x + 196, y + 16, 54, 42, xOn ? mk(65, 165, 255) : mk(55, 45, 80));
    drawTextC(x + 223, y + 27, "X", 3, mk(255, 255, 255));

    // Action label
    drawTextC(x + 332, y + 29, label, 2, (xOn || sqOn) ? mk(255, 235, 110) : mk(190, 180, 215));
}

static void drawStoryArt(int art) {
    int ax = 460, ay = 90, aw = 1000, ah = 500;
    int t = storyAnimTick;

    if (art == 0) {
        fillRect(ax, ay, aw, ah, mk(20, 25, 60));
        fillRect(ax + 780, ay + 60, 80, 80, mk(230, 225, 190));
        fillRect(ax, ay + 380, aw, 120, mk(25, 70, 45));

        for (int i = 0; i < 4; i++) {
            int x = ax + 60 + i * 240;
            fillRect(x + 30, ay + 300, 24, 90, mk(70, 45, 30));
            fillRect(x, ay + 210, 84, 100, mk(15, 55, 35));
        }

        int sway = SN[(t / 5) & 15] / 32;
        for (int i = 0; i < 9; i++)
            drawPlantTuft(ax + 80 + i * 105, ay + 380, 22, sway, 0, i % 3);

        int bob = (t / 12) & 1 ? -6 : 0;
        drawBigCube(ax + 300, ay + 292 + bob);
        drawGirlK(ax + 560, ay + 292 - bob, 1);
        return;
    }

    if (art == 1) {
        fillRect(ax, ay, aw, ah, mk(45, 10, 25));
        fillRect(ax, ay + 420, aw, 80, mk(30, 12, 22));

        int shift = (t % 80) - 40;
        drawBigSaw(ax + 500 + shift, ay + 230, 150);
        drawBigCube(ax + 120, ay + 332);
        drawGirlK(ax + 800, ay + 100 + (SN[(t / 4) & 15] / 32), 1);
        return;
    }

    if (art == 2) {
        fillRect(ax, ay, aw, ah, mk(35, 15, 55));
        fillRect(ax, ay + 420, aw, 80, mk(25, 12, 40));

        int shake = ((t / 3) & 1) ? 3 : -3;
        drawCubeK(ax + 368 + shake, ay + 150, 3);
        fillRect(ax + 368 + 42 + shake, ay + 150 + 40, 84, 14, mk(60, 0, 10));
        fillRect(ax + 368 + 150 + shake, ay + 150 + 40, 84, 14, mk(60, 0, 10));
        return;
    }

    if (art == 3) {
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
        return;
    }

    // ---------- Animated Gameplay Tutorial Cutscenes (art 10..14) ----------
    if (art == 10) {
        // Tutorial 1: Green Forest - Short Jump vs High Jump + Interactive Grass
        fillRect(ax, ay, aw, ah, mk(65, 160, 225));
        fillRect(ax + 820, ay + 35, 70, 70, mk(255, 235, 120));

        // Ground & high ledge
        fillRect(ax, ay + 400, aw, 100, mk(82, 54, 38));
        fillRect(ax, ay + 400, aw, 8, mk(65, 190, 75));
        fillRect(ax + 700, ay + 290, 240, 110, mk(82, 54, 38));
        fillRect(ax + 700, ay + 290, 240, 8, mk(65, 190, 75));

        // Spike obstacle
        for (int j = 0; j < 34; j++) {
            int half = (j * 22) / 33;
            fillRect(ax + 370 - half, ay + 366 + j, 2 * half + 1, 1, mk(215, 215, 225));
        }

        int cyc = t % 180;
        int cx = ax + 90 + (cyc * 4);
        if (cx > ax + 820) cx = ax + 820;
        int cy = ay + 356;
        int dw = 44, dh = 44;
        bool xOn = false;
        const char *lbl = "WALK";

        if (cyc >= 45 && cyc < 85) {
            // Short jump over spike
            int jt = cyc - 45;
            cy = ay + 356 - (jt * (40 - jt)) / 4;
            xOn = (jt < 12);
            lbl = "TAP X";
            if (jt < 6) { dw = 36; dh = 52; }
        } else if (cyc >= 110 && cyc < 160) {
            // High jump (Holding X) onto high platform
            int jt = cyc - 110;
            cy = ay + 356 - (jt * (55 - jt)) / 4;
            if (jt > 32 && cy > ay + 246) cy = ay + 246;
            xOn = (jt < 34);
            lbl = "HOLD X";
            if (jt < 8) { dw = 34; dh = 54; }
        } else if (cyc >= 160) {
            cy = ay + 246;
            lbl = "GREAT!";
        }

        // Interactive grass tufts bending as demo cube passes
        for (int i = 0; i < 7; i++) {
            int gx = ax + 110 + i * 80;
            if (i == 3) continue;
            int bend = (iabs(cx + 22 - gx) < 42 && cy > ay + 320) ? 12 : (SN[(t / 5 + i) & 15] / 32);
            drawPlantTuft(gx, ay + 400, 22, bend, 0, i % 3);
        }
        drawPlantTuft(ax + 760, ay + 290, 22, (iabs(cx + 22 - (ax + 760)) < 42) ? 11 : 2, 0, 1);

        drawMiniDemoCube(cx, cy + (44 - dh), dw, dh, 1);
        drawPadPrompt(ax + 290, ay + 20, false, cyc < 165, false, xOn, lbl);
        return;
    }

    if (art == 11) {
        // Tutorial 2: Golden Countryside - Sprinting with Square + Long Jump
        fillRect(ax, ay, aw, ah, mk(215, 125, 75));
        fillRect(ax + 820, ay + 120, 90, 90, mk(255, 215, 95));

        // Left & Right runways with a wide chasm in between
        fillRect(ax, ay + 400, 420, 100, mk(102, 62, 40));
        fillRect(ax, ay + 400, 420, 8, mk(225, 175, 55));
        fillRect(ax + 660, ay + 400, 340, 100, mk(102, 62, 40));
        fillRect(ax + 660, ay + 400, 340, 8, mk(225, 175, 55));

        // Spikes in chasm
        for (int s = 0; s < 5; s++) {
            int scx = ax + 450 + s * 46;
            for (int j = 0; j < 32; j++) {
                int half = (j * 18) / 31;
                fillRect(scx - half, ay + 468 + j, 2 * half + 1, 1, mk(215, 215, 225));
            }
        }

        int cyc = t % 160;
        int cx = ax + 70 + cyc * 6;
        if (cx > ax + 850) cx = ax + 850;
        int cy = ay + 356;
        bool sqOn = (cyc < 140);
        bool xOn = (cyc >= 50 && cyc < 85);
        const char *lbl = "HOLD SQ";

        if (cyc >= 50 && cyc < 102) {
            int jt = cyc - 50;
            cy = ay + 356 - (jt * (52 - jt)) / 5;
            lbl = "SPRINT+X";
        } else if (cyc >= 102) {
            lbl = "LONG JUMP";
        }

        // Golden wheat bending sharply as player sprints through
        for (int i = 0; i < 5; i++) {
            int wx = ax + 80 + i * 70;
            int bend = (cx > wx - 25 && cx < wx + 70) ? 14 : (SN[(t / 5 + i) & 15] / 28);
            drawPlantTuft(wx, ay + 400, 26, bend, 1, i % 2);
        }
        for (int i = 0; i < 3; i++) {
            int wx = ax + 700 + i * 80;
            int bend = (cx > wx - 25 && cx < wx + 70) ? 14 : 3;
            drawPlantTuft(wx, ay + 400, 26, bend, 1, i % 2);
        }

        // Sprint speed dust trail
        if (sqOn && cyc < 52) {
            fillRect(cx - 18, cy + 32, 10, 10, mk(245, 225, 180));
            fillRect(cx - 34, cy + 35, 7, 7, mk(215, 195, 150));
        }

        drawMiniDemoCube(cx, cy, 44, 44, 1);
        drawPadPrompt(ax + 290, ay + 20, false, cyc < 140, sqOn, xOn, lbl);
        return;
    }

    if (art == 12) {
        // Tutorial 3: Sandy Desert - Wall Slide & Wall Jump
        fillRect(ax, ay, aw, ah, mk(235, 165, 85));
        fillRect(ax, ay + 420, aw, 80, mk(165, 118, 68));
        fillRect(ax, ay + 420, aw, 8, mk(240, 200, 115));

        // Left & Right desert canyon walls
        fillRect(ax + 340, ay + 150, 55, 270, mk(150, 105, 58));
        fillRect(ax + 590, ay + 120, 55, 300, mk(150, 105, 58));
        fillRect(ax + 590, ay + 120, 220, 45, mk(165, 118, 68));
        fillRect(ax + 590, ay + 120, 220, 7, mk(240, 200, 115));

        drawPlantTuft(ax + 160, ay + 420, 20, SN[(t / 5) & 15] / 32, 2, 0);
        drawPlantTuft(ax + 250, ay + 420, 22, SN[(t / 5 + 3) & 15] / 32, 2, 1);
        drawPlantTuft(ax + 720, ay + 120, 20, SN[(t / 5 + 6) & 15] / 32, 2, 0);

        int cyc = t % 190;
        int cx = ax + 220;
        int cy = ay + 376;
        int dir = 1;
        bool dirL = false, dirR = false, xOn = false;
        const char *lbl = "JUMP";

        if (cyc < 35) {
            cx = ax + 220 + (cyc * 326) / 35;
            cy = ay + 376 - (cyc * 140) / 35;
            dirR = true;
            xOn = (cyc < 10);
        } else if (cyc < 75) {
            // Sliding down right wall
            cx = ax + 546;
            cy = ay + 236 + ((cyc - 35) * 60) / 40;
            dirR = true;
            lbl = "SLIDE";
            fillRect(cx + 42, cy + 10, 8, 8, mk(240, 220, 180));
        } else if (cyc < 115) {
            // Wall Jump #1 to left wall
            int wt = cyc - 75;
            cx = ax + 546 - (wt * 151) / 40;
            cy = ay + 296 - (wt * 120) / 40;
            dir = -1;
            dirL = true;
            xOn = (wt < 12);
            lbl = "WALL X!";
        } else if (cyc < 155) {
            // Wall Jump #2 to top right ledge
            int wt = cyc - 115;
            cx = ax + 395 + (wt * 225) / 40;
            cy = ay + 176 - (wt * 100) / 40;
            if (cy < ay + 76) cy = ay + 76;
            dir = 1;
            dirR = true;
            xOn = (wt < 12);
            lbl = "WALL X!";
        } else {
            cx = ax + 640;
            cy = ay + 76;
            lbl = "CLIMBED!";
        }

        drawMiniDemoCube(cx, cy, 44, 44, dir);
        drawPadPrompt(ax + 290, ay + 20, dirL, dirR, false, xOn, lbl);
        return;
    }

    if (art == 13) {
        // Tutorial 4: Snowy Peaks - Spinning Saws & Ledge Timing
        fillRect(ax, ay, aw, ah, mk(52, 85, 130));
        fillRect(ax, ay + 400, 410, 100, mk(65, 88, 118));
        fillRect(ax, ay + 400, 410, 8, mk(230, 245, 255));
        fillRect(ax + 590, ay + 400, 410, 100, mk(65, 88, 118));
        fillRect(ax + 590, ay + 400, 410, 8, mk(230, 245, 255));

        // Live spinning saw in the center gap
        drawSaw(ax + 500, ay + 370);

        int cyc = t % 160;
        int cx = ax + 110 + cyc * 5;
        if (cx > ax + 820) cx = ax + 820;
        int cy = ay + 356;
        bool xOn = (cyc >= 50 && cyc < 80);
        const char *lbl = "TIMING";

        if (cyc >= 50 && cyc < 106) {
            int jt = cyc - 50;
            cy = ay + 356 - (jt * (56 - jt)) / 4;
            lbl = "EDGE JUMP";
        }

        for (int i = 0; i < 4; i++) {
            int gx = ax + 100 + i * 85;
            int bend = (iabs(cx + 22 - gx) < 42) ? 12 : 2;
            drawPlantTuft(gx, ay + 400, 22, bend, 3, i % 2);
        }
        drawPlantTuft(ax + 680, ay + 400, 22, (iabs(cx + 22 - (ax + 680)) < 42) ? 12 : -2, 3, 1);

        drawMiniDemoCube(cx, cy, 44, 44, 1);
        drawPadPrompt(ax + 290, ay + 20, false, cyc < 140, false, xOn, lbl);
        return;
    }

    // Tutorial 5 (art == 14): Lava Valley - Master Combo
    fillRect(ax, ay, aw, ah, mk(55, 20, 30));
    fillRect(ax, ay + 410, 430, 90, mk(55, 36, 46));
    fillRect(ax, ay + 410, 430, 8, mk(235, 90, 35));

    // Middle wall & upper goal platform
    fillRect(ax + 580, ay + 190, 50, 240, mk(55, 36, 46));
    fillRect(ax + 700, ay + 250, 260, 250, mk(55, 36, 46));
    fillRect(ax + 700, ay + 250, 260, 8, mk(235, 90, 35));

    drawSaw(ax + 310, ay + 380);

    // Bandage Girl waiting at the goal
    fillRect(ax + 860, ay + 206, PW, PH, mk(255, 140, 170));
    fillRect(ax + 860, ay + 209, PW, 7, mk(240, 230, 200));
    drawFace(ax + 860, ay + 206, -1, mk(255, 255, 255));

    int cyc = t % 180;
    int cx = ax + 80;
    int cy = ay + 366;
    bool sqOn = (cyc < 140);
    bool xOn = (cyc >= 25 && cyc < 50) || (cyc >= 95 && cyc < 115);
    const char *lbl = "COMBO!";

    if (cyc < 25) {
        cx = ax + 80 + cyc * 6;
    } else if (cyc < 75) {
        int jt = cyc - 25;
        cx = ax + 230 + (jt * 306) / 50;
        cy = ay + 366 - (jt * (50 - jt)) / 4 - jt * 2;
    } else if (cyc < 95) {
        cx = ax + 536;
        cy = ay + 266 + (cyc - 75);
        lbl = "WALL!";
    } else if (cyc < 140) {
        int wt = cyc - 95;
        cx = ax + 536 + (wt * 240) / 45;
        cy = ay + 286 - (wt * (45 - wt)) / 5 - (wt * 80) / 45;
        lbl = "LEAP!";
    } else {
        cx = ax + 780;
        cy = ay + 206 - ((t / 6) & 1) * 6;
        lbl = "VICTORY!";
    }

    drawPlantTuft(ax + 130, ay + 410, 22, (iabs(cx - (ax + 130)) < 45) ? 13 : 3, 4, 0);
    drawPlantTuft(ax + 750, ay + 250, 22, (iabs(cx - (ax + 750)) < 45) ? 13 : 3, 4, 1);

    drawMiniDemoCube(cx, cy, 44, 44, 1);
    drawPadPrompt(ax + 290, ay + 20, false, cyc < 140, sqOn, xOn, lbl);
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
        fillRect(460, 630, 1000, 300, mk(30, 22, 48));

        fillRect(460, 950, 1000, 60, mk(12, 8, 24));
        drawLabel(T_H_STORY, 960, 950, 60, 3, mk(200, 190, 220));

        storyDrawnPage[b] = storyPage;
        storyShown[b] = 0;
    }

    // Animate the cutscene / gameplay tutorial viewport in real-time
    drawStoryArt(PAGES[storyPage].art);

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
    currentLevel = levelSel;
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
    storyAnimTick = 0;

    storyBgDone[0] = false;
    storyBgDone[1] = false;

    state = S_STORY;
}

static void endStory() {
    if (storyId == 0) {
        // After the intro story, show Stage 1's animated gameplay tutorial
        startStory(2);
    } else if (storyId >= 2 && storyId <= 6) {
        startLevel();
    } else {
        setState(S_LEVELS);
    }
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
            currentLevel = levelSel;

            if (levelSel == 0 && !introSeen) {
                introSeen = true;
                startStory(0);
            } else if (levelSel >= 0 && levelSel <= 4) {
                startStory(2 + levelSel);
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

                if (winTimer == 0) {
                    if (currentLevel + 2 > unlockedLevels && unlockedLevels < NUM_LEVELS)
                        unlockedLevels = currentLevel + 2;

                    if (currentLevel < 4) {
                        currentLevel++;
                        levelSel = currentLevel;
                        startStory(2 + currentLevel);
                    } else if (currentLevel == 4) {
                        currentLevel++;
                        levelSel = currentLevel;
                        startLevel();
                    } else {
                        startStory(1);
                    }
                }
            }

            else {
                update();
            }
        }

        else if (state == S_STORY) {
            int total = storyTotal();
            storyAnimTick++;

            if (typed < total)
                typed++;

            if (in.crossNew) {
                if (typed < total) {
                    typed = total;
                } else {
                    snd(0);
                    storyPage++;
                    typed = 0;
                    storyAnimTick = 0;

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
