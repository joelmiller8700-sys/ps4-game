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
#define MAX_SPLATS 280
#define MAX_PARTS 120
#define MAX_PLANTS 640
#define MAX_AMB 18
#define MAX_BIRDS 5
#define MAX_BOSS_SHOTS 14
#define MAX_ENEMIES 56
#define MAX_CHECKPOINTS 6
#define MAX_DIRTY 440

// 1.5 Minutes at 60 FPS = 90 * 60 = 5400 frames
#define SPLAT_MAX_AGE 5400

std::stringstream debugLogStream;

static Scene2D *scene;
static char level[ROWS][COLS + 1];

static int padHandle = -1;
static int padUserID = 0;
static OrbisPadData padData;

#ifndef NO_AUDIO
static int audioHandle = -1;
#endif

// ---------- Game & Fixed Room Camera state ----------
static float px, py, vx, vy;
static int roomIdx = 0;
static int levelCols = COLS;
static int levelPixelWidth = WORLD_WIDTH;

static float checkpointX = 80.0f;
static float checkpointY = 900.0f;
static int checkCols[MAX_CHECKPOINTS];
static int checkRows[MAX_CHECKPOINTS];
static bool checkActive[MAX_CHECKPOINTS];
static int checkCount = 0;

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

// ---------- Ground & Aerial Enemies System (Defeated ONLY by Stomping from Above!) ----------
enum {
    E_WALKER = 0,
    E_FLYER  = 1,
    E_JUMPER = 2,
    E_TURRET = 3
};

struct Enemy {
    float x, y, vx, vy;
    float minX, maxX, baseY;
    int kind, hp, maxHp, hurt, timer, dir;
    bool active;
};

static Enemy enemies[MAX_ENEMIES];
static int enemyCount = 0;

// ---------- Boss Fight state ----------
static bool bossActive = false;
static float bossX = 0.0f;
static float bossY = 0.0f;
static float bossVX = 0.0f;
static float bossVY = 0.0f;
static int bossW = 104;
static int bossH = 104;
static int bossHP = 5;
static int bossMaxHP = 5;
static int bossHurt = 0;
static int bossState = 0;
static int bossTimer = 0;
static int bossSwitchCooldown = 0;
static int bossBeamTimer = 0;

struct BossShot {
    float x, y, vx, vy;
    bool active;
};

static BossShot bossShots[MAX_BOSS_SHOTS];

// Blood stains that fly, land on ground, and fade/vanish after 1.5 minutes (5400 frames)
struct Splat {
    short room;
    short x, y, w, h;
    int bornTick;
    unsigned char stage;
    unsigned char drawnStage[2];
    bool drawn[2];
};

static Splat splats[MAX_SPLATS];
static int splatCount = 0;

struct Part {
    float x, y, vx, vy;
    short life, maxLife;
    unsigned char kind, size;
};

static Part parts[MAX_PARTS];

// Semi-dense interactive world grass
struct Plant {
    short room;
    short x, y;
    float angle;
    float vel;
    unsigned char height;
    unsigned char style;
    unsigned char seed;
    signed char drawnBend[2];
    bool drawn[2];
};

static Plant plants[MAX_PLANTS];
static int plantCount = 0;

// Ambient living world particles
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
static int unlockedLevels = 1;
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
    X(T_BOSS_HP, "BOSS HP", AR(BOSS_HP)) \
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

static const Page PAGES[11] = {
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

    {10, 3, {"STAGE 1: LIVING FOREST - STOMP ENEMIES",
             "TAP X FOR SHORT JUMP, HOLD X FOR HIGH JUMP.",
             "JUMP ON TOP OF ENEMIES TO DEFEAT THEM!"},
     {AR(T1L0), AR(T1L1), AR(T1L2)}},
    {11, 3, {"STAGE 2: COUNTRYSIDE - SPRINT & STOMP",
             "HOLD SQUARE TO SPRINT ACROSS WIDE BRIDGES.",
             "STOMP ENEMIES FROM ABOVE TO CLEAR THE PATH!"},
     {AR(T2L0), AR(T2L1), AR(T2L2)}},
    {12, 3, {"STAGE 3: GOLDEN DESERT - TWO-WALL JUMP",
             "ENTER BETWEEN TWO WALLS AND SLIDE SLOWLY.",
             "PRESS X TO CLIMB AND STOMP DESERT FOES!"},
     {AR(T3L0), AR(T3L1), AR(T3L2)}},
    {13, 3, {"STAGE 4: SNOWY PEAKS - LEDGE TIMING",
             "WATCH SPINNING SAWS AND TIME YOUR JUMPS.",
             "STOMP ENEMIES AND CROSS FROZEN LEDGES!"},
     {AR(T4L0), AR(T4L1), AR(T4L2)}},
    {14, 3, {"STAGE 5: BOSS FIGHT - MAGMA CRUSHER!",
             "STOMP ON THE BOSS HEAD OR HIT THE TOP SWITCH.",
             "DODGE FIREBALLS AND DEFEAT THE BOSS!"},
     {AR(T5L0), AR(T5L1), AR(T5L2)}},
    {15, 3, {"STAGE 10: BOSS FIGHT - SAW OVERLORD!",
             "BOUNCE ON THE BOSS HEAD USING SIDE WALLS.",
             "DODGE DOUBLE SHOTS AND SAVE THE FOREST!"},
     {AR(T10L0), AR(T10L1), AR(T10L2)}}
};

static const int STORY_FIRST[8] = {0, 3, 5, 6, 7, 8, 9, 10};
static const int STORY_LAST[8]  = {3, 5, 6, 7, 8, 9, 10, 11};

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

static bool isBossStage(int lvl) {
    return ((lvl + 1) % 5) == 0;
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

// ---------- PS4 Halal Audio Engine ----------
static int curSfx = -1;
static int sfxPos = 0;
static int sfxLen = 0;
static int sfxPhase = 0;

static int bgmSample = 0;
static int humPhase1 = 0;
static int humPhase2 = 0;
static int duffPhase = 0;
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
                if (phase >= 48000)
                    phase -= 48000;

                int tri = phase < 24000 ? phase : 48000 - phase;
                s = ((tri - 12000) * vol) / 12000;

                int remain = total - idx;
                if (remain < 1200)
                    s = (s * remain) / 1200;
            }

            buf[i * 2]     = (short)s;
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
    if (state != S_PLAY) {
        if (k == 0) {
            playToneImmediate(880, 25, 16000);
        } else if (k == 1) {
            playToneImmediate(660, 35, 18000);
            playToneImmediate(990, 55, 18000);
        } else if (k == 2) {
            playToneImmediate(440, 45, 16000);
        } else {
            playToneImmediate(220, 80, 16000);
        }
        return;
    }

    curSfx = k;
    sfxPos = 0;
    sfxPhase = 0;

    if (k == 0)      sfxLen = 1400;
    else if (k == 1) sfxLen = 4000;
    else if (k == 2) sfxLen = 2600;
    else if (k == 3) sfxLen = 3500;
    else if (k == 4) sfxLen = 2800;
    else if (k == 5) sfxLen = 3400;
    else if (k == 6) sfxLen = 6500;
    else if (k == 7) sfxLen = 7200;
    else             sfxLen = 13000;
}

