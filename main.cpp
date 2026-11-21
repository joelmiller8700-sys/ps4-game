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

#define FW 1920
#define FH 1080
#define TILE 60
#define RCOLS 32
#define ROOMS 6
#define COLS (RCOLS * ROOMS)
#define ROWS 18
#define WW (COLS * TILE)
#define WH (ROWS * TILE)
#define PW 44
#define PH 44

#define TOTAL_STAGES 17
#define MAX_ENEMIES 64
#define MAX_SHOTS 20
#define MAX_COINS 96
#define MAX_CHESTS 12
#define MAX_POWERUPS 16
#define MAX_MPLAT 16
#define MAX_DPLAT 24
#define MAX_NPCS 6
#define MAX_CHECKS 6
#define MAX_PARTS 140
#define MAX_SPLATS 260
#define MAX_PLANTS 620
#define MAX_LEAVES 24
#define MAX_FIREFLIES 18
#define MAX_DIRTY 512
#define SPLAT_MAX_AGE 5400

std::stringstream debugLogStream;
static Scene2D *scene;
static char level[ROWS][COLS + 1];
static int padHandle = -1, padUserID = 0;
static OrbisPadData padData;
#ifndef NO_AUDIO
static int audioHandle = -1;
#endif

// ---------- Save Data & Progression ----------
struct SaveData {
    unsigned int magic;
    int unlockedStages, coins;
    int upHp, upDash, upMagnet, upSlam;
    unsigned int skinsOwned;
    int selectedSkin; // 0..4 = Meat Cubes, 5..9 = Secret Naruto Roster
    unsigned int achMask;
    int bestTime[TOTAL_STAGES];
    int secretsFound;
};

static SaveData save = {
    0x4D433236u, 1, 0, 0, 0, 0, 0, 1u, 0, 0u,
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, 0
};

static void loadSave() {
    FILE *f = fopen("/data/meat_cube_save.dat", "rb");
    if (!f) return;
    SaveData tmp;
    if (fread(&tmp, sizeof(SaveData), 1, f) == 1 && tmp.magic == 0x4D433236u) {
        save = tmp;
        if (save.unlockedStages < 1) save.unlockedStages = 1;
        if (save.unlockedStages > TOTAL_STAGES) save.unlockedStages = TOTAL_STAGES;
        save.skinsOwned |= 1u;
    }
    fclose(f);
}

static void writeSave() {
    FILE *f = fopen("/data/meat_cube_save.dat", "wb");
    if (!f) return;
    fwrite(&save, sizeof(SaveData), 1, f);
    fclose(f);
}

// ---------- Game World & Player State ----------
static float px, py, vx, vy, squashX = 1.0f, squashY = 1.0f, cpX = 80.0f, cpY = 900.0f;
static int roomIdx = 0, levelCols = COLS, levelPW = WW, curStage = 0, curWorld = 0;
static int hp = 3, maxHp = 3, invulnTimer = 0, jumpsLeft = 2, dashTimer = 0, dashCd = 0;
static bool slamActive = false, onGround = false, timeTrialMode = false;
static int slamShockTimer = 0, shieldTimer = 0, speedTimer = 0, animTick = 0;
static int coyote = 0, wallLock = 0, facing = 1, winTimer = 0, deadTimer = 0, tick = 0, frameID = 0;
static int stageTimeFrames = 0, shakeTimer = 0, shakeMag = 0;
static int toastTid = -1, toastTimer = 0, npcDialogTid = -1, npcDialogTimer = 0;
static unsigned int rng = 12345;

struct Ghost { float x, y; int life, dir; };
static Ghost ghosts[8];

static int checkC[MAX_CHECKS], checkR[MAX_CHECKS], checkCount = 0;
static bool checkAct[MAX_CHECKS];

enum { E_SPIDER = 0, E_BAT = 1, E_MUSHROOM = 2, E_ELITE = 3 };
struct Enemy { float x, y, vx, vy, minX, maxX, baseY; int kind, hp, maxHp, hurt, timer, dir, state; bool active; };
static Enemy enemies[MAX_ENEMIES];
static int enemyCount = 0;

static bool bossActive = false;
static int bossType = 0, bossW = 120, bossH = 112, bossHP = 10, bossMaxHP = 10;
static int bossHurt = 0, bossTimer = 0, bossState = 0, bossTongueLen = 0, bossSwCd = 0;
static float bossX, bossY, bossVX, bossVY;

struct Shot { float x, y, vx, vy; int kind; bool active; };
static Shot shots[MAX_SHOTS];

struct Coin { short room, x, y; bool active; };
static Coin coins[MAX_COINS]; static int coinCount = 0;

struct Chest { short room, x, y; bool opened, secret; };
static Chest chests[MAX_CHESTS]; static int chestCount = 0;

enum { PU_HEART = 0, PU_SHIELD = 1, PU_SPEED = 2 };
struct PowerUp { short room, x, y; int kind; bool active; };
static PowerUp powerups[MAX_POWERUPS]; static int puCount = 0;

struct MPlat { short room; float x, y, minX, maxX, vx; short w; };
static MPlat mplats[MAX_MPLAT]; static int mplatCount = 0;

struct DPlat { short room, x, y, w; int timer, state; };
static DPlat dplats[MAX_DPLAT]; static int dplatCount = 0;

struct NPC { short room, x, y; int dialogTid; };
static NPC npcs[MAX_NPCS]; static int npcCount = 0;

struct Splat { short room, x, y, w, h; int born; unsigned char stage, dStage[2]; bool drawn[2]; };
static Splat splats[MAX_SPLATS]; static int splatCount = 0;

struct Part { float x, y, vx, vy; short life, maxLife; unsigned char kind, size; };
static Part parts[MAX_PARTS];

struct Plant { short room, x, y; float angle, vel; unsigned char h, style, seed; signed char dBend[2]; bool drawn[2]; };
static Plant plants[MAX_PLANTS]; static int plantCount = 0;

struct Leaf { float x, y, vx, vy; unsigned char sz, seed; };
static Leaf leaves[MAX_LEAVES];

struct Firefly { float x, y, vx, vy; unsigned char phase; };
static Firefly flies[MAX_FIREFLIES];

struct Rect { int x, y, w, h; };
static Rect dirty[2][MAX_DIRTY];
static int dirtyCnt[2] = {0, 0}, bufRoom[2] = {0, 0};
static bool needFull[2] = {true, true}, needWin[2] = {false, false}, hudNeed[2] = {false, false};
static char bufGrid[2][ROWS][RCOLS];
static bool bufHi[2][ROWS][RCOLS];
static int clipX0 = 0, clipY0 = 0, clipX1 = FW, clipY1 = FH;

static const int CS[16] = {256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98,0,98,181,237};
static const int SN[16] = {0,98,181,237,256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98};

// ---------- UI & Menus State ----------
enum { S_MENU = 0, S_LEVELS = 1, S_SHOP = 2, S_SKINS = 3, S_ACH = 4, S_SETTINGS = 5, S_CREDITS = 6, S_PLAY = 7, S_STORY = 8, S_SECRET = 9 };
static int state = S_MENU, menuSel = 0, levelSel = 0, shopSel = 0, skinSel = 0, settingsSel = 0, secretSel = 0;
static int controlMode = 1, lang = 1;
static bool introSeen = false, uiFull[2] = {true, true}, pend[2][64];
static int storyId = 0, storyPage = 0, typed = 0, storyAnimTick = 0, storyDrawnPage[2] = {-1, -1}, storyShown[2] = {0, 0};
static bool storyBgDone[2] = {false, false};

#define TEXT_LIST \
    X(T_TITLE,"MEAT CUBE",AR(TITLE)) X(T_PLAY,"PLAY ADVENTURE",AR(PLAY)) X(T_SHOP,"UPGRADE SHOP",AR(SHOP)) \
    X(T_SKINS,"SKINS",AR(SKINS)) X(T_ACH,"ACHIEVEMENTS",AR(ACH)) X(T_TIMETRIAL,"TIME TRIAL",AR(TIMETRIAL)) \
    X(T_SETTINGS,"SETTINGS",AR(SETTINGS)) X(T_CREDITS,"CREDITS",AR(CREDITS)) X(T_EXIT,"EXIT",AR(EXIT)) \
    X(T_BACK,"BACK",AR(BACK)) X(T_B_LEVELS,"WORLDS & STAGES",AR(B_LEVELS)) X(T_B_SHOP,"UPGRADE SHOP",AR(B_SHOP)) \
    X(T_B_SKINS,"CHARACTER SKINS",AR(B_SKINS)) X(T_B_ACH,"ACHIEVEMENTS",AR(B_ACH)) X(T_B_SETTINGS,"SETTINGS",AR(B_SETTINGS)) \
    X(T_B_CREDITS,"CREDITS",AR(B_CREDITS)) X(T_CTRL_A,"CONTROLS: ANALOG",AR(CTRL_ANALOG)) X(T_CTRL_B,"CONTROLS: ARROWS",AR(CTRL_ARROWS)) \
    X(T_LANG,"LANGUAGE: ENGLISH",AR(LANG)) X(T_H_SELECT,"X: SELECT   O: BACK",AR(SELECT)) \
    X(T_H_LEVELS,"X: PLAY   SQ: TIME TRIAL   O: BACK",AR(LEVELS_HINT)) X(T_H_BACK,"O: BACK",AR(BACK_HINT)) \
    X(T_TIP,"LEFT RIGHT OR X: CHANGE",AR(CHANGE_TIP)) X(T_H_STORY,"X: NEXT   OPTIONS: SKIP",AR(STORY_HINT)) \
    X(T_PLAYX,"PRESS X TO PLAY",AR(PLAYX)) X(T_LOCKED,"LOCKED",AR(LOCKED)) X(T_DESIGNERS,"GAME DESIGNERS",AR(DESIGNERS)) \
    X(T_KAREEM,"KAREEM",AR(KAREEM)) X(T_CLAUDE,"CLAUDE",AR(CLAUDE)) X(T_GAME,"MEAT CUBE ADVENTURE",AR(GAME_NAME)) \
    X(T_COMPLETE,"STAGE COMPLETE!",AR(COMPLETE)) X(T_GREAT,"LEGENDARY JOB!",AR(GREAT)) \
    X(T_W0,"WORLD 1: LIVING FOREST",AR(W_FOREST)) X(T_W1,"WORLD 2: VOLCANO CORE",AR(W_VOLCANO)) \
    X(T_W2,"WORLD 3: FROZEN PEAKS",AR(W_ICE)) X(T_W3,"WORLD 4: DARK CASTLE",AR(W_CASTLE)) X(T_W4,"SECRET: SHADOW TEMPLE",AR(W_SECRET)) \
    X(T_BFROG,"KING FROGUS",AR(B_FROGUS)) X(T_BSPID,"VENOM SPIDER QUEEN",AR(B_SPIDER)) \
    X(T_BDEV,"FROST DEVOURER",AR(B_DEVOURER)) X(T_BOVER,"DARK OVERLORD",AR(B_OVERLORD)) \
    X(T_UP_HP,"+1 MAX HEART (30 COINS)",AR(UP_HP)) X(T_UP_DASH,"FASTER DASH (35 COINS)",AR(UP_DASH)) \
    X(T_UP_MAG,"COIN MAGNET (40 COINS)",AR(UP_MAGNET)) X(T_UP_SLAM,"SUPER GROUND SLAM (45 COINS)",AR(UP_SLAM)) \
    X(T_BOUGHT,"OWNED",AR(BOUGHT)) X(T_NEED,"NOT ENOUGH COINS",AR(NEED_COINS)) \
    X(T_SK0,"CLASSIC RED CUBE",AR(SK_0)) X(T_SK1,"ROYAL GOLD CUBE",AR(SK_1)) X(T_SK2,"SHADOW NINJA CUBE",AR(SK_2)) \
    X(T_SK3,"CRYSTAL ICE CUBE",AR(SK_3)) X(T_SK4,"MAGMA LAVA CUBE",AR(SK_4)) \
    X(T_SEC_FND,"SECRET ROOM DISCOVERED!",AR(SECRET_FOUND)) X(T_CHEST,"TREASURE! +25 COINS",AR(CHEST_OPEN)) \
    X(T_NPC_H,"TRIANGLE: TALK",AR(NPC_HINT)) \
    X(T_NPC0,"SAGE: USE DOUBLE JUMP (X) AND DASH (O) TO FIND SECRETS!",AR(NPC_0)) \
    X(T_NPC1,"SCOUT: PRESS DOWN + SQUARE IN AIR FOR GROUND SLAM!",AR(NPC_1)) \
    X(T_NPC2,"GUARD: HIDDEN CHESTS LIE ABOVE SECRET LEDGES!",AR(NPC_2)) \
    X(T_NPC3,"MERCHANT: SPEND YOUR COINS IN THE SHOP FOR UPGRADES!",AR(NPC_3)) \
    X(T_ACH0,"FIRST STEPS: CLEAR STAGE 1",AR(ACH_0)) X(T_ACH1,"TREASURE HUNTER: OPEN A CHEST",AR(ACH_1)) \
    X(T_ACH2,"BOSS SLAYER: DEFEAT A BOSS",AR(ACH_2)) X(T_ACH3,"RICH HERO: COLLECT 50 COINS",AR(ACH_3)) \
    X(T_ACH4,"EXPLORER: FIND A SECRET ROOM",AR(ACH_4)) X(T_ACH5,"SPEEDRUNNER: BEAT TIME TRIAL",AR(ACH_5)) \
    X(T_ACH_UNL,"ACHIEVEMENT UNLOCKED!",AR(ACH_UNLOCKED))

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

struct Page { int art, n; const char *en[3]; int ar[3]; };
static const Page PAGES[5] = {
    {0,2,{"MEAT CUBE AND BANDAGE GIRL","LIVED IN THE LIVING FOREST.",""},{AR(P0L0),AR(P0L1),-1}},
    {1,2,{"MONSTERS INVADED THE FOUR WORLDS","AND TOOK HER TO THE DARK CASTLE.",""},{AR(P1L0),AR(P1L1),-1}},
    {2,2,{"WITH DOUBLE JUMP, DASH AND SLAM","I WILL SAVE YOU ACROSS ALL WORLDS!",""},{AR(P2L0),AR(P2L1),-1}},
    {3,3,{"MEAT CUBE DEFEATED THE DARK OVERLORD!","PEACE RETURNED TO ALL FOUR WORLDS.","CAN YOU FIND THE SECRET SHADOW TEMPLE?"},{AR(P3L0),AR(P3L1),AR(P3L2)}},
    {0,1,{"TRUE ENDING - THANK YOU FOR PLAYING!","",""},{AR(P4L0),-1,-1}}
};
static const int STORY_FIRST[2] = {0, 3}, STORY_LAST[2] = {3, 5};

// ---------- Input (With L2 + R2 + L1 + X Secret Combo) ----------
struct Input {
    bool up, down, left, right, dleft, dright;
    bool cross, circle, square, triangle, options, r1, l1, l2, r2;
    bool crossNew, circleNew, squareNew, triNew, optionsNew, dashNew;
    float ana;
};
static Input in;
static bool pCross = false, pCircle = false, pSquare = false, pTri = false, pOpt = false, pDash = false;
static int holdCnt[4] = {0, 0, 0, 0};

static inline int iabs(int a) { return a < 0 ? -a : a; }
static inline int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int rnd(int n) { rng = rng * 1103515245u + 12345u; return (int)((rng >> 16) % (unsigned int)n); }
static inline Color mk(int r, int g, int b) { Color c = {(unsigned char)r, (unsigned char)g, (unsigned char)b}; return c; }
static inline bool useAr() { return lang == 1 && ARABIC_OK; }
static inline bool isBossStage(int s) { return s < 16 && (s % 4) == 3; }

static bool initPad() {
    if (scePadInit() != 0) return false;
    OrbisUserServiceInitializeParams param;
    param.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    sceUserServiceInitialize(&param);
    sceUserServiceGetInitialUser(&padUserID);
    padHandle = scePadOpen(padUserID, 0, 0, NULL);
    return padHandle >= 0;
}

