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
#define MAX_SPLATS 240
#define MAX_PARTS 100
#define MAX_PLANTS 320
#define MAX_AMB 20
#define MAX_BOSS_SHOTS 14
#define MAX_ENEMIES 48
#define MAX_BULLETS 24
#define MAX_CHECKPOINTS 6
#define MAX_DIRTY 320

std::stringstream debugLogStream;

static Scene2D *scene;
static char level[ROWS][COLS + 1];

static int padHandle = -1;
static int padUserID = 0;
static OrbisPadData padData;

#ifndef NO_AUDIO
static int audioHandle = -1;
#endif

// ---------- Game & Camera state ----------
static float px, py, vx, vy;
static float camX = 0.0f;
static int levelPixelWidth = WORLD_WIDTH;
static float checkpointX = 80.0f;
static float checkpointY = 900.0f;
static int checkCols[MAX_CHECKPOINTS];
static int checkRows[MAX_CHECKPOINTS];
static bool checkActive[MAX_CHECKPOINTS];
static int checkCount = 0;

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

// ---------- Weapons System ----------
enum {
    W_BLASTER = 0,
    W_SHOTGUN = 1,
    W_SAW     = 2,
    W_PLASMA  = 3,
    W_TOTAL   = 4
};

static int unlockedWeapons = 1;
static int curWeapon = 0;
static int shootCooldown = 0;
static bool justUnlockedWeapon = false;

struct Bullet {
    float x, y, vx, vy;
    int kind, life, dmg, pierce;
    bool active;
};

static Bullet bullets[MAX_BULLETS];

// ---------- Enemies System ----------
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

// ---------- Boss Fight state (Every 5 levels: Level 5, 10, 15...) ----------
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

// Interactive world plants (world X coordinates for smooth camera)
struct Plant {
    short room;
    int x, y;
    float angle;
    float vel;
    unsigned char height;
    unsigned char style;
    unsigned char seed;
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
static int unlockedLevels = 12; // First 12 stages unlocked
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
    X(T_W0, "WEAPON: BLASTER  -  O: SHOOT  TRIANGLE: SWITCH", AR(W_BLASTER)) \
    X(T_W1, "WEAPON: SHOTGUN  -  O: SHOOT  TRIANGLE: SWITCH", AR(W_SHOTGUN)) \
    X(T_W2, "WEAPON: SAW LAUNCHER  -  O: SHOOT  TRIANGLE: SWITCH", AR(W_SAW)) \
    X(T_W3, "WEAPON: PLASMA CANNON  -  O: SHOOT  TRIANGLE: SWITCH", AR(W_PLASMA)) \
    X(T_NEW_WEAPON, "NEW WEAPON UNLOCKED! PRESS TRIANGLE TO SWITCH", AR(NEW_WEAPON)) \
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

    // Animated Gameplay & Boss Tutorial Cutscenes (Pages 5..10)
    {10, 3, {"STAGE 1: LIVING FOREST - JUMP AND SHOOT",
             "PRESS X TO JUMP, PRESS O OR R1 TO SHOOT ENEMIES.",
             "BEAT STAGES TO UNLOCK WEAPONS AND SWITCH WITH TRIANGLE!"},
     {AR(T1L0), AR(T1L1), AR(T1L2)}},
    {11, 3, {"STAGE 2: COUNTRYSIDE - SPRINT AND SHOTGUN",
             "HOLD SQUARE TO SPRINT AND FIRE YOUR SPREAD SHOTGUN.",
             "STOMP ENEMIES OR BLAST THEM ACROSS WIDE GAPS!"},
     {AR(T2L0), AR(T2L1), AR(T2L2)}},
    {12, 3, {"STAGE 3: GOLDEN DESERT - TWO-WALL JUMP",
             "ENTER BETWEEN TWO WALLS AND SLIDE SLOWLY.",
             "PRESS X TO CLIMB AND DESTROY DESERT TURRETS!"},
     {AR(T3L0), AR(T3L1), AR(T3L2)}},
    {13, 3, {"STAGE 4: SNOWY PEAKS - SAW LAUNCHER",
             "FIRE PIERCING SAW BLADES AT FLYING ENEMIES.",
             "DODGE SPINNING SAWS ACROSS FROZEN BRIDGES!"},
     {AR(T4L0), AR(T4L1), AR(T4L2)}},
    {14, 3, {"STAGE 5: BOSS FIGHT - MAGMA CRUSHER!",
             "SHOOT WITH O, STOMP ITS HEAD, OR HIT THE TOP SWITCH.",
             "DEFEAT IT TO UNLOCK THE HEAVY PLASMA CANNON!"},
     {AR(T5L0), AR(T5L1), AR(T5L2)}},
    {15, 3, {"STAGE 10: BOSS FIGHT - SAW OVERLORD!",
             "USE YOUR PLASMA CANNON AND SIDE WALLS TO FIGHT.",
             "DODGE DOUBLE SAWS AND SAVE THE FOREST!"},
     {AR(T10L0), AR(T10L1), AR(T10L2)}}
};

static const int STORY_FIRST[8] = {0, 3, 5, 6, 7, 8, 9, 10};
static const int STORY_LAST[8]  = {3, 5, 6, 7, 8, 9, 10, 11};

// ---------- Input ----------
struct Input {
    bool up, down, left, right;
    bool dleft, dright;

    bool cross, circle, options, square, triangle, r1, l1;

    bool crossNew;
    bool circleNew;
    bool optionsNew;
    bool shootNew;
    bool switchNew;

    float ana;
};

static Input in;

static bool prevCross = false;
static bool prevCircle = false;
static bool prevOptions = false;
static bool prevShoot = false;
static bool prevSwitch = false;

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
    in.triangle = (btn & ORBIS_PAD_BUTTON_TRIANGLE) != 0;
    in.r1 = (btn & ORBIS_PAD_BUTTON_R1) != 0;
    in.l1 = (btn & ORBIS_PAD_BUTTON_L1) != 0;

    bool shootBtn = in.circle || in.r1;
    bool switchBtn = in.triangle || in.l1;

    in.crossNew = in.cross && !prevCross;
    in.circleNew = in.circle && !prevCircle;
    in.optionsNew = in.options && !prevOptions;
    in.shootNew = shootBtn && !prevShoot;
    in.switchNew = switchBtn && !prevSwitch;

    prevCross = in.cross;
    prevCircle = in.circle;
    prevOptions = in.options;
    prevShoot = shootBtn;
    prevSwitch = switchBtn;
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

// ---------- PS4 Halal Audio Engine (Menu SFX + Gameplay Percussion & Vocal Hum) ----------
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

