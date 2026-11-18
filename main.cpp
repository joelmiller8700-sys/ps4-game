#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>
#include <math.h>

#include <orbis/Pad.h>
#include <orbis/UserService.h>

#include "../../_common/graphics.h"
#include "../../_common/log.h"
#include "ar_data.h"

// Safety fallback definitions in case gen_ar.py is updated in a separate commit
#ifndef AR_TITLE
#define AR_TITLE (-1)
#endif
#ifndef AR_B_SETTINGS
#define AR_B_SETTINGS (-1)
#endif
#ifndef AR_B_CREDITS
#define AR_B_CREDITS (-1)
#endif
#ifndef AR_START
#define AR_START (-1)
#endif
#ifndef AR_SETTINGS
#define AR_SETTINGS (-1)
#endif
#ifndef AR_CREDITS
#define AR_CREDITS (-1)
#endif
#ifndef AR_EXIT
#define AR_EXIT (-1)
#endif
#ifndef AR_BACK
#define AR_BACK (-1)
#endif
#ifndef AR_CTRL_ANALOG
#define AR_CTRL_ANALOG (-1)
#endif
#ifndef AR_CTRL_ARROWS
#define AR_CTRL_ARROWS (-1)
#endif
#ifndef AR_LANG
#define AR_LANG (-1)
#endif
#ifndef AR_SELECT
#define AR_SELECT (-1)
#endif
#ifndef AR_BACK_HINT
#define AR_BACK_HINT (-1)
#endif
#ifndef AR_CHANGE_TIP
#define AR_CHANGE_TIP (-1)
#endif
#ifndef AR_STORY_HINT
#define AR_STORY_HINT (-1)
#endif
#ifndef AR_DESIGNERS
#define AR_DESIGNERS (-1)
#endif
#ifndef AR_KAREEM
#define AR_KAREEM (-1)
#endif
#ifndef AR_CLAUDE
#define AR_CLAUDE (-1)
#endif
#ifndef AR_GAME_NAME
#define AR_GAME_NAME (-1)
#endif
#ifndef AR_BOOKS
#define AR_BOOKS (-1)
#endif
#ifndef AR_FLOOR1
#define AR_FLOOR1 (-1)
#endif
#ifndef AR_FLOOR2
#define AR_FLOOR2 (-1)
#endif
#ifndef AR_STAIRS
#define AR_STAIRS (-1)
#endif
#ifndef AR_FLASH_ON
#define AR_FLASH_ON (-1)
#endif
#ifndef AR_FLASH_OFF
#define AR_FLASH_OFF (-1)
#endif
#ifndef AR_NOISE_LOW
#define AR_NOISE_LOW (-1)
#endif
#ifndef AR_NOISE_HIGH
#define AR_NOISE_HIGH (-1)
#endif
#ifndef AR_STALKER_NEAR
#define AR_STALKER_NEAR (-1)
#endif
#ifndef AR_SPEED_UP
#define AR_SPEED_UP (-1)
#endif
#ifndef AR_PICK_BOOK
#define AR_PICK_BOOK (-1)
#endif
#ifndef AR_DOOR_LOCKED
#define AR_DOOR_LOCKED (-1)
#endif
#ifndef AR_DOOR_OPEN
#define AR_DOOR_OPEN (-1)
#endif
#ifndef AR_ESCAPE_NOW
#define AR_ESCAPE_NOW (-1)
#endif
#ifndef AR_INTRO_1
#define AR_INTRO_1 (-1)
#endif
#ifndef AR_INTRO_2
#define AR_INTRO_2 (-1)
#endif
#ifndef AR_INTRO_3
#define AR_INTRO_3 (-1)
#endif
#ifndef AR_OUTRO_1
#define AR_OUTRO_1 (-1)
#endif
#ifndef AR_OUTRO_2
#define AR_OUTRO_2 (-1)
#endif
#ifndef AR_CAUGHT_1
#define AR_CAUGHT_1 (-1)
#endif
#ifndef AR_CAUGHT_2
#define AR_CAUGHT_2 (-1)
#endif

// Direct C-ABI bindings to PS4 libSceAudioOut
#ifndef NO_AUDIO
extern "C" {
    int sceAudioOutInit(void);
    int sceAudioOutOpen(int userId, int type, int index, unsigned int len, unsigned int freq, unsigned int param);
    int sceAudioOutOutput(int handle, const void *ptr);
    int sceAudioOutSetVolume(int handle, int flag, int *vol);
}
#endif

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

#define VW 320
#define VH 150
#define VP_Y0 110
#define CELL_W 6
#define CELL_H 6

#define MAP_W 16
#define MAP_H 16
#define NUM_FLOORS 2
#define TOTAL_BOOKS 6

std::stringstream debugLogStream;

static Scene2D *scene;
static int padHandle = -1;
static int padUserID = 0;
static OrbisPadData padData;

#ifndef NO_AUDIO
static int audioHandle = -1;
#endif

// ---------- 3D Two-Story House Map ----------
static const char HOUSE_MAP[NUM_FLOORS][MAP_H][MAP_W + 1] = {
    // FLOOR 0: Ground Floor
    {
        "################",
        "#B....#SS#....B#",
        "#.....#..#.....#",
        "#..W........W..#",
        "###.########.###",
        "#...#......#...#",
        "#...#......#...#",
        "#.B..........B.#",
        "#...#......#...#",
        "###.#......#.###",
        "#...#......#...#",
        "#..............#",
        "#..W........W..#",
        "#.....#..#.....#",
        "######DDDD######",
        "################"
    },
    // FLOOR 1: Second Floor
    {
        "################",
        "#W....#SS#....W#",
        "#.....#..#.....#",
        "#..B........B..#",
        "###.########.###",
        "#...#......#...#",
        "#..............#",
        "#.W...####...W.#",
        "#.....####.....#",
        "###.########.###",
        "#...#......#...#",
        "#...#......#...#",
        "#..B........B..#",
        "#.....#..#.....#",
        "#######WW#######",
        "################"
    }
};

// ---------- Game & 3D Camera State ----------
enum {
    S_MENU = 0,
    S_SETTINGS = 1,
    S_CREDITS = 2,
    S_INTRO_3D = 3,
    S_PLAY_3D = 4,
    S_OUTRO_3D = 5,
    S_CAUGHT_3D = 6
};

static int state = S_MENU;
static int menuSel = 0;
static int settingsSel = 0;
static int controlMode = 1;
static int lang = 1;

static int tick = 0;
static int frameID = 0;
static unsigned int rng = 12345;

// Player 3D Transform
static float plX = 7.5f;
static float plY = 12.5f;
static float plAngle = -1.570796f;
static int plFloor = 0;
static float plPitch = 0.0f;
static float headBob = 0.0f;
static float stepTimer = 0.0f;
static bool flashlightOn = true;
static float noiseLevel = 0.0f;
static int speedAlertTimer = 0;
static int doorAlertTimer = 0;
static int stairCooldown = 0;

// 6 Cursed Books (3 on Ground Floor, 3 on Second Floor)
struct Book {
    int floor;
    float x, y;
    bool collected;
};

static Book books[TOTAL_BOOKS];
static int booksCollected = 0;

static const float BOOK_INIT[TOTAL_BOOKS][3] = {
    {0.0f, 2.5f,  2.5f},
    {0.0f, 13.5f, 2.5f},
    {0.0f, 2.5f,  11.5f},
    {1.0f, 2.5f,  2.5f},
    {1.0f, 13.5f, 2.5f},
    {1.0f, 8.0f,  12.5f}
};

// Smart Stalker Entity
enum {
    AI_PATROL = 0,
    AI_INVESTIGATE = 1,
    AI_CHASE = 2
};

static float stX = 7.5f;
static float stY = 3.5f;
static int stFloor = 1;
static int stMode = AI_PATROL;
static float stTargetX = 7.5f;
static float stTargetY = 3.5f;
static int stTargetFloor = 1;
static float stDistToPlayer = 20.0f;
static int stWaypoint = 0;
static int stStairCooldown = 0;
static int stRoarCooldown = 0;