static void readInput() {
    scePadReadState(padHandle, &padData);
    unsigned int b = padData.buttons;
    int lx = (int)padData.leftStick.x - 128, ly = (int)padData.leftStick.y - 128;
    int rx = (int)padData.rightStick.x - 128, ry = (int)padData.rightStick.y - 128;
    in.dleft = (b & ORBIS_PAD_BUTTON_LEFT) != 0; in.dright = (b & ORBIS_PAD_BUTTON_RIGHT) != 0;
    in.up = (b & ORBIS_PAD_BUTTON_UP) || ly < -60 || ry < -60;
    in.down = (b & ORBIS_PAD_BUTTON_DOWN) || ly > 60 || ry > 60;
    in.left = in.dleft || lx < -60 || rx < -60; in.right = in.dright || lx > 60 || rx > 60;
    float a = (iabs(rx) > iabs(lx) ? rx : lx) / 110.0f;
    if (a > 1.0f) a = 1.0f; else if (a < -1.0f) a = -1.0f;
    if (a > -0.2f && a < 0.2f) a = 0.0f;
    in.ana = a;
    in.cross = (b & ORBIS_PAD_BUTTON_CROSS) != 0; in.circle = (b & ORBIS_PAD_BUTTON_CIRCLE) != 0;
    in.square = (b & ORBIS_PAD_BUTTON_SQUARE) != 0; in.triangle = (b & ORBIS_PAD_BUTTON_TRIANGLE) != 0;
    in.options = (b & ORBIS_PAD_BUTTON_OPTIONS) != 0;
    in.r1 = (b & ORBIS_PAD_BUTTON_R1) != 0; in.l1 = (b & ORBIS_PAD_BUTTON_L1) != 0;
    in.l2 = ((b & ORBIS_PAD_BUTTON_L2) != 0) || (padData.analogButtons.l2 > 35);
    in.r2 = ((b & ORBIS_PAD_BUTTON_R2) != 0) || (padData.analogButtons.r2 > 35);
    bool dashBtn = in.circle || in.r1;
    in.crossNew = in.cross && !pCross; in.circleNew = in.circle && !pCircle;
    in.squareNew = in.square && !pSquare; in.triNew = in.triangle && !pTri;
    in.optionsNew = in.options && !pOpt; in.dashNew = dashBtn && !pDash;
    pCross = in.cross; pCircle = in.circle; pSquare = in.square; pTri = in.triangle; pOpt = in.options; pDash = dashBtn;
}

static bool navEvent(int i, bool p) {
    if (!p) { holdCnt[i] = 0; return false; }
    holdCnt[i]++;
    return holdCnt[i] == 1 || (holdCnt[i] > 20 && ((holdCnt[i] - 20) % 6) == 0);
}

// ---------- Multi-World Halal Audio Engine ----------
static int curSfx = -1, sfxPos = 0, sfxLen = 0, sfxPhase = 0, bgmSample = 0, humP1 = 0, humP2 = 0, duffP = 0;
static unsigned int noiseRng = 987654321u;

static void playToneImmediate(int freq, int ms, int vol) {
#ifndef NO_AUDIO
    if (audioHandle < 0) return;
    static short buf[512];
    int total = 48 * ms, ph = 0, done = 0;
    while (done < total) {
        for (int i = 0; i < 256; i++) {
            int idx = done + i, s = 0;
            if (idx < total) {
                if ((ph += freq) >= 48000) ph -= 48000;
                int tri = ph < 24000 ? ph : 48000 - ph;
                s = ((tri - 12000) * vol) / 12000;
                int rem = total - idx;
                if (rem < 1200) s = (s * rem) / 1200;
            }
            buf[i * 2] = buf[i * 2 + 1] = (short)s;
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
    if (audioHandle < 0) audioHandle = sceAudioOutOpen(padUserID, 0, 0, 256, 48000, 1);
    if (audioHandle < 0) audioHandle = sceAudioOutOpen(padUserID, 1, 0, 256, 48000, 1);
    if (audioHandle >= 0) {
        int vol[8] = {32768,32768,32768,32768,32768,32768,32768,32768};
        sceAudioOutSetVolume(audioHandle, 3, vol);
    }
#endif
}

static void snd(int k) {
    if (state != S_PLAY) {
        if (k == 0) playToneImmediate(880, 22, 15000);
        else if (k == 1) { playToneImmediate(660, 32, 17000); playToneImmediate(990, 48, 17000); }
        else if (k == 2) playToneImmediate(440, 40, 15000);
        else playToneImmediate(220, 75, 15000);
        return;
    }
    static const int LENS[12] = {1200, 3600, 2400, 3200, 2600, 3200, 6200, 5200, 12000, 2200, 5800, 650};
    curSfx = k; sfxPos = 0; sfxPhase = 0; sfxLen = LENS[iclamp(k, 0, 11)];
}

static void updateGameplayAudio() {
#ifndef NO_AUDIO
    if (audioHandle < 0 || state != S_PLAY) return;
    static short outBuf[512];
    static const int W_HB[5][4] = {{110,130,146,130},{98,116,130,110},{130,146,165,146},{92,104,116,98},{123,146,165,138}};
    static const int W_HH[5][4] = {{165,195,220,195},{146,174,195,165},{195,220,247,220},{138,155,174,146},{185,220,247,207}};
    static const unsigned char PAT[5][16] = {
        {1,0,3,2,1,0,2,0,1,1,3,2,0,2,3,2},{1,1,2,0,1,0,2,2,1,1,2,0,1,2,3,2},
        {1,0,3,0,2,0,3,0,1,0,3,2,0,3,2,0},{1,0,2,1,0,2,1,0,1,2,0,2,1,1,2,2},{1,3,2,3,1,2,3,2,1,1,2,3,1,2,2,3}
    };
    int w = iclamp(curWorld, 0, 4);
    int stepS = bossActive ? 4800 : (w == 1 ? 5600 : (w == 2 ? 7000 : 6200));
    bool playBgm = (winTimer == 0);
    for (int blk = 0; blk < 2; blk++) {
        for (int i = 0; i < 256; i++) {
            int mix = 0;
            if (playBgm) {
                int step = (bgmSample / stepS) & 15, inS = bgmSample % stepS, bar = (bgmSample / (stepS * 16)) & 3;
                bgmSample++;
                unsigned char hit = PAT[w][step];
                if (hit == 1 && inS < 3000) {
                    int env = 3000 - inS;
                    if ((duffP += 66 + (env >> 6)) >= 48000) duffP -= 48000;
                    mix += ((duffP < 24000 ? duffP - 12000 : 36000 - duffP) * env) / 4200;
                } else if (hit == 2 && inS < 1300) {
                    int env = 1300 - inS; noiseRng = noiseRng * 1664525u + 1013904223u;
                    int nz = ((int)(noiseRng >> 20) & 511) - 256;
                    if ((duffP += 290) >= 48000) duffP -= 48000;
                    mix += ((nz + (duffP < 24000 ? 260 : -260)) * env) / 300;
                } else if (hit == 3 && inS < 700) {
                    noiseRng = noiseRng * 1664525u + 1013904223u;
                    mix += ((((int)(noiseRng >> 22) & 255) - 128) * (700 - inS)) / 340;
                }
                int f1 = W_HB[w][bar] + (bossActive ? 24 : 0), f2 = W_HH[w][bar] + (bossActive ? 36 : 0);
                if ((humP1 += f1) >= 48000) humP1 -= 48000;
                if ((humP2 += f2) >= 48000) humP2 -= 48000;
                int v1 = humP1 < 24000 ? humP1 - 12000 : 36000 - humP1;
                int v2 = humP2 < 24000 ? humP2 - 12000 : 36000 - humP2;
                int br = inS < (stepS / 2) ? inS : (stepS - inS);
                mix += ((v1 + v2) * (1300 + br / 6)) / 10000;
            }
            if (curSfx >= 0) {
                int rem = sfxLen - sfxPos;
                if (rem <= 0) curSfx = -1;
                else {
                    int f = 440, vol = 13500;
                    if (curSfx == 0) { f = 740; vol = 11000; }
                    else if (curSfx == 1) f = sfxPos < 1600 ? 580 : 920;
                    else if (curSfx == 4) { f = 270 + (sfxPos >> 3); vol = 12500; }
                    else if (curSfx == 5) { f = 540 - (sfxPos >> 4); vol = 14000; }
                    else if (curSfx == 6) { f = iclamp(165 - (sfxPos >> 6), 55, 200); vol = 16000; }
                    else if (curSfx == 7) { f = iclamp(240 - (sfxPos >> 5), 70, 260); vol = 16500; }
                    else if (curSfx == 8) { f = sfxPos < 4000 ? 440 : (sfxPos < 8000 ? 550 : 660); vol = 16000; }
                    else if (curSfx == 9) { f = sfxPos < 900 ? 880 : 1180; vol = 13500; }
                    else if (curSfx == 10) { f = iclamp(140 - (sfxPos >> 5), 48, 160); vol = 18000; }
                    else if (curSfx == 11) { f = 140; vol = 4200; }
                    if ((sfxPhase += f) >= 48000) sfxPhase -= 48000;
                    int tri = sfxPhase < 24000 ? sfxPhase : 48000 - sfxPhase;
                    int s = ((tri - 12000) * vol) / 12000;
                    if (curSfx == 5 || curSfx == 6 || curSfx == 7 || curSfx == 10) {
                        noiseRng = noiseRng * 1664525u + 1013904223u;
                        s += (((int)(noiseRng >> 19) & 1023) - 512) * (curSfx == 10 ? 9 : 5);
                    }
                    if (rem < 1000) s = (s * rem) / 1000;
                    mix += s; sfxPos++;
                }
            }
            outBuf[i * 2] = outBuf[i * 2 + 1] = (short)iclamp(mix, -30000, 30000);
        }
        sceAudioOutOutput(audioHandle, outBuf);
    }
#endif
}

// ---------- Drawing Primitives & Text ----------
static inline void setClip(int x, int y, int w, int h) {
    clipX0 = x < 0 ? 0 : x; clipY0 = y < 0 ? 0 : y;
    clipX1 = (x + w) > FW ? FW : (x + w); clipY1 = (y + h) > FH ? FH : (y + h);
}
static inline void resetClip() { clipX0 = 0; clipY0 = 0; clipX1 = FW; clipY1 = FH; }

static void fillRect(int x, int y, int w, int h, Color c) {
    if (w <= 0 || h <= 0) return;
    int x1 = x + w, y1 = y + h;
    if (x < clipX0) x = clipX0; if (y < clipY0) y = clipY0;
    if (x1 > clipX1) x1 = clipX1; if (y1 > clipY1) y1 = clipY1;
    for (int j = y; j < y1; j++) for (int i = x; i < x1; i++) scene->DrawPixel(i, j, c);
}

static void fillCircle(int cx, int cy, int r, Color c) {
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        int py0 = cy + dy; if (py0 < clipY0 || py0 >= clipY1) continue;
        for (int dx = -r; dx <= r; dx++) {
            int px0 = cx + dx;
            if (px0 >= clipX0 && px0 < clipX1 && dx * dx + dy * dy <= r2) scene->DrawPixel(px0, py0, c);
        }
    }
}

static const unsigned char FONT[45][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x11,0x19,0x15,0x13,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},{0x11,0x11,0x11,0x15,0x15,0x15,0x0A},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    {0x00,0x04,0x04,0x00,0x04,0x04,0x00},{0x00,0x00,0x00,0x1F,0x00,0x00,0x00},{0x02,0x04,0x08,0x10,0x08,0x04,0x02},
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08},{0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04},{0x00,0x00,0x00,0x00,0x0C,0x04,0x08},{0x0E,0x11,0x01,0x02,0x04,0x00,0x04}
};

static int glyphIndex(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= '0' && ch <= '9') return 26 + ch - '0';
    static const char *sym = ":-<> .!,?";
    for (int i = 0; sym[i]; i++) if (ch == sym[i]) return 36 + i;
    return 40;
}

static void drawText(int x, int y, const char *s, int sc, Color c) {
    for (; *s; s++, x += 6 * sc) {
        int g = glyphIndex(*s); if (g == 40) continue;
        for (int r = 0; r < 7; r++)
            for (int col = 0; col < 5; col++)
                if (FONT[g][r] & (0x10 >> col)) fillRect(x + col * sc, y + r * sc, sc, sc, c);
    }
}
static int textWidth(const char *s, int sc) { int n = (int)strlen(s); return n > 0 ? n * 6 * sc - sc : 0; }
static void drawTextC(int cx, int y, const char *s, int sc, Color c) { drawText(cx - textWidth(s, sc) / 2, y, s, sc, c); }
static void intToStr(int v, char *o) { sprintf(o, "%d", v); }

static int arW(int id) {
#if ARABIC_OK
    return id >= 0 ? AR_IMGS[id].w : 0;
#else
    (void)id; return 0;
#endif
}
static int arH(int id) {
#if ARABIC_OK
    return id >= 0 ? AR_IMGS[id].h : 0;
#else
    (void)id; return 0;
#endif
}
static void drawArCols(int id, int x, int y, int c0, int c1, Color col) {
#if ARABIC_OK
    if (id < 0) return;
    const ArImg &im = AR_IMGS[id]; int rb = (im.w + 7) / 8;
    if (c0 < 0) c0 = 0; if (c1 > im.w) c1 = im.w;
    for (int cx = c0; cx < c1; cx++)
        for (int ry = 0; ry < im.h; ry++)
            if (im.bits[ry * rb + (cx >> 3)] & (0x80 >> (cx & 7))) scene->DrawPixel(x + cx, y + ry, col);
#else
    (void)id; (void)x; (void)y; (void)c0; (void)c1; (void)col;
#endif
}
static void drawLabel(int tid, int cx, int by, int bh, int sc, Color c) {
    if (useAr() && TXT_AR[tid] >= 0) { int id = TXT_AR[tid]; drawArCols(id, cx - arW(id) / 2, by + (bh - arH(id)) / 2, 0, arW(id), c); }
    else drawTextC(cx, by + (bh - 7 * sc) / 2, TXT_EN[tid], sc, c);
}
static int labelWidth(int tid, int sc) { return (useAr() && TXT_AR[tid] >= 0) ? arW(TXT_AR[tid]) : textWidth(TXT_EN[tid], sc); }

static void unlockAch(int idx) {
    if (idx < 0 || idx >= 6) return;
    if (!(save.achMask & (1u << idx))) { save.achMask |= (1u << idx); toastTid = T_ACH_UNL; toastTimer = 180; writeSave(); snd(1); }
}