// 0=cursor, 1=select, 2=back, 3=locked, 4=jump, 5=walljump, 6=die, 7=bosshit, 8=win, 9=shoot, 10=enemykill
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
    else if (k == 4) sfxLen = 2800;   // Jump whoosh
    else if (k == 5) sfxLen = 3400;   // Wall jump kick
    else if (k == 6) sfxLen = 6500;   // Player death
    else if (k == 7) sfxLen = 7200;   // Boss hit
    else if (k == 8) sfxLen = 13000;  // Level complete fanfare
    else if (k == 9) sfxLen = 2200;   // Weapon fire pulse
    else             sfxLen = 3800;   // Enemy defeated pop
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

    for (int blk = 0; blk < 3; blk++) {
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
                    } else if (curSfx == 8) {
                        f = (sfxPos < 4200) ? 440 : ((sfxPos < 8400) ? 550 : 660);
                        vol = 16000;
                    } else if (curSfx == 9) {
                        f = 620 - (sfxPos >> 2);
                        if (f < 180) f = 180;
                        vol = 14500;
                    } else if (curSfx == 10) {
                        f = 300 + (sfxPos >> 3);
                        vol = 15500;
                    }

                    sfxPhase += f;
                    if (sfxPhase >= 48000) sfxPhase -= 48000;

                    int tri = sfxPhase < 24000 ? sfxPhase : 48000 - sfxPhase;
                    int s = ((tri - 12000) * vol) / 12000;

                    if (curSfx == 6 || curSfx == 7 || curSfx == 9) {
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

// ---------- Level, Enemy & Checkpoint Construction ----------
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
    e.vx = (kind == E_WALKER) ? 2.6f : ((kind == E_FLYER) ? 3.2f : 2.0f);
    e.vy = 0.0f;
    e.maxHp = (kind == E_TURRET) ? 3 : 2;
    e.hp = e.maxHp;
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

    int maxC = levelPixelWidth / TILE - 1;
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

            if (((c * 7 + r * 13) % 3) != 0) {
                if (plantCount < MAX_PLANTS) {
                    Plant &p = plants[plantCount++];
                    p.room = (short)(c / ROOM_COLS);
                    p.x = c * TILE + 18 + ((c * 11 + r * 5) % 24);
                    p.y = r * TILE;
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

    enemyCount = 0;
    checkCount = 0;
    for (int i = 0; i < MAX_BULLETS; i++)
        bullets[i].active = false;
}

// Builds a rich, continuous scrolling action level with enemies, checkpoints, wall-jumps, bridges, and hazards
static void buildScrollingActionStage(int stageIdx) {
    baseCol = 0;
    levelPixelWidth = WORLD_WIDTH;

    // Zone 1 (Cols 1..31): Opening platforms + First Enemy Patrols
    plat(14, 7, 13);
    addEnemy(E_WALKER, 10, 14, 7, 13);

    spk(16, 15, 18);
    plat(12, 16, 22);
    addEnemy(E_WALKER, 19, 12, 16, 22);

    plat(14, 24, 30);
    if (stageIdx >= 1)
        addEnemy(E_JUMPER, 27, 14, 24, 30);

    addCheckpoint(31, 17);

    // Zone 2 (Cols 32..63): Elevated Bridges, Flying Enemies & Sprinting Chasms
    spk(16, 35, 58);
    plat(14, 34, 41);
    addEnemy(E_WALKER, 37, 14, 34, 41);

    plat(12, 44, 51);
    addEnemy(E_FLYER, 47, 9, 42, 53);

    plat(10, 54, 60);
    if (stageIdx >= 2)
        addEnemy(E_TURRET, 58, 10, 54, 60);

    addCheckpoint(63, 17);

    // Zone 3 (Cols 64..95): Twin-Wall Jump Chimneys & Elevated Fortresses
    wall(70, 6, 14);
    wall(73, 6, 16);
    plat(6, 73, 82);
    spk(16, 74, 83);
    addEnemy(E_WALKER, 78, 6, 74, 82);

    plat(11, 84, 92);
    addEnemy(E_JUMPER, 88, 11, 84, 92);
    if (stageIdx >= 3)
        sawAt(10, 86);

    addCheckpoint(95, 17);

    // Zone 4 (Cols 96..127): Saw Gauntlet + Turret Guards + Mid-air Flyers
    spk(16, 99, 123);
    plat(15, 98, 104);
    sawAt(14, 106);
    plat(13, 108, 115);
    addEnemy(E_WALKER, 112, 13, 108, 115);
    addEnemy(E_FLYER, 115, 8, 108, 122);

    plat(11, 118, 125);
    addEnemy(E_TURRET, 123, 11, 118, 125);

    addCheckpoint(127, 17);

    // Zone 5 (Cols 128..159): Double Wall Tower & High Altitude Combat
    plat(14, 131, 136);
    addEnemy(E_JUMPER, 134, 14, 131, 136);

    wall(139, 5, 14);
    wall(142, 5, 16);
    plat(5, 142, 151);
    spk(16, 143, 156);
    addEnemy(E_WALKER, 147, 5, 143, 151);

    plat(10, 153, 158);
    if (stageIdx >= 1)
        sawAt(9, 155);

    addCheckpoint(160, 17);

    // Zone 6 (Cols 160..190): Final Citadel Climb to Bandage Girl
    spk(16, 163, 185);
    plat(14, 162, 168);
    addEnemy(E_WALKER, 165, 14, 162, 168);

    plat(12, 171, 177);
    addEnemy(E_FLYER, 174, 8, 169, 180);
    if (stageIdx >= 2)
        addEnemy(E_TURRET, 176, 12, 171, 177);

    plat(10, 180, 189);
    put(9, 187, 'G');
}

// Wide Scrolling Boss Arena Builder (Used for Level 5, Level 10, and every 5th level!)
static void buildBossArena(int tier) {
    baseCol = 0;
    int arenaCols = 46; // 1.45 screens wide so camera scrolls smoothly during Boss Fights!
    levelPixelWidth = arenaCols * TILE;

    for (int r = 0; r < ROWS; r++)
        level[r][arenaCols - 1] = '#';

    // Left & Right wall-jump pillars
    wall(3, 6, 13);
    wall(arenaCols - 4, 6, 13);

    // Mid-level combat platforms
    plat(12, 7, 13);
    plat(12, 20, 25);
    plat(12, 32, 38);

    // Upper platform holding the Boss Strike Switch (cols 20..25)
    plat(7, 19, 26);

    bossActive = true;
    bossW = 104;
    bossH = 104;
    bossX = (float)(30 * TILE);
    bossY = (float)((ROWS - 1) * TILE - bossH);
    bossVX = (tier >= 2) ? -5.6f : -4.4f;
    bossVY = 0.0f;
    bossMaxHP = (tier >= 2) ? 14 : 10;
    bossHP = bossMaxHP;
    bossHurt = 0;
    bossState = 0;
    bossTimer = 0;
    bossSwitchCooldown = 0;
    bossBeamTimer = 0;

    for (int i = 0; i < MAX_BOSS_SHOTS; i++)
        bossShots[i].active = false;
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
    if (idx == 0)      currentBiome = 0;
    else if (idx == 1) currentBiome = 1;
    else if (idx == 2) currentBiome = 2;
    else if (idx == 3) currentBiome = 3;
    else if (idx == 5) currentBiome = 5;
    else if (idx == 6) currentBiome = 3;
    else if (idx == 7) currentBiome = 1;
    else if (idx == 8) currentBiome = 0;
    else if (idx == 10)currentBiome = 2;
    else               currentBiome = 4;

    buildScrollingActionStage(currentLevel);
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

    if (wx < 0 || wy < 0 || wx >= levelPixelWidth)
        return;

    Splat &s = splats[splatCount++];

    s.room = (short)(wx / FRAME_WIDTH);
    s.x = (short)(wx - w / 2);
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
    float pcx = px + PW * 0.5f;
    float pby = py + PH;
    int viewX0 = (int)camX - 120;
    int viewX1 = (int)camX + FRAME_WIDTH + 120;

    for (int i = 0; i < plantCount; i++) {
        Plant &p = plants[i];
        if (p.x < viewX0 || p.x > viewX1)
            continue;

        int wIdx = ((tick / 5) + (p.x / 48) + p.seed) & 15;
        float wind = (float)SN[wIdx] / 256.0f * 2.6f;

        if (deadTimer == 0) {
            float dx = pcx - (float)p.x;
            float dy = pby - (float)p.y;
            float adx = dx < 0 ? -dx : dx;

            if (adx < 46.0f && dy > -22.0f && dy < 32.0f) {
                float prox = 1.0f - (adx / 46.0f);
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

// ---------- Player, Weapons & Enemies Logic ----------
static void updateCamera(bool snap) {
    float target = px + PW * 0.5f - FRAME_WIDTH * 0.38f;
    float maxCam = (float)(levelPixelWidth - FRAME_WIDTH);
    if (maxCam < 0.0f) maxCam = 0.0f;

    if (target < 0.0f) target = 0.0f;
    if (target > maxCam) target = maxCam;

    if (snap)
        camX = target;
    else
        camX += (target - camX) * 0.14f;
}

static void respawn() {
    px = checkpointX;
    py = checkpointY;

    vx = 0;
    vy = 0;

    squashX = 1.0f;
    squashY = 1.0f;

    onGround = false;
    wallLock = 0;
    coyote = 0;

    updateCamera(true);

    if (bossActive && bossHP > 0) {
        bossX = (float)(30 * TILE);
        bossY = (float)((ROWS - 1) * TILE - bossH);
        for (int i = 0; i < MAX_BOSS_SHOTS; i++)
            bossShots[i].active = false;
    }
}

static void die() {
    deaths++;
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

static void spawnPlayerBullet(float bx, float by, float bvx, float bvy, int kind, int dmg, int pierce) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            Bullet &b = bullets[i];
            b.x = bx;
            b.y = by;
            b.vx = bvx;
            b.vy = bvy;
            b.kind = kind;
            b.dmg = dmg;
            b.pierce = pierce;
            b.life = (kind == W_SHOTGUN) ? 34 : 65;
            b.active = true;
            return;
        }
    }
}

static void fireWeapon() {
    float bx = facing > 0 ? (px + PW + 4.0f) : (px - 16.0f);
    float by = py + PH * 0.45f;
    float dir = (float)facing;

    snd(9);

    if (curWeapon == W_BLASTER) {
        spawnPlayerBullet(bx, by, dir * 15.0f, 0.0f, W_BLASTER, 1, 0);
        shootCooldown = 12;
    } else if (curWeapon == W_SHOTGUN) {
        spawnPlayerBullet(bx, by, dir * 14.0f, -2.4f, W_SHOTGUN, 1, 0);
        spawnPlayerBullet(bx, by, dir * 15.0f,  0.0f, W_SHOTGUN, 1, 0);
        spawnPlayerBullet(bx, by, dir * 14.0f,  2.4f, W_SHOTGUN, 1, 0);
        shootCooldown = 22;
    } else if (curWeapon == W_SAW) {
        spawnPlayerBullet(bx, by, dir * 12.5f, 0.0f, W_SAW, 1, 2);
        shootCooldown = 18;
    } else {
        spawnPlayerBullet(bx, by, dir * 11.5f, 0.0f, W_PLASMA, 2, 1);
        shootCooldown = 24;
    }
}

static void damageBoss(int dmg);
static void spawnBossShot(float sx, float sy, float svx, float svy);

static void killEnemy(Enemy &e) {
    e.active = false;
    snd(10);

    float ecx = e.x + 22.0f;
    float ecy = e.y + 22.0f;
    for (int i = 0; i < 16; i++) {
        float pvx = (float)(rnd(160) - 80) / 10.0f;
        float pvy = (float)(rnd(140) - 110) / 10.0f;
        spawnPart(ecx, ecy, pvx, pvy, 45, 6 + rnd(4), 1);
    }
}

static void updateBulletsAndEnemies() {
    // Update player bullets
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet &b = bullets[i];
        if (!b.active)
            continue;

        b.x += b.vx;
        b.y += b.vy;
        b.life--;

        if (b.life <= 0 || solidAt((int)b.x, (int)b.y)) {
            spawnDust(b.x, b.y, 3, 1.8f);
            b.active = false;
            continue;
        }

        // Check bullet hitting enemies
        for (int j = 0; j < enemyCount; j++) {
            Enemy &e = enemies[j];
            if (!e.active)
                continue;

            if (rectOverlap((int)b.x - 10, (int)b.y - 10, 20, 20,
                            (int)e.x, (int)e.y, 44, 44)) {
                e.hp -= b.dmg;
                e.hurt = 10;
                spawnDust(b.x, b.y, 4, 2.2f);

                if (e.hp <= 0)
                    killEnemy(e);
                else
                    snd(0);

                if (b.pierce > 0) {
                    b.pierce--;
                } else {
                    b.active = false;
                    break;
                }
            }
        }

        // Check bullet hitting Boss
        if (b.active && bossActive && bossHP > 0) {
            if (rectOverlap((int)b.x - 10, (int)b.y - 10, 20, 20,
                            (int)bossX + 6, (int)bossY + 6, bossW - 12, bossH - 12)) {
                damageBoss(b.dmg);
                b.active = false;
            }
        }
    }

    // Update enemies within active camera range
    int activeX0 = (int)camX - 300;
    int activeX1 = (int)camX + FRAME_WIDTH + 300;

    for (int i = 0; i < enemyCount; i++) {
        Enemy &e = enemies[i];
        if (!e.active)
            continue;
        if ((int)e.x < activeX0 || (int)e.x > activeX1)
            continue;

        if (e.hurt > 0)
            e.hurt--;
        e.timer++;

        if (e.kind == E_WALKER) {
            e.x += e.vx * e.dir;
            if (e.x <= e.minX) { e.x = e.minX; e.dir = 1; }
            if (e.x >= e.maxX) { e.x = e.maxX; e.dir = -1; }
        } else if (e.kind == E_FLYER) {
            e.x += e.vx * e.dir;
            if (e.x <= e.minX) { e.x = e.minX; e.dir = 1; }
            if (e.x >= e.maxX) { e.x = e.maxX; e.dir = -1; }
            int wave = SN[(e.timer / 4) & 15];
            e.y = e.baseY + (wave * 28) / 256;
        } else if (e.kind == E_JUMPER) {
            e.x += e.vx * e.dir;
            if (e.x <= e.minX) { e.x = e.minX; e.dir = 1; }
            if (e.x >= e.maxX) { e.x = e.maxX; e.dir = -1; }
            if (e.timer % 55 == 0 && e.y >= e.baseY - 2.0f)
                e.vy = -12.5f;
            e.vy += 0.65f;
            e.y += e.vy;
            if (e.y >= e.baseY) {
                e.y = e.baseY;
                e.vy = 0.0f;
            }
        } else if (e.kind == E_TURRET) {
            e.dir = (px < e.x) ? -1 : 1;
            if (e.timer % 95 == 0) {
                spawnBossShot(e.x + 22.0f, e.y + 18.0f, (float)e.dir * 6.5f, 0.0f);
            }
        }

        // Player vs Enemy collision (Stomp head to kill vs take damage)
        if (deadTimer == 0 &&
            rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8,
                        (int)e.x + 4, (int)e.y + 4, 36, 36)) {
            if (vy > 0.0f && (py + PH) < (e.y + 26.0f)) {
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

    for (int i = 0; i < 18; i++) {
        float pvx = (float)(rnd(200) - 100) / 10.0f;
        float pvy = (float)(rnd(160) - 130) / 10.0f;
        spawnPart(bcx, bcy, pvx, pvy, 50, 6 + rnd(5), 1);
    }

    if (bossHP <= 0) {
        for (int i = 0; i < MAX_BOSS_SHOTS; i++)
            bossShots[i].active = false;

        level[16][22] = 'G';
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
        bossVX = 4.5f;
    } else if (bossX > (float)(38 * TILE)) {
        bossX = (float)(38 * TILE);
        bossVX = -4.5f;
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

    int swX = 21 * TILE;
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
    int c0 = ((int)px / TILE) - 2;
    int c1 = ((int)px + PW) / TILE + 2;
    if (c0 < 0) c0 = 0;
    if (c1 >= COLS) c1 = COLS - 1;

    for (int r = 0; r < ROWS; r++) {
        for (int c = c0; c <= c1; c++) {
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

    if (shootCooldown > 0)
        shootCooldown--;

    // Weapon switching (Triangle or L1)
    if (in.switchNew && unlockedWeapons > 1) {
        curWeapon = (curWeapon + 1) % unlockedWeapons;
        snd(0);
    }

    // Weapon firing (Circle or R1)
    if ((in.shootNew || ((in.circle || in.r1) && shootCooldown == 0)) && shootCooldown == 0) {
        fireWeapon();
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

    float maxSpeed = (run ? 8.2f : 5.6f) * scale;

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
            snd(4);
        } else if (wallL || wallR) {
            vy = -16.0f;
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

    // Smooth camera tracking
    updateCamera(false);

    // Checkpoints activation
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
    updateBulletsAndEnemies();
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
        winTimer = 160;
        needWin[0] = true;
        needWin[1] = true;

        // Unlock new weapon upon completing stages!
        justUnlockedWeapon = false;
        int newWeapCount = unlockedWeapons;
        if (currentLevel >= 0 && unlockedWeapons < 2) newWeapCount = 2;
        if (currentLevel >= 2 && unlockedWeapons < 3) newWeapCount = 3;
        if (currentLevel >= 4 && unlockedWeapons < 4) newWeapCount = 4;

        if (newWeapCount > unlockedWeapons) {
            unlockedWeapons = newWeapCount;
            curWeapon = unlockedWeapons - 1;
            justUnlockedWeapon = true;
        }

        snd(8);
    }

    int nr = ((int)px + PW / 2) / FRAME_WIDTH;
    if (nr < 0) nr = 0;
    if (nr > LAST_ROOM) nr = LAST_ROOM;
    roomIdx = nr;
}

// ---------- Game drawing (With Smooth Camera Offset) ----------
static void drawSpikeAt(int screenX, int r) {
    int center = screenX + TILE / 2;
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
    if (cx < -30 || cx > FRAME_WIDTH + 30)
        return;

    int a = (tick / 2) & 15;

    int cc = CS[a];
    int ss = SN[a];

    for (int dy = -26; dy <= 26; dy++) {
        for (int dx = -26; dx <= 26; dx++) {
            int d2 = dx * dx + dy * dy;

            if (d2 > 26 * 26)
                continue;

            int px0 = cx + dx;
            int py0 = cy + dy;
            if (px0 < 0 || px0 >= FRAME_WIDTH || py0 < 0 || py0 >= FRAME_HEIGHT)
                continue;

            int u = (dx * cc + dy * ss) >> 8;
            int v = (-dx * ss + dy * cc) >> 8;

            if (d2 < 36) {
                scene->DrawPixel(px0, py0, mk(50, 50, 55));
            } else if (d2 <= 20 * 20) {
                scene->DrawPixel(px0, py0, mk(165, 165, 178));
            } else if (
                iabs(u) < 6 ||
                iabs(v) < 6 ||
                iabs(u - v) < 8 ||
                iabs(u + v) < 8
            ) {
                scene->DrawPixel(px0, py0, mk(215, 215, 225));
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

static void drawTileCellScreen(int r, int screenX, char ch, bool hi) {
    int y = r * TILE;

    if (ch == '#') {
        Color mainC = mk(58, 48, 82);
        Color topC  = mk(145, 105, 180);
        Color botC  = mk(38, 30, 58);

        if (currentBiome == 0) {
            mainC = mk(82, 54, 38);
            topC  = mk(65, 190, 75);
            botC  = mk(55, 36, 24);
        } else if (currentBiome == 1) {
            mainC = mk(102, 62, 40);
            topC  = mk(225, 175, 55);
            botC  = mk(70, 42, 26);
        } else if (currentBiome == 2) {
            mainC = mk(165, 118, 68);
            topC  = mk(240, 200, 115);
            botC  = mk(120, 82, 46);
        } else if (currentBiome == 3) {
            mainC = mk(65, 88, 118);
            topC  = mk(230, 245, 255);
            botC  = mk(42, 58, 82);
        } else if (currentBiome == 4) {
            mainC = mk(55, 36, 46);
            topC  = mk(235, 90, 35);
            botC  = mk(34, 20, 28);
        }

        fillRect(screenX, y, TILE, TILE, mainC);

        if (hi)
            fillRect(screenX, y, TILE, 7, topC);

        fillRect(screenX, y + TILE - 5, TILE, 5, botC);
    }

    else if (ch == '^') {
        drawSpikeAt(screenX, r);
    }

    else if (ch == 'G') {
        int gx = screenX + 8;
        int gy = y + 16;

        fillRect(gx, gy, PW, PH, mk(255, 140, 170));
        fillRect(gx, gy + 3, PW, 7, mk(240, 230, 200));

        drawFace(gx, gy, -1, mk(255, 255, 255));
    }
}

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

        fillRect(ox - 2, by - segH, 4, segH, stemDark);
        fillRect(ox - 2 + b1, by - segH * 2, 4, segH, stemBright);
        fillRect(ox - 2 + b2, by - segH * 3, 3, segH, stemBright);

        if (biome == 1) {
            fillRect(ox - 2 + b3, by - segH * 3 - 5, 5, 6, tipCol);
        } else if (b == 0 && style != 2) {
            fillRect(ox - 3 + b3, by - segH * 3 - 4, 6, 5, tipCol);
        }
    }
}

// Helper for star positions & twinkling
static void starPos(int i, int &sx, int &sy) {
    sx = (i * 271 + 91) % FRAME_WIDTH;
    sy = (i * 137 + 31) % 570;
}

static bool starDim(int i, int phase) {
    return ((phase + i) % 4) == 0;
}

// Helper for seamless parallax wrapping
static int wrapX(int val, int span) {
    int m = val % span;
    return m < 0 ? m + span : m;
}

// Animated flying birds in the sky
static void drawAnimatedBirds(int camP, Color birdCol) {
    int wing = ((tick / 8) & 1) ? -7 : 6;
    for (int i = 0; i < 6; i++) {
        int bx = wrapX(i * 360 + tick * 2 - camP, FRAME_WIDTH + 120) - 60;
        int by = 135 + (i % 3) * 55 + (SN[((tick / 6) + i * 3) & 15] >> 5);
        fillRect(bx, by, 10, 5, birdCol);
        fillRect(bx - 8, by + wing, 9, 4, birdCol);
        fillRect(bx + 9, by + wing, 9, 4, birdCol);
    }
}

// Vibrant Multi-Layer Parallax Background with Animated World Elements
static void paintBackground() {
    int pSlow = (int)(camX * 0.08f);
    int pFar  = (int)(camX * 0.22f);
    int pNear = (int)(camX * 0.45f);
    int pulse = SN[(tick / 4) & 15] >> 5;

    if (currentBiome == 0) {
        // Biome 0: Living Green Forest (Animated sun rays, drifting clouds, flapping birds, waterfalls)
        Color sky1 = mk(65, 165, 235);
        Color sky2 = mk(115, 205, 248);
        Color farC = mk(42, 115, 75);
        Color nearC = mk(26, 82, 52);

        fillRect(0, 0, FRAME_WIDTH, 280, sky1);
        fillRect(0, 280, FRAME_WIDTH, 290, sky2);
        fillRect(0, 570, FRAME_WIDTH, 130, farC);
        fillRect(0, 700, FRAME_WIDTH, 380, nearC);

        int sunX = 1520 - (pSlow % 300);
        fillRect(sunX - 12 - pulse, 83 - pulse, 134 + pulse * 2, 134 + pulse * 2, mk(255, 245, 175));
        fillRect(sunX, 95, 110, 110, mk(255, 235, 110));

        for (int i = 0; i < 5; i++) {
            int cx = wrapX(i * 440 + tick / 2 - pSlow, FRAME_WIDTH + 240) - 180;
            int cy = 120 + (i & 1) * 60;
            fillRect(cx, cy, 170, 38, mk(235, 248, 255));
            fillRect(cx + 30, cy - 20, 110, 24, mk(248, 253, 255));
        }

        drawAnimatedBirds(pSlow, mk(30, 75, 55));

        for (int i = 0; i < 6; i++) {
            int x = wrapX(i * 420 - pFar, 2520) - 300;
            fillRect(x + 60, 420, 160, 150, farC);
            fillRect(x + 90, 370, 100, 60, farC);
            if (i % 2 == 0) {
                fillRect(x + 125, 420, 26, 280, mk(150, 225, 255));
                int wf = (tick * 4) % 40;
                fillRect(x + 129, 420 + wf, 18, 24, mk(225, 250, 255));
                fillRect(x + 129, 520 + wf, 18, 24, mk(225, 250, 255));
            }
        }

        int sway = SN[(tick / 6) & 15] >> 5;
        for (int i = 0; i < 7; i++) {
            int x = wrapX(i * 340 - pNear, 2380) - 220;
            fillRect(x + 70, 610, 36, 100, mk(68, 44, 28));
            fillRect(x + 20 + sway, 500, 136, 120, nearC);
            fillRect(x + 44 - sway, 455, 88, 55, mk(36, 102, 64));
        }
        return;
    }

    if (currentBiome == 1) {
        // Biome 1: Golden Countryside (Sunset glow, flying birds, rolling hills, barns & silos)
        Color sky1 = mk(195, 92, 72);
        Color sky2 = mk(245, 160, 85);
        Color farC = mk(138, 82, 52);
        Color nearC = mk(92, 55, 36);

        fillRect(0, 0, FRAME_WIDTH, 290, sky1);
        fillRect(0, 290, FRAME_WIDTH, 280, sky2);
        fillRect(0, 570, FRAME_WIDTH, 130, farC);
        fillRect(0, 700, FRAME_WIDTH, 380, nearC);

        int sunX = 1440 - (pSlow % 260);
        fillRect(sunX - 10 - pulse, 190 - pulse, 150 + pulse * 2, 150 + pulse * 2, mk(255, 195, 95));
        fillRect(sunX, 200, 130, 130, mk(255, 230, 125));

        drawAnimatedBirds(pSlow, mk(95, 40, 30));

        for (int i = 0; i < 5; i++) {
            int x = wrapX(i * 500 - pFar, 2500) - 300;
            fillRect(x + 40, 465, 280, 105, farC);
            fillRect(x + 90, 435, 180, 40, farC);
        }

        for (int i = 0; i < 4; i++) {
            int x = wrapX(i * 620 - pNear, 2480) - 260;
            fillRect(x + 60, 580, 150, 120, nearC);
            fillRect(x + 85, 550, 100, 35, nearC);
            fillRect(x + 250, 550, 76, 150, nearC);
            fillRect(x + 262, 528, 52, 26, nearC);
        }
        return;
    }

    if (currentBiome == 2) {
        // Biome 2: Golden Desert
        Color sky1 = mk(230, 142, 62);
        Color sky2 = mk(250, 202, 118);
        Color farC = mk(195, 135, 68);
        Color nearC = mk(158, 102, 48);

        fillRect(0, 0, FRAME_WIDTH, 300, sky1);
        fillRect(0, 300, FRAME_WIDTH, 270, sky2);
        fillRect(0, 570, FRAME_WIDTH, 130, farC);
        fillRect(0, 700, FRAME_WIDTH, 380, nearC);

        int sunX = 1500 - (pSlow % 250);
        fillRect(sunX - 12 - pulse, 93 - pulse, 144 + pulse * 2, 144 + pulse * 2, mk(255, 230, 145));
        fillRect(sunX, 105, 120, 120, mk(255, 250, 200));

        for (int i = 0; i < 4; i++) {
            int px0 = wrapX(i * 580 - pFar, 2320) - 200;
            for (int step = 0; step < 8; step++) {
                int w = (step + 1) * 34;
                fillRect(px0 - w / 2, 410 + step * 20, w, 20, farC);
            }
        }

        for (int i = 0; i < 5; i++) {
            int x = wrapX(i * 480 - pNear, 2400) - 250;
            fillRect(x + 60, 605, 290, 95, nearC);
            fillRect(x + 120, 575, 170, 40, nearC);
        }
        return;
    }

    if (currentBiome == 3) {
        // Biome 3: Snowy Peaks (Animated Aurora Borealis ribbons + Snow-capped peaks)
        Color sky1 = mk(32, 56, 96);
        Color sky2 = mk(62, 100, 148);
        Color farC = mk(95, 135, 178);
        Color nearC = mk(62, 95, 135);

        fillRect(0, 0, FRAME_WIDTH, 300, sky1);
        fillRect(0, 300, FRAME_WIDTH, 270, sky2);
        fillRect(0, 570, FRAME_WIDTH, 130, farC);
        fillRect(0, 700, FRAME_WIDTH, 380, nearC);

        for (int i = 0; i < 8; i++) {
            int ax = i * 240;
            int ay = 130 + (SN[((tick / 5) + i * 2) & 15] >> 3);
            fillRect(ax, ay, 240, 22, mk(65, 215, 175));
            fillRect(ax, ay + 22, 240, 14, mk(95, 245, 205));
        }

        fillRect(1500 - (pSlow % 220), 105, 96, 96, mk(225, 245, 255));

        for (int i = 0; i < 4; i++) {
            int x = wrapX(i * 620 - pFar, 2480) - 260;
            fillRect(x + 80, 410, 200, 160, farC);
            fillRect(x + 110, 370, 140, 50, mk(230, 245, 255));
            fillRect(x + 340, 440, 180, 130, farC);
            fillRect(x + 365, 410, 130, 40, mk(230, 245, 255));
        }

        for (int i = 0; i < 5; i++) {
            int x = wrapX(i * 500 - pNear, 2500) - 250;
            fillRect(x + 70, 550, 200, 150, nearC);
            fillRect(x + 105, 520, 130, 40, mk(210, 232, 250));
        }
        return;
    }

    if (currentBiome == 4) {
        // Biome 4: Volcanic Lava Valley (Erupting volcanoes, pulsing crimson moon)
        Color sky1 = mk(42, 14, 24);
        Color sky2 = mk(82, 26, 34);
        Color farC = mk(58, 22, 30);
        Color nearC = mk(38, 15, 22);

        fillRect(0, 0, FRAME_WIDTH, 300, sky1);
        fillRect(0, 300, FRAME_WIDTH, 270, sky2);
        fillRect(0, 570, FRAME_WIDTH, 130, farC);
        fillRect(0, 700, FRAME_WIDTH, 380, nearC);

        int moonX = 1500 - (pSlow % 220);
        fillRect(moonX - pulse, 115 - pulse, 100 + pulse * 2, 100 + pulse * 2, mk(235, 95, 50));
        fillRect(moonX + 18, 130, 65, 70, mk(255, 165, 70));

        for (int i = 0; i < 4; i++) {
            int x = wrapX(i * 640 - pFar, 2560) - 260;
            fillRect(x + 120, 400, 220, 170, farC);
            fillRect(x + 180, 392, 100, 20, mk(255, 105, 35));
            int jetH = 25 + ((tick * 3 + i * 20) % 45);
            fillRect(x + 218, 392 - jetH, 24, jetH, mk(255, 195, 60));
            fillRect(x + 222, 412, 16, 158, mk(235, 80, 30));
        }

        for (int i = 0; i < 5; i++) {
            int x = wrapX(i * 520 - pNear, 2600) - 260;
            fillRect(x + 80, 540, 200, 160, nearC);
        }
        return;
    }

    // Biome 5: Classic Night Sky with Parallax Moon, Twinkling Stars & Castle Towers
    Color farC = mk(35, 20, 75);
    Color nearC = mk(25, 14, 55);

    fillRect(0, 0, FRAME_WIDTH, 300, mk(24, 12, 55));
    fillRect(0, 300, FRAME_WIDTH, 270, mk(31, 15, 66));
    fillRect(0, 570, FRAME_WIDTH, 130, farC);
    fillRect(0, 700, FRAME_WIDTH, 380, nearC);

    int moonX = 1500 - (pSlow % 220);
    fillRect(moonX, 115, 100, 100, mk(145, 125, 205));
    fillRect(moonX + 15, 100, 70, 130, mk(145, 125, 205));
    fillRect(moonX + 20, 125, 65, 75, mk(185, 170, 220));

    int phase = tick / 20;

    for (int i = 0; i < NUM_STARS; i++) {
        int sx0, sy0;
        starPos(i, sx0, sy0);
        int sx = wrapX(sx0 - (pSlow / 2), FRAME_WIDTH);
        int sy = sy0 % 550;

        if (i % 7 == 0)
            fillRect(sx - 2, sy - 2, 7, 7, mk(70, 55, 110));

        fillRect(
            sx, sy, 3, 3,
            starDim(i, phase) ? mk(130, 115, 185)
                              : mk(220, 210, 255)
        );
    }

    for (int i = 0; i < 4; i++) {
        int x = wrapX(i * 680 - pFar, 2720) - 280;
        fillRect(x + 80, 450, 180, 130, farC);
        fillRect(x + 220, 390, 180, 190, farC);
        fillRect(x + 390, 470, 180, 110, farC);
    }

    for (int i = 0; i < 5; i++) {
        int x = wrapX(i * 540 - pNear, 2700) - 260;
        fillRect(x + 70, 590, 160, 120, nearC);
        fillRect(x + 220, 530, 180, 180, nearC);
        fillRect(x + 145, 560, 18, 28, (tick & 16) ? mk(245, 210, 95) : mk(185, 145, 65));
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

    // Current Weapon HUD badge at top-right
    int wTid = T_W0 + curWeapon;
    Color wCol = mk(95, 215, 255);
    if (curWeapon == W_SHOTGUN) wCol = mk(255, 195, 70);
    else if (curWeapon == W_SAW) wCol = mk(140, 250, 160);
    else if (curWeapon == W_PLASMA) wCol = mk(255, 120, 235);

    fillRect(1300, 22, 560, 46, mk(28, 20, 48));
    fillRect(1303, 25, 554, 40, mk(44, 32, 72));
    drawLabel(wTid, 1580, 25, 40, 2, wCol);

    hudSq[b] = hudN;
    hudProg[b] = prog;
    hudNeed[b] = false;
}

static void drawEnemyChar(const Enemy &e, int sx, int sy) {
    bool flash = (e.hurt & 2) != 0;

    if (e.kind == E_WALKER) {
        Color body = flash ? mk(255, 255, 255) : mk(145, 35, 165);
        Color trim = mk(85, 18, 105);
        fillRect(sx, sy, 44, 44, trim);
        fillRect(sx + 4, sy + 4, 36, 36, body);
        fillRect(sx + 10, sy - 6, 8, 6, mk(225, 225, 235));
        fillRect(sx + 26, sy - 6, 8, 6, mk(225, 225, 235));
        drawFace(sx, sy, e.dir, mk(255, 235, 90));
    } else if (e.kind == E_FLYER) {
        Color body = flash ? mk(255, 255, 255) : mk(225, 85, 35);
        int wingY = ((tick / 4) & 1) ? -10 : 8;
        fillRect(sx - 12, sy + 14 + wingY, 14, 10, mk(175, 45, 25));
        fillRect(sx + 42, sy + 14 + wingY, 14, 10, mk(175, 45, 25));
        fillRect(sx + 4, sy + 4, 36, 36, body);
        drawFace(sx, sy, e.dir, mk(255, 245, 120));
    } else if (e.kind == E_JUMPER) {
        Color body = flash ? mk(255, 255, 255) : mk(45, 185, 135);
        fillRect(sx, sy, 44, 44, mk(25, 115, 80));
        fillRect(sx + 4, sy + 4, 36, 36, body);
        fillRect(sx + 12, sy + 42, 20, 6, mk(210, 220, 230));
        drawFace(sx, sy, e.dir, mk(255, 255, 255));
    } else {
        Color body = flash ? mk(255, 255, 255) : mk(95, 105, 125);
        fillRect(sx, sy + 6, 44, 38, mk(50, 55, 70));
        fillRect(sx + 4, sy + 10, 36, 30, body);
        int barrelX = (e.dir < 0) ? (sx - 14) : (sx + 38);
        fillRect(barrelX, sy + 18, 18, 12, mk(235, 75, 45));
        fillRect(sx + 14, sy + 16, 16, 12, mk(255, 215, 70));
    }
}

static void drawBossChar(int bx, int by, int bw, int bh, bool hurt) {
    Color armorDark = hurt ? mk(255, 240, 180) : mk(85, 22, 32);
    Color armorMain = hurt ? mk(255, 255, 255) : mk(175, 35, 45);
    Color coreCol   = (tick & 4) ? mk(255, 215, 65) : mk(255, 110, 30);

    fillRect(bx, by, bw, bh, armorDark);
    fillRect(bx + 8, by + 8, bw - 16, bh - 16, armorMain);

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

            drawLabel(T_COMPLETE, 960, 330, 110, 10,
                      mk(255, 255, 255));

            drawLabel(T_GREAT, 960, 470, 80, 6,
                      mk(255, 220, 180));

            if (justUnlockedWeapon) {
                fillRect(410, 600, 1100, 84, mk(35, 25, 60));
                fillRect(414, 604, 1092, 76, mk(215, 145, 30));
                drawLabel(T_NEW_WEAPON, 960, 604, 76, 4, mk(255, 255, 255));
            }

            needWin[b] = false;
        }

        return;
    }

    int camI = (int)camX;

    // 1. Vibrant Multi-Layer Parallax Background
    paintBackground();

    // 2. Visible World Tiles
    int c0 = camI / TILE;
    int c1 = (camI + FRAME_WIDTH) / TILE + 1;
    if (c0 < 0) c0 = 0;
    if (c1 > COLS) c1 = COLS;

    for (int r = 0; r < ROWS; r++) {
        for (int c = c0; c < c1; c++) {
            char ch = staticChar(r, c);
            if (ch != '.') {
                drawTileCellScreen(r, c * TILE - camI, ch, hiAt(r, c));
            }
        }
    }

    // 3. Checkpoint Flags
    for (int i = 0; i < checkCount; i++) {
        int fx = checkCols[i] * TILE + 24 - camI;
        int fy = checkRows[i] * TILE - 64;
        if (fx < -40 || fx > FRAME_WIDTH + 40)
            continue;
        fillRect(fx, fy, 6, 64, mk(220, 220, 235));
        Color flagCol = checkActive[i] ? mk(65, 235, 110) : mk(235, 190, 55);
        fillRect(fx + 6, fy + 4, 28, 20, flagCol);
    }

    // 4. Visible Blood Splats
    for (int i = 0; i < splatCount; i++) {
        const Splat &s = splats[i];
        int sx = s.x - camI;
        if (sx + s.w < 0 || sx > FRAME_WIDTH)
            continue;
        fillRect(sx, s.y, s.w, s.h, mk(140, 10, 20));
        fillRect(sx + 1, s.y + 1, s.w / 3 + 1, s.h / 3 + 1, mk(185, 22, 34));
    }

    // 5. Ambient Living World Particles
    Color ambCol = mk(235, 245, 255);
    if (currentBiome == 0) ambCol = mk(140, 235, 110);
    else if (currentBiome == 1) ambCol = mk(255, 230, 130);
    else if (currentBiome == 2) ambCol = mk(245, 215, 145);
    else if (currentBiome == 3) ambCol = mk(240, 250, 255);
    else if (currentBiome == 4) ambCol = mk(255, 145, 45);
    else ambCol = mk(175, 155, 235);

    for (int i = 0; i < MAX_AMB; i++) {
        AmbPart &a = ambParts[i];
        fillRect((int)a.x, (int)a.y, a.sz, a.sz, ambCol);
    }

    // 6. Interactive World Plants
    for (int i = 0; i < plantCount; i++) {
        const Plant &p = plants[i];
        int sx = p.x - camI;
        if (sx < -30 || sx > FRAME_WIDTH + 30)
            continue;
        drawPlantTuft(sx, p.y, p.height, (int)p.angle, currentBiome, p.style);
    }

    // 7. Spinning Saws
    for (int r = 0; r < ROWS; r++) {
        for (int c = c0; c < c1; c++) {
            if (level[r][c] == 'O') {
                drawSaw(c * TILE + 30 - camI, r * TILE + 30);
            }
        }
    }

    // 8. Active Enemies
    for (int i = 0; i < enemyCount; i++) {
        const Enemy &e = enemies[i];
        if (!e.active)
            continue;
        int sx = (int)e.x - camI;
        if (sx < -60 || sx > FRAME_WIDTH + 60)
            continue;
        drawEnemyChar(e, sx, (int)e.y);
    }

    // 9. Boss Arena Objects & Projectiles
    if (bossActive && bossHP > 0) {
        int swX = 21 * TILE + 20 - camI;
        int swY = 7 * TILE - 18;
        Color swCol = (bossSwitchCooldown == 0) ?
                      ((tick & 8) ? mk(80, 255, 120) : mk(40, 200, 85)) :
                      mk(90, 80, 100);
        fillRect(swX, swY, 80, 18, swCol);

        int bx = (int)bossX - camI;
        int by = (int)bossY;

        if (bossBeamTimer > 0) {
            int beamX = bx + bossW / 2 - 16;
            fillRect(beamX, 7 * TILE, 32, by - 7 * TILE + 20, mk(120, 255, 220));
        }

        drawBossChar(bx, by, bossW, bossH, (bossHurt & 2) != 0);
    }

    for (int i = 0; i < MAX_BOSS_SHOTS; i++) {
        BossShot &s = bossShots[i];
        if (!s.active)
            continue;
        int sx = (int)s.x - camI;
        int sy = (int)s.y;
        fillRect(sx - 14, sy - 14, 28, 28, mk(255, 95, 35));
        fillRect(sx - 8, sy - 8, 16, 16, mk(255, 235, 90));
    }

    // 10. Player Weapon Bullets
    for (int i = 0; i < MAX_BULLETS; i++) {
        const Bullet &bl = bullets[i];
        if (!bl.active)
            continue;
        int sx = (int)bl.x - camI;
        int sy = (int)bl.y;

        if (bl.kind == W_BLASTER) {
            fillRect(sx - 10, sy - 5, 20, 10, mk(80, 220, 255));
            fillRect(sx - 6, sy - 3, 12, 6, mk(255, 255, 255));
        } else if (bl.kind == W_SHOTGUN) {
            fillRect(sx - 7, sy - 7, 14, 14, mk(255, 195, 55));
            fillRect(sx - 4, sy - 4, 8, 8, mk(255, 255, 220));
        } else if (bl.kind == W_SAW) {
            fillRect(sx - 12, sy - 12, 24, 24, mk(210, 220, 235));
            fillRect(sx - 6, sy - 6, 12, 12, mk(80, 245, 130));
        } else {
            fillRect(sx - 16, sy - 16, 32, 32, mk(235, 65, 215));
            fillRect(sx - 10, sy - 10, 20, 20, mk(255, 220, 255));
        }
    }

    // 11. Dust & Blood Particles
    for (int i = 0; i < MAX_PARTS; i++) {
        Part &p = parts[i];
        if (p.life <= 0)
            continue;

        int sx = (int)p.x - camI;
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
    }

    // 12. Player & Equipped Weapon Barrel
    if (deadTimer == 0) {
        int x = (int)px - camI;
        int y = (int)py;

        int dw = (int)(PW * squashX);
        int dh = (int)(PH * squashY);

        if (dw < 20) dw = 20;
        if (dh < 20) dh = 20;

        int dx = x + (PW - dw) / 2;
        int dy = y + (PH - dh);

        fillRect(dx, dy, dw, dh, mk(140, 0, 15));
        fillRect(dx + 3, dy + 3, dw - 6, dh - 6, mk(205, 25, 35));
        drawPlayerFace(dx, dy, dw, dh, facing);

        // Draw equipped weapon on Meat Cube's side
        Color gunCol = mk(95, 215, 255);
        if (curWeapon == W_SHOTGUN) gunCol = mk(255, 195, 70);
        else if (curWeapon == W_SAW) gunCol = mk(140, 250, 160);
        else if (curWeapon == W_PLASMA) gunCol = mk(255, 120, 235);

        int gx = (facing > 0) ? (dx + dw - 4) : (dx - 14);
        int gy = dy + dh / 2 - 4;
        fillRect(gx, gy, 18, 10, mk(45, 45, 55));
        fillRect(gx + (facing > 0 ? 6 : 2), gy + 2, 10, 6, gunCol);
    }

    // 13. HUD Overlay
    drawHUDFull(b);
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

    // ---------- Animated Gameplay & Boss Tutorial Cutscenes (art 10..15) ----------
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
        const char *lbl = "O: SHOOT";

        if (cyc < 75) {
            Enemy de = {0,0,0,0,0,0,0, E_WALKER, 2,2, (cyc > 55 ? 2 : 0), t, -1, true};
            drawEnemyChar(de, ax + 480, ay + 356);
        }
        if (cyc >= 25 && cyc < 70) {
            int bx = ax + 240 + (cyc - 25) * 6;
            fillRect(bx, ay + 372, 20, 10, mk(80, 220, 255));
        }

        if (cyc >= 110 && cyc < 160) {
            int jt = cyc - 110;
            cy = ay + 356 - (jt * (55 - jt)) / 4;
            if (jt > 32 && cy > ay + 246) cy = ay + 246;
            xOn = (jt < 34);
            lbl = "HOLD X";
        } else if (cyc >= 160) {
            cy = ay + 246;
            lbl = "GREAT!";
        }

        for (int i = 0; i < 7; i++) {
            int gx = ax + 110 + i * 80;
            int bend = (iabs(cx + 22 - gx) < 42 && cy > ay + 320) ? 12 : (SN[(t / 5 + i) & 15] / 32);
            drawPlantTuft(gx, ay + 400, 22, bend, 0, i % 3);
        }

        drawMiniDemoCube(cx, cy + (44 - dh), dw, dh, 1);
        drawPadPrompt(ax + 290, ay + 20, false, cyc < 165, cyc < 70, xOn, lbl);
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
        const char *lbl = "SHOTGUN";

        if (cyc >= 50 && cyc < 102) {
            int jt = cyc - 50;
            cy = ay + 356 - (jt * (52 - jt)) / 5;
            lbl = "SPRINT+X";
        }

        for (int i = 0; i < 5; i++) {
            int wx = ax + 80 + i * 70;
            int bend = (cx > wx - 25 && cx < wx + 70) ? 14 : (SN[(t / 5 + i) & 15] / 28);
            drawPlantTuft(wx, ay + 400, 26, bend, 1, i % 2);
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
        const char *lbl = "SAW GUN";

        if (cyc >= 50 && cyc < 106) {
            int jt = cyc - 50;
            cy = ay + 356 - (jt * (56 - jt)) / 4;
            lbl = "EDGE JUMP";
        }

        drawMiniDemoCube(cx, cy, 44, 44, 1);
        drawPadPrompt(ax + 290, ay + 20, false, cyc < 140, false, xOn, lbl);
        return;
    }

    // Animated Boss Fight Tutorial Cutscene (art 14 & 15)
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
        lbl = "STOMP+O!";
    } else {
        int bt = cyc - 72;
        cx = ax + 520 + (bt * 240) / 88;
        cy = ay + 262 - (bt * (70 - bt)) / 6;
        if (cy > ay + 366) cy = ay + 366;
        lbl = "BOSS HIT!";
    }

    drawMiniDemoCube(cx, cy, 44, 44, 1);
    drawPadPrompt(ax + 290, ay + 20, false, true, true, xOn, lbl);
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

    // Unlock weapons matching the stage progression automatically
    if (currentLevel >= 1 && unlockedWeapons < 2) unlockedWeapons = 2;
    if (currentLevel >= 3 && unlockedWeapons < 3) unlockedWeapons = 3;
    if (currentLevel >= 5 && unlockedWeapons < 4) unlockedWeapons = 4;

    buildLevel();

    roomIdx = 0;
    respawn();

    deaths = 0;
    splatCount = 0;
    winTimer = 0;
    deadTimer = 0;
    justUnlockedWeapon = false;

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