static void updateGameplayAudio() {
#ifndef NO_AUDIO
    if (audioHandle < 0)
        return;

    if (state != S_PLAY)
        return;

    static short outBuf[256 * 2];

    static const int HUM_BASE[4]    = { 110, 130, 146, 130 };
    static const int HUM_HARMONY[4] = { 165, 195, 220, 195 };

    static const unsigned char BEAT_PAT[16] = {
        1, 0, 3, 2,  1, 0, 2, 0,
        1, 1, 3, 2,  0, 2, 3, 2
    };

    bool playBgm = (winTimer == 0);
    int stepSamples = bossActive ? 5200 : 6400;

    for (int blk = 0; blk < 2; blk++) {
        for (int i = 0; i < 256; i++) {
            int mix = 0;

            if (playBgm) {
                int step = (bgmSample / stepSamples) & 15;
                int inStep = bgmSample % stepSamples;
                int bar = (bgmSample / (stepSamples * 16)) & 3;
                bgmSample++;

                unsigned char hit = BEAT_PAT[step];

                if (hit == 1 && inStep < 3200) {
                    int env = 3200 - inStep;
                    int freq = 68 + (env >> 6);
                    duffPhase += freq;
                    if (duffPhase >= 48000) duffPhase -= 48000;
                    int wave = duffPhase < 24000 ? (duffPhase - 12000) : (36000 - duffPhase);
                    mix += (wave * env) / 4200;
                }
                else if (hit == 2 && inStep < 1400) {
                    int env = 1400 - inStep;
                    noiseRng = noiseRng * 1664525u + 1013904223u;
                    int nz = ((int)(noiseRng >> 20) & 511) - 256;
                    duffPhase += 280;
                    if (duffPhase >= 48000) duffPhase -= 48000;
                    int body = duffPhase < 24000 ? 280 : -280;
                    mix += ((nz + body) * env) / 280;
                }
                else if (hit == 3 && inStep < 750) {
                    int env = 750 - inStep;
                    noiseRng = noiseRng * 1664525u + 1013904223u;
                    int nz = ((int)(noiseRng >> 22) & 255) - 128;
                    mix += (nz * env) / 320;
                }

                int f1 = HUM_BASE[bar];
                int f2 = HUM_HARMONY[bar];
                if (bossActive) {
                    f1 += 22;
                    f2 += 33;
                }

                humPhase1 += f1;
                if (humPhase1 >= 48000) humPhase1 -= 48000;
                humPhase2 += f2;
                if (humPhase2 >= 48000) humPhase2 -= 48000;

                int v1 = humPhase1 < 24000 ? (humPhase1 - 12000) : (36000 - humPhase1);
                int v2 = humPhase2 < 24000 ? (humPhase2 - 12000) : (36000 - humPhase2);

                int breath = inStep < (stepSamples / 2) ? inStep : (stepSamples - inStep);
                int humAmp = 1400 + (breath / 6);
                mix += ((v1 + v2) * humAmp) / 10000;
            }

            if (curSfx >= 0) {
                int rem = sfxLen - sfxPos;
                if (rem <= 0) {
                    curSfx = -1;
                } else {
                    int f = 440;
                    int vol = 14000;

                    if (curSfx == 0) {
                        f = 720;
                        vol = 12000;
                    } else if (curSfx == 1) {
                        f = (sfxPos < 1800) ? 580 : 880;
                        vol = 14000;
                    } else if (curSfx == 4) {
                        f = 260 + (sfxPos >> 3);
                        vol = 13000;
                    } else if (curSfx == 5) {
                        f = 350 + (sfxPos >> 2);
                        vol = 14500;
                    } else if (curSfx == 6) {
                        f = 165 - (sfxPos >> 6);
                        if (f < 55) f = 55;
                        vol = 16000;
                    } else if (curSfx == 7) {
                        f = 220 - (sfxPos >> 6);
                        if (f < 65) f = 65;
                        vol = 17500;
                    } else {
                        f = (sfxPos < 4200) ? 440 : ((sfxPos < 8400) ? 550 : 660);
                        vol = 16000;
                    }

                    sfxPhase += f;
                    if (sfxPhase >= 48000) sfxPhase -= 48000;

                    int tri = sfxPhase < 24000 ? sfxPhase : 48000 - sfxPhase;
                    int s = ((tri - 12000) * vol) / 12000;

                    if (curSfx == 6 || curSfx == 7) {
                        noiseRng = noiseRng * 1664525u + 1013904223u;
                        int nz = ((int)(noiseRng >> 19) & 1023) - 512;
                        s += nz * 6;
                    }

                    if (rem < 1200)
                        s = (s * rem) / 1200;

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

// ---------- Level, Smart Ground Enemy & Checkpoint Construction ----------
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

static void addEnemy(int kind, int col, int row, int minC, int maxC) {
    if (enemyCount >= MAX_ENEMIES)
        return;

    Enemy &e = enemies[enemyCount++];
    e.kind = kind;
    e.x = (float)((baseCol + col) * TILE + 8);
    e.y = (float)(row * TILE - 44);
    e.baseY = e.y;
    e.minX = (float)((baseCol + minC) * TILE + 4);
    e.maxX = (float)((baseCol + maxC) * TILE + 12);
    e.dir = 1;
    e.vx = (kind == E_WALKER) ? 2.7f : ((kind == E_FLYER) ? 2.9f : 2.2f);
    e.vy = 0.0f;
    e.maxHp = 1;
    e.hp = 1;
    e.hurt = 0;
    e.timer = (col * 17) & 63;
    e.active = true;
}

static void addCheckpoint(int col, int row) {
    if (checkCount >= MAX_CHECKPOINTS)
        return;

    checkCols[checkCount] = baseCol + col;
    checkRows[checkCount] = row;
    checkActive[checkCount] = false;
    checkCount++;
}

static void initAmbientParticles() {
    for (int i = 0; i < MAX_AMB; i++) {
        AmbPart &a = ambParts[i];
        a.x = (float)((i * 173 + 47) % FRAME_WIDTH);
        a.y = (float)(110 + ((i * 113 + 31) % (FRAME_HEIGHT - 260)));
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

    int maxC = levelCols - 1;
    for (int r = 2; r < ROWS; r++) {
        for (int c = 1; c < maxC; c++) {
            if (level[r][c] != '#')
                continue;
            if (level[r - 1][c] != '.')
                continue;

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

            for (int sub = 0; sub < 2; sub++) {
                if (plantCount >= MAX_PLANTS)
                    break;

                int rm = c / ROOM_COLS;
                int lc = c % ROOM_COLS;

                Plant &p = plants[plantCount++];
                p.room = (short)rm;
                p.x = (short)(lc * TILE + 15 + sub * 28 + ((c * 5 + r * 3 + sub * 7) % 7));
                p.y = (short)(r * TILE);
                p.angle = 0.0f;
                p.vel   = 0.0f;
                p.height = (unsigned char)(11 + ((c * 3 + r * 7 + sub * 5) % 6));
                p.style  = (unsigned char)((c + r + sub) % 3);
                p.seed   = (unsigned char)((c * 5 + r * 9 + sub * 11) & 15);
                p.drawn[0] = false;
                p.drawn[1] = false;
                p.drawnBend[0] = 99;
                p.drawnBend[1] = 99;
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

    enemyCount = 0;
    checkCount = 0;
}

static void buildForestStage() {
    baseCol = 0;
    levelCols = COLS;
    levelPixelWidth = WORLD_WIDTH;

    baseCol = 0 * ROOM_COLS;
    plat(15, 7, 13);
    addEnemy(E_WALKER, 10, 17, 6, 15);
    addEnemy(E_WALKER, 10, 15, 7, 13);
    plat(14, 16, 22);
    addEnemy(E_JUMPER, 19, 14, 16, 22);
    plat(15, 25, 30);
    addEnemy(E_WALKER, 27, 17, 24, 30);

    baseCol = 1 * ROOM_COLS;
    spk(16, 11, 13);
    plat(14, 15, 21);
    addEnemy(E_WALKER, 18, 17, 14, 23);
    addEnemy(E_FLYER, 18, 10, 15, 22);
    plat(13, 24, 30);
    addEnemy(E_WALKER, 27, 13, 24, 30);
    addCheckpoint(26, 13);

    baseCol = 2 * ROOM_COLS;
    wall(9, 8, 14);
    wall(13, 8, 16);
    plat(8, 13, 20);
    addEnemy(E_WALKER, 16, 8, 13, 20);
    addEnemy(E_WALKER, 24, 17, 21, 29);
    plat(13, 23, 30);
    addEnemy(E_JUMPER, 26, 13, 23, 30);

    baseCol = 3 * ROOM_COLS;
    plat(14, 4, 10);
    sawAt(15, 13);
    plat(14, 16, 22);
    addEnemy(E_WALKER, 19, 17, 15, 24);
    addEnemy(E_FLYER, 19, 10, 16, 23);
    plat(12, 25, 30);
    addEnemy(E_TURRET, 29, 12, 25, 30);
    addCheckpoint(26, 12);

    baseCol = 4 * ROOM_COLS;
    plat(15, 4, 10);
    plat(13, 13, 19);
    addEnemy(E_WALKER, 16, 13, 13, 19);
    addEnemy(E_WALKER, 15, 17, 11, 21);
    plat(11, 22, 30);
    addEnemy(E_JUMPER, 25, 11, 22, 30);

    baseCol = 5 * ROOM_COLS;
    plat(14, 4, 10);
    plat(12, 13, 19);
    addEnemy(E_FLYER, 16, 8, 13, 20);
    addEnemy(E_WALKER, 16, 17, 12, 21);
    plat(10, 22, 30);
    put(9, 27, 'G');
    baseCol = 0;
}

static void buildCountryStage() {
    baseCol = 0;
    levelCols = COLS;
    levelPixelWidth = WORLD_WIDTH;

    baseCol = 0 * ROOM_COLS;
    plat(15, 7, 13);
    addEnemy(E_WALKER, 10, 15, 7, 13);
    addEnemy(E_WALKER, 18, 17, 14, 22);
    spk(16, 15, 17);
    plat(14, 20, 26);
    addEnemy(E_JUMPER, 23, 14, 20, 26);
    plat(15, 28, 30);

    baseCol = 1 * ROOM_COLS;
    plat(14, 4, 10);
    plat(12, 13, 19);
    addEnemy(E_FLYER, 16, 8, 13, 20);
    plat(14, 22, 30);
    addEnemy(E_WALKER, 25, 14, 22, 30);
    addCheckpoint(24, 14);

    baseCol = 2 * ROOM_COLS;
    wall(9, 8, 14);
    wall(13, 8, 16);
    plat(8, 13, 20);
    addEnemy(E_TURRET, 19, 8, 13, 20);
    addEnemy(E_WALKER, 24, 17, 21, 29);
    plat(13, 23, 30);
    addEnemy(E_JUMPER, 26, 13, 23, 30);

    baseCol = 3 * ROOM_COLS;
    plat(14, 4, 10);
    sawAt(15, 13);
    plat(14, 16, 22);
    addEnemy(E_WALKER, 19, 14, 16, 22);
    plat(12, 25, 30);
    addCheckpoint(26, 12);

    baseCol = 4 * ROOM_COLS;
    plat(15, 4, 10);
    plat(12, 13, 19);
    addEnemy(E_FLYER, 16, 8, 13, 20);
    addEnemy(E_WALKER, 16, 17, 12, 21);
    plat(10, 22, 30);
    addEnemy(E_JUMPER, 25, 10, 22, 30);

    baseCol = 5 * ROOM_COLS;
    plat(14, 4, 10);
    plat(12, 13, 19);
    addEnemy(E_WALKER, 16, 12, 13, 19);
    plat(10, 22, 30);
    put(9, 27, 'G');
    baseCol = 0;
}

static void buildDesertStage() {
    baseCol = 0;
    levelCols = COLS;
    levelPixelWidth = WORLD_WIDTH;

    baseCol = 0 * ROOM_COLS;
    wall(9, 8, 14);
    wall(13, 8, 16);
    plat(8, 13, 20);
    addEnemy(E_WALKER, 16, 8, 13, 20);
    addEnemy(E_WALKER, 18, 17, 15, 22);
    plat(13, 23, 30);
    addEnemy(E_JUMPER, 26, 13, 23, 30);

    baseCol = 1 * ROOM_COLS;
    plat(14, 4, 10);
    plat(11, 13, 19);
    addEnemy(E_TURRET, 18, 11, 13, 19);
    plat(14, 22, 30);
    addEnemy(E_FLYER, 25, 10, 22, 30);
    addCheckpoint(24, 14);

    baseCol = 2 * ROOM_COLS;
    wall(8, 8, 14);
    wall(12, 8, 16);
    plat(8, 12, 18);
    plat(13, 21, 30);
    addEnemy(E_WALKER, 25, 13, 21, 30);

    baseCol = 3 * ROOM_COLS;
    plat(14, 4, 10);
    sawAt(15, 13);
    plat(13, 16, 22);
    addEnemy(E_JUMPER, 19, 13, 16, 22);
    addEnemy(E_WALKER, 18, 17, 14, 24);
    plat(11, 25, 30);
    addEnemy(E_TURRET, 29, 11, 25, 30);
    addCheckpoint(26, 11);

    baseCol = 4 * ROOM_COLS;
    plat(15, 4, 10);
    wall(12, 7, 13);
    wall(16, 7, 15);
    plat(7, 16, 22);
    addEnemy(E_FLYER, 19, 4, 16, 23);
    plat(12, 25, 30);

    baseCol = 5 * ROOM_COLS;
    plat(14, 4, 10);
    plat(12, 13, 19);
    addEnemy(E_WALKER, 16, 12, 13, 19);
    plat(10, 22, 30);
    put(9, 27, 'G');
    baseCol = 0;
}

static void buildSnowStage() {
    baseCol = 0;
    levelCols = COLS;
    levelPixelWidth = WORLD_WIDTH;

    baseCol = 0 * ROOM_COLS;
    plat(14, 7, 13);
    sawAt(15, 15);
    plat(14, 18, 24);
    addEnemy(E_WALKER, 21, 14, 18, 24);
    addEnemy(E_WALKER, 10, 17, 6, 14);
    plat(12, 26, 30);

    baseCol = 1 * ROOM_COLS;
    plat(14, 4, 10);
    wall(12, 7, 13);
    wall(16, 7, 15);
    plat(7, 16, 22);
    addEnemy(E_JUMPER, 19, 7, 16, 22);
    plat(12, 25, 30);
    addCheckpoint(26, 12);

    baseCol = 2 * ROOM_COLS;
    plat(14, 4, 10);
    sawAt(15, 12);
    plat(13, 15, 21);
    addEnemy(E_TURRET, 20, 13, 15, 21);
    plat(14, 24, 30);

    baseCol = 3 * ROOM_COLS;
    plat(14, 4, 10);
    plat(12, 13, 19);
    addEnemy(E_FLYER, 16, 8, 13, 20);
    addEnemy(E_WALKER, 16, 17, 12, 21);
    plat(11, 22, 30);
    addCheckpoint(23, 11);

    baseCol = 4 * ROOM_COLS;
    plat(14, 4, 10);
    wall(12, 7, 13);
    wall(16, 7, 15);
    plat(7, 16, 22);
    plat(11, 25, 30);
    addEnemy(E_WALKER, 27, 11, 25, 30);

    baseCol = 5 * ROOM_COLS;
    plat(14, 4, 10);
    plat(12, 13, 19);
    addEnemy(E_JUMPER, 16, 12, 13, 19);
    plat(10, 22, 30);
    put(9, 27, 'G');
    baseCol = 0;
}

static void buildBossArena(int tier) {
    baseCol = 0;
    levelCols = ROOM_COLS;
    levelPixelWidth = FRAME_WIDTH;

    for (int r = 0; r < ROWS; r++)
        level[r][ROOM_COLS - 1] = '#';

    wall(3, 7, 14);
    wall(28, 7, 14);

    plat(12, 5, 9);
    plat(7, 13, 18);
    plat(12, 22, 26);

    bossActive = true;
    bossW = 104;
    bossH = 104;
    bossX = (float)(21 * TILE);
    bossY = (float)((ROWS - 1) * TILE - bossH);
    bossVX = (tier >= 2) ? -5.0f : -3.8f;
    bossVY = 0.0f;
    bossMaxHP = (tier >= 2) ? 10 : 6;
    bossHP = bossMaxHP;
    bossHurt = 0;
    bossState = 0;
    bossTimer = 0;
    bossSwitchCooldown = 0;
    bossBeamTimer = 0;

    for (int i = 0; i < MAX_BOSS_SHOTS; i++)
        bossShots[i].active = false;
}

static void buildExtraStage(int idx) {
    baseCol = 0;
    levelCols = COLS;
    levelPixelWidth = WORLD_WIDTH;

    for (int rm = 0; rm < TOTAL_ROOMS; rm++) {
        baseCol = rm * ROOM_COLS;
        plat(14, 5, 11);
        plat(12, 14, 20);
        plat(14, 23, 29);

        if (rm == 1 || rm == 3) {
            wall(9, 8, 13);
            wall(13, 8, 14);
            addCheckpoint(25, 14);
        }

        if (rm % 2 == 0) {
            addEnemy(E_WALKER, 8, 14, 5, 11);
            addEnemy(E_WALKER, 14, 17, 11, 21);
            addEnemy(E_JUMPER, 17, 12, 14, 20);
        } else {
            addEnemy(E_WALKER, 9, 17, 5, 14);
            addEnemy(E_FLYER, 17, 8, 14, 21);
            addEnemy(E_TURRET, 28, 14, 23, 29);
        }
    }

    baseCol = 5 * ROOM_COLS;
    plat(10, 22, 30);
    put(9, 27, 'G');
    baseCol = 0;
    (void)idx;
}

static void buildLevel() {
    clearLevelGrid();
    bossActive = false;

    checkpointX = (float)(TILE + 16);
    checkpointY = (float)((ROWS - 1) * TILE - PH);

    if (isBossStage(currentLevel)) {
        int tier = (currentLevel + 1) / 5;
        currentBiome = (tier & 1) ? 4 : 5;
        buildBossArena(tier);
        populateLevelPlants();
        initAmbientParticles();
        return;
    }

    int idx = currentLevel % 12;
    if (idx == 0) {
        currentBiome = 0;
        buildForestStage();
    } else if (idx == 1) {
        currentBiome = 1;
        buildCountryStage();
    } else if (idx == 2) {
        currentBiome = 2;
        buildDesertStage();
    } else if (idx == 3) {
        currentBiome = 3;
        buildSnowStage();
    } else {
        if (idx == 5)      currentBiome = 5;
        else if (idx == 6) currentBiome = 3;
        else if (idx == 7) currentBiome = 1;
        else if (idx == 8) currentBiome = 0;
        else if (idx == 10)currentBiome = 2;
        else               currentBiome = 4;
        buildExtraStage(idx);
    }

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

// ---------- Blood Stains (Fly, Land on Ground, Fade & Vanish after 1.5 Minutes) ----------
static void addSplat(int wx, int wy, int w, int h) {
    if (wx < 0 || wy < 0 || wx >= levelPixelWidth)
        return;

    int idx = -1;
    if (splatCount < MAX_SPLATS) {
        idx = splatCount++;
    } else {
        for (int i = 0; i < MAX_SPLATS; i++) {
            if (splats[i].stage >= 4 && !splats[i].drawn[0] && !splats[i].drawn[1]) {
                idx = i;
                break;
            }
        }
        if (idx < 0)
            return;
    }

    int rm = wx / FRAME_WIDTH;
    if (rm > LAST_ROOM) rm = LAST_ROOM;

    Splat &s = splats[idx];
    s.room = (short)rm;
    s.x = (short)(wx - rm * FRAME_WIDTH - w / 2);
    s.y = (short)(wy - h / 2);
    s.w = (short)w;
    s.h = (short)h;
    s.bornTick = tick;
    s.stage = 0;
    s.drawn[0] = false;
    s.drawn[1] = false;
    s.drawnStage[0] = 99;
    s.drawnStage[1] = 99;
}

static void updateSplatsAging() {
    for (int i = 0; i < splatCount; i++) {
        Splat &s = splats[i];
        if (s.stage >= 4)
            continue;

        int age = tick - s.bornTick;
        unsigned char ns = 0;
        if (age >= SPLAT_MAX_AGE)          ns = 4;
        else if (age >= SPLAT_MAX_AGE - 300) ns = 3;
        else if (age >= SPLAT_MAX_AGE - 600) ns = 2;
        else if (age >= SPLAT_MAX_AGE - 900) ns = 1;

        s.stage = ns;
    }
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

            p.vy += 0.62f;

            if (p.vy > 16.0f)
                p.vy = 16.0f;

            p.x += p.vx;
            p.y += p.vy;

            if (solidAt((int)p.x, (int)p.y)) {
                int s = p.size;
                if (solidAt((int)ox, (int)p.y)) {
                    addSplat((int)ox, (int)oy, s + 6 + rnd(8), s / 2 + 3);
                } else {
                    addSplat((int)p.x, (int)oy, s + 6 + rnd(8), s / 2 + 3);
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

        int wIdx = ((tick / 12) + (p.x / 60) + p.seed) & 15;
        float wind = (float)SN[wIdx] / 256.0f * 1.8f;

        if (deadTimer == 0) {
            float dx = pcx - (float)p.x;
            float dy = pby - (float)p.y;
            float adx = dx < 0 ? -dx : dx;

            if (adx < 46.0f && dy > -22.0f && dy < 32.0f) {
                float prox = 1.0f - (adx / 46.0f);
                p.vel += vx * 0.40f * prox;

                float push = (dx < 0.0f ? 7.0f : -7.0f) * prox;
                wind += push;

                if (landedOrJumped)
                    p.vel += (dx < 0.0f ? 5.0f : -5.0f) * prox;
            }
        }

        float force = (wind - p.angle) * 0.18f;
        p.vel = (p.vel + force) * 0.78f;
        p.angle += p.vel;

        if (p.angle > 12.0f) p.angle = 12.0f;
        if (p.angle < -12.0f) p.angle = -12.0f;
    }

    for (int i = 0; i < MAX_AMB; i++) {
        AmbPart &a = ambParts[i];
        int swayIdx = ((tick / 8) + a.seed * 3) & 15;
        float sway = (float)CS[swayIdx] / 256.0f * 0.4f;

        a.x += a.vx + sway;
        a.y += a.vy;

        if (a.x > (float)(FRAME_WIDTH - 24)) a.x = 24.0f;
        if (a.x < 24.0f) a.x = (float)(FRAME_WIDTH - 28);

        if (a.y > (float)(FRAME_HEIGHT - 130)) a.y = 110.0f;
        if (a.y < 105.0f) a.y = (float)(FRAME_HEIGHT - 140);
    }
}

// ---------- Player & Stomp-Only Enemy Combat ----------
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

    if (bossActive && bossHP > 0) {
        bossX = (float)(21 * TILE);
        bossY = (float)((ROWS - 1) * TILE - bossH);
        for (int i = 0; i < MAX_BOSS_SHOTS; i++)
            bossShots[i].active = false;
    }
}

static void die() {
    deaths++;
    hudNeed[0] = true;
    hudNeed[1] = true;
    snd(6);

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

static void damageBoss(int dmg) {
    if (bossHurt > 0 || bossHP <= 0)
        return;

    bossHP -= dmg;
    if (bossHP < 0) bossHP = 0;
    bossHurt = 24;
    hudNeed[0] = true;
    hudNeed[1] = true;
    snd(7);

    float bcx = bossX + bossW * 0.5f;
    float bcy = bossY + bossH * 0.5f;

    for (int i = 0; i < 22; i++) {
        float pvx = (float)(rnd(200) - 100) / 10.0f;
        float pvy = -((float)(40 + rnd(100))) / 10.0f;
        spawnPart(bcx, bcy, pvx, pvy, 90, 6 + rnd(5), 1);
    }

    if (bossHP <= 0) {
        for (int i = 0; i < MAX_BOSS_SHOTS; i++)
            bossShots[i].active = false;

        level[16][15] = 'G';
        needFull[0] = true;
        needFull[1] = true;
    }
}

static void spawnBossShot(float sx, float sy, float svx, float svy) {
    for (int i = 0; i < MAX_BOSS_SHOTS; i++) {
        if (!bossShots[i].active) {
            bossShots[i].x = sx;
            bossShots[i].y = sy;
            bossShots[i].vx = svx;
            bossShots[i].vy = svy;
            bossShots[i].active = true;
            return;
        }
    }
}

static void killEnemy(Enemy &e) {
    e.active = false;
    snd(7);

    float ecx = e.x + 22.0f;
    float ecy = e.y + 22.0f;
    // Flying blood droplets that arc through the air and settle on the ground
    for (int i = 0; i < 24; i++) {
        float pvx = (float)(rnd(180) - 90) / 10.0f;
        float pvy = -((float)(45 + rnd(95))) / 10.0f;
        spawnPart(ecx, ecy, pvx, pvy, 110, 6 + rnd(4), 1);
    }
}

static void updateEnemies() {
    int rmX0 = roomIdx * FRAME_WIDTH - 240;
    int rmX1 = roomIdx * FRAME_WIDTH + FRAME_WIDTH + 240;

    for (int i = 0; i < enemyCount; i++) {
        Enemy &e = enemies[i];
        if (!e.active)
            continue;
        if ((int)e.x < rmX0 || (int)e.x > rmX1)
            continue;

        if (e.hurt > 0)
            e.hurt--;
        e.timer++;

        if (e.kind == E_WALKER) {
            float distX = px - e.x;
            float adx = distX < 0.0f ? -distX : distX;
            float ady = py - e.y;
            if (ady < 0.0f) ady = -ady;

            if (adx < 340.0f && ady < 140.0f) {
                e.dir = (distX >= 0.0f) ? 1 : -1;
            }

            float spd = (adx < 340.0f && ady < 140.0f) ? (e.vx * 1.25f) : e.vx;
            float nextX = e.x + spd * (float)e.dir;

            if (nextX <= e.minX) {
                nextX = e.minX;
                e.dir = 1;
            } else if (nextX >= e.maxX) {
                nextX = e.maxX;
                e.dir = -1;
            }

            int probeX = (e.dir > 0) ? ((int)nextX + 46) : ((int)nextX - 4);
            if (solidAt(probeX, (int)e.y + 20) && e.vy == 0.0f) {
                e.vy = -11.5f;
            } else if (solidAt(probeX, (int)e.y + 20)) {
                e.dir = -e.dir;
            }

            if (!rectSolid((int)nextX, (int)e.y, 44, 44))
                e.x = nextX;
            else
                e.dir = -e.dir;

            e.vy += 0.65f;
            e.y += e.vy;
            if (rectSolid((int)e.x, (int)e.y, 44, 44)) {
                if (e.vy > 0.0f) {
                    e.y = (float)((((int)e.y + 44 - 1) / TILE) * TILE - 44);
                }
                e.vy = 0.0f;
            }
        } else if (e.kind == E_FLYER) {
            float distX = px - e.x;
            if (iabs((int)distX) < 360)
                e.dir = (distX >= 0.0f) ? 1 : -1;
            e.x += e.vx * (float)e.dir;
            if (e.x <= e.minX) { e.x = e.minX; e.dir = 1; }
            if (e.x >= e.maxX) { e.x = e.maxX; e.dir = -1; }
            int wave = SN[(e.timer / 4) & 15];
            e.y = e.baseY + (wave * 28) / 256;
        } else if (e.kind == E_JUMPER) {
            float distX = px - e.x;
            if (iabs((int)distX) < 320)
                e.dir = (distX >= 0.0f) ? 1 : -1;
            e.x += e.vx * (float)e.dir;
            if (e.x <= e.minX) { e.x = e.minX; e.dir = 1; }
            if (e.x >= e.maxX) { e.x = e.maxX; e.dir = -1; }
            if (e.timer % 50 == 0 && e.y >= e.baseY - 2.0f)
                e.vy = -12.5f;
            e.vy += 0.65f;
            e.y += e.vy;
            if (e.y >= e.baseY) {
                e.y = e.baseY;
                e.vy = 0.0f;
            }
        } else if (e.kind == E_TURRET) {
            e.dir = (px < e.x) ? -1 : 1;
            if (e.timer % 90 == 0 && iabs((int)(px - e.x)) < 680) {
                spawnBossShot(e.x + 22.0f, e.y + 18.0f, (float)e.dir * 6.8f, 0.0f);
            }
        }

        // Player vs Enemy: Stomp from above defeats enemy; touching sides/bottom hurts player
        if (deadTimer == 0 &&
            rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8,
                        (int)e.x + 4, (int)e.y + 4, 36, 36)) {
            if (vy > 0.0f && (py + PH) < (e.y + 28.0f)) {
                py = e.y - PH;
                vy = -15.5f;
                squashX = 0.72f;
                squashY = 1.28f;
                killEnemy(e);
            } else {
                die();
                return;
            }
        }
    }
}

static void updateProjectiles() {
    for (int i = 0; i < MAX_BOSS_SHOTS; i++) {
        BossShot &s = bossShots[i];
        if (!s.active)
            continue;

        s.x += s.vx;
        s.y += s.vy;

        if (s.x < (float)TILE || s.x > (float)(levelPixelWidth - TILE) ||
            s.y < (float)TILE || s.y > (float)(FRAME_HEIGHT - TILE) ||
            solidAt((int)s.x, (int)s.y)) {
            s.active = false;
            continue;
        }

        if (deadTimer == 0 && rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8,
                                          (int)s.x - 14, (int)s.y - 14, 28, 28)) {
            s.active = false;
            die();
            return;
        }
    }
}

static void updateBoss() {
    if (!bossActive || bossHP <= 0)
        return;

    if (bossHurt > 0)
        bossHurt--;
    if (bossSwitchCooldown > 0)
        bossSwitchCooldown--;
    if (bossBeamTimer > 0)
        bossBeamTimer--;

    bossTimer++;
    int tier = (currentLevel + 1) / 5;
    float speedMul = (tier >= 2) ? 1.3f : 1.0f;
    float floorY = (float)((ROWS - 1) * TILE - bossH);

    bossX += bossVX * speedMul;

    if (bossX < (float)(5 * TILE)) {
        bossX = (float)(5 * TILE);
        bossVX = 4.2f;
    } else if (bossX > (float)(24 * TILE)) {
        bossX = (float)(24 * TILE);
        bossVX = -4.2f;
    }

    if (bossTimer % 105 == 50 && bossY >= floorY - 2.0f) {
        bossVY = -14.5f;
    }

    bossVY += 0.65f;
    bossY += bossVY;
    if (bossY >= floorY) {
        bossY = floorY;
        bossVY = 0.0f;
    }

    int fireRate = (tier >= 2) ? 60 : 80;
    if (bossTimer % fireRate == 0) {
        float dir = (px < bossX) ? -1.0f : 1.0f;
        spawnBossShot(bossX + bossW * 0.5f, bossY + bossH - 28.0f, dir * 7.0f, 0.0f);
        if (tier >= 2) {
            spawnBossShot(bossX + bossW * 0.5f, bossY + 24.0f, dir * 6.0f, -2.5f);
        }
    }

    if (deadTimer > 0)
        return;

    int swX = 15 * TILE;
    int swY = 6 * TILE + 20;
    if (bossSwitchCooldown == 0 &&
        rectOverlap((int)px, (int)py, PW, PH, swX, swY, 120, 40)) {
        bossSwitchCooldown = 130;
        bossBeamTimer = 25;
        vy = -12.0f;
        damageBoss(2);
        return;
    }

    if (rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8,
                    (int)bossX + 8, (int)bossY + 8, bossW - 16, bossH - 16)) {
        if (vy > 0.0f && (py + PH) < (bossY + 46.0f)) {
            py = bossY - PH;
            vy = -16.5f;
            squashX = 0.70f;
            squashY = 1.32f;
            damageBoss(2);
        } else if (bossHurt == 0) {
            die();
        }
    }
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
    updateSplatsAging();

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

    float maxSpeed = (run ? 8.2f : 5.8f) * scale;

    if (dir != 0)
        facing = dir;

    if (wallLock > 0) {
        wallLock--;
    } else if (dir != 0) {
        float acc = onGround ? 1.25f : 0.72f;

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
            vy = -17.2f;
            coyote = 0;

            squashX = 0.72f;
            squashY = 1.30f;

            spawnDust(px + PW / 2.0f, py + PH, 6, 3.0f);
            didJumpOrLand = true;
            snd(4);
        } else if (wallL || wallR) {
            vy = -16.2f;
            vx = wallL ? 8.0f : -8.0f;

            wallLock = 9;
            facing = wallL ? 1 : -1;

            squashX = 0.72f;
            squashY = 1.30f;

            spawnDust(wallL ? px : px + PW, py + PH / 2.0f, 4, 2.0f);
            didJumpOrLand = true;
            snd(5);
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

    for (int i = 0; i < checkCount; i++) {
        if (!checkActive[i]) {
            int cpx = checkCols[i] * TILE;
            if ((int)px + PW >= cpx) {
                checkActive[i] = true;
                checkpointX = (float)cpx;
                checkpointY = (float)(checkRows[i] * TILE - PH);
                snd(1);
                spawnDust((float)(cpx + 20), (float)(checkRows[i] * TILE - 40), 10, 3.5f);
            }
        }
    }

    updatePlantsAndAmbient(didJumpOrLand);
    updateEnemies();
    updateProjectiles();

    if (bossActive)
        updateBoss();

    if (deadTimer > 0)
        return;

    if (touchingHazard()) {
        die();
        return;
    }

    if (touchingGoal()) {
        winTimer = 150;
        needWin[0] = true;
        needWin[1] = true;
        snd(8);
    }

    int nr = ((int)px + PW / 2) / FRAME_WIDTH;
    if (nr < 0) nr = 0;
    if (nr > LAST_ROOM) nr = LAST_ROOM;
    roomIdx = nr;
}

// ---------- High-Detail Graphics & Shading Engine ----------
static void drawSpike(int cx, int r) {
    int center = cx * TILE + TILE / 2;
    int top = r * TILE + 18;

    Color hiCol   = mk(245, 248, 255);
    Color midCol  = mk(195, 200, 215);
    Color shdCol  = mk(125, 132, 152);

    if (currentBiome == 2) {
        hiCol  = mk(255, 232, 185);
        midCol = mk(224, 184, 128);
        shdCol = mk(162, 124, 78);
    } else if (currentBiome == 3) {
        hiCol  = mk(240, 252, 255);
        midCol = mk(175, 224, 250);
        shdCol = mk(105, 165, 205);
    } else if (currentBiome == 4) {
        hiCol  = mk(255, 195, 115);
        midCol = mk(240, 112, 52);
        shdCol = mk(165, 52, 26);
    }

    for (int j = 0; j < 42; j++) {
        int half = (j * 26) / 41;
        int py0 = top + j;
        if (half > 0) {
            fillRect(center - half, py0, half, 1, hiCol);
            fillRect(center, py0, half + 1, 1, shdCol);
        }
        fillRect(center - 1, py0, 3, 1, midCol);
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
                scene->DrawPixel(cx + dx, cy + dy, mk(45, 45, 52));
            } else if (d2 <= 20 * 20) {
                Color rim = (dx + dy < 0) ? mk(195, 200, 215) : mk(135, 140, 155);
                scene->DrawPixel(cx + dx, cy + dy, rim);
            } else if (
                iabs(u) < 6 ||
                iabs(v) < 6 ||
                iabs(u - v) < 8 ||
                iabs(u + v) < 8
            ) {
                scene->DrawPixel(cx + dx, cy + dy, mk(235, 238, 248));
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

// High-detail shaded tile rendering with depth highlights, bricks/strata & shadows
static void drawTileCell(int r, int cx, char ch, bool hi) {
    int x = cx * TILE;
    int y = r * TILE;

    if (ch == '#') {
        Color mainC = mk(64, 52, 92);
        Color litC  = mk(84, 70, 118);
        Color topC  = mk(165, 122, 205);
        Color topHi = mk(205, 168, 238);
        Color botC  = mk(36, 28, 56);

        if (currentBiome == 0) {
            mainC = mk(124, 76, 44);
            litC  = mk(148, 94, 56);
            topC  = mk(76, 208, 68);
            topHi = mk(135, 240, 115);
            botC  = mk(74, 44, 24);
        } else if (currentBiome == 1) {
            mainC = mk(122, 74, 44);
            litC  = mk(146, 92, 56);
            topC  = mk(236, 184, 56);
            topHi = mk(255, 224, 115);
            botC  = mk(76, 44, 24);
        } else if (currentBiome == 2) {
            mainC = mk(184, 124, 66);
            litC  = mk(208, 146, 84);
            topC  = mk(248, 208, 98);
            topHi = mk(255, 236, 155);
            botC  = mk(122, 76, 38);
        } else if (currentBiome == 3) {
            mainC = mk(72, 98, 134);
            litC  = mk(94, 124, 165);
            topC  = mk(232, 248, 255);
            topHi = mk(255, 255, 255);
            botC  = mk(42, 58, 86);
        } else if (currentBiome == 4) {
            mainC = mk(62, 36, 44);
            litC  = mk(86, 50, 60);
            topC  = mk(245, 98, 36);
            topHi = mk(255, 178, 85);
            botC  = mk(34, 18, 24);
        }

        fillRect(x, y, TILE, TILE, mainC);
        // Shaded stone/earth inner detail block
        fillRect(x + 6, y + 14, TILE - 12, TILE - 22, litC);
        fillRect(x + 10, y + 18, 14, 8, mainC);
        fillRect(x + 32, y + 32, 16, 10, botC);

        if (hi) {
            fillRect(x, y, TILE, 9, topC);
            fillRect(x, y, TILE, 3, topHi);
            fillRect(x + 4, y + 9, 10, 4, topC);
            fillRect(x + 22, y + 9, 12, 5, topC);
            fillRect(x + 42, y + 9, 12, 4, topC);
        }

        fillRect(x, y + TILE - 6, TILE, 6, botC);
    }

    else if (ch == '^') {
        drawSpike(cx, r);
    }

    else if (ch == 'G') {
        int gx = x + 8;
        int gy = y + 16;

        fillRect(gx - 2, gy - 2, PW + 4, PH + 4, mk(255, 225, 130));
        fillRect(gx, gy, PW, PH, mk(255, 140, 170));
        fillRect(gx, gy + 3, PW, 7, mk(240, 230, 200));

        drawFace(gx, gy, -1, mk(255, 255, 255));
    }
}

static void drawPlantTuft(int bx, int by, int h, int bend, int biome, int style) {
    if (biome == 0) {
        Color dark = mk(34, 142, 48);
        Color mid  = mk(78, 218, 76);
        Color bud  = (style == 0) ? mk(255, 105, 145) : ((style == 1) ? mk(255, 235, 80) : mk(245, 245, 255));
        for (int b = -1; b <= 1; b++) {
            int ox = bx + b * 5;
            int bh = h - (b == 0 ? 0 : 2);
            int sh = bh / 2;
            fillRect(ox - 1, by - sh, 3, sh, dark);
            fillRect(ox - 1 + (bend / 2) + b, by - bh, 3, bh - sh, mid);
            if (b == 0)
                fillRect(ox - 2 + (bend / 2), by - bh - 4, 5, 4, bud);
        }
    } else if (biome == 1) {
        Color stalk = mk(185, 132, 38);
        Color grain = mk(248, 205, 72);
        for (int b = -1; b <= 1; b++) {
            int ox = bx + b * 5;
            int bh = h + 2 - (b == 0 ? 0 : 2);
            int sh = bh / 2;
            fillRect(ox - 1, by - sh, 2, sh, stalk);
            fillRect(ox - 1 + (bend / 2) + b, by - bh, 2, bh - sh, grain);
            fillRect(ox - 2 + (bend / 2) + b, by - bh - 4, 4, 5, mk(255, 232, 115));
        }
    } else if (biome == 2) {
        Color scrub = mk(82, 168, 56);
        Color thorn = mk(255, 125, 145);
        int s1 = bend / 2;
        fillRect(bx - 2, by - h - 2, 4, h + 2, scrub);
        fillRect(bx - 8 + s1, by - (h * 2) / 3, 6, 4, scrub);
        fillRect(bx - 8 + s1, by - h + 1, 3, 6, scrub);
        fillRect(bx + 2 + s1, by - (h * 3) / 4, 6, 4, scrub);
        fillRect(bx + 5 + s1, by - h, 3, 6, scrub);
        if (style == 0)
            fillRect(bx - 2 + s1, by - h - 5, 4, 3, thorn);
    } else if (biome == 3) {
        Color iceDark = mk(105, 175, 220);
        Color iceMid  = mk(185, 230, 255);
        Color snowTip = mk(255, 255, 255);
        for (int b = -1; b <= 1; b++) {
            int ox = bx + b * 5;
            int bh = h - (b == 0 ? 0 : 3);
            fillRect(ox - 1, by - bh / 2, 3, bh / 2, iceDark);
            fillRect(ox - 1 + (bend / 2) + b, by - bh, 3, bh - bh / 2, iceMid);
            fillRect(ox - 2 + (bend / 2) + b, by - bh - 3, 4, 3, snowTip);
        }
    } else if (biome == 4) {
        Color ash   = mk(95, 32, 32);
        Color flame = mk(242, 102, 32);
        Color spark = mk(255, 225, 75);
        for (int b = -1; b <= 1; b++) {
            int ox = bx + b * 5;
            int bh = h - (b == 0 ? 0 : 2);
            fillRect(ox - 1, by - bh / 2, 3, bh / 2, ash);
            fillRect(ox - 1 + (bend / 2) + b * 2, by - bh, 3, bh - bh / 2, flame);
            fillRect(ox - 1 + (bend / 2) + b * 2, by - bh - 2, 3, 3, spark);
        }
    } else {
        Color shDark = mk(78, 52, 128);
        Color shMid  = mk(138, 98, 205);
        Color shTip  = mk(185, 235, 255);
        for (int b = -1; b <= 1; b++) {
            int ox = bx + b * 5;
            int bh = h - (b == 0 ? 0 : 2);
            fillRect(ox - 1, by - bh / 2, 3, bh / 2, shDark);
            fillRect(ox - 1 + (bend / 2) + b, by - bh, 3, bh - bh / 2, shMid);
            if (b == 0)
                fillRect(ox - 1 + (bend / 2), by - bh - 3, 3, 3, shTip);
        }
    }
}

static void drawSplat(const Splat &s) {
    if (s.stage >= 4)
        return;

    int div = s.stage + 1;
    int dw = (s.w * (4 - s.stage)) / 4;
    int dh = (s.h * (4 - s.stage)) / 4;
    if (dw < 2) dw = 2;
    if (dh < 2) dh = 2;

    int ox = s.x + (s.w - dw) / 2;
    int oy = s.y + (s.h - dh) / 2;

    Color c1 = mk(140 / div + 20, 10, 20);
    Color c2 = mk(185 / div + 20, 22 / div, 34 / div);

    fillRect(ox, oy, dw, dh, c1);
    if (s.stage <= 1)
        fillRect(ox + 1, oy + 1, dw / 3 + 1, dh / 3 + 1, c2);
}

static void starPos(int i, int &sx, int &sy) {
    sx = (i * 271 + 91) % FRAME_WIDTH;
    sy = (i * 137 + 31) % 570;
}

static bool starDim(int i, int phase) {
    return ((phase + i) % 4) == 0;
}

static void drawDistantCastle(int cx, int baseY, Color wallCol, Color roofCol, Color flagCol) {
    fillRect(cx - 65, baseY - 120, 130, 120, wallCol);
    fillRect(cx - 105, baseY - 155, 44, 155, wallCol);
    fillRect(cx + 61, baseY - 155, 44, 155, wallCol);
    fillRect(cx - 26, baseY - 175, 52, 175, wallCol);

    for (int j = 0; j < 26; j++) {
        int half = (j * 28) / 25 + 4;
        fillRect(cx - half, baseY - 200 + j, half * 2 + 1, 1, roofCol);
        fillRect(cx - 83 - (j / 2), baseY - 175 + j, j + 10, 1, roofCol);
        fillRect(cx + 83 - (j / 2), baseY - 175 + j, j + 10, 1, roofCol);
    }

    fillRect(cx - 18, baseY - 52, 36, 52, mk(45, 32, 48));
    fillRect(cx - 45, baseY - 95, 16, 22, mk(250, 215, 90));
    fillRect(cx + 29, baseY - 95, 16, 22, mk(250, 215, 90));

    fillRect(cx, baseY - 216, 18, 10, flagCol);
    fillRect(cx - 83, baseY - 190, 14, 8, flagCol);
    fillRect(cx + 83, baseY - 190, 14, 8, flagCol);
}

static void drawDesertTempleAndPalms(int tx, int baseY, Color sandWall, Color trimCol) {
    fillRect(tx - 95, baseY - 110, 190, 110, sandWall);
    fillRect(tx - 65, baseY - 155, 130, 45, sandWall);
    fillRect(tx - 30, baseY - 185, 60, 30, trimCol);
    fillRect(tx - 24, baseY - 56, 48, 56, mk(62, 38, 22));
    fillRect(tx - 80, baseY - 95, 18, 32, mk(195, 45, 35));
    fillRect(tx + 62, baseY - 95, 18, 32, mk(195, 45, 35));

    int px0 = tx - 170;
    fillRect(px0 - 7, baseY - 135, 14, 135, mk(118, 72, 36));
    fillRect(px0 - 34, baseY - 144, 34, 10, mk(58, 168, 52));
    fillRect(px0, baseY - 144, 34, 10, mk(58, 168, 52));
    fillRect(px0 - 26, baseY - 156, 24, 9, mk(85, 198, 68));
    fillRect(px0 + 2, baseY - 156, 24, 9, mk(85, 198, 68));
    fillRect(px0 - 12, baseY - 166, 24, 9, mk(110, 215, 78));
}

static void paintBackground() {
    if (currentBiome == 0) {
        Color sky1  = mk(52, 162, 238);
        Color sky2  = mk(125, 215, 250);
        Color hill1 = mk(58, 155, 92);
        Color hill2 = mk(38, 118, 68);
        Color lush  = mk(28, 88, 52);

        fillRect(0, 0, FRAME_WIDTH, 260, sky1);
        fillRect(0, 260, FRAME_WIDTH, 240, sky2);
        fillRect(0, 500, FRAME_WIDTH, 110, hill1);
        fillRect(0, 610, FRAME_WIDTH, 130, hill2);
        fillRect(0, 740, FRAME_WIDTH, 340, lush);

        fillRect(1540, 72, 136, 136, mk(255, 242, 155));
        fillRect(1556, 88, 104, 104, mk(255, 224, 72));

        for (int i = 0; i < 4; i++) {
            int cx = 150 + i * 380;
            int cy = 115 + (i & 1) * 48;
            fillRect(cx, cy, 165, 36, mk(236, 248, 255));
            fillRect(cx + 28, cy - 20, 105, 24, mk(248, 253, 255));
        }

        drawDistantCastle(1260, 500, mk(218, 212, 224), mk(195, 55, 65), mk(255, 215, 65));

        fillRect(360, 430, 96, 250, mk(72, 178, 245));
        fillRect(376, 430, 64, 250, mk(145, 224, 255));
        fillRect(394, 430, 28, 250, mk(225, 248, 255));
        fillRect(260, 655, 420, 65, mk(62, 165, 235));
        fillRect(310, 670, 310, 28, mk(135, 218, 252));

        for (int i = 0; i < 6; i++) {
            int x = i * 320 + 40;
            fillRect(x + 42, 580, 26, 95, mk(92, 56, 32));
            fillRect(x, 470, 110, 115, lush);
            fillRect(x + 20, 430, 70, 50, mk(45, 132, 74));
        }
        return;
    }

    if (currentBiome == 1) {
        Color sky1 = mk(198, 96, 72);
        Color sky2 = mk(246, 162, 86);
        Color farC = mk(142, 86, 52);
        Color nearC = mk(94, 56, 36);

        fillRect(0, 0, FRAME_WIDTH, 300, sky1);
        fillRect(0, 300, FRAME_WIDTH, 270, sky2);
        fillRect(0, 570, FRAME_WIDTH, 130, farC);
        fillRect(0, 700, FRAME_WIDTH, 380, nearC);

        fillRect(1420, 195, 146, 146, mk(255, 200, 85));
        fillRect(1434, 209, 118, 118, mk(255, 232, 125));

        drawDistantCastle(1180, 570, mk(215, 175, 135), mk(175, 58, 48), mk(255, 225, 90));

        for (int i = 0; i < 4; i++) {
            int x = i * 500;
            fillRect(x + 40, 485, 260, 85, farC);
            fillRect(x + 90, 458, 160, 32, farC);
        }

        fillRect(360, 570, 140, 130, nearC);
        fillRect(385, 540, 90, 35, nearC);
        fillRect(1480, 560, 76, 140, nearC);
        fillRect(1492, 538, 52, 26, nearC);
        return;
    }

    if (currentBiome == 2) {
        Color sky1 = mk(68, 168, 238);
        Color sky2 = mk(185, 228, 248);
        Color farC = mk(218, 165, 86);
        Color nearC = mk(178, 122, 58);

        fillRect(0, 0, FRAME_WIDTH, 270, sky1);
        fillRect(0, 270, FRAME_WIDTH, 250, sky2);
        fillRect(0, 520, FRAME_WIDTH, 140, farC);
        fillRect(0, 660, FRAME_WIDTH, 420, nearC);

        fillRect(1495, 82, 132, 132, mk(255, 242, 165));
        fillRect(1511, 98, 100, 100, mk(255, 225, 95));

        for (int p = 0; p < 2; p++) {
            int px0 = 1120 + p * 360;
            for (int step = 0; step < 9; step++) {
                int w = (step + 1) * 34;
                fillRect(px0 - w / 2, 340 + step * 20, w, 20, mk(226, 174, 92));
            }
        }

        drawDesertTempleAndPalms(520, 560, mk(216, 162, 88), mk(242, 198, 105));

        fillRect(760, 625, 310, 58, mk(62, 175, 235));
        fillRect(800, 638, 220, 24, mk(142, 222, 252));
        return;
    }

    if (currentBiome == 3) {
        Color sky1 = mk(32, 56, 96);
        Color sky2 = mk(62, 100, 148);
        Color farC = mk(95, 135, 178);
        Color nearC = mk(62, 95, 135);

        fillRect(0, 0, FRAME_WIDTH, 300, sky1);
        fillRect(0, 300, FRAME_WIDTH, 270, sky2);
        fillRect(0, 570, FRAME_WIDTH, 130, farC);
        fillRect(0, 700, FRAME_WIDTH, 380, nearC);

        for (int i = 0; i < 6; i++) {
            fillRect(120 + i * 280, 140 + (i & 1) * 18, 220, 18, mk(65, 215, 175));
        }

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
        Color sky1 = mk(42, 14, 24);
        Color sky2 = mk(78, 24, 32);
        Color farC = mk(58, 22, 30);
        Color nearC = mk(38, 15, 22);

        fillRect(0, 0, FRAME_WIDTH, 300, sky1);
        fillRect(0, 300, FRAME_WIDTH, 270, sky2);
        fillRect(0, 570, FRAME_WIDTH, 130, farC);
        fillRect(0, 700, FRAME_WIDTH, 380, nearC);

        fillRect(1500, 115, 100, 100, mk(235, 95, 50));
        fillRect(1520, 130, 65, 70, mk(255, 155, 70));

        for (int i = 0; i < 3; i++) {
            int x = i * 680;
            fillRect(x + 120, 400, 220, 170, farC);
            fillRect(x + 180, 392, 100, 20, mk(245, 95, 35));
            fillRect(x + 220, 412, 18, 158, mk(235, 75, 28));
        }

        for (int i = 0; i < 4; i++) {
            int x = i * 540;
            fillRect(x + 80, 540, 200, 160, nearC);
        }
        return;
    }

    // Biome 5: Classic Night Sky
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
        if (splats[i].drawn[b] && splats[i].stage < 4)
            drawSplat(splats[i]);
    }
}

static void drawPlantsClipped(int b) {
    for (int i = 0; i < plantCount; i++) {
        Plant &p = plants[i];
        if (p.room != bufRoom[b])
            continue;
        if (p.x + 16 < clipX0 || p.x - 16 > clipX1)
            continue;
        if (p.y < clipY0 || p.y - p.height - 6 > clipY1)
            continue;
        drawPlantTuft(p.x, p.y, p.height, (int)p.angle, currentBiome, p.style);
        p.drawn[b] = true;
        p.drawnBend[b] = (signed char)((int)p.angle);
    }
}

static void restoreRect(
    int b, int x, int y, int w, int h, bool withDetails
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

    if (withDetails) {
        drawSplatsClipped(b);
        drawPlantsClipped(b);
    }

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
    if (bossActive) {
        if (bossMaxHP <= 0) return 0;
        return (bossHP * 520) / bossMaxHP;
    }

    int p = (int)((px / (float)levelPixelWidth) * 520.0f);

    if (p < 0) p = 0;
    if (p > 520) p = 520;

    return p;
}

static void drawHUDFull(int b) {
    int hudN = deaths < 30 ? deaths : 30;

    if (useAr() && TXT_AR[T_DEATHS] >= 0) {
        int id = TXT_AR[T_DEATHS];
        drawArCols(id, 70, 22, 0, arW(id), mk(235, 225, 255));
    } else {
        drawText(70, 28, "DEATHS", 3, mk(235, 225, 255));
    }

    for (int i = 0; i < hudN; i++) {
        fillRect(
            70 + i * 20,
            60,
            13, 13,
            mk(205, 25, 35)
        );
    }

    int prog = progressNow();

    if (bossActive) {
        drawLabel(T_BOSS_HP, 960, 10, 30, 3, mk(255, 215, 90));
        fillRect(696, 40, 528, 20, mk(30, 18, 45));
        fillRect(700, 43, 520, 14, mk(65, 25, 35));
        fillRect(700, 43, prog, 14, mk(245, 55, 40));
    } else {
        fillRect(696, 40, 528, 20, mk(30, 20, 50));
        fillRect(700, 43, 520, 14, mk(45, 35, 70));
        fillRect(700, 43, prog, 14, mk(205, 40, 100));
    }

    hudSq[b] = hudN;
    hudProg[b] = prog;
    hudNeed[b] = false;
}

static void drawRoomSplats(int b) {
    for (int i = 0; i < splatCount; i++) {
        if (splats[i].room == bufRoom[b] && splats[i].stage < 4) {
            drawSplat(splats[i]);
            splats[i].drawn[b] = true;
            splats[i].drawnStage[b] = splats[i].stage;
        }
    }
}

static void drawRoomPlants(int b) {
    for (int i = 0; i < plantCount; i++) {
        Plant &p = plants[i];
        if (p.room == bufRoom[b]) {
            int bend = (int)p.angle;
            drawPlantTuft(p.x, p.y, p.height, bend, currentBiome, p.style);
            p.drawn[b] = true;
            p.drawnBend[b] = (signed char)bend;
        } else {
            p.drawn[b] = false;
        }
    }
}

static void drawEnemyChar(const Enemy &e, int sx, int sy) {
    bool flash = (e.hurt & 2) != 0;

    if (e.kind == E_WALKER) {
        Color body = flash ? mk(255, 255, 255) : mk(158, 42, 182);
        Color lit  = mk(198, 86, 224);
        Color trim = mk(82, 16, 102);
        fillRect(sx, sy, 44, 44, trim);
        fillRect(sx + 4, sy + 4, 36, 36, body);
        fillRect(sx + 6, sy + 6, 32, 10, lit);
        fillRect(sx + 10, sy - 6, 8, 6, mk(235, 238, 248));
        fillRect(sx + 26, sy - 6, 8, 6, mk(235, 238, 248));
        drawFace(sx, sy, e.dir, mk(255, 235, 90));
    } else if (e.kind == E_FLYER) {
        Color body = flash ? mk(255, 255, 255) : mk(232, 92, 38);
        int wingY = ((tick / 4) & 1) ? -10 : 8;
        fillRect(sx - 12, sy + 14 + wingY, 14, 10, mk(175, 45, 25));
        fillRect(sx + 42, sy + 14 + wingY, 14, 10, mk(175, 45, 25));
        fillRect(sx + 4, sy + 4, 36, 36, body);
        fillRect(sx + 6, sy + 6, 32, 8, mk(255, 155, 85));
        drawFace(sx, sy, e.dir, mk(255, 245, 120));
    } else if (e.kind == E_JUMPER) {
        Color body = flash ? mk(255, 255, 255) : mk(48, 195, 142);
        fillRect(sx, sy, 44, 44, mk(24, 112, 78));
        fillRect(sx + 4, sy + 4, 36, 36, body);
        fillRect(sx + 6, sy + 6, 32, 8, mk(115, 235, 185));
        fillRect(sx + 12, sy + 42, 20, 6, mk(215, 224, 236));
        drawFace(sx, sy, e.dir, mk(255, 255, 255));
    } else {
        Color body = flash ? mk(255, 255, 255) : mk(102, 114, 136);
        fillRect(sx, sy + 6, 44, 38, mk(48, 54, 68));
        fillRect(sx + 4, sy + 10, 36, 30, body);
        fillRect(sx + 6, sy + 12, 32, 7, mk(152, 166, 192));
        int barrelX = (e.dir < 0) ? (sx - 14) : (sx + 38);
        fillRect(barrelX, sy + 18, 18, 12, mk(235, 75, 45));
        fillRect(sx + 14, sy + 16, 16, 12, mk(255, 215, 70));
    }
}

static void drawBossChar(int bx, int by, int bw, int bh, bool hurt) {
    Color armorDark = hurt ? mk(255, 240, 180) : mk(85, 22, 32);
    Color armorMain = hurt ? mk(255, 255, 255) : mk(175, 35, 45);
    Color armorHi   = mk(220, 68, 82);
    Color coreCol   = (tick & 4) ? mk(255, 215, 65) : mk(255, 110, 30);

    fillRect(bx, by, bw, bh, armorDark);
    fillRect(bx + 8, by + 8, bw - 16, bh - 16, armorMain);
    fillRect(bx + 12, by + 12, bw - 24, 22, armorHi);

    fillRect(bx + 20, by - 8, bw - 40, 10, coreCol);

    fillRect(bx + 18, by + 26, 24, 20, mk(255, 240, 90));
    fillRect(bx + bw - 42, by + 26, 24, 20, mk(255, 240, 90));
    fillRect(bx + 24, by + 32, 12, 12, mk(20, 0, 0));
    fillRect(bx + bw - 36, by + 32, 12, 12, mk(20, 0, 0));

    fillRect(bx + 14, by + 20, 32, 6, armorDark);
    fillRect(bx + bw - 46, by + 20, 32, 6, armorDark);

    fillRect(bx + 22, by + 68, bw - 44, 20, mk(35, 10, 18));
    for (int k = 0; k < 4; k++)
        fillRect(bx + 28 + k * 14, by + 70, 6, 16, coreCol);
}

static void renderGame(int b) {
    resetClip();

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
            splats[i].drawn[b] = false;

        drawRoomSplats(b);
        drawRoomPlants(b);

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
                if (splats[i].drawn[b]) {
                    Splat &s = splats[i];
                    restoreRect(b, s.x - 1, s.y - 1, s.w + 2, s.h + 2, false);
                    splats[i].drawn[b] = false;
                }
            }

            for (int i = 0; i < plantCount; i++) {
                if (plants[i].drawn[b]) {
                    Plant &p = plants[i];
                    restoreRect(b, p.x - 12, p.y - p.height - 5, 24, p.height + 5, false);
                    p.drawn[b] = false;
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
            drawRoomPlants(b);
        }

        for (int i = 0; i < splatCount; i++) {
            Splat &s = splats[i];
            if (s.room != bufRoom[b])
                continue;
            if (s.drawn[b] && (s.stage >= 4 || s.drawnStage[b] != s.stage)) {
                restoreRect(b, s.x - 1, s.y - 1, s.w + 2, s.h + 2, false);
                s.drawn[b] = false;
            }
            if (!s.drawn[b] && s.stage < 4) {
                drawSplat(s);
                s.drawn[b] = true;
                s.drawnStage[b] = s.stage;
            }
        }

        for (int i = 0; i < plantCount; i++) {
            Plant &p = plants[i];
            if (p.room != bufRoom[b])
                continue;
            int bend = (int)p.angle;
            if (p.drawn[b] && p.drawnBend[b] != bend) {
                restoreRect(b, p.x - 12, p.y - p.height - 5, 24, p.height + 5, false);
                p.drawn[b] = false;
            }
            if (!p.drawn[b]) {
                drawPlantTuft(p.x, p.y, p.height, bend, currentBiome, p.style);
                p.drawn[b] = true;
                p.drawnBend[b] = (signed char)bend;
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

        int hudN = deaths < 30 ? deaths : 30;
        int prog = progressNow();

        if (hudNeed[b]) {
            drawHUDFull(b);
        } else {
            for (int i = hudSq[b]; i < hudN; i++) {
                fillRect(
                    70 + i * 20, 60,
                    13, 13,
                    mk(205, 25, 35)
                );
            }

            hudSq[b] = hudN;

            if (prog > hudProg[b]) {
                fillRect(
                    700 + hudProg[b], 43,
                    prog - hudProg[b], 14,
                    bossActive ? mk(245, 55, 40) : mk(205, 40, 100)
                );
            } else if (prog < hudProg[b]) {
                fillRect(
                    700 + prog, 43,
                    hudProg[b] - prog, 14,
                    bossActive ? mk(65, 25, 35) : mk(45, 35, 70)
                );
            }

            hudProg[b] = prog;
        }
    }

    // ----- Moving Objects (Tracked via addDirty for locked 60 FPS!) -----
    resetClip();

    int roomX = roomIdx * FRAME_WIDTH;

    if (currentBiome == 0) {
        int wf = (tick * 4) % 44;
        for (int k = 0; k < 5; k++) {
            int wy = 435 + wf + k * 48;
            if (wy < 660) {
                fillRect(398, wy, 20, 18, mk(245, 252, 255));
                addDirty(b, 396, wy - 2, 24, 22);
            }
        }
    }

    if (currentBiome <= 2) {
        Color birdCol = (currentBiome == 0) ? mk(26, 75, 52) : mk(95, 42, 30);
        int wing = ((tick / 8) & 1) ? -6 : 5;
        for (int i = 0; i < MAX_BIRDS; i++) {
            int bx = (i * 390 + tick * 2) % (FRAME_WIDTH - 80) + 40;
            int by = 125 + (i % 3) * 48 + (SN[((tick / 6) + i * 3) & 15] >> 5);
            fillRect(bx, by, 10, 4, birdCol);
            fillRect(bx - 7, by + wing, 8, 4, birdCol);
            fillRect(bx + 9, by + wing, 8, 4, birdCol);
            addDirty(b, bx - 9, by - 8, 28, 18);
        }
    }

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

    for (int i = 0; i < checkCount; i++) {
        int cRoom = checkCols[i] / ROOM_COLS;
        if (cRoom != roomIdx)
            continue;
        int fx = (checkCols[i] % ROOM_COLS) * TILE + 24;
        int fy = checkRows[i] * TILE - 64;
        fillRect(fx, fy, 6, 64, mk(220, 220, 235));
        Color flagCol = checkActive[i] ? mk(65, 235, 110) : mk(235, 190, 55);
        fillRect(fx + 6, fy + 4, 28, 20, flagCol);
        addDirty(b, fx - 2, fy - 2, 38, 66);
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

    for (int i = 0; i < enemyCount; i++) {
        const Enemy &e = enemies[i];
        if (!e.active)
            continue;
        int sx = (int)e.x - roomX;
        int sy = (int)e.y;
        if (sx < -60 || sx > FRAME_WIDTH + 60)
            continue;
        drawEnemyChar(e, sx, sy);
        addDirty(b, sx - 16, sy - 8, 76, 60);
    }

    if (bossActive && bossHP > 0) {
        int swX = 15 * TILE + 20;
        int swY = 7 * TILE - 18;
        Color swCol = (bossSwitchCooldown == 0) ?
                      ((tick & 8) ? mk(80, 255, 120) : mk(40, 200, 85)) :
                      mk(90, 80, 100);
        fillRect(swX, swY, 80, 18, swCol);
        addDirty(b, swX - 2, swY - 2, 84, 22);

        int bx = (int)bossX;
        int by = (int)bossY;

        if (bossBeamTimer > 0) {
            int beamX = bx + bossW / 2 - 16;
            fillRect(beamX, 7 * TILE, 32, by - 7 * TILE + 20, mk(120, 255, 220));
            addDirty(b, beamX - 4, 7 * TILE, 40, by - 7 * TILE + 24);
        }

        drawBossChar(bx, by, bossW, bossH, (bossHurt & 2) != 0);
        addDirty(b, bx - 6, by - 12, bossW + 12, bossH + 16);
    }

    for (int i = 0; i < MAX_BOSS_SHOTS; i++) {
        BossShot &s = bossShots[i];
        if (!s.active)
            continue;
        int sx = (int)s.x - roomX;
        int sy = (int)s.y;
        if (sx < -30 || sx > FRAME_WIDTH + 30)
            continue;
        fillRect(sx - 14, sy - 14, 28, 28, mk(255, 95, 35));
        fillRect(sx - 8, sy - 8, 16, 16, mk(255, 235, 90));
        addDirty(b, sx - 16, sy - 16, 32, 32);
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
            col = mk(210, 18, 32);
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

        // Shaded Meat Cube with top highlight & depth rim
        fillRect(dx, dy, dw, dh, mk(125, 0, 14));
        fillRect(dx + 3, dy + 3, dw - 6, dh - 6, mk(208, 26, 36));
        fillRect(dx + 5, dy + 5, dw - 10, 7, mk(242, 85, 95));

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

static void drawPadPrompt(int x, int y, bool dirL, bool dirR, bool sqOn, bool xOn, const char *label) {
    fillRect(x, y, 420, 74, mk(22, 16, 38));
    fillRect(x + 3, y + 3, 414, 68, mk(38, 28, 62));

    fillRect(x + 18, y + 16, 42, 42, dirL ? mk(90, 200, 255) : mk(55, 45, 80));
    drawTextC(x + 39, y + 27, "<", 3, mk(255, 255, 255));

    fillRect(x + 68, y + 16, 42, 42, dirR ? mk(90, 200, 255) : mk(55, 45, 80));
    drawTextC(x + 89, y + 27, ">", 3, mk(255, 255, 255));

    fillRect(x + 130, y + 16, 54, 42, sqOn ? mk(235, 65, 145) : mk(55, 45, 80));
    fillRect(x + 147, y + 25, 20, 20, mk(255, 255, 255));
    fillRect(x + 151, y + 29, 12, 12, sqOn ? mk(235, 65, 145) : mk(55, 45, 80));

    fillRect(x + 196, y + 16, 54, 42, xOn ? mk(65, 165, 255) : mk(55, 45, 80));
    drawTextC(x + 223, y + 27, "X", 3, mk(255, 255, 255));

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
        for (int i = 0; i < 16; i++)
            drawPlantTuft(ax + 50 + i * 60, ay + 380, 15, sway, 0, i % 3);

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

    if (art == 10) {
        fillRect(ax, ay, aw, ah, mk(65, 160, 225));
        fillRect(ax + 820, ay + 35, 70, 70, mk(255, 235, 120));

        fillRect(ax, ay + 400, aw, 100, mk(82, 54, 38));
        fillRect(ax, ay + 400, aw, 8, mk(65, 190, 75));
        fillRect(ax + 700, ay + 290, 240, 110, mk(82, 54, 38));
        fillRect(ax + 700, ay + 290, 240, 8, mk(65, 190, 75));

        int cyc = t % 180;
        int cx = ax + 90 + (cyc * 4);
        if (cx > ax + 820) cx = ax + 820;
        int cy = ay + 356;
        int dw = 44, dh = 44;
        bool xOn = false;
        const char *lbl = "WALK";

        if (cyc >= 45 && cyc < 90) {
            int jt = cyc - 45;
            cy = ay + 356 - (jt * (45 - jt)) / 4;
            xOn = (jt < 14);
            lbl = "STOMP!";
        } else if (cyc >= 110 && cyc < 160) {
            int jt = cyc - 110;
            cy = ay + 356 - (jt * (55 - jt)) / 4;
            if (jt > 32 && cy > ay + 246) cy = ay + 246;
            xOn = (jt < 34);
            lbl = "HOLD X";
        } else if (cyc >= 160) {
            cy = ay + 246;
            lbl = "GREAT!";
        }

        if (cyc < 72) {
            Enemy de = {0,0,0,0,0,0,0, E_WALKER, 1,1, 0, t, -1, true};
            drawEnemyChar(de, ax + 480, ay + 356);
        }

        for (int i = 0; i < 14; i++) {
            int gx = ax + 60 + i * 45;
            int bend = (iabs(cx + 22 - gx) < 42 && cy > ay + 320) ? 12 : (SN[(t / 5 + i) & 15] / 32);
            drawPlantTuft(gx, ay + 400, 15, bend, 0, i % 3);
        }

        drawMiniDemoCube(cx, cy + (44 - dh), dw, dh, 1);
        drawPadPrompt(ax + 290, ay + 20, false, cyc < 165, false, xOn, lbl);
        return;
    }

    if (art == 11) {
        fillRect(ax, ay, aw, ah, mk(215, 125, 75));
        fillRect(ax + 820, ay + 120, 90, 90, mk(255, 215, 95));

        fillRect(ax, ay + 400, 420, 100, mk(102, 62, 40));
        fillRect(ax, ay + 400, 420, 8, mk(225, 175, 55));
        fillRect(ax + 660, ay + 400, 340, 100, mk(102, 62, 40));
        fillRect(ax + 660, ay + 400, 340, 8, mk(225, 175, 55));

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
        }

        for (int i = 0; i < 8; i++) {
            int wx = ax + 50 + i * 45;
            int bend = (cx > wx - 25 && cx < wx + 55) ? 12 : (SN[(t / 5 + i) & 15] / 28);
            drawPlantTuft(wx, ay + 400, 16, bend, 1, i % 2);
        }

        drawMiniDemoCube(cx, cy, 44, 44, 1);
        drawPadPrompt(ax + 290, ay + 20, false, cyc < 140, sqOn, xOn, lbl);
        return;
    }

    if (art == 12) {
        fillRect(ax, ay, aw, ah, mk(235, 165, 85));
        fillRect(ax, ay + 420, aw, 80, mk(165, 118, 68));
        fillRect(ax, ay + 420, aw, 8, mk(240, 200, 115));

        fillRect(ax + 340, ay + 150, 55, 200, mk(150, 105, 58));
        fillRect(ax + 590, ay + 120, 55, 300, mk(150, 105, 58));
        fillRect(ax + 590, ay + 120, 220, 45, mk(165, 118, 68));
        fillRect(ax + 590, ay + 120, 220, 7, mk(240, 200, 115));

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
            cx = ax + 546;
            cy = ay + 236 + ((cyc - 35) * 60) / 40;
            dirR = true;
            lbl = "SLIDE";
        } else if (cyc < 115) {
            int wt = cyc - 75;
            cx = ax + 546 - (wt * 151) / 40;
            cy = ay + 296 - (wt * 120) / 40;
            dir = -1;
            dirL = true;
            xOn = (wt < 12);
            lbl = "WALL X!";
        } else if (cyc < 155) {
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
        fillRect(ax, ay, aw, ah, mk(52, 85, 130));
        fillRect(ax, ay + 400, 410, 100, mk(65, 88, 118));
        fillRect(ax, ay + 400, 410, 8, mk(230, 245, 255));
        fillRect(ax + 590, ay + 400, 410, 100, mk(65, 88, 118));
        fillRect(ax + 590, ay + 400, 410, 8, mk(230, 245, 255));

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

        drawMiniDemoCube(cx, cy, 44, 44, 1);
        drawPadPrompt(ax + 290, ay + 20, false, cyc < 140, false, xOn, lbl);
        return;
    }

    fillRect(ax, ay, aw, ah, (art == 14) ? mk(55, 20, 30) : mk(28, 22, 58));
    fillRect(ax, ay + 410, aw, 90, mk(55, 36, 46));
    fillRect(ax, ay + 410, aw, 8, (art == 14) ? mk(235, 90, 35) : mk(145, 105, 180));

    fillRect(ax + 400, ay + 180, 200, 32, mk(55, 36, 46));
    fillRect(ax + 460, ay + 164, 80, 16, (t & 8) ? mk(80, 255, 120) : mk(40, 200, 85));

    int cyc = t % 160;
    int bx = ax + 620 - (cyc < 80 ? cyc * 2 : (160 - cyc) * 2);
    int by = ay + 306;
    bool bossHit = (cyc >= 70 && cyc < 110);

    drawBossChar(bx, by, 104, 104, bossHit && (t & 2));

    int cx = ax + 180;
    int cy = ay + 366;
    bool xOn = (cyc >= 25 && cyc < 55);
    const char *lbl = "BOSS FIGHT";

    if (cyc < 25) {
        cx = ax + 160 + cyc * 5;
    } else if (cyc < 72) {
        int jt = cyc - 25;
        cx = ax + 285 + (jt * 235) / 47;
        cy = ay + 366 - (jt * (47 - jt)) / 3 - (jt * 104) / 47;
        lbl = "STOMP HEAD!";
    } else {
        int bt = cyc - 72;
        cx = ax + 520 + (bt * 240) / 88;
        cy = ay + 262 - (bt * (70 - bt)) / 6;
        if (cy > ay + 366) cy = ay + 366;
        lbl = "BOSS HIT!";
    }

    drawMiniDemoCube(cx, cy, 44, 44, 1);
    drawPadPrompt(ax + 290, ay + 20, false, true, false, xOn, lbl);
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
            splats[i].drawn[b] = false;

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

static void launchSelectedLevel(int lvl) {
    levelSel = lvl;
    currentLevel = lvl;

    if (lvl == 0 && !introSeen) {
        introSeen = true;
        startStory(0);
    } else if (lvl >= 0 && lvl <= 4) {
        startStory(2 + lvl);
    } else if (lvl == 9 || isBossStage(lvl)) {
        startStory(7);
    } else {
        startLevel();
    }
}

static void endStory() {
    if (storyId == 0) {
        startStory(2);
    } else if (storyId >= 2 && storyId <= 7) {
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

static void paintMenuBackdrop() {
    fillRect(0, 0, FRAME_WIDTH, 320, mk(32, 22, 68));
    fillRect(0, 320, FRAME_WIDTH, 360, mk(48, 30, 88));
    fillRect(0, 680, FRAME_WIDTH, 260, mk(34, 78, 58));
    fillRect(0, 940, FRAME_WIDTH, 140, mk(78, 48, 30));
    fillRect(0, 940, FRAME_WIDTH, 10, mk(68, 198, 72));

    fillRect(1560, 85, 110, 110, mk(255, 210, 85));
    fillRect(1576, 101, 78, 78, mk(255, 238, 140));

    for (int i = 0; i < 4; i++) {
        int hx = i * 520 + 40;
        fillRect(hx, 580, 260, 100, mk(42, 98, 72));
        fillRect(hx + 50, 545, 160, 40, mk(42, 98, 72));
    }

    int sway = SN[(tick / 8) & 15] / 36;
    for (int gx = 90; gx < FRAME_WIDTH - 80; gx += 75) {
        drawPlantTuft(gx, 940, 14, sway, 0, (gx / 75) % 3);
    }
}

static void drawBanner(
    int tid,
    int scale,
    int y,
    int h,
    bool cubes
) {
    fillRect(456, y - 4, 1008, h + 8, mk(245, 190, 65));
    fillRect(460, y, 1000, h, mk(35, 25, 55));

    drawLabel(tid, 960, y, h, scale, mk(255, 75, 115));

    if (cubes) {
        drawBigCube(500, y + (h - 88) / 2);
        drawBigCube(1332, y + (h - 88) / 2);
    }
}

static void drawHint(int tid) {
    int w = labelWidth(tid, 3) + 80;

    fillRect(960 - w / 2 - 3, 977, w + 6, 66, mk(235, 185, 65));
    fillRect(960 - w / 2, 980, w, 60, mk(35, 25, 55));

    drawLabel(tid, 960, 980, 60, 3, mk(220, 212, 238));
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

        fillRect(607, 737, 706, 66, mk(235, 185, 65));
        fillRect(610, 740, 700, 60, mk(35, 25, 55));

        drawLabel(T_TIP, 960, 740, 60, 3, mk(220, 212, 238));

        drawHint(T_H_BACK);
    }

    else if (state == S_CREDITS) {
        drawBanner(T_B_CREDITS, 8, 130, 110, false);

        fillRect(456, 296, 1008, 548, mk(235, 185, 65));
        fillRect(460, 300, 1000, 540, mk(35, 25, 55));

        drawLabel(T_DESIGNERS, 960, 340, 60, 5, mk(220, 212, 238));
        drawLabel(T_KAREEM, 960, 450, 110, 10, mk(255, 75, 115));
        drawLabel(T_CLAUDE, 960, 580, 110, 10, mk(255, 75, 115));
        drawLabel(T_GAME, 960, 740, 60, 4, mk(165, 165, 185));

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

        if (sel)
            fillRect(x - 4, y - 4, 708, 98, mk(255, 210, 85));

        fillRect(
            x, y, 700, 90,
            sel ? mk(215, 30, 45) : mk(48, 36, 68)
        );

        drawLabel(
            MENU_T[i], 960, y, 90, 6,
            sel ? mk(255,255,255) : mk(215,205,230)
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
            bool boss = isBossStage(i);

            if (sel)
                fillRect(x, y, 90, 90,
                         mk(245,245,255));
            else if (boss && unlocked)
                fillRect(x, y, 90, 90,
                         mk(255,195,55));

            int inset = (sel || (boss && unlocked)) ? 6 : 0;

            Color boxCol = unlocked ?
                           (boss ? mk(165, 15, 45) : mk(205, 25, 35)) :
                           mk(50, 40, 62);

            fillRect(
                x + inset,
                y + inset,
                90 - 2 * inset,
                90 - 2 * inset,
                boxCol
            );

            if (unlocked) {
                if (boss) {
                    fillRect(x + 27, y + 10, 36, 6, mk(255, 215, 70));
                    fillRect(x + 27, y + 6, 8, 6, mk(255, 215, 70));
                    fillRect(x + 41, y + 4, 8, 8, mk(255, 215, 70));
                    fillRect(x + 55, y + 6, 8, 6, mk(255, 215, 70));
                }

                char buf[4];
                intToStr(i + 1, buf);

                drawTextC(
                    x + 45,
                    y + (boss ? 28 : 24),
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

                strcpy(buf, isBossStage(levelSel) ? "BOSS LEVEL " : "LEVEL ");
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

        if (sel)
            fillRect(x - 4, y - 4, 908, 98, mk(255, 210, 85));

        fillRect(
            x, y, 900, 90,
            sel ? mk(215, 30, 45) : mk(48, 36, 68)
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
            sel ? mk(255,255,255) : mk(215,205,230)
        );
    }
}

static void drawUI(int b) {
    resetClip();

    if (uiFull[b]) {
        paintMenuBackdrop();

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
            launchSelectedLevel(levelSel);
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
    snd(1);

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

                    if (currentLevel < 11) {
                        launchSelectedLevel(currentLevel + 1);
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
        updateGameplayAudio();
        scene->FrameWait(frameID);
        scene->FrameBufferSwap();

        frameID++;
        tick++;
    }

    return 0;
}