// ---------- High-Resolution 20x32 Pixel-Art Secret Naruto Characters ----------
static void drawNarutoHD(int idx, int x, int y, int ps, int dirFace) {
    // 0=OBITO, 1=PAIN, 2=ITACHI, 3=MADARA, 4=KAKASHI
    static const char *SPR[5][32] = {
        { // 0: OBITO (Orange Spiral Mask + Akatsuki Cloak)
            "......KKKKKKKK......",".....KNNNNNNNNK.....","....KNNKOOOOOKNK....","....KNKOoYYoOOKK....",
            "...KNNKOoKKoOOKK....","...KNNKOYOOoOOKK....","....KNKOoOOoOOKK....",".....KKOOOOOOKK.....",
            "......KKKKKKKK......",".....KNNNRRNNNK.....","....KNNNNRRNNNNK....","...KNNnNNRRNNnNNK...",
            "...KNNnNNRRNNnNNK...","..KNNnNWWWWWNNnNNK..","..KNNnWRRRRRWNnNNK..",".KNNnNWRRRRRWNnNNK..",".KNNnNNWWWWWNNnNNK..",
            ".KRRnNNNNRRNNNNnNK..",".KNNnNNNNRRNNNNnNK..",".KNNnNNNNRRNNWWNnK..","..KNnNNNNRRNWRRNnK..","..KNnNNWWWRNWRRNnK..",
            "..KnnNWRRRRNWWNnK...","..KnnNWRRRRNNNnNK...","..KNNNNWWWNNNNnNK...","...KNNNKKKKNNnNK....","....KKK....KKKK.....",
            ".....KWWK..KWWK.....",".....KWWK..KWWK.....","....KNNNK..KNNNK....","....KSSSK..KSSSK....","....KKKKK..KKKKK...."
        },
        { // 1: PAIN (Spiky Orange Hair + Rinnegan + Akatsuki Cloak)
            ".......K..K..K......","......KOKKOKKOK.....",".....KOOOKOOOKOK....","....KOOYOOOYOOOK....",
            "...KOOYOOOOOOYOOK...","....KOOOOOOOOOOK....","....KKBBBBBBBBKK....","....KKSBKBBKBSKK....",
            "....KSSPPSSPPSSK....",".....KSSKSSKSSK.....","......KSSSSSSK......",".....KNNNRRNNNK.....",
            "....KNNNNRRNNNNK....","...KNNnNNRRNNnNNK...","...KNNnNNRRNNnNNK...","..KNNnNWWWWWNNnNNK..",
            "..KNNnWRRRRRWNnNNK..",".KNNnNWRRRRRWNnNNK..",".KRRnNNWWWWWNNnNNK..",".KNNnNNNNRRNNNNnNK..",
            "..KNnNNNNRRNNWWNnK..","..KNnNNWWWRNWRRNnK..","..KnnNWRRRRNWWNnK...","..KnnNWRRRRNNNnNK...",
            "..KNNNNWWWNNNNnNK...","...KNNNKKKKNNnNK....","....KKK....KKKK.....",".....KWWK..KWWK.....",
            ".....KWWK..KWWK.....","....KNNNK..KNNNK....","....KSSSK..KSSSK....","....KKKKK..KKKKK...."
        },
        { // 2: ITACHI (Dark Parted Hair + Sharingan + Akatsuki Cloak)
            "......KKKKKKKK......",".....KNNNNNNNNK.....","....KNNnNNNNnNNK....","....KNNBBBBBBNNK....",
            "...KNNnBKBBKBnNNK...","...KNNKSSSSSSKNNK...","...KNNKSRSSRSKNNK...","...KNNKSSSSSSKNNK...",
            "....KNKKSSSSKKNK....",".....KNNNRRNNNK.....","....KNNNNRRNNNNK....","...KNNnNNRRNNnNNK...",
            "...KNNnNNRRNNnNNK...","..KNNnNWWWWWNNnNNK..","..KNNnWRRRRRWNnNNK..",".KNNnNWRRRRRWNnNNK..",
            ".KRRnNNWWWWWNNnNNK..",".KSSnNNNNRRNNNNnNK..",".KKKnNNNNRRNNWWNnK..","..KNnNNNNRRNWRRNnK..",
            "..KNnNNWWWRNWRRNnK..","..KnnNWRRRRNWWNnK...","..KnnNWRRRRNNNnNK...","..KNNNNWWWNNNNnNK...",
            "...KNNNKKKKNNnNK....","....KKK....KKKK.....",".....KWWK..KWWK.....",".....KWWK..KWWK.....",
            ".....KWWK..KWWK.....","....KNNNK..KNNNK....","....KSSSK..KSSSK....","....KKKKK..KKKKK...."
        },
        { // 3: MADARA (Wild Long Spiky Black Hair + Red Samurai Armor + Sharingan)
            ".....K..KKKK..K.....","....KNKNNNNNNKNK....","...KNNNNNNNNNNNNK...","..KNNnNNNNNNNNnNNK..",
            ".KNNnNNKSSSSKNNnNNK.","..KNNnKSRSSRSKnNNK..",".KNNnNKSSSSSSKnNNK..","..KNNnKKSSSSKKnNNK..",
            ".KNNnKMMMMMMMMKnNK..","..KNnKMmMMMMmMKnNK..",".KNNnKMMMMMMMMKnNK..","..KNnKMMKMMKMMKnNK..",
            ".KNNnKMMMMMMMMKnNK..","..KNnKNNMMMMNNKnNK..",".KNNnKSKMMMMKKnNNK..","..KNnKKKmmmmKKnNNK..",
            ".KNNnKMMMMMMMMKnNK..","..KNnKMmMMMMmMKnNK..","..KNNKMMMMMMMMKNNK..","...KKKMmMMMMmMKKK...",
            "....KKMMMMMMMMKK....","....KNNKMMMMKNNK....","....KNNKmmmmKNNK....","....KNNK.KK.KNNK....",
            "....KNNK....KNNK....","....KNNK....KNNK....",".....KWWK..KWWK.....",".....KWWK..KWWK.....",
            ".....KWWK..KWWK.....","....KNNNK..KNNNK....","....KSSSK..KSSSK....","....KKKKK..KKKKK...."
        },
        { // 4: KAKASHI (Spiky Silver Hair + Tilted Headband + Mask + Olive Jonin Vest)
            "........K..K........","......KWKWWKWK......",".....KWWWWWWWWK.....","....KWWwWWWWwWWK....",
            "...KWWWWWWWWWWWWK...","....KwwWWWWWWwwK....","....KKNNNNBBBBKK....","....KNKSSKBBKBNK....",
            "....KNKSSKNNNNNK....",".....KKNNNNNNNKK....",".....KGGGGGGGGGK....","....KGGgGGGGgGGGK...",
            "...KNNKGGGGGGGKNNK..","..KNRRKGGKKGGKKNNK..","..KNRRKGgKKgGGKNNK..","..KNNNKGGGGGGGKNNK..",
            "..KNNNKGGggGGGKNNK..","..KNNNKGGGGGGGKNNK..","...KSSKNNNNNNNKSSK..","....KKKNNNNNNNKKK...",
            "......KNNNNNNNK.....",".....KNNWWWWWNNK....",".....KNNNNNNNNNK....",".....KNNWWWWWNNK....",
            ".....KNNKK..KNNK....",".....KNNK....KNNK...",".....KWWK....KWWK...",".....KWWK....KWWK...",
            ".....KWWK....KWWK...","....KNNNK....KNNNK..","....KSSSK....KSSSK..","....KKKKK....KKKKK.."
        }
    };
    int id = iclamp(idx, 0, 4);
    for (int r = 0; r < 32; r++) {
        const char *ln = SPR[id][r];
        for (int c = 0; c < 20; c++) {
            char ch = ln[c]; if (ch == '.') continue;
            int dc = (dirFace >= 0) ? c : (19 - c);
            Color col = mk(255, 255, 255);
            switch (ch) {
                case 'K': col = mk(12, 12, 16); break;
                case 'N': col = mk(24, 26, 38); break;
                case 'n': col = mk(44, 48, 66); break;
                case 'W': col = mk(228, 232, 244); break;
                case 'w': col = mk(168, 174, 192); break;
                case 'B': col = mk(182, 190, 206); break;
                case 'S': col = mk(238, 188, 152); break;
                case 's': col = mk(196, 144, 112); break;
                case 'R': col = mk(206, 36, 44); break;
                case 'r': col = mk(136, 22, 30); break;
                case 'M': col = mk(164, 34, 42); break;
                case 'm': col = mk(112, 22, 28); break;
                case 'O': col = mk(242, 112, 24); break;
                case 'o': col = mk(188, 62, 16); break;
                case 'Y': col = mk(255, 178, 52); break;
                case 'P': col = mk(168, 124, 224); break;
                case 'G': col = mk(98, 118, 82); break;
                case 'g': col = mk(68, 84, 56); break;
            }
            fillRect(x + dc * ps, y + r * ps, ps, ps, col);
        }
    }
}

// ---------- World & Stage Builders ----------
static int baseCol = 0;
static void put(int r, int c, char ch) { int g = baseCol + c; if (r >= 0 && r < ROWS && g >= 0 && g < COLS) level[r][g] = ch; }
static void plat(int r, int a, int b) { for (int c = a; c <= b; c++) put(r, c, '#'); }
static void wall(int c, int r0, int r1) { for (int r = r0; r <= r1; r++) put(r, c, '#'); }
static void spk(int r, int a, int b) { for (int c = a; c <= b; c++) put(r, c, '^'); }
static void sawAt(int r, int c) { put(r, c, 'O'); }
static void addCP(int c, int r) { if (checkCount < MAX_CHECKS) { checkC[checkCount] = baseCol + c; checkR[checkCount] = r; checkAct[checkCount++] = false; } }
static void addCoin(int c, int r) { if (coinCount < MAX_COINS) { int g = baseCol + c; coins[coinCount++] = {(short)(g / RCOLS), (short)((g % RCOLS) * TILE + 30), (short)(r * TILE + 26), true}; } }
static void addCoinRow(int r, int a, int b) { for (int c = a; c <= b; c += 2) addCoin(c, r); }
static void addChest(int c, int r, bool sec) { if (chestCount < MAX_CHESTS) { int g = baseCol + c; chests[chestCount++] = {(short)(g / RCOLS), (short)((g % RCOLS) * TILE + 8), (short)(r * TILE - 34), false, sec}; } }
static void addPU(int c, int r, int kind) { if (puCount < MAX_POWERUPS) { int g = baseCol + c; powerups[puCount++] = {(short)(g / RCOLS), (short)((g % RCOLS) * TILE + 18), (short)(r * TILE - 32), kind, true}; } }
static void addMPlat(int r, int a, int b, int wT, float spd) { if (mplatCount < MAX_MPLAT) { int g = baseCol + a; mplats[mplatCount++] = {(short)(g / RCOLS), (float)((g % RCOLS) * TILE), (float)(r * TILE), (float)((g % RCOLS) * TILE), (float)(((baseCol + b) % RCOLS) * TILE), spd, (short)(wT * TILE)}; } }
static void addDPlat(int r, int c, int wT) { if (dplatCount < MAX_DPLAT) { int g = baseCol + c; dplats[dplatCount++] = {(short)(g / RCOLS), (short)((g % RCOLS) * TILE), (short)(r * TILE), (short)(wT * TILE), 0, 0}; } }
static void addNPC(int c, int r, int tid) { if (npcCount < MAX_NPCS) { int g = baseCol + c; npcs[npcCount++] = {(short)(g / RCOLS), (short)((g % RCOLS) * TILE + 10), (short)(r * TILE - 48), tid}; } }
static void addE(int k, int c, int r, int a, int b) {
    if (enemyCount >= MAX_ENEMIES) return;
    Enemy &e = enemies[enemyCount++];
    e.kind = k; e.x = (float)((baseCol + c) * TILE + 8); e.y = e.baseY = (float)(r * TILE - 44);
    e.minX = (float)((baseCol + a) * TILE + 4); e.maxX = (float)((baseCol + b) * TILE + 12);
    e.dir = 1; e.vx = k == E_SPIDER ? 2.6f : (k == E_BAT ? 3.0f : (k == E_ELITE ? 2.9f : 0.0f));
    e.vy = 0; e.maxHp = e.hp = (k == E_ELITE) ? 3 : 1; e.hurt = 0; e.timer = (c * 19) & 63; e.state = 0; e.active = true;
}

static void initWorldDetails() {
    plantCount = 0;
    for (int r = 2; r < ROWS; r++) for (int c = 1; c < levelCols - 1; c++) {
        if (level[r][c] != '#' || level[r - 1][c] != '.') continue;
        bool skip = false;
        for (int dr = -2; dr <= 0 && !skip; dr++)
            for (int dc = -1; dc <= 1; dc++)
                if (r + dr >= 0 && c + dc >= 0 && c + dc < COLS && (level[r + dr][c + dc] == 'O' || level[r + dr][c + dc] == 'G')) skip = true;
        if (skip) continue;
        for (int s = 0; s < 2 && plantCount < MAX_PLANTS; s++) {
            Plant &p = plants[plantCount++];
            p.room = (short)(c / RCOLS); p.x = (short)((c % RCOLS) * TILE + 14 + s * 28 + ((c * 5 + r * 3 + s * 7) % 7));
            p.y = (short)(r * TILE); p.angle = p.vel = 0; p.h = (unsigned char)(12 + ((c * 3 + r * 7 + s * 5) % 7));
            p.style = (unsigned char)((c + r + s) % 3); p.seed = (unsigned char)((c * 5 + r * 9 + s * 11) & 15);
            p.drawn[0] = p.drawn[1] = false; p.dBend[0] = p.dBend[1] = 99;
        }
    }
    static const float LVX[5] = {0.7f, -0.4f, -0.8f, 0.3f, 0.5f}, LVY[5] = {0.9f, -1.4f, 1.5f, -0.5f, -0.8f};
    for (int i = 0; i < MAX_LEAVES; i++) {
        leaves[i].x = (float)((i * 173 + 47) % FW); leaves[i].y = (float)(100 + ((i * 113 + 31) % (FH - 240)));
        leaves[i].vx = LVX[curWorld] + (i % 3) * 0.25f; leaves[i].vy = LVY[curWorld] + (i % 2) * (curWorld == 1 ? -0.35f : 0.35f);
        leaves[i].sz = (unsigned char)(5 + (i % 3)); leaves[i].seed = (unsigned char)i;
    }
    for (int i = 0; i < MAX_FIREFLIES; i++) {
        flies[i].x = (float)((i * 211 + 83) % (FW - 80) + 40); flies[i].y = (float)(220 + ((i * 139 + 41) % 620));
        flies[i].vx = ((i & 1) ? 0.5f : -0.5f); flies[i].vy = ((i & 2) ? 0.3f : -0.3f); flies[i].phase = (unsigned char)(i * 3);
    }
}

static void buildStage(int st) {
    for (int r = 0; r < ROWS; r++) { memset(level[r], '.', COLS); level[r][COLS] = 0; level[r][0] = level[r][COLS - 1] = '#'; }
    memset(level[0], '#', COLS); memset(level[ROWS - 1], '#', COLS);
    enemyCount = checkCount = coinCount = chestCount = puCount = mplatCount = dplatCount = npcCount = 0;
    bossActive = false; baseCol = 0; levelCols = COLS; levelPW = WW;
    curWorld = st >= 16 ? 4 : (st / 4);
    cpX = (float)(TILE + 16); cpY = (float)((ROWS - 1) * TILE - PH);

    if (isBossStage(st)) {
        levelCols = RCOLS; levelPW = FW; bossActive = true; bossType = curWorld;
        for (int r = 0; r < ROWS; r++) level[r][RCOLS - 1] = '#';
        wall(2, 6, 14); wall(29, 6, 14); plat(12, 5, 9); plat(7, 13, 18); plat(12, 22, 26);
        if (bossType == 1) { plat(10, 6, 10); plat(10, 21, 25); }
        addPU(7, 12, PU_HEART); addPU(24, 12, PU_SHIELD);
        bossW = (bossType == 1) ? 136 : 120; bossH = (bossType == 1) ? 124 : 112;
        bossX = (float)(21 * TILE); bossY = (float)((ROWS - 1) * TILE - bossH);
        bossVX = -3.8f - bossType * 0.5f; bossVY = 0; bossMaxHP = bossHP = 8 + bossType * 3;
        bossHurt = bossTimer = bossState = bossTongueLen = bossSwCd = 0;
        for (int i = 0; i < MAX_SHOTS; i++) shots[i].active = false;
        initWorldDetails(); return;
    }

    int sub = st % 4;
    baseCol = 0 * RCOLS; addNPC(4, 17, T_NPC0 + (curWorld & 3)); plat(14, 7, 13); addCoinRow(13, 8, 12); addE(E_SPIDER, 10, 17, 6, 15);
    plat(12, 16, 22); addE(E_MUSHROOM, 19, 12, 16, 22); plat(6, 4, 9); addChest(6, 6, true); addCoinRow(5, 5, 8); plat(14, 25, 30); addE(E_SPIDER, 27, 14, 25, 30);
    baseCol = 1 * RCOLS; spk(16, 6, 25); plat(15, 2, 6); addMPlat(14, 8, 15, 3, 2.2f + curWorld * 0.4f); addCoinRow(12, 10, 14);
    addE(E_BAT, 14, 9, 8, 22); addDPlat(12, 19, 3); addDPlat(12, 23, 3); plat(13, 26, 30); addCP(28, 13); addPU(27, 13, PU_HEART);
    baseCol = 2 * RCOLS; wall(9, 6, 14); wall(13, 6, 16); plat(6, 13, 21); addCoinRow(5, 14, 20); addE(E_MUSHROOM, 17, 6, 13, 21);
    plat(6, 2, 7); addChest(4, 6, true); spk(16, 14, 22); plat(12, 23, 30); addE(E_SPIDER, 26, 12, 23, 30);
    baseCol = 3 * RCOLS; spk(16, 4, 27); plat(14, 2, 7); addPU(5, 14, (sub == 1) ? PU_SPEED : PU_SHIELD);
    if (curWorld == 1 || curWorld == 3) { sawAt(13, 11); sawAt(11, 20); }
    addDPlat(13, 9, 3); plat(11, 14, 19); addCoinRow(10, 15, 18); addE(E_BAT, 16, 7, 11, 23); addMPlat(12, 21, 26, 3, 2.5f); plat(13, 27, 30); addCP(29, 13);
    baseCol = 4 * RCOLS; plat(15, 3, 9); addE(E_SPIDER, 6, 15, 3, 9); addDPlat(13, 11, 3); addDPlat(11, 16, 3); addCoinRow(10, 16, 18);
    plat(10, 21, 29); addE(E_MUSHROOM, 24, 10, 21, 29); addE(E_BAT, 25, 6, 19, 29); addChest(27, 10, false);
    baseCol = 5 * RCOLS; plat(14, 3, 8); plat(12, 11, 21); addCoinRow(11, 12, 20); addE(E_ELITE, 16, 12, 11, 21);
    if (curWorld >= 2 || st == 16) addE(E_BAT, 18, 7, 12, 25);
    plat(10, 24, 30); put(9, 28, 'G'); baseCol = 0; initWorldDetails();
}