static const float PATROL_PTS[6][2] = {
    {2.5f, 2.5f}, {13.5f, 2.5f}, {7.5f, 6.0f},
    {2.5f, 12.0f}, {13.5f, 12.0f}, {7.5f, 2.5f}
};

static int cutTick = 0;

static unsigned short vbuf[2][VH][VW];
static float zBuffer[VW];
static bool uiFull[2] = {true, true};
static bool pend[2][16];
static bool hudFull[2] = {true, true};

static int clipX0 = 0;
static int clipY0 = 0;
static int clipX1 = FRAME_WIDTH;
static int clipY1 = FRAME_HEIGHT;

// ---------- Texts (English + Arabic image id) ----------
#define TEXT_LIST \
    X(T_TITLE, "OLD HOUSE NIGHTMARE", AR(TITLE)) \
    X(T_B_SETTINGS, "SETTINGS", AR(B_SETTINGS)) \
    X(T_B_CREDITS, "CREDITS", AR(B_CREDITS)) \
    X(T_START, "START GAME", AR(START)) \
    X(T_SETTINGS, "SETTINGS", AR(SETTINGS)) \
    X(T_CREDITS, "CREDITS", AR(CREDITS)) \
    X(T_EXIT, "EXIT", AR(EXIT)) \
    X(T_BACK, "BACK", AR(BACK)) \
    X(T_CTRL_A, "CONTROLS: ANALOG", AR(CTRL_ANALOG)) \
    X(T_CTRL_B, "CONTROLS: ARROWS", AR(CTRL_ARROWS)) \
    X(T_LANG, "LANGUAGE: ENGLISH", AR(LANG)) \
    X(T_H_SELECT, "X: SELECT", AR(SELECT)) \
    X(T_H_BACK, "O: BACK", AR(BACK_HINT)) \
    X(T_TIP, "LEFT RIGHT OR X: CHANGE", AR(CHANGE_TIP)) \
    X(T_H_STORY, "X: SKIP CUTSCENE", AR(STORY_HINT)) \
    X(T_DESIGNERS, "GAME DESIGNERS", AR(DESIGNERS)) \
    X(T_KAREEM, "KAREEM", AR(KAREEM)) \
    X(T_CLAUDE, "CLAUDE", AR(CLAUDE)) \
    X(T_GAME, "OLD HOUSE NIGHTMARE 3D", AR(GAME_NAME)) \
    X(T_BOOKS, "BOOKS:", AR(BOOKS)) \
    X(T_FLOOR1, "GROUND FLOOR", AR(FLOOR1)) \
    X(T_FLOOR2, "SECOND FLOOR", AR(FLOOR2)) \
    X(T_STAIRS, "WOODEN STAIRS", AR(STAIRS)) \
    X(T_FLASH_ON, "FLASHLIGHT: ON  -  TRIANGLE", AR(FLASH_ON)) \
    X(T_FLASH_OFF, "FLASHLIGHT: OFF  -  TRIANGLE", AR(FLASH_OFF)) \
    X(T_NOISE_LOW, "SOUND: QUIET  -  SQUARE: RUN  O: SNEAK", AR(NOISE_LOW)) \
    X(T_NOISE_HIGH, "SOUND: LOUD! THE STALKER HEARS YOU!", AR(NOISE_HIGH)) \
    X(T_STALKER_NEAR, "DANGER! THE STALKER IS VERY CLOSE!", AR(STALKER_NEAR)) \
    X(T_SPEED_UP, "THE MONSTER GREW FASTER!", AR(SPEED_UP)) \
    X(T_PICK_BOOK, "PRESS X TO COLLECT BOOK", AR(PICK_BOOK)) \
    X(T_DOOR_LOCKED, "MAIN DOOR LOCKED! COLLECT 6 BOOKS TO ESCAPE", AR(DOOR_LOCKED)) \
    X(T_DOOR_OPEN, "MAIN DOOR UNLOCKED ON GROUND FLOOR! ESCAPE NOW!", AR(DOOR_OPEN)) \
    X(T_ESCAPE_NOW, "PRESS X TO ESCAPE THROUGH THE MAIN DOOR!", AR(ESCAPE_NOW)) \
    X(T_INTRO_1, "YOU ENTERED THE OLD MANSION TO FIND THE 6 BOOKS...", AR(INTRO_1)) \
    X(T_INTRO_2, "THE MAIN DOOR SLAMMED SHUT! SOMETHING WATCHES YOU...", AR(INTRO_2)) \
    X(T_INTRO_3, "IT HEARS YOUR FOOTSTEPS! COLLECT 6 BOOKS AND ESCAPE!", AR(INTRO_3)) \
    X(T_OUTRO_1, "YOU BURST THROUGH THE MAIN DOOR AND ESCAPED!", AR(OUTRO_1)) \
    X(T_OUTRO_2, "YOU SURVIVED THE NIGHTMARE WITH ALL 6 BOOKS!", AR(OUTRO_2)) \
    X(T_CAUGHT_1, "THE STALKER CAUGHT YOU IN THE DARK!", AR(CAUGHT_1)) \
    X(T_CAUGHT_2, "PRESS X TO RETRY OR OPTIONS FOR MENU", AR(CAUGHT_2))

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

// ---------- Input ----------
struct Input {
    bool up, down, left, right;
    bool dup, ddown, dleft, dright;
    bool cross, circle, square, triangle, options, r1, l1;
    bool crossNew, circleNew, triangleNew, optionsNew;
    float lx, ly, rx, ry;
};

static Input in;
static bool prevCross = false;
static bool prevCircle = false;
static bool prevTriangle = false;
static bool prevOptions = false;
static int holdCnt[4] = {0, 0, 0, 0};

static int iabs(int a) { return a < 0 ? -a : a; }
static float fabs_f(float a) { return a < 0.0f ? -a : a; }
static int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static Color mk(int r, int g, int b) {
    Color c = { (unsigned char)r, (unsigned char)g, (unsigned char)b };
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

    int ilx = (int)padData.leftStick.x - 128;
    int ily = (int)padData.leftStick.y - 128;
    int irx = (int)padData.rightStick.x - 128;
    int iry = (int)padData.rightStick.y - 128;

    in.dup    = (btn & ORBIS_PAD_BUTTON_UP) != 0;
    in.ddown  = (btn & ORBIS_PAD_BUTTON_DOWN) != 0;
    in.dleft  = (btn & ORBIS_PAD_BUTTON_LEFT) != 0;
    in.dright = (btn & ORBIS_PAD_BUTTON_RIGHT) != 0;

    in.up    = in.dup    || ily < -55;
    in.down  = in.ddown  || ily > 55;
    in.left  = in.dleft  || ilx < -55;
    in.right = in.dright || ilx > 55;

    in.lx = (iabs(ilx) > 22) ? (ilx / 128.0f) : 0.0f;
    in.ly = (iabs(ily) > 22) ? (ily / 128.0f) : 0.0f;
    in.rx = (iabs(irx) > 22) ? (irx / 128.0f) : 0.0f;
    in.ry = (iabs(iry) > 22) ? (iry / 128.0f) : 0.0f;

    in.cross    = (btn & ORBIS_PAD_BUTTON_CROSS) != 0;
    in.circle   = (btn & ORBIS_PAD_BUTTON_CIRCLE) != 0;
    in.square   = (btn & ORBIS_PAD_BUTTON_SQUARE) != 0;
    in.triangle = (btn & ORBIS_PAD_BUTTON_TRIANGLE) != 0;
    in.options  = (btn & ORBIS_PAD_BUTTON_OPTIONS) != 0;
    in.r1       = (btn & ORBIS_PAD_BUTTON_R1) != 0;
    in.l1       = (btn & ORBIS_PAD_BUTTON_L1) != 0;

    bool triOrR1 = in.triangle || in.r1;

    in.crossNew    = in.cross && !prevCross;
    in.circleNew   = in.circle && !prevCircle;
    in.triangleNew = triOrR1 && !prevTriangle;
    in.optionsNew  = in.options && !prevOptions;

    prevCross    = in.cross;
    prevCircle   = in.circle;
    prevTriangle = triOrR1;
    prevOptions  = in.options;
}