// ---------- Collision, Physics & Entities Update ----------
static inline char tileAt(int r, int c) { return (r < 0 || r >= ROWS || c < 0 || c >= COLS) ? '#' : level[r][c]; }
static inline char staticChar(int r, int c) { char ch = tileAt(r, c); return ch == 'O' ? '.' : ch; }
static inline bool hiAt(int r, int c) { return r > 0 && staticChar(r, c) == '#' && staticChar(r - 1, c) != '#'; }
static inline bool solidAt(int x, int y) { return x < 0 || y < 0 || x / TILE >= COLS || y / TILE >= ROWS || level[y / TILE][x / TILE] == '#'; }
static inline bool rectSolid(int x, int y, int w, int h) { return solidAt(x, y) || solidAt(x + w - 1, y) || solidAt(x, y + h - 1) || solidAt(x + w - 1, y + h - 1); }
static inline bool rectOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) { return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by; }
static inline bool circleHit(int cx, int cy, int rad, int rx, int ry, int rw, int rh) {
    int dx = cx - iclamp(cx, rx, rx + rw), dy = cy - iclamp(cy, ry, ry + rh);
    return dx * dx + dy * dy < rad * rad;
}
static void triggerShake(int f, int m) { if (f > shakeTimer) shakeTimer = f; if (m > shakeMag) shakeMag = m; }
static void showToast(int tid) { toastTid = tid; toastTimer = 150; hudNeed[0] = hudNeed[1] = true; }

static void addSplat(int wx, int wy, int w, int h) {
    if (wx < 0 || wy < 0 || wx >= levelPW) return;
    int idx = splatCount < MAX_SPLATS ? splatCount++ : -1;
    if (idx < 0) for (int i = 0; i < MAX_SPLATS; i++) if (splats[i].stage >= 4 && !splats[i].drawn[0] && !splats[i].drawn[1]) { idx = i; break; }
    if (idx < 0) return;
    int rm = iclamp(wx / FW, 0, ROOMS - 1); Splat &s = splats[idx];
    s.room = (short)rm; s.x = (short)(wx - rm * FW - w / 2); s.y = (short)(wy - h / 2); s.w = (short)w; s.h = (short)h;
    s.born = tick; s.stage = 0; s.drawn[0] = s.drawn[1] = false; s.dStage[0] = s.dStage[1] = 99;
}
static void spawnPart(float x, float y, float pvx, float pvy, int life, int sz, int kind) {
    for (int i = 0; i < MAX_PARTS; i++) if (parts[i].life <= 0) { parts[i] = {x, y, pvx, pvy, (short)life, (short)life, (unsigned char)kind, (unsigned char)sz}; return; }
}
static void spawnDust(float x, float y, int n, float sp, int kind = 0) {
    for (int i = 0; i < n; i++) spawnPart(x + (rnd(20) - 10), y - rnd(4), ((rnd(100) - 50) / 50.0f) * sp, -(rnd(100) / 100.0f) * 1.8f, 16 + rnd(12), 6 + rnd(5), kind);
}
static void spawnShot(float sx, float sy, float svx, float svy, int kind) {
    for (int i = 0; i < MAX_SHOTS; i++) if (!shots[i].active) { shots[i] = {sx, sy, svx, svy, kind, true}; return; }
}

static void respawn() {
    px = cpX; py = cpY; vx = vy = 0; squashX = squashY = 1.0f;
    onGround = false; wallLock = coyote = dashTimer = dashCd = 0; slamActive = false; jumpsLeft = 2;
    maxHp = 3 + save.upHp; hp = maxHp; invulnTimer = 40;
    roomIdx = iclamp(((int)px + PW / 2) / FW, 0, ROOMS - 1);
    if (bossActive && bossHP > 0) { bossX = (float)(21 * TILE); bossY = (float)((ROWS - 1) * TILE - bossH); for (int i = 0; i < MAX_SHOTS; i++) shots[i].active = false; }
    hudNeed[0] = hudNeed[1] = true;
}

static void hurtPlayer(int dmg) {
    if (invulnTimer > 0 || deadTimer > 0) return;
    if (shieldTimer > 0) { shieldTimer = 0; invulnTimer = 45; snd(5); triggerShake(8, 5); hudNeed[0] = hudNeed[1] = true; return; }
    hp -= dmg; invulnTimer = 60; hudNeed[0] = hudNeed[1] = true; triggerShake(12, 8);
    if (hp <= 0) {
        snd(6); float cx = px + PW * 0.5f, cy = py + PH * 0.5f;
        for (int i = 0; i < 24; i++) spawnPart(cx + rnd(20) - 10, cy + rnd(20) - 10, (rnd(180) - 90) / 10.0f, (rnd(140) - 120) / 10.0f, 90, 5 + rnd(5), 1);
        for (int i = 0; i < 4; i++) addSplat((int)cx + rnd(40) - 20, (int)cy + rnd(40) - 20, 8 + rnd(10), 8 + rnd(10));
        vx = vy = 0; deadTimer = 30;
    } else { vy = -10.0f; vx = -facing * 5.0f; snd(6); }
}

static void damageBoss(int dmg) {
    if (bossHurt > 0 || bossHP <= 0) return;
    bossHP = iclamp(bossHP - dmg, 0, bossMaxHP); bossHurt = 24; hudNeed[0] = hudNeed[1] = true; snd(7); triggerShake(10, 7);
    for (int i = 0; i < 24; i++) spawnPart(bossX + bossW * 0.5f, bossY + bossH * 0.5f, (rnd(200) - 100) / 10.0f, -(40 + rnd(100)) / 10.0f, 90, 6 + rnd(5), 1);
    if (bossHP <= 0) {
        for (int i = 0; i < MAX_SHOTS; i++) shots[i].active = false;
        save.coins += 50; unlockAch(2); if (save.coins >= 50) unlockAch(3); writeSave();
        level[16][15] = 'G'; needFull[0] = needFull[1] = true; triggerShake(22, 12);
    }
}

static void killEnemy(Enemy &e) {
    e.active = false; snd(7); triggerShake(6, 4);
    for (int k = 0; k < 22; k++) spawnPart(e.x + 22, e.y + 22, (rnd(180) - 90) / 10.0f, -(45 + rnd(95)) / 10.0f, 110, 6 + rnd(4), 1);
}

static void update() {
    stageTimeFrames++; animTick++;
    if (invulnTimer > 0) invulnTimer--; if (dashCd > 0) dashCd--; if (slamShockTimer > 0) slamShockTimer--;
    if (shieldTimer > 0) shieldTimer--; if (speedTimer > 0) speedTimer--; if (shakeTimer > 0) shakeTimer--;
    if (toastTimer > 0 && --toastTimer == 0) hudNeed[0] = hudNeed[1] = true;
    if (npcDialogTimer > 0 && --npcDialogTimer == 0) hudNeed[0] = hudNeed[1] = true;
    for (int i = 0; i < 8; i++) if (ghosts[i].life > 0) ghosts[i].life--;

    for (int i = 0; i < MAX_PARTS; i++) {
        Part &p = parts[i]; if (p.life <= 0) continue; p.life--;
        if (p.kind == 0 || p.kind == 2) { p.x += p.vx; p.y += p.vy; p.vx *= 0.9f; p.vy *= 0.92f; }
        else {
            float ox = p.x, oy = p.y; p.vy = p.vy + 0.62f > 16.0f ? 16.0f : p.vy + 0.62f; p.x += p.vx; p.y += p.vy;
            if (solidAt((int)p.x, (int)p.y)) { addSplat(solidAt((int)ox, (int)p.y) ? (int)ox : (int)p.x, (int)oy, p.size + 6 + rnd(8), p.size / 2 + 3); p.life = 0; }
            else if (p.y > WH) p.life = 0;
        }
    }
    for (int i = 0; i < splatCount; i++) {
        if (splats[i].stage >= 4) continue;
        int age = tick - splats[i].born;
        splats[i].stage = age >= SPLAT_MAX_AGE ? 4 : (age >= SPLAT_MAX_AGE - 300 ? 3 : (age >= SPLAT_MAX_AGE - 600 ? 2 : (age >= SPLAT_MAX_AGE - 900 ? 1 : 0)));
    }
    for (int i = 0; i < mplatCount; i++) {
        MPlat &m = mplats[i]; if (m.room != roomIdx) continue; m.x += m.vx;
        if (m.x <= m.minX) { m.x = m.minX; m.vx = fabsf(m.vx); } else if (m.x >= m.maxX) { m.x = m.maxX; m.vx = -fabsf(m.vx); }
    }
    for (int i = 0; i < dplatCount; i++) {
        DPlat &d = dplats[i]; if (d.room != roomIdx) continue;
        if (d.state == 1 && ++d.timer >= 32) { d.state = 2; d.timer = 0; } else if (d.state == 2 && ++d.timer >= 140) { d.state = 0; d.timer = 0; }
    }

    bool burst = false;
    if (deadTimer > 0) { if (--deadTimer == 0) respawn(); }
    else {
        bool wasG = onGround;
        float k = onGround ? 0.0f : (iabs((int)vy) / 18.0f > 1.0f ? 1.0f : iabs((int)vy) / 18.0f);
        squashX += (1.0f - 0.22f * k - squashX) * 0.3f; squashY += (1.0f + 0.30f * k - squashY) * 0.3f;
        int dir = controlMode == 1 ? (in.ana != 0.0f ? (in.ana > 0 ? 1 : -1) : 0) : ((in.dright ? 1 : 0) - (in.dleft ? 1 : 0));
        float sc = controlMode == 1 && in.ana != 0.0f ? (fabsf(in.ana) < 0.5f ? 0.5f : fabsf(in.ana)) : 1.0f;
        bool wL = rectSolid((int)px - 1, (int)py, 1, PH), wR = rectSolid((int)px + PW, (int)py, 1, PH);
        if (dir) facing = dir;

        if (in.dashNew && dashCd == 0 && !slamActive) {
            dashTimer = 11; dashCd = save.upDash ? 24 : 40; vx = (float)(facing * 15.5f); vy = 0.0f;
            squashX = 1.38f; squashY = 0.68f; snd(5); spawnDust(px + PW * 0.5f, py + PH * 0.5f, 8, 3.5f, 2);
        }
        if (!onGround && in.down && in.squareNew && !slamActive) {
            slamActive = true; dashTimer = 0; vx = 0; vy = 22.0f; squashX = 0.65f; squashY = 1.40f; snd(5);
        }
        if (dashTimer > 0) {
            dashTimer--; vy = 0.0f;
            if ((dashTimer & 1) == 0) for (int g = 0; g < 8; g++) if (ghosts[g].life <= 0) { ghosts[g] = {px, py, 10, facing}; break; }
        } else if (slamActive) {
            vy = 22.0f; vx = 0;
            if ((tick & 1) == 0) for (int g = 0; g < 8; g++) if (ghosts[g].life <= 0) { ghosts[g] = {px, py, 8, facing}; break; }
        } else {
            float maxS = ((in.square ? 8.0f : 6.0f) + (speedTimer > 0 ? 2.2f : 0.0f)) * sc;
            if (wallLock > 0) wallLock--;
            else if (dir) { vx += dir * (onGround ? 1.35f : 0.78f); if (vx > maxS) vx = maxS; if (vx < -maxS) vx = -maxS; }
            else { float fr = onGround ? 1.1f : 0.3f; vx = vx > fr ? vx - fr : (vx < -fr ? vx + fr : 0); }
            vy = vy + 0.88f > 18.0f ? 18.0f : vy + 0.88f;
            bool slide = !onGround && vy > 0 && ((wL && dir < 0) || (wR && dir > 0));
            if (slide && vy > 3.0f) vy = 3.0f;
            if (slide && (tick & 3) == 0) spawnDust(wL ? px : px + PW, py + PH - 6.0f, 1, 1.0f);
        }
        if (onGround) { coyote = 6; jumpsLeft = 2; if (dir != 0 && (tick % 14) == 0) snd(11); }
        else if (coyote > 0) coyote--;

        if (in.crossNew && !slamActive) {
            if (coyote > 0) { vy = -16.8f; coyote = 0; jumpsLeft = 1; squashX = 0.72f; squashY = 1.30f; spawnDust(px + PW * 0.5f, py + PH, 6, 3.0f); burst = true; snd(4); }
            else if (wL || wR) { vy = -16.0f; vx = wL ? 8.2f : -8.2f; wallLock = 9; facing = wL ? 1 : -1; jumpsLeft = 1; squashX = 0.72f; squashY = 1.30f; spawnDust(wL ? px : px + PW, py + PH * 0.5f, 5, 2.2f); burst = true; snd(4); }
            else if (jumpsLeft > 0) { vy = -15.6f; jumpsLeft--; dashTimer = 0; squashX = 0.70f; squashY = 1.32f; spawnDust(px + PW * 0.5f, py + PH, 9, 4.0f, 2); burst = true; snd(4); }
        }
        if (!in.cross && vy < -6.0f && !slamActive) vy = -6.0f;
        px += vx;
        if (rectSolid((int)px, (int)py, PW, PH)) { px = (float)(vx > 0 ? ((((int)px + PW - 1) / TILE) * TILE - PW) : ((((int)px) / TILE + 1) * TILE)); vx = 0; dashTimer = 0; }

        float prevBot = py + PH; py += vy; onGround = false;
        if (rectSolid((int)px, (int)py, PW, PH)) {
            if (vy > 0) { py = (float)((((int)py + PH - 1) / TILE) * TILE - PH); onGround = true; }
            else if (vy < 0) py = (float)((((int)py) / TILE + 1) * TILE);
            vy = 0;
        }
        int rmX = roomIdx * FW;
        if (vy >= 0) {
            for (int i = 0; i < mplatCount; i++) {
                MPlat &m = mplats[i]; if (m.room != roomIdx) continue; float mx = rmX + m.x;
                if (px + PW > mx + 2 && px < mx + m.w - 2 && prevBot <= m.y + 10 && py + PH >= m.y) { py = m.y - PH; vy = 0; onGround = true; px += m.vx; }
            }
            for (int i = 0; i < dplatCount; i++) {
                DPlat &d = dplats[i]; if (d.room != roomIdx || d.state == 2) continue; float dx0 = (float)(rmX + d.x);
                if (px + PW > dx0 + 2 && px < dx0 + d.w - 2 && prevBot <= d.y + 10 && py + PH >= d.y) { py = (float)(d.y - PH); vy = 0; onGround = true; if (d.state == 0) { d.state = 1; d.timer = 0; } }
            }
        }
        if (onGround && slamActive) {
            slamActive = false; slamShockTimer = 16; triggerShake(12, 9); snd(10); spawnDust(px + PW * 0.5f, py + PH, 16, 6.5f, 2); burst = true;
            int slamRad = save.upSlam ? 210 : 140;
            for (int i = 0; i < enemyCount; i++) if (enemies[i].active && iabs((int)(enemies[i].x - px)) < slamRad && iabs((int)(enemies[i].y - py)) < 90) killEnemy(enemies[i]);
            if (bossActive && bossHP > 0 && iabs((int)(bossX - px)) < slamRad + 40 && iabs((int)(bossY - py)) < 110) damageBoss(2);
        } else if (onGround && !wasG && vy > 4.0f) { squashY = 0.72f; squashX = 1.32f; spawnDust(px + PW * 0.5f, py + PH, 6, 3.5f); burst = true; }
    }

    float pcx = px - roomIdx * FW + PW * 0.5f, pby = py + PH;
    for (int i = 0; i < plantCount; i++) {
        Plant &p = plants[i]; if (p.room != roomIdx) continue;
        float wind = SN[((tick / 12) + (p.x / 60) + p.seed) & 15] / 256.0f * 1.8f;
        if (deadTimer == 0) {
            float dx = pcx - p.x, dy = pby - p.y, adx = fabsf(dx);
            if (adx < 46.0f && dy > -22.0f && dy < 32.0f) { float pr = 1.0f - adx / 46.0f; p.vel += vx * 0.40f * pr; wind += (dx < 0 ? 7.0f : -7.0f) * pr; if (burst) p.vel += (dx < 0 ? 5.0f : -5.0f) * pr; }
        }
        p.vel = (p.vel + (wind - p.angle) * 0.18f) * 0.78f; p.angle = iclamp((int)((p.angle + p.vel) * 10), -120, 120) / 10.0f;
    }
    for (int i = 0; i < MAX_LEAVES; i++) {
        Leaf &a = leaves[i]; a.x += a.vx + CS[((tick / 7) + a.seed * 3) & 15] / 256.0f * 0.6f; a.y += a.vy;
        if (a.x > FW - 20) a.x = 20; else if (a.x < 20) a.x = FW - 24;
        if (a.y > FH - 120) a.y = 95; else if (a.y < 90) a.y = FH - 130;
    }
    for (int i = 0; i < MAX_FIREFLIES; i++) {
        Firefly &f = flies[i]; f.x += f.vx + SN[((tick / 9) + f.phase) & 15] / 256.0f * 0.8f; f.y += f.vy + CS[((tick / 11) + f.phase) & 15] / 256.0f * 0.6f;
        if (f.x < 40 || f.x > FW - 40) f.vx = -f.vx; if (f.y < 180 || f.y > FH - 160) f.vy = -f.vy;
    }
    if (deadTimer > 0) return;

    int rmX = roomIdx * FW;
    for (int i = 0; i < checkCount; i++) if (!checkAct[i] && (int)px + PW >= checkC[i] * TILE) { checkAct[i] = true; cpX = (float)(checkC[i] * TILE); cpY = (float)(checkR[i] * TILE - PH); snd(1); spawnDust(cpX + 20, cpY + 4, 10, 3.5f, 2); }
    int magRad = save.upMagnet ? 160 : 36;
    for (int i = 0; i < coinCount; i++) if (coins[i].active && coins[i].room == roomIdx) {
        float dx = (px + PW * 0.5f) - (rmX + coins[i].x), dy = (py + PH * 0.5f) - coins[i].y;
        if (fabsf(dx) < magRad && fabsf(dy) < magRad) {
            if (save.upMagnet && (fabsf(dx) > 26 || fabsf(dy) > 26)) { coins[i].x += (short)(dx > 0 ? 5 : -5); coins[i].y += (short)(dy > 0 ? 5 : -5); }
            else { coins[i].active = false; save.coins++; snd(9); hudNeed[0] = hudNeed[1] = true; if (save.coins >= 50) unlockAch(3); }
        }
    }
    for (int i = 0; i < chestCount; i++) if (!chests[i].opened && chests[i].room == roomIdx && rectOverlap((int)px, (int)py, PW, PH, rmX + chests[i].x, chests[i].y, 44, 34)) {
        chests[i].opened = true; save.coins += 25; snd(1); triggerShake(8, 5); spawnDust((float)(rmX + chests[i].x + 22), (float)chests[i].y, 14, 4.5f, 2);
        unlockAch(1); if (chests[i].secret) { save.secretsFound++; unlockAch(4); showToast(T_SEC_FND); } else showToast(T_CHEST);
        if (save.coins >= 50) unlockAch(3); writeSave();
    }
    for (int i = 0; i < puCount; i++) if (powerups[i].active && powerups[i].room == roomIdx && rectOverlap((int)px, (int)py, PW, PH, rmX + powerups[i].x, powerups[i].y, 28, 28)) {
        powerups[i].active = false; snd(1);
        if (powerups[i].kind == PU_HEART && hp < maxHp) hp++; else if (powerups[i].kind == PU_SHIELD) shieldTimer = 600; else speedTimer = 480;
        hudNeed[0] = hudNeed[1] = true;
    }
    for (int i = 0; i < npcCount; i++) if (npcs[i].room == roomIdx && in.triNew && iabs((int)px - (rmX + npcs[i].x)) < 90 && iabs((int)py - npcs[i].y) < 80) {
        npcDialogTid = npcs[i].dialogTid; npcDialogTimer = 240; snd(0); hudNeed[0] = hudNeed[1] = true;
    }

    int rmX0 = rmX - 240, rmX1 = rmX + FW + 240;
    for (int i = 0; i < enemyCount; i++) {
        Enemy &e = enemies[i]; if (!e.active || (int)e.x < rmX0 || (int)e.x > rmX1) continue;
        if (e.hurt > 0) e.hurt--; e.timer++;
        float dx = px - e.x, adx = fabsf(dx), ady = fabsf(py - e.y);
        if (e.kind == E_SPIDER || e.kind == E_ELITE) {
            if (adx < 360 && ady < 150) e.dir = dx >= 0 ? 1 : -1;
            float nx = e.x + ((adx < 300 && ady < 140) ? e.vx * 1.35f : e.vx) * e.dir;
            if (nx <= e.minX) { nx = e.minX; e.dir = 1; } else if (nx >= e.maxX) { nx = e.maxX; e.dir = -1; }
            if (solidAt(e.dir > 0 ? (int)nx + 46 : (int)nx - 4, (int)e.y + 20)) { if (e.vy == 0) e.vy = -11.5f; else e.dir = -e.dir; }
            if (!rectSolid((int)nx, (int)e.y, 44, 44)) e.x = nx; else e.dir = -e.dir;
            e.vy += 0.65f; e.y += e.vy; if (rectSolid((int)e.x, (int)e.y, 44, 44)) { if (e.vy > 0) e.y = (float)((((int)e.y + 43) / TILE) * TILE - 44); e.vy = 0; }
            if (e.kind == E_ELITE && e.timer % 85 == 0 && adx < 560) spawnShot(e.x + 22, e.y + 18, e.dir * 6.5f, 0, 0);
        } else if (e.kind == E_BAT) {
            if (adx < 400) e.dir = dx >= 0 ? 1 : -1;
            e.x += e.vx * e.dir; if (e.x <= e.minX) { e.x = e.minX; e.dir = 1; } else if (e.x >= e.maxX) { e.x = e.maxX; e.dir = -1; }
            e.y = e.baseY + (SN[(e.timer / 4) & 15] * 26) / 256 + ((adx < 180 && py > e.baseY) ? 55.0f : 0.0f);
        } else {
            e.state = (adx < 230 && ady < 140) ? 1 : 0; e.dir = dx >= 0 ? 1 : -1;
            if (e.state == 1 && e.timer % 70 == 0) spawnShot(e.x + 22, e.y + 12, e.dir * 5.4f, -1.8f, 1);
        }
        if (rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, (int)e.x + 4, (int)e.y + 4, 36, 36)) {
            if ((vy > 0 && py + PH < e.y + 30) || dashTimer > 0 || slamActive) {
                if (vy > 0 && !slamActive) { py = e.y - PH; vy = -15.5f; jumpsLeft = 1; squashX = 0.72f; squashY = 1.28f; }
                e.hp--; e.hurt = 14; if (e.hp <= 0) killEnemy(e); else snd(7);
            } else { hurtPlayer(1); return; }
        }
    }
    for (int i = 0; i < MAX_SHOTS; i++) {
        Shot &s = shots[i]; if (!s.active) continue; s.x += s.vx; s.y += s.vy; if (s.kind == 1) s.vy += 0.12f;
        if (s.x < TILE || s.x > levelPW - TILE || s.y < TILE || s.y > FH - TILE || solidAt((int)s.x, (int)s.y)) { s.active = false; continue; }
        if (rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, (int)s.x - 14, (int)s.y - 14, 28, 28)) { s.active = false; hurtPlayer(1); return; }
    }
    if (bossActive && bossHP > 0) {
        if (bossHurt > 0) bossHurt--; if (bossSwCd > 0) bossSwCd--; bossTimer++;
        float floorY = (float)((ROWS - 1) * TILE - bossH);
        if (bossType == 0) {
            bossX += bossVX * 0.8f; if (bossX < 4 * TILE) { bossX = 4 * TILE; bossVX = 3.8f; } else if (bossX > 24 * TILE) { bossX = 24 * TILE; bossVX = -3.8f; }
            if (bossTimer % 95 == 40 && bossY >= floorY - 2) bossVY = -15.0f;
            bossTongueLen = (bossTimer % 120 > 75 && bossTimer % 120 < 105) ? 180 : 0;
            if (bossTongueLen > 0 && rectOverlap((int)px, (int)py, PW, PH, bossVX >= 0 ? (int)(bossX + bossW) : (int)(bossX - bossTongueLen), (int)bossY + 42, bossTongueLen, 22)) hurtPlayer(1);
        } else if (bossType == 1) {
            bossX += bossVX * 1.15f; if (bossX < 4 * TILE) { bossX = 4 * TILE; bossVX = 4.6f; } else if (bossX > 24 * TILE) { bossX = 24 * TILE; bossVX = -4.6f; }
            bossY = 260.0f + (SN[(bossTimer / 5) & 15] * 140) / 256;
            if (bossTimer % 65 == 0) spawnShot(bossX + bossW * 0.5f, bossY + bossH - 12, (px < bossX ? -5.5f : 5.5f), 3.2f, 1);
        } else {
            bossX += bossVX * ((bossTimer % 140 > 90) ? 1.85f : 1.0f);
            if (bossX < 4 * TILE) { bossX = 4 * TILE; bossVX = 4.6f; } else if (bossX > 24 * TILE) { bossX = 24 * TILE; bossVX = -4.6f; }
            if (bossTimer % 90 == 30 && bossY >= floorY - 2) bossVY = -15.5f;
            if (bossTimer % 65 == 0) { float d = px < bossX ? -1.0f : 1.0f; spawnShot(bossX + bossW * 0.5f, bossY + bossH - 28, d * 7.2f, 0, 0); if (bossType == 3) spawnShot(bossX + bossW * 0.5f, bossY + 24, d * 6.0f, -2.6f, 0); }
        }
        if (bossType != 1) { bossVY += 0.65f; bossY += bossVY; if (bossY >= floorY) { bossY = floorY; bossVY = 0; } }
        if (bossSwCd == 0 && rectOverlap((int)px, (int)py, PW, PH, 15 * TILE, 6 * TILE + 20, 120, 40)) { bossSwCd = 130; vy = -12.0f; damageBoss(2); return; }
        if (rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, (int)bossX + 8, (int)bossY + 8, bossW - 16, bossH - 16)) {
            if ((vy > 0 && py + PH < bossY + 52) || slamActive) { py = bossY - PH; vy = -16.5f; jumpsLeft = 1; slamActive = false; damageBoss(2); }
            else if (bossHurt == 0) { hurtPlayer(1); return; }
        }
    }
    int c0 = iclamp((int)px / TILE - 2, 0, COLS - 1), c1 = iclamp(((int)px + PW) / TILE + 2, 0, COLS - 1);
    for (int r = 0; r < ROWS; r++) for (int c = c0; c <= c1; c++) {
        char ch = level[r][c];
        if ((ch == '^' && rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, c * TILE + 14, r * TILE + 26, 32, 34)) ||
            (ch == 'O' && circleHit(c * TILE + 30, r * TILE + 30, 24, (int)px + 4, (int)py + 4, PW - 8, PH - 8))) { hurtPlayer(1); return; }
        if (ch == 'G' && rectOverlap((int)px, (int)py, PW, PH, c * TILE + 8, r * TILE + 16, PW, PH)) {
            winTimer = 140; needWin[0] = needWin[1] = true; snd(8);
            if (curStage == 0) unlockAch(0);
            if (timeTrialMode) { unlockAch(5); if (save.bestTime[curStage] == 0 || stageTimeFrames < save.bestTime[curStage]) save.bestTime[curStage] = stageTimeFrames; }
            if (curStage + 2 > save.unlockedStages && save.unlockedStages < TOTAL_STAGES) save.unlockedStages = curStage + 2;
            writeSave(); return;
        }
    }
    roomIdx = iclamp(((int)px + PW / 2) / FW, 0, ROOMS - 1);
}
// ====== نهاية الجزء 1 (الصق الجزء 2 أسفل هذا السطر مباشرة) ======
// ====== بداية الجزء 2: الجرافيكس، القائمة السرية (L2+R2+L1+X)، القوائم و main() ======
static void drawSaw(int cx, int cy) {
    int a = (tick / 2) & 15, cc = CS[a], ss = SN[a];
    for (int dy = -26; dy <= 26; dy++) for (int dx = -26; dx <= 26; dx++) {
        int d2 = dx * dx + dy * dy; if (d2 > 676) continue;
        int u = (dx * cc + dy * ss) >> 8, v = (-dx * ss + dy * cc) >> 8;
        if (d2 < 36) scene->DrawPixel(cx + dx, cy + dy, mk(45, 45, 52));
        else if (d2 <= 400) scene->DrawPixel(cx + dx, cy + dy, (dx + dy < 0) ? mk(195, 200, 215) : mk(135, 140, 155));
        else if (iabs(u) < 6 || iabs(v) < 6 || iabs(u - v) < 8 || iabs(u + v) < 8) scene->DrawPixel(cx + dx, cy + dy, mk(235, 238, 248));
    }
}

static void drawFace(int x, int y, int d, Color ew) {
    fillRect(x + 9, y + 12, 10, 14, ew); fillRect(x + 25, y + 12, 10, 14, ew);
    int o = d > 0 ? 5 : 0; fillRect(x + 9 + o, y + 16, 5, 8, mk(0, 0, 0)); fillRect(x + 25 + o, y + 16, 5, 8, mk(0, 0, 0));
}

static void drawPlayerFace(int dx, int dy, int dw, int dh, int d) {
    int ew = dw * 10 / 44 < 4 ? 4 : dw * 10 / 44, eh = dh * 14 / 44 < 6 ? 6 : dh * 14 / 44;
    int pw = dw * 5 / 44 < 2 ? 2 : dw * 5 / 44, ph = dh * 8 / 44 < 3 ? 3 : dh * 8 / 44;
    int e1 = dx + dw * 9 / 44, e2 = dx + dw * 25 / 44, ey = dy + dh * 12 / 44, po = d > 0 ? dw * 5 / 44 : 0, pyy = dy + dh * 16 / 44;
    fillRect(e1, ey, ew, eh, mk(255, 255, 255)); fillRect(e2, ey, ew, eh, mk(255, 255, 255));
    fillRect(e1 + po, pyy, pw, ph, mk(0, 0, 0)); fillRect(e2 + po, pyy, pw, ph, mk(0, 0, 0));
    fillRect(dx + dw * 15 / 44, dy + dh * 32 / 44, dw * 14 / 44, dh * 4 / 44 < 2 ? 2 : dh * 4 / 44, mk(90, 0, 12));
}

static void drawSkinCube(int dx, int dy, int dw, int dh, int dir, int skin) {
    if (skin >= 5 && skin <= 9) {
        int ps = dh >= 60 ? 2 : 2;
        drawNarutoHD(skin - 5, dx + (dw - 20 * ps) / 2, dy + dh - 32 * ps, ps, dir);
        return;
    }
    static const Color BDR[5] = {{125,0,14},{165,112,18},{32,24,52},{42,108,162},{120,24,12}};
    static const Color BDY[5] = {{208,26,36},{245,192,48},{62,48,96},{98,205,248},{242,92,28}};
    static const Color HI[5]  = {{248,96,108},{255,236,135},{112,92,162},{198,242,255},{255,186,88}};
    int s = iclamp(skin, 0, 4);
    fillRect(dx, dy, dw, dh, BDR[s]); fillRect(dx + 3, dy + 3, dw - 6, dh - 6, BDY[s]);
    fillRect(dx + 5, dy + 5, dw - 10, 7, HI[s]);
    drawPlayerFace(dx, dy, dw, dh, dir);
}

struct TilePal { Color mainC, litC, topC, topHi, botC, spHi, spMid, spShd; };
static const TilePal TPAL[5] = {
    {{112,68,38},{142,88,52},{64,198,58},{138,242,108},{68,38,22},{245,248,255},{195,200,215},{125,132,152}},
    {{62,36,44},{88,52,62},{245,98,36},{255,182,82},{34,18,24},{255,195,115},{240,112,52},{165,52,26}},
    {{72,98,134},{98,128,172},{232,248,255},{255,255,255},{42,58,86},{240,252,255},{175,224,250},{105,165,205}},
    {{58,46,84},{82,66,116},{158,112,205},{208,168,245},{32,24,50},{245,248,255},{195,200,215},{125,132,152}},
    {{38,32,54},{58,48,82},{120,235,175},{195,255,225},{22,18,34},{210,255,230},{120,220,175},{65,140,105}}
};

static void drawTileCell(int r, int cx, char ch, bool hi) {
    int x = cx * TILE, y = r * TILE; const TilePal &tp = TPAL[iclamp(curWorld, 0, 4)];
    if (ch == '#') {
        fillRect(x, y, TILE, TILE, tp.mainC); fillRect(x + 6, y + 14, TILE - 12, TILE - 22, tp.litC);
        fillRect(x + 10, y + 18, 14, 8, tp.mainC); fillRect(x + 32, y + 32, 16, 10, tp.botC);
        if (hi) {
            fillRect(x, y, TILE, 10, tp.topC); fillRect(x, y, TILE, 3, tp.topHi);
            fillRect(x + 4, y + 10, 10, 4, tp.topC); fillRect(x + 22, y + 10, 12, 5, tp.topC); fillRect(x + 42, y + 10, 12, 4, tp.topC);
        }
        fillRect(x, y + TILE - 6, TILE, 6, tp.botC);
    } else if (ch == '^') {
        int ctr = x + TILE / 2, top = y + 18;
        for (int j = 0; j < 42; j++) {
            int h = (j * 26) / 41;
            if (h > 0) { fillRect(ctr - h, top + j, h, 1, tp.spHi); fillRect(ctr, top + j, h + 1, 1, tp.spShd); }
            fillRect(ctr - 1, top + j, 3, 1, tp.spMid);
        }
    } else if (ch == 'G') {
        int gx = x + 8, gy = y + 16;
        fillRect(gx - 3, gy - 3, PW + 6, PH + 6, mk(255, 228, 130));
        fillRect(gx, gy, PW, PH, mk(255, 140, 170)); fillRect(gx, gy + 3, PW, 7, mk(240, 230, 200));
        drawFace(gx, gy, -1, mk(255, 255, 255));
    }
}