static bool navEvent(int i, bool pressed) {
    if (!pressed) {
        holdCnt[i] = 0;
        return false;
    }
    holdCnt[i]++;
    if (holdCnt[i] == 1)
        return true;
    if (holdCnt[i] > 20 && ((holdCnt[i] - 20) % 6) == 0)
        return true;
    return false;
}

// ---------- PS4 Halal Horror Audio Engine (Heartbeat, Footsteps, Wind, Jumpscare) ----------
static int curSfx = -1;
static int sfxPos = 0;
static int sfxLen = 0;
static int sfxPhase = 0;

static int windSample = 0;
static int heartSample = 0;
static int heartPhase = 0;
static unsigned int noiseRng = 987654321u;

static void playToneImmediate(int freq, int ms, int vol) {
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
                if (phase >= 48000) phase -= 48000;
                int tri = phase < 24000 ? phase : 48000 - phase;
                s = ((tri - 12000) * vol) / 12000;
                int remain = total - idx;
                if (remain < 1200) s = (s * remain) / 1200;
            }
            buf[i * 2]     = (short)s;
            buf[i * 2 + 1] = (short)s;
        }
        sceAudioOutOutput(audioHandle, buf);
        done += 256;
    }
#else
    (void)freq; (void)ms; (void)vol;
#endif
}

static void initAudio() {
#ifndef NO_AUDIO
    sceAudioOutInit();
    audioHandle = sceAudioOutOpen(0xFF, 0, 0, 256, 48000, 1);
    if (audioHandle < 0)
        audioHandle = sceAudioOutOpen(padUserID, 0, 0, 256, 48000, 1);
    if (audioHandle < 0)
        audioHandle = sceAudioOutOpen(padUserID, 1, 0, 256, 48000, 1);

    if (audioHandle >= 0) {
        int vol[8] = { 32768, 32768, 32768, 32768, 32768, 32768, 32768, 32768 };
        sceAudioOutSetVolume(audioHandle, 3, vol);
    }
#endif
}

static void snd(int k) {
    if (state == S_MENU || state == S_SETTINGS || state == S_CREDITS) {
        if (k == 0) playToneImmediate(760, 25, 15000);
        else if (k == 1) {
            playToneImmediate(520, 35, 17000);
            playToneImmediate(780, 55, 17000);
        } else {
            playToneImmediate(380, 45, 15000);
        }
        return;
    }

    if (k == 3 && curSfx >= 4)
        return;

    curSfx = k;
    sfxPos = 0;
    sfxPhase = 0;

    if (k == 3)      sfxLen = 2200;
    else if (k == 4) sfxLen = 8500;
    else if (k == 5) sfxLen = 12000;
    else if (k == 6) sfxLen = 9500;
    else if (k == 7) sfxLen = 11000;
    else if (k == 8) sfxLen = 22000;
    else if (k == 9) sfxLen = 16000;
    else             sfxLen = 1000;
}

static void updateHorrorAudio() {
#ifndef NO_AUDIO
    if (audioHandle < 0)
        return;
    if (state == S_MENU || state == S_SETTINGS || state == S_CREDITS)
        return;

    static short outBuf[256 * 2];

    float effDist = stDistToPlayer + (stFloor != plFloor ? 6.0f : 0.0f);
    int heartPeriod = 36000;
    int heartVol = 4500;

    if (effDist < 12.0f) {
        heartPeriod = 14000 + (int)(effDist * 1400.0f);
        heartVol = 16000 - (int)(effDist * 800.0f);
        if (stMode == AI_CHASE) {
            heartPeriod = 12500;
            heartVol = 19000;
        }
    }

    for (int blk = 0; blk < 3; blk++) {
        for (int i = 0; i < 256; i++) {
            int mix = 0;
            windSample++;

            noiseRng = noiseRng * 1664525u + 1013904223u;
            int rawNz = ((int)(noiseRng >> 21) & 511) - 256;
            static int lpWind = 0;
            lpWind = (lpWind * 31 + rawNz) >> 5;
            mix += lpWind * 6;

            if (state == S_PLAY_3D) {
                heartSample++;
                if (heartSample >= heartPeriod)
                    heartSample = 0;

                int hEnv = 0;
                if (heartSample < 2400) {
                    hEnv = 2400 - heartSample;
                } else if (heartSample >= 4200 && heartSample < 6200) {
                    hEnv = 6200 - heartSample;
                }

                if (hEnv > 0) {
                    int hFreq = 52 + (hEnv >> 7);
                    heartPhase += hFreq;
                    if (heartPhase >= 48000) heartPhase -= 48000;
                    int hWave = heartPhase < 24000 ? (heartPhase - 12000) : (36000 - heartPhase);
                    mix += ((hWave * (hEnv >> 4)) / 12000) * (heartVol >> 10);
                }
            }

            if (curSfx >= 0) {
                int rem = sfxLen - sfxPos;
                if (rem <= 0) {
                    curSfx = -1;
                } else {
                    int f = 300;
                    int vol = 14000;

                    if (curSfx == 3) {
                        f = 95 - (sfxPos >> 6);
                        if (f < 48) f = 48;
                        vol = (noiseLevel > 0.6f) ? 14000 : 7500;
                    } else if (curSfx == 4) {
                        f = (sfxPos < 4000) ? 528 : 792;
                        vol = 15000;
                    } else if (curSfx == 5) {
                        f = (sfxPos < 6000) ? 145 : 110;
                        vol = 17500;
                    } else if (curSfx == 6) {
                        f = 82 - (sfxPos >> 8);
                        if (f < 40) f = 40;
                        vol = 19000;
                    } else if (curSfx == 7) {
                        f = 135 + ((sfxPos >> 5) & 31);
                        vol = 18500;
                    } else if (curSfx == 8) {
                        f = 380 + ((sfxPos >> 3) & 127);
                        vol = 22000;
                    } else if (curSfx == 9) {
                        f = (sfxPos < 5000) ? 440 : ((sfxPos < 10000) ? 554 : 660);
                        vol = 16500;
                    } else {
                        f = 1200;
                        vol = 11000;
                    }

                    sfxPhase += f;
                    if (sfxPhase >= 48000) sfxPhase -= 48000;

                    int tri = sfxPhase < 24000 ? sfxPhase : 48000 - sfxPhase;
                    int s = ((tri - 12000) * vol) / 12000;

                    if (curSfx == 3 || curSfx == 6 || curSfx == 7 || curSfx == 8) {
                        int nz = ((int)(noiseRng >> 18) & 1023) - 512;
                        s += nz * (curSfx == 8 ? 14 : 7);
                    }

                    if (rem < 1500)
                        s = (s * rem) / 1500;

                    mix += s;
                    sfxPos++;
                }
            }

            if (mix > 30000) mix = 30000;
            if (mix < -30000) mix = -30000;

            outBuf[i * 2]     = (short)mix;
            outBuf[i * 2 + 1] = (short)mix;
        }

        sceAudioOutOutput(audioHandle, outBuf);
    }
#endif
}

// ---------- 2D & Text Drawing Helpers ----------
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
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= '0' && ch <= '9') return 26 + ch - '0';
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

static void drawText(int x, int y, const char *s, int scale, Color c) {
    for (; *s; s++) {
        int g = glyphIndex(*s);
        if (g != 40) {
            for (int row = 0; row < 7; row++) {
                unsigned char bits = FONT[g][row];
                for (int col = 0; col < 5; col++) {
                    if (bits & (0x10 >> col)) {
                        fillRect(x + col * scale, y + row * scale, scale, scale, c);
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

static void drawTextC(int cx, int y, const char *s, int scale, Color c) {
    drawText(cx - textWidth(s, scale) / 2, y, s, scale, c);
}

static int arW(int id) {
#if ARABIC_OK
    return (id >= 0 && id < AR_COUNT) ? AR_IMGS[id].w : 0;
#else
    (void)id; return 0;
#endif
}

static int arH(int id) {
#if ARABIC_OK
    return (id >= 0 && id < AR_COUNT) ? AR_IMGS[id].h : 0;
#else
    (void)id; return 0;
#endif
}

static void drawArCols(int id, int x, int y, int c0, int c1, Color col) {
#if ARABIC_OK
    if (id < 0 || id >= AR_COUNT) return;
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

static void drawLabel(int tid, int cx, int boxY, int boxH, int sc, Color c) {
    if (useAr() && TXT_AR[tid] >= 0 && TXT_AR[tid] < AR_COUNT) {
        int id = TXT_AR[tid];
        int w = arW(id);
        int h = arH(id);
        drawArCols(id, cx - w / 2, boxY + (boxH - h) / 2, 0, w, c);
    } else {
        drawTextC(cx, boxY + (boxH - 7 * sc) / 2, TXT_EN[tid], sc, c);
    }
}

static int labelWidth(int tid, int sc) {
    if (useAr() && TXT_AR[tid] >= 0 && TXT_AR[tid] < AR_COUNT)
        return arW(TXT_AR[tid]);
    return textWidth(TXT_EN[tid], sc);
}

// ---------- Map & Collision Helpers ----------
static char mapCell(int fl, int mx, int my) {
    if (fl < 0 || fl >= NUM_FLOORS || mx < 0 || mx >= MAP_W || my < 0 || my >= MAP_H)
        return '#';
    return HOUSE_MAP[fl][my][mx];
}

static bool isWallCell(int fl, int mx, int my) {
    char c = mapCell(fl, mx, my);
    return c == '#' || c == 'W' || c == 'B' || c == 'D';
}

static bool canWalk(int fl, float nx, float ny, float rad) {
    return !isWallCell(fl, (int)(nx - rad), (int)(ny - rad)) &&
           !isWallCell(fl, (int)(nx + rad), (int)(ny - rad)) &&
           !isWallCell(fl, (int)(nx - rad), (int)(ny + rad)) &&
           !isWallCell(fl, (int)(nx + rad), (int)(ny + rad));
}

static bool hasLineOfSight(int fl, float x0, float y0, float x1, float y1) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 0.1f) return true;
    int steps = (int)(dist * 4.0f) + 1;
    for (int i = 1; i < steps; i++) {
        float t = (float)i / (float)steps;
        if (isWallCell(fl, (int)(x0 + dx * t), (int)(y0 + dy * t)))
            return false;
    }
    return true;
}

static void invalidate3DBuffers() {
    for (int b = 0; b < 2; b++) {
        hudFull[b] = true;
        for (int y = 0; y < VH; y++)
            for (int x = 0; x < VW; x++)
                vbuf[b][y][x] = 0xFFFF;
    }
}

static void resetGame() {
    plX = 7.5f;
    plY = 12.5f;
    plAngle = -1.570796f;
    plFloor = 0;
    plPitch = 0.0f;
    headBob = 0.0f;
    stepTimer = 0.0f;
    flashlightOn = true;
    noiseLevel = 0.0f;
    speedAlertTimer = 0;
    doorAlertTimer = 0;
    stairCooldown = 0;

    booksCollected = 0;
    for (int i = 0; i < TOTAL_BOOKS; i++) {
        books[i].floor = (int)BOOK_INIT[i][0];
        books[i].x = BOOK_INIT[i][1];
        books[i].y = BOOK_INIT[i][2];
        books[i].collected = false;
    }

    stX = 7.5f;
    stY = 2.5f;
    stFloor = 1;
    stMode = AI_PATROL;
    stWaypoint = 0;
    stTargetX = PATROL_PTS[0][0];
    stTargetY = PATROL_PTS[0][1];
    stTargetFloor = 1;
    stDistToPlayer = 20.0f;
    stStairCooldown = 0;
    stRoarCooldown = 0;

    invalidate3DBuffers();
}

// ---------- Smart Stalker AI & Player Update ----------
static float getStalkerSpeed() {
    int tier = booksCollected / 2;
    return 0.025f + (float)tier * 0.0085f;
}

static void updateStalkerAI() {
    if (stStairCooldown > 0) stStairCooldown--;
    if (stRoarCooldown > 0) stRoarCooldown--;

    float dxP = plX - stX;
    float dyP = plY - stY;
    stDistToPlayer = sqrtf(dxP * dxP + dyP * dyP);

    bool canSee = false;
    if (stFloor == plFloor) {
        float seeRange = flashlightOn ? 10.5f : 6.2f;
        if (stDistToPlayer < seeRange && hasLineOfSight(stFloor, stX, stY, plX, plY)) {
            canSee = true;
        }
    }

    if (canSee) {
        if (stMode != AI_CHASE && stRoarCooldown == 0) {
            snd(7);
            stRoarCooldown = 180;
        }
        stMode = AI_CHASE;
        stTargetX = plX;
        stTargetY = plY;
        stTargetFloor = plFloor;
    } else {
        float hearRange = noiseLevel * 13.5f;
        float floorPenalty = (stFloor == plFloor) ? 0.0f : 3.5f;
        if (stDistToPlayer + floorPenalty < hearRange) {
            if (stMode == AI_PATROL && stRoarCooldown == 0) {
                snd(7);
                stRoarCooldown = 210;
            }
            stMode = AI_INVESTIGATE;
            stTargetX = plX;
            stTargetY = plY;
            stTargetFloor = plFloor;
        }
    }

    float goalX = stTargetX;
    float goalY = stTargetY;

    if (stFloor != stTargetFloor) {
        goalX = 7.5f;
        goalY = 2.0f;
        float dStair = sqrtf((stX - goalX) * (stX - goalX) + (stY - goalY) * (stY - goalY));
        if (dStair < 0.7f && stStairCooldown == 0) {
            stFloor = stTargetFloor;
            stStairCooldown = 90;
            hudFull[0] = true;
            hudFull[1] = true;
        }
    }

    float gdx = goalX - stX;
    float gdy = goalY - stY;
    float gdist = sqrtf(gdx * gdx + gdy * gdy);

    float spd = getStalkerSpeed();
    if (stMode == AI_CHASE)
        spd *= 1.18f;

    if (gdist > 0.25f) {
        float vx = (gdx / gdist) * spd;
        float vy = (gdy / gdist) * spd;

        if (canWalk(stFloor, stX + vx, stY, 0.28f))
            stX += vx;
        else if (canWalk(stFloor, stX, stY + (vy > 0 ? spd : -spd), 0.28f))
            stY += (vy > 0 ? spd : -spd);

        if (canWalk(stFloor, stX, stY + vy, 0.28f))
            stY += vy;
        else if (canWalk(stFloor, stX + (vx > 0 ? spd : -spd), stY, 0.28f))
            stX += (vx > 0 ? spd : -spd);
    } else {
        stMode = AI_PATROL;
        stWaypoint = (stWaypoint + 1) % 6;
        stTargetX = PATROL_PTS[stWaypoint][0];
        stTargetY = PATROL_PTS[stWaypoint][1];
        if (stWaypoint == 0 && stFloor != plFloor)
            stTargetFloor = plFloor;
    }

    if (stFloor == plFloor && stDistToPlayer < 0.62f) {
        state = S_CAUGHT_3D;
        cutTick = 0;
        snd(8);
        invalidate3DBuffers();
    }
}

static void updatePlay3D() {
    if (stairCooldown > 0) stairCooldown--;
    if (speedAlertTimer > 0) {
        speedAlertTimer--;
        if (speedAlertTimer == 0) { hudFull[0] = true; hudFull[1] = true; }
    }
    if (doorAlertTimer > 0) {
        doorAlertTimer--;
        if (doorAlertTimer == 0) { hudFull[0] = true; hudFull[1] = true; }
    }

    if (in.triangleNew) {
        flashlightOn = !flashlightOn;
        snd(10);
        hudFull[0] = true;
        hudFull[1] = true;
    }

    float rotSpd = 0.048f;
    if (in.rx != 0.0f)
        plAngle += in.rx * rotSpd;
    if (controlMode == 0) {
        if (in.dleft)  plAngle -= rotSpd * 0.85f;
        if (in.dright) plAngle += rotSpd * 0.85f;
    }
    if (in.ry != 0.0f) {
        plPitch = iclamp((int)(plPitch - in.ry * 3.5f), -24, 24);
    } else {
        plPitch *= 0.9f;
    }

    float fwd = 0.0f;
    float str = 0.0f;

    if (controlMode == 1) {
        fwd = -in.ly;
        str = in.lx;
        if (in.dup)    fwd = 1.0f;
        if (in.ddown)  fwd = -1.0f;
        if (in.dleft)  str = -1.0f;
        if (in.dright) str = 1.0f;
    } else {
        if (in.dup)   fwd = 1.0f;
        if (in.ddown) fwd = -1.0f;
    }

    bool sprinting = in.square && (fabs_f(fwd) > 0.1f || fabs_f(str) > 0.1f);
    bool sneaking  = in.circle;

    float moveSpd = 0.046f;
    if (sprinting)     moveSpd = 0.074f;
    else if (sneaking) moveSpd = 0.026f;

    float cosA = cosf(plAngle);
    float sinA = sinf(plAngle);

    float mvX = (cosA * fwd - sinA * str) * moveSpd;
    float mvY = (sinA * fwd + cosA * str) * moveSpd;

    bool moved = (fabs_f(mvX) > 0.002f || fabs_f(mvY) > 0.002f);

    if (moved) {
        if (canWalk(plFloor, plX + mvX, plY, 0.25f))
            plX += mvX;
        if (canWalk(plFloor, plX, plY + mvY, 0.25f))
            plY += mvY;

        headBob += sprinting ? 0.24f : (sneaking ? 0.10f : 0.16f);
        stepTimer += sprinting ? 1.8f : (sneaking ? 0.6f : 1.0f);

        if (stepTimer >= 26.0f) {
            stepTimer = 0.0f;
            if (!sneaking)
                snd(3);
        }

        float targetNoise = sprinting ? 1.0f : (sneaking ? 0.08f : 0.38f);
        if ((noiseLevel < 0.6f && targetNoise >= 0.6f) || (noiseLevel >= 0.6f && targetNoise < 0.6f)) {
            hudFull[0] = true;
            hudFull[1] = true;
        }
        noiseLevel = targetNoise;
    } else {
        if (noiseLevel >= 0.6f) {
            hudFull[0] = true;
            hudFull[1] = true;
        }
        noiseLevel *= 0.85f;
        headBob = 0.0f;
    }

    if (stairCooldown == 0 && mapCell(plFloor, (int)plX, (int)plY) == 'S') {
        plFloor = 1 - plFloor;
        plY = 2.4f;
        stairCooldown = 75;
        snd(3);
        hudFull[0] = true;
        hudFull[1] = true;
    }

    for (int i = 0; i < TOTAL_BOOKS; i++) {
        if (books[i].collected || books[i].floor != plFloor)
            continue;

        float bdx = books[i].x - plX;
        float bdy = books[i].y - plY;
        float bdist = sqrtf(bdx * bdx + bdy * bdy);

        if (bdist < 0.75f || (bdist < 1.35f && in.crossNew)) {
            books[i].collected = true;
            booksCollected++;
            hudFull[0] = true;
            hudFull[1] = true;

            if (booksCollected == TOTAL_BOOKS) {
                doorAlertTimer = 300;
                speedAlertTimer = 180;
                snd(6);
            } else if ((booksCollected % 2) == 0) {
                speedAlertTimer = 200;
                snd(5);
            } else {
                snd(4);
            }
        }
    }

    if (plFloor == 0 && plX >= 6.5f && plX <= 9.5f && plY >= 12.8f) {
        if (booksCollected >= TOTAL_BOOKS && (plY >= 13.4f || in.crossNew)) {
            state = S_OUTRO_3D;
            cutTick = 0;
            snd(9);
            invalidate3DBuffers();
            return;
        }
    }

    updateStalkerAI();
}

// ---------- High-Speed 60-FPS 3D Raycasting & Sprite Renderer ----------
static void putVirtualPixel(int b, int vx, int vy, int r, int g, int bl) {
    if (vx < 0 || vx >= VW || vy < 0 || vy >= VH)
        return;

    int qr = iclamp(r, 0, 255) & 0xF8;
    int qg = iclamp(g, 0, 255) & 0xF8;
    int qb = iclamp(bl, 0, 255) & 0xF8;

    unsigned short key = (unsigned short)(((qr >> 3) << 10) | ((qg >> 3) << 5) | (qb >> 3));
    if (vbuf[b][vy][vx] == key)
        return;

    vbuf[b][vy][vx] = key;
    Color c = mk(qr, qg, qb);

    int sx0 = vx * CELL_W;
    int sy0 = VP_Y0 + vy * CELL_H;

    for (int py = sy0; py < sy0 + 4; py++) {
        for (int px0 = sx0; px0 < sx0 + CELL_W; px0++) {
            scene->DrawPixel(px0, py, c);
        }
    }
}

static void render3DScene(
    int b,
    float camX0, float camY0, float camAng,
    int camFl, float pitchOff, bool flashActive,
    bool drawStalker, float stalkerX, float stalkerY, int stalkerFl
) {
    float dirX = cosf(camAng);
    float dirY = sinf(camAng);
    float planeX = -dirY * 0.66f;
    float planeY =  dirX * 0.66f;

    int horizon = (VH / 2) + (int)pitchOff + (int)(sinf(headBob) * 2.5f);

    for (int x = 0; x < VW; x++) {
        float camScreenX = 2.0f * (float)x / (float)VW - 1.0f;
        float rayDirX = dirX + planeX * camScreenX;
        float rayDirY = dirY + planeY * camScreenX;

        int mapX = (int)camX0;
        int mapY = (int)camY0;

        float deltaDistX = (fabs_f(rayDirX) < 0.0001f) ? 1e30f : fabs_f(1.0f / rayDirX);
        float deltaDistY = (fabs_f(rayDirY) < 0.0001f) ? 1e30f : fabs_f(1.0f / rayDirY);

        float sideDistX, sideDistY;
        int stepX, stepY;

        if (rayDirX < 0.0f) {
            stepX = -1;
            sideDistX = (camX0 - (float)mapX) * deltaDistX;
        } else {
            stepX = 1;
            sideDistX = ((float)mapX + 1.0f - camX0) * deltaDistX;
        }

        if (rayDirY < 0.0f) {
            stepY = -1;
            sideDistY = (camY0 - (float)mapY) * deltaDistY;
        } else {
            stepY = 1;
            sideDistY = ((float)mapY + 1.0f - camY0) * deltaDistY;
        }

        int side = 0;
        char hitTile = '#';

        for (int iter = 0; iter < 32; iter++) {
            if (sideDistX < sideDistY) {
                sideDistX += deltaDistX;
                mapX += stepX;
                side = 0;
            } else {
                sideDistY += deltaDistY;
                mapY += stepY;
                side = 1;
            }

            char c = mapCell(camFl, mapX, mapY);
            if (c == '#' || c == 'W' || c == 'B' || c == 'D') {
                hitTile = c;
                break;
            }
        }

        float perpWallDist = (side == 0) ? (sideDistX - deltaDistX) : (sideDistY - deltaDistY);
        if (perpWallDist < 0.12f) perpWallDist = 0.12f;
        zBuffer[x] = perpWallDist;

        int lineH = (int)((float)VH / perpWallDist);
        int drawStart = -lineH / 2 + horizon;
        int drawEnd   =  lineH / 2 + horizon;

        float wallX = (side == 0) ? (camY0 + perpWallDist * rayDirY) : (camX0 + perpWallDist * rayDirX);
        wallX -= floorf(wallX);
        int texX = (int)(wallX * 16.0f) & 15;

        float centerXDist = camScreenX * camScreenX;
        float wallLight = 0.24f / (1.0f + perpWallDist * 0.18f);
        if (flashActive) {
            float coneX = 1.0f - centerXDist * 1.15f;
            if (coneX > 0.0f) {
                float reach = 1.35f / (1.0f + perpWallDist * 0.11f);
                wallLight += coneX * reach;
            }
        }
        if (side == 1) wallLight *= 0.80f;
        if (wallLight > 1.35f) wallLight = 1.35f;

        int baseR = (camFl == 0) ? 128 : 105;
        int baseG = (camFl == 0) ? 88  : 75;
        int baseB = (camFl == 0) ? 58  : 110;

        if (hitTile == 'W') {
            baseR = 95; baseG = 110; baseB = 138;
        } else if (hitTile == 'B') {
            baseR = 140; baseG = 72; baseB = 45;
        } else if (hitTile == 'D') {
            if (booksCollected >= TOTAL_BOOKS) {
                baseR = 45; baseG = 220; baseB = 110;
            } else {
                baseR = 175; baseG = 45; baseB = 38;
            }
        }

        if (texX == 0 || texX == 15) {
            baseR = (baseR * 3) / 4;
            baseG = (baseG * 3) / 4;
            baseB = (baseB * 3) / 4;
        }

        for (int y = 0; y < VH; y++) {
            if (y < drawStart) {
                int dy = horizon - y;
                if (dy < 1) dy = 1;
                float cDist = (float)VH / (2.0f * (float)dy);
                float cLight = 0.18f / (1.0f + cDist * 0.22f);
                if (flashActive) {
                    float dyNorm = (float)(y - horizon) / (float)(VH / 2);
                    float spot = 1.0f - (centerXDist * 1.3f + dyNorm * dyNorm * 0.9f);
                    if (spot > 0.0f)
                        cLight += spot * (1.1f / (1.0f + cDist * 0.15f));
                }
                int cr = (int)(58.0f * cLight);
                int cg = (int)(44.0f * cLight);
                int cb = (int)(64.0f * cLight);
                putVirtualPixel(b, x, y, cr, cg, cb);
            }
            else if (y <= drawEnd) {
                int texY = (((y - drawStart) * 16) / (lineH > 0 ? lineH : 1)) & 15;
                int wr = baseR, wg = baseG, wb = baseB;

                if (hitTile == 'B' && (texY & 3) == 2 && texX > 1 && texX < 14) {
                    wr = ((texX * 37) & 64) ? 180 : 70;
                    wg = ((texY * 23) & 64) ? 150 : 60;
                    wb = 130;
                } else if (hitTile == 'D' && texX >= 5 && texX <= 10 && texY >= 4 && texY <= 11) {
                    wr = (booksCollected >= TOTAL_BOOKS) ? 180 : 255;
                    wg = (booksCollected >= TOTAL_BOOKS) ? 255 : 185;
                    wb = 90;
                } else if (texY == 0 || texY == 8) {
                    wr = (wr * 4) / 5;
                    wg = (wg * 4) / 5;
                    wb = (wb * 4) / 5;
                }

                putVirtualPixel(b, x, y,
                                (int)(wr * wallLight),
                                (int)(wg * wallLight),
                                (int)(wb * wallLight));
            }
            else {
                int dy = y - horizon;
                if (dy < 1) dy = 1;
                float fDist = (float)VH / (2.0f * (float)dy);
                float fx = camX0 + rayDirX * fDist;
                float fy = camY0 + rayDirY * fDist;

                float fLight = 0.22f / (1.0f + fDist * 0.20f);
                if (flashActive) {
                    float dyNorm = (float)(y - horizon) / (float)(VH / 2);
                    float spot = 1.0f - (centerXDist * 1.25f + dyNorm * dyNorm * 0.85f);
                    if (spot > 0.0f)
                        fLight += spot * (1.25f / (1.0f + fDist * 0.12f));
                }

                char fTile = mapCell(camFl, (int)fx, (int)fy);
                int checker = (((int)(fx * 2.0f) + (int)(fy * 2.0f)) & 1);
                int fr, fg, fb;

                if (fTile == 'S') {
                    int stepStripe = ((int)(fy * 8.0f)) & 1;
                    fr = stepStripe ? 210 : 145;
                    fg = stepStripe ? 165 : 105;
                    fb = stepStripe ? 85  : 50;
                } else if (camFl == 0) {
                    fr = checker ? 96 : 78;
                    fg = checker ? 66 : 52;
                    fb = checker ? 46 : 36;
                } else {
                    fr = checker ? 76 : 62;
                    fg = checker ? 58 : 46;
                    fb = checker ? 88 : 72;
                }

                putVirtualPixel(b, x, y,
                                (int)(fr * fLight),
                                (int)(fg * fLight),
                                (int)(fb * fLight));
            }
        }
    }

    float invDet = 1.0f / (planeX * dirY - dirX * planeY);

    for (int i = 0; i < TOTAL_BOOKS; i++) {
        if (books[i].collected || books[i].floor != camFl)
            continue;

        float sx = books[i].x - camX0;
        float sy = books[i].y - camY0;

        float transX = invDet * (dirY * sx - dirX * sy);
        float transY = invDet * (-planeY * sx + planeX * sy);

        if (transY <= 0.2f)
            continue;

        int screenX = (int)((VW / 2) * (1.0f + transX / transY));
        int spH = iabs((int)(VH / transY));
        int spW = spH;

        int bookH = spH / 3;
        int bookW = spW / 3;
        if (bookH < 4) bookH = 4;
        if (bookW < 4) bookW = 4;

        int floatOffset = (int)(sinf((float)tick * 0.1f + (float)i) * (spH * 0.08f));
        int yCenter = horizon + spH / 5 + floatOffset;

        int y0 = iclamp(yCenter - bookH / 2, 0, VH - 1);
        int y1 = iclamp(yCenter + bookH / 2, 0, VH - 1);
        int x0 = iclamp(screenX - bookW / 2, 0, VW - 1);
        int x1 = iclamp(screenX + bookW / 2, 0, VW - 1);

        for (int xx = x0; xx <= x1; xx++) {
            if (transY >= zBuffer[xx])
                continue;

            bool border = (xx == x0 || xx == x1);
            for (int yy = y0; yy <= y1; yy++) {
                bool goldTrim = border || (yy == y0 || yy == y1);
                if (goldTrim)
                    putVirtualPixel(b, xx, yy, 255, 225, 85);
                else
                    putVirtualPixel(b, xx, yy, 220, 35, 55);
            }
        }
    }

    if (drawStalker && stalkerFl == camFl) {
        float sx = stalkerX - camX0;
        float sy = stalkerY - camY0;

        float transX = invDet * (dirY * sx - dirX * sy);
        float transY = invDet * (-planeY * sx + planeX * sy);

        if (transY > 0.18f) {
            int screenX = (int)((VW / 2) * (1.0f + transX / transY));
            int spH = iabs((int)((VH * 1.15f) / transY));
            int spW = (spH * 3) / 5;

            int y0 = iclamp(horizon - spH / 2, 0, VH - 1);
            int y1 = iclamp(horizon + spH / 2, 0, VH - 1);
            int x0 = iclamp(screenX - spW / 2, 0, VW - 1);
            int x1 = iclamp(screenX + spW / 2, 0, VW - 1);

            float shade = 1.3f / (1.0f + transY * 0.14f);
            if (shade > 1.2f) shade = 1.2f;

            for (int xx = x0; xx <= x1; xx++) {
                if (transY >= zBuffer[xx])
                    continue;

                int relX = ((xx - (screenX - spW / 2)) * 16) / (spW > 0 ? spW : 1);

                for (int yy = y0; yy <= y1; yy++) {
                    int relY = ((yy - (horizon - spH / 2)) * 24) / (spH > 0 ? spH : 1);

                    if (relY < 6) {
                        if (relX < 4 || relX > 11) continue;
                        if (relY == 3 && (relX == 5 || relX == 6 || relX == 9 || relX == 10)) {
                            putVirtualPixel(b, xx, yy, 255, 45, 35);
                        } else {
                            putVirtualPixel(b, xx, yy, (int)(35 * shade), (int)(15 * shade), (int)(25 * shade));
                        }
                    } else if (relY < 17) {
                        if (relX < 2 || relX > 13) continue;
                        bool rib = ((relY & 1) == 0 && relX >= 5 && relX <= 10);
                        int cr = rib ? (int)(120 * shade) : (int)(30 * shade);
                        int cg = rib ? (int)(25 * shade)  : (int)(12 * shade);
                        int cb = rib ? (int)(35 * shade)  : (int)(22 * shade);
                        putVirtualPixel(b, xx, yy, cr, cg, cb);
                    } else {
                        if (relX < 4 || relX > 11 || (relX == 7 || relX == 8)) continue;
                        putVirtualPixel(b, xx, yy, (int)(28 * shade), (int)(12 * shade), (int)(20 * shade));
                    }
                }
            }
        }
    }
}

// ---------- HUD & 3D Cutscene Overlays ----------
static void draw3DHUD(int b) {
    if (hudFull[b]) {
        fillRect(0, 0, FRAME_WIDTH, VP_Y0 - 2, mk(16, 12, 24));
        fillRect(0, VP_Y0 - 4, FRAME_WIDTH, 4, mk(65, 35, 55));

        drawLabel(T_BOOKS, 140, 18, 42, 4, mk(240, 215, 120));
        for (int i = 0; i < TOTAL_BOOKS; i++) {
            int bx = 250 + i * 44;
            bool got = (i < booksCollected);
            fillRect(bx, 20, 32, 38, got ? mk(255, 215, 70) : mk(45, 35, 60));
            fillRect(bx + 4, 24, 24, 30, got ? mk(210, 35, 50) : mk(28, 22, 38));
        }

        int flTid = (plFloor == 0) ? T_FLOOR1 : T_FLOOR2;
        if (mapCell(plFloor, (int)plX, (int)plY) == 'S')
            flTid = T_STAIRS;
        fillRect(560, 18, 280, 44, mk(32, 24, 48));
        drawLabel(flTid, 700, 18, 44, 3, mk(185, 220, 255));

        fillRect(880, 18, 380, 44, mk(32, 24, 48));
        drawLabel(flashlightOn ? T_FLASH_ON : T_FLASH_OFF,
                  1070, 18, 44, 2,
                  flashlightOn ? mk(255, 235, 130) : mk(140, 130, 160));

        bool loud = (noiseLevel >= 0.6f);
        fillRect(1290, 18, 580, 44, loud ? mk(145, 25, 35) : mk(32, 24, 48));
        drawLabel(loud ? T_NOISE_HIGH : T_NOISE_LOW,
                  1580, 18, 44, 2,
                  loud ? mk(255, 230, 210) : mk(165, 230, 185));

        int botY = VP_Y0 + VH * CELL_H;
        fillRect(0, botY, FRAME_WIDTH, FRAME_HEIGHT - botY, mk(16, 12, 24));
        fillRect(0, botY, FRAME_WIDTH, 4, mk(65, 35, 55));

        int promptTid = -1;
        Color promptCol = mk(230, 220, 245);

        if (doorAlertTimer > 0 || booksCollected >= TOTAL_BOOKS) {
            promptTid = (plFloor == 0 && plX >= 6.0f && plX <= 10.0f && plY >= 12.0f) ?
                        T_ESCAPE_NOW : T_DOOR_OPEN;
            promptCol = mk(95, 255, 135);
        } else if (speedAlertTimer > 0) {
            promptTid = T_SPEED_UP;
            promptCol = mk(255, 140, 60);
        } else if (stFloor == plFloor && stDistToPlayer < 5.5f) {
            promptTid = T_STALKER_NEAR;
            promptCol = mk(255, 75, 75);
        } else if (plFloor == 0 && plX >= 6.0f && plX <= 10.0f && plY >= 12.2f) {
            promptTid = T_DOOR_LOCKED;
            promptCol = mk(255, 195, 95);
        } else {
            for (int i = 0; i < TOTAL_BOOKS; i++) {
                if (!books[i].collected && books[i].floor == plFloor) {
                    float dx = books[i].x - plX;
                    float dy = books[i].y - plY;
                    if (dx * dx + dy * dy < 3.2f) {
                        promptTid = T_PICK_BOOK;
                        promptCol = mk(255, 235, 110);
                        break;
                    }
                }
            }
        }

        if (promptTid >= 0) {
            drawLabel(promptTid, 960, botY + 8, 56, 3, promptCol);
        }

        hudFull[b] = false;
    }
}

static void renderIntro3D(int b) {
    float t = (float)cutTick;
    float camX0 = 7.5f;
    float camY0 = 12.8f;
    float camAng = -1.570796f;
    float pitch = 0.0f;
    int subTid = T_INTRO_1;

    if (cutTick < 130) {
        camY0 = 13.0f - (t / 130.0f) * 2.2f;
        headBob = t * 0.14f;
        subTid = T_INTRO_1;
    } else if (cutTick < 260) {
        camY0 = 10.8f;
        float p = (t - 130.0f) / 60.0f;
        if (p > 1.0f) p = 1.0f;
        camAng = -1.570796f + p * 3.14159f;
        subTid = T_INTRO_2;
    } else {
        camY0 = 10.8f;
        float p = (t - 260.0f) / 50.0f;
        if (p > 1.0f) p = 1.0f;
        camAng = 1.570796f - p * 3.14159f;
        pitch = 6.0f;
        subTid = T_INTRO_3;
    }

    float stalkX = 5.5f + (t / 420.0f) * 4.5f;
    float stalkY = 6.5f;

    render3DScene(b, camX0, camY0, camAng, 0, pitch, true,
                  (cutTick >= 270), stalkX, stalkY, 0);

    if (cutTick == 1 || cutTick == 2 || cutTick == 130 || cutTick == 131 || cutTick == 260 || cutTick == 261) {
        fillRect(0, 0, FRAME_WIDTH, VP_Y0 - 2, mk(12, 8, 18));
        drawLabel(T_TITLE, 960, 15, 80, 6, mk(215, 35, 55));

        int botY = VP_Y0 + VH * CELL_H;
        fillRect(0, botY, FRAME_WIDTH, FRAME_HEIGHT - botY, mk(12, 8, 18));
        drawLabel(subTid, 960, botY + 4, 42, 3, mk(245, 235, 255));
        drawLabel(T_H_STORY, 960, botY + 42, 26, 2, mk(170, 160, 195));
    }
}

static void renderOutro3D(int b) {
    float t = (float)cutTick;
    float camX0 = 7.5f;
    float camY0 = 10.5f + (t / 320.0f) * 3.0f;
    if (camY0 > 13.4f) camY0 = 13.4f;
    float camAng = -1.570796f + sinf(t * 0.03f) * 0.12f;

    render3DScene(b, camX0, camY0, camAng, 0, 0.0f, true,
                  true, 7.5f, 7.5f, 0);

    if (cutTick == 1 || cutTick == 2 || cutTick == 140 || cutTick == 141) {
        fillRect(0, 0, FRAME_WIDTH, VP_Y0 - 2, mk(15, 55, 38));
        drawLabel(T_OUTRO_1, 960, 22, 65, 4, mk(140, 255, 175));

        int botY = VP_Y0 + VH * CELL_H;
        fillRect(0, botY, FRAME_WIDTH, FRAME_HEIGHT - botY, mk(15, 55, 38));
        drawLabel(T_OUTRO_2, 960, botY + 12, 50, 4, mk(255, 240, 150));
    }
}

static void renderCaught3D(int b) {
    float shakeX = ((cutTick & 2) ? 0.08f : -0.08f);
    float stalkX = plX + cosf(plAngle) * 0.65f + shakeX;
    float stalkY = plY + sinf(plAngle) * 0.65f;

    render3DScene(b, plX, plY, plAngle, plFloor, 4.0f, true,
                  true, stalkX, stalkY, plFloor);

    if (cutTick == 1 || cutTick == 2) {
        fillRect(0, 0, FRAME_WIDTH, VP_Y0 - 2, mk(90, 12, 20));
        drawLabel(T_CAUGHT_1, 960, 20, 70, 5, mk(255, 220, 220));

        int botY = VP_Y0 + VH * CELL_H;
        fillRect(0, botY, FRAME_WIDTH, FRAME_HEIGHT - botY, mk(90, 12, 20));
        drawLabel(T_CAUGHT_2, 960, botY + 12, 50, 3, mk(255, 240, 180));
    }
}

// ---------- Menus ----------
static const int MENU_T[4] = { T_START, T_SETTINGS, T_CREDITS, T_EXIT };

static void setState(int s) {
    state = s;
    uiFull[0] = true;
    uiFull[1] = true;
    invalidate3DBuffers();
}

static void markPend(int i) {
    if (i >= 0 && i < 16) {
        pend[0][i] = true;
        pend[1][i] = true;
    }
}

static int itemCount() {
    if (state == S_MENU) return 4;
    if (state == S_SETTINGS) return 3;
    return 0;
}

static void drawBanner(int tid, int scale, int y, int h) {
    fillRect(420, y, 1080, h, mk(28, 18, 38));
    fillRect(424, y + 4, 1072, h - 8, mk(42, 24, 52));
    drawLabel(tid, 960, y, h, scale, mk(225, 40, 75));
}

static void drawHint(int tid) {
    int w = labelWidth(tid, 3) + 80;
    fillRect(960 - w / 2, 980, w, 60, mk(28, 18, 38));
    drawLabel(tid, 960, 980, 60, 3, mk(200, 190, 220));
}

static void drawStaticMenu() {
    if (state == S_MENU) {
        drawBanner(T_TITLE, 10, 130, 150);
        drawHint(T_H_SELECT);
    } else if (state == S_SETTINGS) {
        drawBanner(T_B_SETTINGS, 8, 130, 110);
        fillRect(610, 740, 700, 60, mk(28, 18, 38));
        drawLabel(T_TIP, 960, 740, 60, 3, mk(200, 190, 220));
        drawHint(T_H_BACK);
    } else if (state == S_CREDITS) {
        drawBanner(T_B_CREDITS, 8, 130, 110);
        fillRect(460, 300, 1000, 540, mk(28, 18, 38));
        drawLabel(T_DESIGNERS, 960, 340, 60, 5, mk(200, 190, 220));
        drawLabel(T_KAREEM, 960, 450, 110, 10, mk(225, 40, 80));
        drawLabel(T_CLAUDE, 960, 580, 110, 10, mk(225, 40, 80));
        drawLabel(T_GAME, 960, 740, 60, 4, mk(150, 140, 175));
        drawHint(T_H_BACK);
    }
}

static void drawMenuItem(int i) {
    if (state == S_MENU) {
        if (i < 0 || i > 3) return;
        int x = 610;
        int y = 410 + i * 120;
        bool sel = (i == menuSel);

        fillRect(x, y, 700, 90, sel ? mk(195, 25, 45) : mk(38, 28, 52));
        drawLabel(MENU_T[i], 960, y, 90, 6, sel ? mk(255, 255, 255) : mk(200, 190, 215));
    } else if (state == S_SETTINGS) {
        if (i < 0 || i > 2) return;
        int x = 510;
        int y = 380 + i * 110;
        bool sel = (i == settingsSel);

        fillRect(x, y, 900, 90, sel ? mk(195, 25, 45) : mk(38, 28, 52));
        int tid = (i == 0) ? (controlMode == 1 ? T_CTRL_A : T_CTRL_B) :
                  ((i == 1) ? T_LANG : T_BACK);
        drawLabel(tid, 960, y, 90, 6, sel ? mk(255, 255, 255) : mk(200, 190, 215));
    }
}

static void drawUI(int b) {
    if (uiFull[b]) {
        scene->FrameBufferClear();
        drawStaticMenu();
        int n = itemCount();
        for (int i = 0; i < n; i++)
            drawMenuItem(i);
        uiFull[b] = false;
        for (int i = 0; i < 16; i++)
            pend[b][i] = false;
    } else {
        for (int i = 0; i < 16; i++) {
            if (pend[b][i]) {
                drawMenuItem(i);
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

        if (menuSel != old) {
            markPend(old);
            markPend(menuSel);
            snd(0);
        }

        if (ok) {
            if (menuSel == 0) {
                snd(1);
                resetGame();
                cutTick = 0;
                setState(S_INTRO_3D);
            } else if (menuSel == 1) {
                snd(1);
                settingsSel = 0;
                setState(S_SETTINGS);
            } else if (menuSel == 2) {
                snd(1);
                setState(S_CREDITS);
            } else {
                snd(2);
                exit(0);
            }
        }
    } else if (state == S_SETTINGS) {
        int old = settingsSel;
        if (nu) settingsSel = (settingsSel + 2) % 3;
        if (nd) settingsSel = (settingsSel + 1) % 3;

        if (settingsSel != old) {
            markPend(old);
            markPend(settingsSel);
            snd(0);
        }

        if (settingsSel == 0 && (nl || nr || ok)) {
            controlMode = 1 - controlMode;
            markPend(0);
            snd(0);
        } else if (settingsSel == 1 && (nl || nr || ok)) {
            if (ARABIC_OK) {
                lang = 1 - lang;
                snd(1);
                setState(S_SETTINGS);
            }
        } else if (settingsSel == 2 && ok) {
            snd(2);
            setState(S_MENU);
        }

        if (back) {
            snd(2);
            setState(S_MENU);
        }
    } else if (state == S_CREDITS) {
        if (ok || back) {
            snd(2);
            setState(S_MENU);
        }
    }
}

// ---------- Main ----------
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    scene = new Scene2D(FRAME_WIDTH, FRAME_HEIGHT, FRAME_DEPTH);
    if (!scene->Init(0xC000000, 2)) {
        for (;;);
    }

    if (!initPad()) {
        for (;;);
    }

    initAudio();
    resetGame();
    setState(S_MENU);
    snd(1);

    for (;;) {
        int b = frameID & 1;

        readInput();

        if (state == S_INTRO_3D) {
            cutTick++;
            if (cutTick == 135) snd(6);
            if (cutTick == 275) snd(7);

            renderIntro3D(b);

            if (in.crossNew || in.optionsNew || cutTick >= 390) {
                resetGame();
                setState(S_PLAY_3D);
            }
        }
        else if (state == S_PLAY_3D) {
            if (in.optionsNew) {
                setState(S_MENU);
            } else {
                updatePlay3D();
                if (state == S_PLAY_3D) {
                    render3DScene(b, plX, plY, plAngle, plFloor, plPitch, flashlightOn,
                                  true, stX, stY, stFloor);
                    draw3DHUD(b);
                }
            }
        }
        else if (state == S_OUTRO_3D) {
            cutTick++;
            renderOutro3D(b);

            if ((cutTick > 60 && in.crossNew) || in.optionsNew || cutTick >= 360) {
                setState(S_MENU);
            }
        }
        else if (state == S_CAUGHT_3D) {
            cutTick++;
            renderCaught3D(b);

            if (cutTick > 35 && in.crossNew) {
                resetGame();
                setState(S_PLAY_3D);
            } else if (in.optionsNew) {
                setState(S_MENU);
            }
        }
        else {
            updateUI();
            drawUI(b);
        }

        scene->SubmitFlip(frameID);
        updateHorrorAudio();
        scene->FrameWait(frameID);
        scene->FrameBufferSwap();

        frameID++;
        tick++;
    }

    return 0;
}