static void drawPlantTuft(int bx, int by, int h, int bend, int bm, int st) {
    static const Color PD[5] = {{28,136,42},{95,32,32},{105,175,220},{78,52,128},{42,135,98}};
    static const Color PM[5] = {{76,216,74},{242,102,32},{185,230,255},{138,98,205},{98,225,165}};
    static const Color PT[5] = {{255,235,80},{255,225,75},{255,255,255},{185,235,255},{220,255,235}};
    int w = iclamp(bm, 0, 4);
    Color tip = w == 0 ? (st == 0 ? mk(255,95,135) : (st == 1 ? mk(255,235,80) : mk(125,225,255))) : PT[w];
    for (int b = -1; b <= 1; b++) {
        int ox = bx + b * 5, bh = h - (b ? 2 : 0), sh = bh / 2, off = bend / 2 + b;
        fillRect(ox - 1, by - sh, 3, sh, PD[w]); fillRect(ox - 1 + off, by - bh, 3, bh - sh, PM[w]);
        if (b == 0 || w == 1) fillRect(ox - 2 + off, by - bh - 4, 4, 4, tip);
    }
}

static void drawSplat(const Splat &s) {
    if (s.stage >= 4) return;
    int div = s.stage + 1, dw = (s.w * (4 - s.stage)) / 4, dh = (s.h * (4 - s.stage)) / 4;
    if (dw < 2) dw = 2; if (dh < 2) dh = 2;
    int ox = s.x + (s.w - dw) / 2, oy = s.y + (s.h - dh) / 2;
    fillRect(ox, oy, dw, dh, mk(140 / div + 20, 10, 20));
    if (s.stage <= 1) fillRect(ox + 1, oy + 1, dw / 3 + 1, dh / 3 + 1, mk(185 / div + 20, 22 / div, 34 / div));
}

static void paintBackground() {
    int pFar = roomIdx * 90, pNear = roomIdx * 180;
    if (curWorld == 0) {
        fillRect(0, 0, FW, 240, mk(46,148,225)); fillRect(0, 240, FW, 260, mk(115,205,245));
        fillRect(0, 500, FW, 180, mk(38,118,68)); fillRect(0, 680, FW, 400, mk(22,74,44));
        fillCircle(1580, 130, 58, mk(255,242,155)); fillCircle(1580, 130, 44, mk(255,222,72));
        for (int i = 0; i < 10; i++) { int tx = ((i * 210 - pFar) % FW + FW) % FW; fillRect(tx, 160, 48, 560, mk(52,135,98)); fillRect(tx + 8, 160, 12, 560, mk(72,162,122)); }
        for (int i = 0; i < 6; i++) {
            int tx = ((i * 340 - pNear) % FW + FW) % FW;
            fillRect(tx, 260, 62, 520, mk(76,46,26)); fillRect(tx + 8, 260, 16, 520, mk(104,64,38));
            fillCircle(tx + 30, 240, 88, mk(32,128,54)); fillCircle(tx + 10, 210, 56, mk(62,178,76));
            fillRect(tx + 70, 60, 8, 140 + (i % 3) * 35, mk(46,162,62)); fillRect(tx + 96, 60, 6, 110 + (i % 2) * 45, mk(68,196,76));
        }
        fillRect(340, 420, 86, 280, mk(82,188,248)); fillRect(362, 420, 42, 280, mk(185,238,255));
    } else if (curWorld == 1) {
        fillRect(0, 0, FW, 280, mk(42,14,24)); fillRect(0, 280, FW, 260, mk(86,26,32));
        fillRect(0, 540, FW, 180, mk(58,20,28)); fillRect(0, 720, FW, 360, mk(34,12,18));
        fillCircle(1540, 150, 52, mk(242,96,42)); fillCircle(1540, 150, 36, mk(255,172,76));
        for (int i = 0; i < 4; i++) {
            int x = ((i * 520 - pFar) % FW + FW) % FW;
            fillRect(x, 380, 200, 340, mk(52,18,26)); fillRect(x + 56, 376, 88, 16, mk(255,112,35));
            fillRect(x + 88, 392, 24, 328, mk(242,82,28)); fillRect(x + 94, 392, 10, 328, mk(255,195,72));
        }
    } else if (curWorld == 2) {
        fillRect(0, 0, FW, 280, mk(26,48,88)); fillRect(0, 280, FW, 260, mk(56,94,142));
        fillRect(0, 540, FW, 180, mk(86,126,172)); fillRect(0, 720, FW, 360, mk(52,84,126));
        for (int i = 0; i < 7; i++) fillRect(i * 280 + 40, 120 + (i & 1) * 24, 230, 18, mk(72,224,182));
        fillCircle(1540, 145, 48, mk(224,244,255));
        for (int i = 0; i < 4; i++) { int x = ((i * 510 - pFar) % FW + FW) % FW; fillRect(x + 40, 390, 210, 330, mk(86,126,172)); fillRect(x + 70, 350, 150, 50, mk(236,248,255)); }
    } else {
        Color s1 = curWorld == 4 ? mk(14,28,32) : mk(22,12,48), s2 = curWorld == 4 ? mk(24,48,54) : mk(34,18,68);
        fillRect(0, 0, FW, 300, s1); fillRect(0, 300, FW, 280, s2); fillRect(0, 580, FW, 500, curWorld == 4 ? mk(16,32,38) : mk(24,14,52));
        fillCircle(1540, 145, 50, curWorld == 4 ? mk(120,245,195) : mk(185,172,228));
        for (int i = 0; i < 6; i++) {
            int x = ((i * 340 - pNear) % FW + FW) % FW;
            fillRect(x, 220, 72, 680, mk(38,28,62)); fillRect(x + 12, 220, 14, 680, mk(54,42,84));
            fillRect(x + 140, 340, 64, 110, curWorld == 4 ? mk(48,145,112) : mk(98,42,128));
            fillRect(x + 150, 352, 44, 86, curWorld == 4 ? mk(92,225,175) : mk(165,78,195));
        }
    }
}

static void restoreRect(int b, int x, int y, int w, int h, bool det) {
    if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
    if (x + w > FW) w = FW - x; if (y + h > FH) h = FH - y;
    if (w <= 0 || h <= 0) return;
    setClip(x, y, w, h); paintBackground();
    for (int r = y / TILE; r <= (y + h - 1) / TILE; r++)
        for (int c = x / TILE; c <= (x + w - 1) / TILE; c++)
            if (bufGrid[b][r][c] != '.') drawTileCell(r, c, bufGrid[b][r][c], bufHi[b][r][c]);
    if (det) {
        for (int i = 0; i < splatCount; i++) if (splats[i].drawn[b] && splats[i].stage < 4) drawSplat(splats[i]);
        for (int i = 0; i < plantCount; i++) {
            Plant &p = plants[i];
            if (p.room == bufRoom[b] && p.x + 16 >= clipX0 && p.x - 16 <= clipX1 && p.y >= clipY0 && p.y - p.h - 6 <= clipY1) {
                drawPlantTuft(p.x, p.y, p.h, (int)p.angle, curWorld, p.style); p.drawn[b] = true; p.dBend[b] = (signed char)p.angle;
            }
        }
    }
    resetClip();
}
static inline void addDirty(int b, int x, int y, int w, int h) { if (dirtyCnt[b] < MAX_DIRTY) dirty[b][dirtyCnt[b]++] = {x, y, w, h}; }

static void drawEnemyChar(const Enemy &e, int sx, int sy) {
    bool fl = (e.hurt & 2) != 0;
    if (e.kind == E_SPIDER) {
        int leg = ((tick / 5) & 1) ? 4 : -4;
        fillRect(sx - 10, sy + 20 + leg, 14, 5, mk(32,36,44)); fillRect(sx + 40, sy + 20 - leg, 14, 5, mk(32,36,44));
        fillRect(sx - 8, sy + 32 - leg, 12, 5, mk(32,36,44)); fillRect(sx + 40, sy + 32 + leg, 12, 5, mk(32,36,44));
        fillCircle(sx + 22, sy + 18, 16, fl ? mk(255,255,255) : mk(148,88,42)); fillCircle(sx + 22, sy + 28, 12, mk(112,176,48));
        fillRect(sx + 14, sy + 26, 6, 6, mk(225,24,36)); fillRect(sx + 24, sy + 26, 6, 6, mk(225,24,36));
    } else if (e.kind == E_BAT) {
        int wg = ((tick / 4) & 1) ? -12 : 8; Color wC = fl ? mk(255,255,255) : mk(124,52,72);
        fillRect(sx - 16, sy + 14 + wg, 18, 10, wC); fillRect(sx + 42, sy + 14 + wg, 18, 10, wC);
        fillCircle(sx + 22, sy + 22, 13, mk(88,42,62)); fillRect(sx + 15, sy + 18, 4, 4, mk(255,220,75)); fillRect(sx + 25, sy + 18, 4, 4, mk(255,220,75));
    } else if (e.kind == E_MUSHROOM) {
        if (e.state == 0) {
            fillRect(sx + 8, sy + 26, 28, 18, mk(232,216,188)); fillRect(sx + 2, sy + 16, 40, 14, mk(208,42,58));
            fillRect(sx + 10, sy + 19, 6, 6, mk(255,245,230)); fillRect(sx + 28, sy + 19, 6, 6, mk(255,245,230));
        } else {
            fillRect(sx + 6, sy + 16, 32, 28, fl ? mk(255,255,255) : mk(238,224,196)); fillRect(sx, sy + 2, 44, 18, mk(215,38,54));
            fillRect(sx + 8, sy + 6, 7, 7, mk(255,250,235)); fillRect(sx + 29, sy + 6, 7, 7, mk(255,250,235)); drawFace(sx, sy + 10, e.dir, mk(255,225,85));
        }
    } else {
        fillRect(sx - 2, sy - 4, 48, 48, mk(36,24,48)); fillRect(sx + 2, sy, 40, 40, fl ? mk(255,255,255) : mk(168,32,58));
        fillRect(sx + 6, sy - 10, 32, 8, mk(245,195,65)); drawFace(sx, sy, e.dir, mk(255,235,115));
    }
}

static void drawBossChar(int bx, int by, int bw, int bh, bool hurt) {
    if (bossType == 0) {
        fillRect(bx + 10, by + 48, bw - 20, bh - 48, mk(178,28,42));
        fillCircle(bx + bw / 2, by + bh / 2 + 6, 52, hurt ? mk(255,255,255) : mk(72,212,96)); fillCircle(bx + bw / 2, by + bh / 2 + 22, 34, mk(158,245,168));
        fillRect(bx + 36, by - 14, 48, 16, mk(255,212,55)); fillRect(bx + 36, by - 24, 10, 12, mk(255,212,55)); fillRect(bx + 55, by - 28, 10, 16, mk(255,212,55)); fillRect(bx + 74, by - 24, 10, 12, mk(255,212,55));
        fillRect(bx + 26, by + 26, 18, 18, mk(255,255,255)); fillRect(bx + 76, by + 26, 18, 18, mk(255,255,255));
        fillRect(bx + 32, by + 32, 8, 8, mk(0,0,0)); fillRect(bx + 82, by + 32, 8, 8, mk(0,0,0)); fillRect(bx + 28, by + 56, 64, 24, mk(142,24,42));
        if (bossTongueLen > 0) { int tx = bossVX >= 0 ? bx + bw : bx - bossTongueLen; fillRect(tx, by + 56, bossTongueLen, 16, mk(224,96,118)); fillCircle(bossVX >= 0 ? tx + bossTongueLen : tx, by + 64, 14, mk(195,72,96)); }
    } else if (bossType == 1) {
        int lg = ((tick / 5) & 1) ? 8 : -8;
        for (int k = 0; k < 3; k++) { fillRect(bx - 24, by + 20 + k * 28 + lg, 32, 8, mk(36,42,52)); fillRect(bx + bw - 8, by + 20 + k * 28 - lg, 32, 8, mk(36,42,52)); }
        fillCircle(bx + bw / 2, by + 76, 42, hurt ? mk(255,255,255) : mk(168,104,56));
        fillRect(bx + bw / 2 - 26, by + 68, 52, 8, mk(112,62,28)); fillRect(bx + bw / 2 - 22, by + 84, 44, 8, mk(112,62,28));
        fillCircle(bx + bw / 2, by + 34, 30, mk(118,178,42)); fillCircle(bx + bw / 2 - 14, by + 32, 9, mk(225,22,38)); fillCircle(bx + bw / 2 + 14, by + 32, 9, mk(225,22,38));
    } else if (bossType == 2) {
        fillCircle(bx + bw / 2, by + bh / 2 + 8, 54, hurt ? mk(255,255,255) : mk(218,132,158));
        fillRect(bx + 26, by - 12, 16, 26, mk(218,132,158)); fillRect(bx + 78, by - 12, 16, 26, mk(218,132,158));
        fillCircle(bx + 36, by + 34, 10, mk(255,224,52)); fillCircle(bx + 84, by + 34, 10, mk(255,224,52));
        fillRect(bx + 24, by + 54, 72, 42, mk(62,18,28));
        for (int t0 = 0; t0 < 5; t0++) { fillRect(bx + 28 + t0 * 14, by + 54, 10, 12, mk(248,244,232)); fillRect(bx + 28 + t0 * 14, by + 84, 10, 12, mk(248,244,232)); }
    } else {
        Color dC = hurt ? mk(255,240,180) : mk(42,18,62), mC = hurt ? mk(255,255,255) : mk(124,28,158), cC = (tick & 4) ? mk(255,215,65) : mk(255,75,115);
        fillRect(bx, by, bw, bh, dC); fillRect(bx + 8, by + 8, bw - 16, bh - 16, mC); fillRect(bx + 20, by - 10, bw - 40, 12, cC);
        fillRect(bx + 22, by + 28, 24, 20, mk(255,240,90)); fillRect(bx + bw - 46, by + 28, 24, 20, mk(255,240,90));
        fillRect(bx + 28, by + 34, 12, 12, mk(20,0,0)); fillRect(bx + bw - 40, by + 34, 12, 12, mk(20,0,0)); fillRect(bx + 24, by + 70, bw - 48, 20, mk(24,10,38));
    }
}

static void drawHUDFull(int b) {
    for (int i = 0; i < maxHp; i++) {
        int hx = 42 + i * 38, hy = 26; Color hc = i < hp ? mk(235,32,52) : mk(68,54,72);
        fillCircle(hx + 8, hy + 8, 8, hc); fillCircle(hx + 20, hy + 8, 8, hc); fillRect(hx + 4, hy + 10, 20, 12, hc); fillRect(hx + 10, hy + 20, 8, 6, hc);
    }
    fillCircle(60, 74, 12, mk(255,208,45)); fillCircle(60, 74, 7, mk(255,238,120));
    char cb[16]; intToStr(save.coins, cb); drawText(82, 64, cb, 3, mk(255,235,110));
    if (bossActive && bossHP > 0) {
        drawLabel(T_BFROG + bossType, 960, 10, 32, 3, mk(255,215,90));
        fillRect(676, 44, 568, 22, mk(28,16,38)); fillRect(680, 47, 560, 16, mk(68,24,34)); fillRect(680, 47, (bossHP * 560) / bossMaxHP, 16, mk(242,58,68));
    } else if (timeTrialMode) {
        char tb[24]; sprintf(tb, "TIME %02d:%02d", stageTimeFrames / 60, (stageTimeFrames % 60) * 100 / 60);
        fillRect(820, 22, 280, 46, mk(26,20,48)); drawTextC(960, 34, tb, 3, mk(115,245,165));
    } else drawLabel(T_W0 + curWorld, 960, 18, 38, 3, mk(235,232,250));
    fillRect(1560, 22, 310, 44, mk(26,20,48));
    drawTextC(1715, 34, dashCd == 0 ? "O: DASH READY" : "DASH CHARGING", 2, dashCd == 0 ? mk(95,235,255) : mk(175,165,195));
    if (shieldTimer > 0) { fillRect(1560, 72, 145, 28, mk(35,98,175)); drawTextC(1632, 79, "SHIELD", 2, mk(210,245,255)); }
    if (speedTimer > 0) { fillRect(1720, 72, 150, 28, mk(185,125,24)); drawTextC(1795, 79, "SPEED+", 2, mk(255,245,155)); }
    if (toastTimer > 0 && toastTid >= 0) { fillRect(540, 108, 840, 56, mk(245,195,55)); fillRect(544, 112, 832, 48, mk(34,24,58)); drawLabel(toastTid, 960, 112, 48, 3, mk(255,238,125)); }
    if (npcDialogTimer > 0 && npcDialogTid >= 0) { fillRect(260, 910, 1400, 90, mk(245,195,65)); fillRect(264, 914, 1392, 82, mk(28,22,48)); drawLabel(npcDialogTid, 960, 914, 82, 3, mk(255,255,255)); }
    hudNeed[b] = false;
}

static void renderGame(int b) {
    resetClip();
    if (winTimer > 0) {
        if (needWin[b]) { fillRect(0, 0, FW, FH, mk(20, 90, 60)); drawLabel(T_COMPLETE, 960, 350, 110, 10, mk(255,255,255)); drawLabel(T_GREAT, 960, 500, 80, 6, mk(255,220,180)); needWin[b] = false; }
        return;
    }
    int shkX = shakeTimer > 0 ? (rnd(shakeMag * 2 + 1) - shakeMag) : 0;
    int shkY = shakeTimer > 0 ? (rnd(shakeMag * 2 + 1) - shakeMag) : 0;

    // FIX: Redraw full background cleanly whenever transitioning to a new room so parallax never tears!
    if (bufRoom[b] != roomIdx) needFull[b] = true;
    if (needFull[b]) {
        paintBackground();
        for (int r = 0; r < ROWS; r++) for (int c = 0; c < RCOLS; c++) {
            int wc = roomIdx * RCOLS + c; bufGrid[b][r][c] = staticChar(r, wc); bufHi[b][r][c] = hiAt(r, wc);
            if (bufGrid[b][r][c] != '.') drawTileCell(r, c, bufGrid[b][r][c], bufHi[b][r][c]);
        }
        bufRoom[b] = roomIdx;
        for (int i = 0; i < MAX_SPLATS; i++) splats[i].drawn[b] = false;
        for (int i = 0; i < splatCount; i++) if (splats[i].room == roomIdx && splats[i].stage < 4) { drawSplat(splats[i]); splats[i].drawn[b] = true; splats[i].dStage[b] = splats[i].stage; }
        for (int i = 0; i < plantCount; i++) {
            Plant &p = plants[i]; p.drawn[b] = (p.room == roomIdx);
            if (p.drawn[b]) { drawPlantTuft(p.x, p.y, p.h, (int)p.angle, curWorld, p.style); p.dBend[b] = (signed char)p.angle; }
        }
        dirtyCnt[b] = 0; drawHUDFull(b); needFull[b] = false;
    } else {
        for (int i = 0; i < dirtyCnt[b]; i++) { if (dirty[b][i].y < 170 || dirty[b][i].y > 890) hudNeed[b] = true; restoreRect(b, dirty[b][i].x, dirty[b][i].y, dirty[b][i].w, dirty[b][i].h, true); }
        dirtyCnt[b] = 0;
        for (int i = 0; i < splatCount; i++) {
            Splat &s = splats[i]; if (s.room != bufRoom[b]) continue;
            if (s.drawn[b] && (s.stage >= 4 || s.dStage[b] != s.stage)) { restoreRect(b, s.x - 1, s.y - 1, s.w + 2, s.h + 2, false); s.drawn[b] = false; }
            if (!s.drawn[b] && s.stage < 4) { drawSplat(s); s.drawn[b] = true; s.dStage[b] = s.stage; }
        }
        for (int i = 0; i < plantCount; i++) {
            Plant &p = plants[i]; if (p.room != bufRoom[b]) continue;
            int bd = (int)p.angle;
            if (p.drawn[b] && p.dBend[b] != bd) { restoreRect(b, p.x - 12, p.y - p.h - 5, 24, p.h + 5, false); p.drawn[b] = false; }
            if (!p.drawn[b]) { drawPlantTuft(p.x, p.y, p.h, bd, curWorld, p.style); p.drawn[b] = true; p.dBend[b] = (signed char)bd; }
        }
        if (hudNeed[b] || timeTrialMode || (tick & 7) == 0) {
            restoreRect(b, 0, 0, FW, 170, true);
            if (npcDialogTimer == 0) restoreRect(b, 250, 900, 1420, 110, true);
            drawHUDFull(b);
        }
    }

    resetClip();
    int roomX = roomIdx * FW;
    static const Color LCOL[5] = {{98,225,84},{255,135,42},{235,248,255},{175,145,235},{112,245,185}};
    for (int i = 0; i < MAX_LEAVES; i++) { int lx = (int)leaves[i].x, ly = (int)leaves[i].y, sz = leaves[i].sz; fillRect(lx, ly, sz, sz - 1, LCOL[curWorld]); addDirty(b, lx - 1, ly - 1, sz + 2, sz + 2); }
    for (int i = 0; i < MAX_FIREFLIES; i++) {
        int fx = (int)flies[i].x, fy = (int)flies[i].y; bool br = (((tick / 8) + flies[i].phase) & 3) != 0;
        fillRect(fx - 2, fy - 2, 7, 7, curWorld == 1 ? mk(165,65,24) : mk(95,125,45)); fillRect(fx, fy, 3, 3, br ? mk(255,248,135) : mk(185,205,95)); addDirty(b, fx - 3, fy - 3, 9, 9);
    }
    for (int i = 0; i < mplatCount; i++) if (mplats[i].room == roomIdx) { int mx = (int)mplats[i].x + shkX, my = (int)mplats[i].y + shkY, mw = mplats[i].w; fillRect(mx, my, mw, 26, mk(96,68,42)); fillRect(mx, my, mw, 7, TPAL[curWorld].topC); addDirty(b, mx - 4, my - 4, mw + 8, 34); }
    for (int i = 0; i < dplatCount; i++) if (dplats[i].room == roomIdx && dplats[i].state < 2) {
        int dx0 = dplats[i].x + (dplats[i].state == 1 ? ((tick & 2) ? 3 : -3) : 0), dy0 = dplats[i].y;
        fillRect(dx0, dy0, dplats[i].w, 24, dplats[i].state == 1 ? mk(215,95,65) : mk(142,112,86)); fillRect(dx0 + 4, dy0 + 4, dplats[i].w - 8, 6, mk(225,205,175)); addDirty(b, dx0 - 6, dy0 - 2, dplats[i].w + 12, 28);
    }
    for (int i = 0; i < coinCount; i++) if (coins[i].active && coins[i].room == roomIdx) {
        int cx0 = coins[i].x, cy0 = coins[i].y + (SN[((tick / 5) + i) & 15] >> 6), cw = ((tick / 6 + i) & 3) == 0 ? 6 : 14;
        fillRect(cx0 - cw / 2, cy0 - 10, cw, 20, mk(255,208,42)); fillRect(cx0 - cw / 4, cy0 - 6, cw / 2 + 1, 12, mk(255,245,145)); addDirty(b, cx0 - 10, cy0 - 14, 20, 28);
    }
    for (int i = 0; i < chestCount; i++) if (chests[i].room == roomIdx) {
        int cx0 = chests[i].x, cy0 = chests[i].y; fillRect(cx0, cy0, 44, 34, chests[i].opened ? mk(95,62,36) : mk(168,104,42));
        fillRect(cx0, cy0 + 12, 44, 5, mk(245,195,55)); fillRect(cx0 + 18, cy0 + 10, 8, 10, mk(255,232,105)); addDirty(b, cx0 - 2, cy0 - 2, 48, 38);
    }
    for (int i = 0; i < puCount; i++) if (powerups[i].active && powerups[i].room == roomIdx) {
        int px0 = powerups[i].x, py0 = powerups[i].y + (SN[(tick / 5) & 15] >> 6);
        Color pc = powerups[i].kind == PU_HEART ? mk(245,42,68) : (powerups[i].kind == PU_SHIELD ? mk(75,185,255) : mk(255,215,55));
        fillCircle(px0 + 14, py0 + 14, 13, pc); fillCircle(px0 + 14, py0 + 14, 6, mk(255,255,255)); addDirty(b, px0 - 2, py0 - 2, 32, 32);
    }
    for (int i = 0; i < checkCount; i++) if (checkC[i] / RCOLS == roomIdx) {
        int fx = (checkC[i] % RCOLS) * TILE + 24, fy = checkR[i] * TILE - 64;
        fillRect(fx, fy, 6, 64, mk(220,220,235)); fillRect(fx + 6, fy + 4, 28, 20, checkAct[i] ? mk(65,235,110) : mk(235,190,55)); addDirty(b, fx - 2, fy - 2, 38, 66);
    }
    for (int i = 0; i < npcCount; i++) if (npcs[i].room == roomIdx) {
        int nx = npcs[i].x, ny = npcs[i].y;
        fillRect(nx, ny, 38, 48, mk(62,88,165)); fillRect(nx + 4, ny + 4, 30, 18, mk(242,205,168));
        fillRect(nx + 8, ny + 10, 6, 6, mk(0,0,0)); fillRect(nx + 22, ny + 10, 6, 6, mk(0,0,0)); fillRect(nx - 2, ny - 10, 42, 12, mk(195,55,65));
        if (iabs((int)px - (roomX + nx)) < 90) drawLabel(T_NPC_H, nx + 19, ny - 44, 30, 2, mk(255,235,95));
        addDirty(b, nx - 60, ny - 48, 160, 100);
    }
    for (int r = 0; r < ROWS; r++) for (int c = roomIdx * RCOLS; c < roomIdx * RCOLS + RCOLS && c < COLS; c++)
        if (level[r][c] == 'O') { int cx = c * TILE + 30 - roomX + shkX, cy = r * TILE + 30 + shkY; drawSaw(cx, cy); addDirty(b, cx - 28, cy - 28, 56, 56); }
    for (int i = 0; i < enemyCount; i++) if (enemies[i].active) {
        int sx = (int)enemies[i].x - roomX + shkX, sy = (int)enemies[i].y + shkY;
        if (sx >= -60 && sx <= FW + 60) { drawEnemyChar(enemies[i], sx, sy); addDirty(b, sx - 20, sy - 16, 86, 68); }
    }
    if (bossActive && bossHP > 0) {
        fillRect(15 * TILE + 20, 7 * TILE - 18, 80, 18, bossSwCd == 0 ? ((tick & 8) ? mk(80,255,120) : mk(40,200,85)) : mk(90,80,100));
        addDirty(b, 15 * TILE + 18, 7 * TILE - 20, 84, 22);
        int bx = (int)bossX + shkX, by = (int)bossY + shkY; drawBossChar(bx, by, bossW, bossH, (bossHurt & 2) != 0);
        addDirty(b, bx - (bossTongueLen + 36), by - 36, bossW + bossTongueLen * 2 + 72, bossH + 56);
    }
    for (int i = 0; i < MAX_SHOTS; i++) if (shots[i].active) {
        int sx = (int)shots[i].x - roomX, sy = (int)shots[i].y;
        if (sx >= -30 && sx <= FW + 30) { fillCircle(sx, sy, 13, shots[i].kind ? mk(125,225,55) : mk(255,95,35)); fillCircle(sx, sy, 6, mk(255,242,115)); addDirty(b, sx - 16, sy - 16, 32, 32); }
    }
    for (int i = 0; i < MAX_PARTS; i++) {
        Part &p = parts[i]; if (p.life <= 0) continue;
        int sx = (int)p.x - roomX, sy = (int)p.y; if (sx < -20 || sx > FW + 20 || sy < -20 || sy > FH + 20) continue;
        int sz = p.kind == 1 ? p.size : 2 + (p.size - 2) * p.life / p.maxLife;
        Color col = p.kind == 1 ? mk(210,18,32) : (p.kind == 2 ? mk(115,235,255) : mk(205,195,230));
        fillRect(sx, sy, sz, sz, col); addDirty(b, sx - 1, sy - 1, sz + 2, sz + 2);
    }
    for (int i = 0; i < 8; i++) if (ghosts[i].life > 0) {
        int gx = (int)ghosts[i].x - roomX, gy = (int)ghosts[i].y;
        fillRect(gx, gy, PW, PH, mk(115,215,255)); addDirty(b, gx - 2, gy - 2, PW + 4, PH + 4);
    }
    if (slamShockTimer > 0) {
        int rad = (16 - slamShockTimer) * (save.upSlam ? 14 : 9), sx = (int)px - roomX + PW / 2, sy = (int)py + PH - 8;
        fillRect(sx - rad, sy, rad * 2, 10, mk(255,235,115)); addDirty(b, sx - rad - 4, sy - 4, rad * 2 + 8, 18);
    }
    if (deadTimer == 0 && ((invulnTimer & 2) == 0)) {
        int x = (int)px - roomX + shkX, y = (int)py + shkY, dw = iclamp((int)(PW * squashX), 20, 80), dh = iclamp((int)(PH * squashY), 20, 80);
        int walkBob = (onGround && iabs((int)vx) > 1 && ((animTick / 6) & 1)) ? -3 : 0;
        int dx = x + (PW - dw) / 2, dy = y + (PH - dh) + walkBob;
        if (shieldTimer > 0) fillCircle(dx + dw / 2, dy + dh / 2, 34, mk(85,195,255));
        drawSkinCube(dx, dy, dw, dh, facing, save.selectedSkin);
        addDirty(b, x - 36, y - 36, PW + 72, PH + 68);
    }
}

// ---------- Menus, Secret Naruto Roster (L2+R2+L1+X), Shop, Skins, Achievements & Story ----------
static void drawBanner(int tid, int sc, int y, int h, bool cubes) {
    fillRect(456, y - 4, 1008, h + 8, mk(245,190,65)); fillRect(460, y, 1000, h, mk(35,25,55));
    drawLabel(tid, 960, y, h, sc, mk(255,75,115));
    if (cubes) { drawSkinCube(500, y + (h - 68) / 2, 68, 68, 1, save.selectedSkin); drawSkinCube(1352, y + (h - 68) / 2, 68, 68, -1, save.selectedSkin); }
}
static void drawHint(int tid) {
    int w = labelWidth(tid, 3) + 80;
    fillRect(960 - w / 2 - 3, 977, w + 6, 66, mk(235,185,65)); fillRect(960 - w / 2, 980, w, 60, mk(35,25,55));
    drawLabel(tid, 960, 980, 60, 3, mk(220,212,238));
}
static void markPend(int i) { if (i >= 0 && i < 64) pend[0][i] = pend[1][i] = true; }
static void setState(int s) { state = s; uiFull[0] = uiFull[1] = true; }
static void startStory(int id) { storyId = id; storyPage = STORY_FIRST[id]; typed = storyAnimTick = 0; storyBgDone[0] = storyBgDone[1] = false; state = S_STORY; }
static void startStage(int st, bool tt) {
    curStage = st; timeTrialMode = tt; buildStage(st); roomIdx = 0; stageTimeFrames = 0;
    toastTimer = npcDialogTimer = shieldTimer = speedTimer = slamShockTimer = shakeTimer = 0;
    respawn(); winTimer = deadTimer = splatCount = 0;
    for (int i = 0; i < MAX_PARTS; i++) parts[i].life = 0;
    for (int b = 0; b < 2; b++) { for (int i = 0; i < MAX_SPLATS; i++) splats[i].drawn[b] = false; needFull[b] = true; needWin[b] = hudNeed[b] = false; dirtyCnt[b] = 0; }
    state = S_PLAY;
}

static void drawSecretMenu() {
    static const char *NAMES[5] = {"OBITO", "PAIN", "ITACHI", "MADARA", "KAKASHI"};
    static const char *TITLES[5] = {"MASKED SHINOBI", "AKATSUKI LEADER", "SHARINGAN PRODIGY", "LEGENDARY UCHIHA", "COPY NINJA"};
    fillRect(0, 0, FW, FH, mk(14, 12, 28));
    fillRect(260, 64, 1400, 96, mk(225, 48, 62)); fillRect(264, 68, 1392, 88, mk(28, 20, 48));
    drawTextC(960, 96, "SECRET SHINOBI CHARACTERS", 5, mk(255, 212, 72));
    for (int i = 0; i < 5; i++) {
        int bx = 135 + i * 335, by = 225; bool sel = (i == secretSel);
        if (sel) fillRect(bx - 6, by - 6, 302, 522, mk(255, 212, 72));
        fillRect(bx, by, 290, 510, sel ? mk(48, 28, 72) : mk(26, 20, 44));
        fillRect(bx + 14, by + 18, 262, 340, mk(18, 16, 32));
        drawNarutoHD(i, bx + 85, by + 48, 6, 1); // Crisp 120x192 Pixel-Art Preview!
        drawTextC(bx + 145, by + 390, NAMES[i], 4, sel ? mk(255, 224, 92) : mk(235, 235, 245));
        drawTextC(bx + 145, by + 448, TITLES[i], 2, mk(145, 215, 245));
    }
    fillRect(420, 820, 1080, 64, mk(32, 24, 54));
    drawTextC(960, 842, "LEFT / RIGHT: CHOOSE   -   X: EQUIP CHARACTER   -   O: BACK", 2, mk(225, 225, 240));
}

static void drawItem(int i) {
    static const int MENU_T[7] = {T_PLAY, T_SHOP, T_SKINS, T_ACH, T_SETTINGS, T_CREDITS, T_EXIT};
    static const int SHOP_COST[4] = {30, 35, 40, 45}, SKIN_COST[5] = {0, 40, 50, 60, 75};
    if (state == S_MENU && i >= 0 && i < 7) {
        int y = 315 + i * 90; bool sel = (i == menuSel);
        if (sel) fillRect(606, y - 4, 708, 80, mk(255,210,85));
        fillRect(610, y, 700, 72, sel ? mk(215,30,45) : mk(48,36,68));
        drawLabel(MENU_T[i], 960, y, 72, 5, sel ? mk(255,255,255) : mk(215,205,230));
    } else if (state == S_LEVELS) {
        if (i >= 0 && i < TOTAL_STAGES) {
            int col = i % 4, row = i / 4;
            int x = (i == 16) ? 890 : (540 + col * 220), y = (i == 16) ? 770 : (290 + row * 116);
            bool sel = (i == levelSel), unl = (i < save.unlockedStages || (i == 16 && save.secretsFound > 0)), bs = isBossStage(i);
            if (sel) fillRect(x - 6, y - 6, 152, 92, mk(255,245,255));
            fillRect(x, y, 140, 80, unl ? (bs ? mk(175,24,48) : (i == 16 ? mk(48,145,112) : mk(205,42,58))) : mk(50,40,62));
            if (unl) { char buf[16]; if (i == 16) strcpy(buf, "SECRET"); else sprintf(buf, "%d-%d%s", row + 1, col + 1, bs ? " B" : ""); drawTextC(x + 70, y + 26, buf, 4, mk(255,255,255)); }
            else drawTextC(x + 70, y + 28, "LOCK", 3, mk(150,150,165));
        } else if (i == TOTAL_STAGES) {
            fillRect(540, 880, 840, 60, mk(35,25,55));
            drawLabel(T_W0 + (levelSel >= 16 ? 4 : (levelSel / 4)), 960, 880, 60, 3, mk(135,238,165));
        }
    } else if (state == S_SHOP && i >= 0 && i < 4) {
        int y = 330 + i * 115; bool sel = (i == shopSel);
        int owned = (i == 0 ? save.upHp : (i == 1 ? save.upDash : (i == 2 ? save.upMagnet : save.upSlam)));
        if (sel) fillRect(406, y - 4, 1108, 98, mk(255,210,85));
        fillRect(410, y, 1100, 90, sel ? mk(205,36,52) : mk(48,36,68));
        drawLabel(T_UP_HP + i, 840, y, 90, 4, mk(255,255,255));
        if (owned) drawLabel(T_BOUGHT, 1380, y, 90, 3, mk(115,245,145));
        else { char cb[16]; sprintf(cb, "%d C", SHOP_COST[i]); drawTextC(1380, y + 32, cb, 4, mk(255,215,75)); }
    } else if (state == S_SKINS && i >= 0 && i < 5) {
        int y = 300 + i * 112; bool sel = (i == skinSel), owned = (save.skinsOwned & (1u << i)) != 0;
        if (sel) fillRect(446, y - 4, 1028, 96, mk(255,210,85));
        fillRect(450, y, 1020, 88, sel ? mk(205,36,52) : mk(48,36,68));
        drawSkinCube(480, y + 22, 44, 44, 1, i); drawLabel(T_SK0 + i, 920, y, 88, 4, mk(255,255,255));
        if (save.selectedSkin == i) drawTextC(1350, y + 32, "EQUIPPED", 3, mk(115,245,145));
        else if (owned) drawLabel(T_BOUGHT, 1350, y, 88, 3, mk(200,235,255));
        else { char cb[16]; sprintf(cb, "%d C", SKIN_COST[i]); drawTextC(1350, y + 32, cb, 4, mk(255,215,75)); }
    } else if (state == S_SETTINGS && i >= 0 && i < 3) {
        int y = 380 + i * 110; bool sel = (i == settingsSel);
        if (sel) fillRect(506, y - 4, 908, 98, mk(255,210,85));
        fillRect(510, y, 900, 90, sel ? mk(215,30,45) : mk(48,36,68));
        drawLabel(i == 0 ? (controlMode ? T_CTRL_A : T_CTRL_B) : (i == 1 ? T_LANG : T_BACK), 960, y, 90, 5, mk(255,255,255));
    }
}

static void drawUI(int b) {
    resetClip();
    if (state == S_SECRET) { if (uiFull[b]) { drawSecretMenu(); uiFull[b] = false; } return; }
    if (uiFull[b]) {
        fillRect(0,0,FW,300,mk(36,122,205)); fillRect(0,300,FW,340,mk(92,188,238));
        fillRect(0,640,FW,280,mk(32,102,58)); fillRect(0,920,FW,160,mk(84,52,32)); fillRect(0,920,FW,12,mk(72,208,68));
        fillCircle(1640, 130, 58, mk(255,242,155)); fillCircle(1640, 130, 44, mk(255,222,72));
        for (int i = 0; i < 6; i++) { fillRect(i*340+40,560,220,120,mk(44,132,74)); fillRect(i*340+80,520,140,48,mk(44,132,74)); }
        for (int gx = 60; gx < FW - 60; gx += 64) drawPlantTuft(gx, 920, 15, SN[(tick/8)&15]/32, 0, (gx/64)%3);
        fillRect(50, 30, 220, 54, mk(32,24,54)); fillCircle(82, 57, 13, mk(255,208,45));
        char cb[16]; intToStr(save.coins, cb); drawText(108, 45, cb, 4, mk(255,235,110));

        if (state == S_MENU) { drawBanner(T_TITLE, 11, 110, 140, true); drawHint(T_H_SELECT); }
        else if (state == S_LEVELS) { drawBanner(T_B_LEVELS, 7, 100, 105, false); drawHint(T_H_LEVELS); }
        else if (state == S_SHOP) { drawBanner(T_B_SHOP, 7, 100, 105, false); drawHint(T_H_SELECT); }
        else if (state == S_SKINS) { drawBanner(T_B_SKINS, 7, 100, 105, false); drawHint(T_H_SELECT); }
        else if (state == S_ACH) {
            drawBanner(T_B_ACH, 7, 100, 105, false);
            for (int k = 0; k < 6; k++) { bool ok = (save.achMask & (1u << k)) != 0; int y = 260 + k * 105; fillRect(420, y, 1080, 82, ok ? mk(42,118,68) : mk(42,32,62)); drawLabel(T_ACH0 + k, 960, y, 82, 3, ok ? mk(255,245,145) : mk(165,160,185)); }
            drawHint(T_H_BACK);
        } else if (state == S_SETTINGS) { drawBanner(T_B_SETTINGS, 7, 100, 105, false); fillRect(610,740,700,60,mk(35,25,55)); drawLabel(T_TIP, 960, 740, 60, 3, mk(220,212,238)); drawHint(T_H_BACK); }
        else if (state == S_CREDITS) { drawBanner(T_B_CREDITS, 7, 100, 105, false); fillRect(460,280,1000,540,mk(35,25,55)); drawLabel(T_DESIGNERS,960,320,60,5,mk(220,212,238)); drawLabel(T_KAREEM,960,430,100,9,mk(255,75,115)); drawLabel(T_CLAUDE,960,550,100,9,mk(255,75,115)); drawLabel(T_GAME,960,710,60,4,mk(165,165,185)); drawHint(T_H_BACK); }
        int cnt = state == S_MENU ? 7 : (state == S_LEVELS ? TOTAL_STAGES + 1 : (state == S_SHOP ? 4 : (state == S_SKINS ? 5 : (state == S_SETTINGS ? 3 : 0))));
        for (int i = 0; i < cnt; i++) drawItem(i);
        uiFull[b] = false; memset(pend[b], 0, 64);
    } else {
        for (int i = 0; i < 64; i++) if (pend[b][i]) { drawItem(i); pend[b][i] = false; }
    }
}

static void renderStory(int b) {
    resetClip();
    if (!storyBgDone[b]) { fillRect(0, 0, FW, FH, mk(12,8,24)); storyBgDone[b] = true; storyDrawnPage[b] = -1; }
    if (storyDrawnPage[b] != storyPage) {
        fillRect(460, 90, 1000, 500, mk(26,42,78)); drawSkinCube(860, 320, 88, 88, 1, save.selectedSkin);
        fillRect(460, 630, 1000, 300, mk(30,22,48)); fillRect(460, 950, 1000, 60, mk(12,8,24));
        drawLabel(T_H_STORY, 960, 950, 60, 3, mk(200,190,220)); storyDrawnPage[b] = storyPage; storyShown[b] = 0;
    }
    const Page &p = PAGES[storyPage];
    if (typed > storyShown[b]) {
        bool ar = useAr(); int f = ar ? storyShown[b] * 14 : storyShown[b] / 2, t = ar ? typed * 14 : typed / 2, bef = 0;
        for (int i = 0; i < p.n; i++) {
            int y = 660 + i * 80;
            if (ar) { int id = p.ar[i]; if (id < 0) continue; int w = arW(id), a = iclamp(f - bef, 0, w), b2 = iclamp(t - bef, 0, w); if (b2 > a) drawArCols(id, 960 - w / 2, y + (70 - arH(id)) / 2, w - b2, w - a, mk(255,255,255)); bef += w; }
            else { int len = (int)strlen(p.en[i]), a = iclamp(f - bef, 0, len), b2 = iclamp(t - bef, 0, len), x0 = 960 - textWidth(p.en[i], 4) / 2; for (int k = a; k < b2; k++) { char tmp[2] = {p.en[i][k], 0}; drawText(x0 + k * 24, y + 21, tmp, 4, mk(255,255,255)); } bef += len; }
        }
        storyShown[b] = typed;
    }
}

static void updateUI() {
    bool nu = navEvent(0, in.up), nd = navEvent(1, in.down), nl = navEvent(2, in.left), nr = navEvent(3, in.right);
    if (state == S_SECRET) {
        int o = secretSel; if (nl) secretSel = (secretSel + 4) % 5; if (nr) secretSel = (secretSel + 1) % 5;
        if (secretSel != o) { snd(0); uiFull[0] = uiFull[1] = true; }
        if (in.crossNew) { save.selectedSkin = 5 + secretSel; writeSave(); snd(1); setState(S_MENU); }
        else if (in.circleNew) { snd(2); setState(S_MENU); }
        return;
    }
    if (state == S_MENU) {
        // SECRET MENU COMBO: Hold L2 + R2 + L1 and press X!
        if (in.l2 && in.r2 && in.l1 && in.crossNew) { snd(1); setState(S_SECRET); return; }
        int o = menuSel; if (nu) menuSel = (menuSel + 6) % 7; if (nd) menuSel = (menuSel + 1) % 7;
        if (menuSel != o) { markPend(o); markPend(menuSel); snd(0); }
        if (in.crossNew) {
            if (menuSel == 0) { snd(1); setState(S_LEVELS); }
            else if (menuSel == 1) { snd(1); setState(S_SHOP); }
            else if (menuSel == 2) { snd(1); setState(S_SKINS); }
            else if (menuSel == 3) { snd(1); setState(S_ACH); }
            else if (menuSel == 4) { snd(1); settingsSel = 0; setState(S_SETTINGS); }
            else if (menuSel == 5) { snd(1); setState(S_CREDITS); }
            else { snd(2); exit(0); }
        }
    } else if (state == S_LEVELS) {
        int o = levelSel, n = levelSel;
        if (nl && n > 0) n--; if (nr && n < TOTAL_STAGES - 1) n++;
        if (nu && n >= 4) n -= 4; if (nd && n + 4 < TOTAL_STAGES) n += 4;
        if (n != o) { levelSel = n; markPend(o); markPend(n); markPend(TOTAL_STAGES); snd(0); }
        bool unl = (levelSel < save.unlockedStages || (levelSel == 16 && save.secretsFound > 0));
        if ((in.crossNew || in.squareNew) && unl) {
            snd(1);
            if (levelSel == 0 && !introSeen && !in.squareNew) { introSeen = true; startStory(0); }
            else startStage(levelSel, in.squareNew);
        } else if (in.crossNew || in.squareNew) snd(3);
        else if (in.circleNew) { snd(2); setState(S_MENU); }
    } else if (state == S_SHOP) {
        static const int COST[4] = {30, 35, 40, 45};
        int o = shopSel; if (nu) shopSel = (shopSel + 3) % 4; if (nd) shopSel = (shopSel + 1) % 4;
        if (shopSel != o) { markPend(o); markPend(shopSel); snd(0); }
        if (in.crossNew) {
            int *ptr = shopSel == 0 ? &save.upHp : (shopSel == 1 ? &save.upDash : (shopSel == 2 ? &save.upMagnet : &save.upSlam));
            if (*ptr == 0 && save.coins >= COST[shopSel]) { save.coins -= COST[shopSel]; *ptr = 1; writeSave(); snd(1); setState(S_SHOP); }
            else snd(3);
        } else if (in.circleNew) { snd(2); setState(S_MENU); }
    } else if (state == S_SKINS) {
        static const int COST[5] = {0, 40, 50, 60, 75};
        int o = skinSel; if (nu) skinSel = (skinSel + 4) % 5; if (nd) skinSel = (skinSel + 1) % 5;
        if (skinSel != o) { markPend(o); markPend(skinSel); snd(0); }
        if (in.crossNew) {
            if (save.skinsOwned & (1u << skinSel)) { save.selectedSkin = skinSel; writeSave(); snd(1); setState(S_SKINS); }
            else if (save.coins >= COST[skinSel]) { save.coins -= COST[skinSel]; save.skinsOwned |= (1u << skinSel); save.selectedSkin = skinSel; writeSave(); snd(1); setState(S_SKINS); }
            else snd(3);
        } else if (in.circleNew) { snd(2); setState(S_MENU); }
    } else if (state == S_SETTINGS) {
        int o = settingsSel; if (nu) settingsSel = (settingsSel + 2) % 3; if (nd) settingsSel = (settingsSel + 1) % 3;
        if (settingsSel != o) { markPend(o); markPend(settingsSel); snd(0); }
        if (settingsSel == 0 && (nl || nr || in.crossNew)) { controlMode = 1 - controlMode; markPend(0); snd(0); }
        else if (settingsSel == 1 && (nl || nr || in.crossNew)) { if (ARABIC_OK) { lang = 1 - lang; snd(1); setState(S_SETTINGS); } else snd(3); }
        else if (in.crossNew || in.circleNew) { snd(2); setState(S_MENU); }
    } else if ((state == S_CREDITS || state == S_ACH) && (in.crossNew || in.circleNew)) { snd(2); setState(S_MENU); }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    scene = new Scene2D(FW, FH, 4);
    if (!scene->Init(0xC000000, 2) || !initPad()) for (;;);
    loadSave(); initAudio(); setState(S_MENU); snd(1);
    for (;;) {
        int b = frameID & 1; readInput();
        if (state == S_PLAY) {
            if (in.optionsNew) setState(S_LEVELS);
            else if (winTimer > 0) {
                if (--winTimer == 0) {
                    if (curStage == 15) startStory(1);
                    else if (curStage + 1 < TOTAL_STAGES && !timeTrialMode) startStage(curStage + 1, false);
                    else setState(S_LEVELS);
                }
            } else update();
        } else if (state == S_STORY) {
            const Page &p = PAGES[storyPage]; int tot = 0;
            if (useAr()) { for (int i = 0; i < p.n; i++) if (p.ar[i] >= 0) tot += arW(p.ar[i]); tot = tot / 14 + 1; }
            else { for (int i = 0; i < p.n; i++) tot += (int)strlen(p.en[i]); tot *= 2; }
            storyAnimTick++; if (typed < tot) typed++;
            if (in.crossNew) {
                if (typed < tot) typed = tot;
                else { snd(0); storyPage++; typed = storyAnimTick = 0; if (storyPage >= STORY_LAST[storyId]) { if (storyId == 0) startStage(0, false); else setState(S_LEVELS); } }
            }
            if (state == S_STORY && in.optionsNew) { if (storyId == 0) startStage(0, false); else setState(S_LEVELS); }
        } else updateUI();

        if (state == S_PLAY) renderGame(b); else if (state == S_STORY) renderStory(b); else drawUI(b);
        scene->SubmitFlip(frameID); updateGameplayAudio(); scene->FrameWait(frameID); scene->FrameBufferSwap();
        frameID++; tick++;
    }
    return 0;
}
// ====== نهاية الجزء 2 ======
