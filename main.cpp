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
#define NUM_LEVELS 55
#define LCOLS 11
#define NUM_STARS 75
#define MAX_SPLATS 180
#define MAX_PARTS 80
#define MAX_PLANTS 640
#define MAX_AMB 18
#define MAX_BIRDS 4
#define MAX_SHOTS 16
#define MAX_ENEMIES 64
#define MAX_CHECKS 6
#define MAX_DIRTY 260
#define MAX_COINS 96
#define MAX_CHESTS 12
#define MAX_MPLATS 16
#define MAX_DPLATS 24
#define MAX_POWERUPS 12
#define MAX_NPCS 6
#define MAX_TRAIL 4
#define NUM_SKINS 6
#define NUM_UPGS 3
#define NUM_ACHS 6
#define SPLAT_MAX_AGE 5400

std::stringstream debugLogStream;
static Scene2D *scene;
static char level[ROWS][COLS + 1];
static int padHandle = -1, padUserID = 0;
static OrbisPadData padData;
#ifndef NO_AUDIO
static int audioHandle = -1;
#endif

// ---------- Game & World State ----------
static float px, py, vx, vy, squashX = 1.0f, squashY = 1.0f, cpX = 80.0f, cpY = 900.0f;
static int roomIdx = 0, levelCols = COLS, levelPW = WW, curLvl = 0, curBiome = 0;
static int coyote = 0, wallLock = 0, facing = 1, deaths = 0, winTimer = 0, deadTimer = 0, tick = 0, frameID = 0;
static bool onGround = false, hasDoubleJump = true, isSlamming = false;
static int jumpsLeft = 1, dashTimer = 0, dashCd = 0, dashDir = 1, slamWaveTimer = 0, slamWaveX = 0, slamWaveY = 0;
static int shakeTimer = 0, shakeMag = 0, shakeX = 0, shakeY = 0;
static int shieldHits = 0, speedBoostTimer = 0, magnetTimer = 0;
static int totalCoins = 0, stageCoins = 0, stageTimer = 0, secretsFound = 0, totalChestsOpened = 0;
static int bestTime[NUM_LEVELS];
static int curSkin = 0;
static bool skinOwned[NUM_SKINS] = {true, false, false, false, false, false};
static bool upgOwned[NUM_UPGS] = {false, false, false};
static bool achUnlocked[NUM_ACHS] = {false, false, false, false, false, false};
static int bannerTimer = 0, bannerTextId = -1;
static unsigned int rng = 12345;
static int checkC[MAX_CHECKS], checkR[MAX_CHECKS], checkCount = 0;
static bool checkAct[MAX_CHECKS];

struct Trail { float x, y; int life, dir; };
static Trail trails[MAX_TRAIL];

enum { E_SLIME = 0, E_BAT = 1, E_MUSHROOM = 2, E_TURRET = 3, E_WASP = 4, E_CHOMPER = 5 };
struct Enemy {
    float x, y, vx, vy, minX, maxX, baseY;
    int kind, hp, maxHp, hurt, timer, dir, state, deathTimer;
    bool active;
};
static Enemy enemies[MAX_ENEMIES];
static int enemyCount = 0;

enum { B_KING_FROGUS = 0, B_CHOMP_TOTEM = 1, B_FROST_LORD = 2, B_MAGMA_CRUSHER = 3, B_SAW_OVERLORD = 4 };
static bool bossActive = false;
static int bossKind = B_KING_FROGUS;
static float bossX, bossY, bossVX, bossVY;
static int bossW = 148, bossH = 132, bossHP = 8, bossMaxHP = 8, bossHurt = 0, bossTimer = 0, bossSwCd = 0, bossBeam = 0;
static int bossPhase = 0, bossAttackState = 0, bossTongueLen = 0, bossTongueDir = -1;

struct Shot { float x, y, vx, vy; int kind; bool active; };
static Shot shots[MAX_SHOTS];

struct Coin { int room, x, y; bool taken, secret; };
static Coin coins[MAX_COINS];
static int coinCount = 0;

struct Chest { int room, x, y; bool opened; };
static Chest chests[MAX_CHESTS];
static int chestCount = 0;

struct MPlat { int room; float x, y, minX, maxX, minY, maxY, vx, vy; int w; };
static MPlat mplats[MAX_MPLATS];
static int mplatCount = 0;

struct DPlat { int room, c, r, timer; bool triggered; };
static DPlat dplats[MAX_DPLATS];
static int dplatCount = 0;

enum { PU_SHIELD = 0, PU_SPEED = 1, PU_MAGNET = 2 };
struct PowerUp { int room, x, y, kind; bool active; };
static PowerUp powerups[MAX_POWERUPS];
static int powerupCount = 0;

struct NPC { int room, x, y, msgId; };
static NPC npcs[MAX_NPCS];
static int npcCount = 0, activeNpcMsg = -1;

struct Splat { short room, x, y, w, h; int born; unsigned char stage, dStage[2]; bool drawn[2]; };
static Splat splats[MAX_SPLATS];
static int splatCount = 0;

struct Part { float x, y, vx, vy; short life, maxLife; unsigned char kind, size; };
static Part parts[MAX_PARTS];

struct Plant { short room, x, y; float angle, vel; unsigned char h, style, seed; signed char dBend[2]; bool drawn[2]; };
static Plant plants[MAX_PLANTS];
static int plantCount = 0;

struct Amb { float x, y, vx, vy; unsigned char sz, seed, kind; };
static Amb amb[MAX_AMB];

struct Rect { int x, y, w, h; };
static Rect dirty[2][MAX_DIRTY];
static int dirtyCnt[2] = {0, 0}, bufRoom[2] = {0, 0}, bufStar[2] = {0, 0}, hudSq[2] = {0, 0}, hudProg[2] = {0, 0}, hudCoins[2] = {-1, -1};
static bool needFull[2] = {true, true}, needWin[2] = {false, false}, hudNeed[2] = {false, false};
static char bufGrid[2][ROWS][RCOLS];
static bool bufHi[2][ROWS][RCOLS];
static int clipX0 = 0, clipY0 = 0, clipX1 = FW, clipY1 = FH;

static const int CS[16] = {256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98,0,98,181,237};
static const int SN[16] = {0,98,181,237,256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98};

// ---------- UI & Story State ----------
enum { S_MENU = 0, S_LEVELS = 1, S_SETTINGS = 2, S_CREDITS = 3, S_PLAY = 4, S_STORY = 5, S_SHOP = 6, S_ACH = 7 };
static int state = S_MENU, menuSel = 0, levelSel = 0, settingsSel = 0, shopSel = 0, achSel = 0;
static int controlMode = 1, lang = 0, unlockedLevels = 1;
static bool introSeen = false, uiFull[2] = {true, true}, pend[2][64];
static int storyId = 0, storyPage = 0, typed = 0, storyAnimTick = 0, storyDrawnPage[2] = {-1, -1}, storyShown[2] = {0, 0};
static bool storyBgDone[2] = {false, false};

#define TEXT_LIST \
    X(T_TITLE,"MEAT CUBE",AR(TITLE)) X(T_B_LEVELS,"LEVELS",AR(B_LEVELS)) X(T_B_SETTINGS,"SETTINGS",AR(B_SETTINGS)) \
    X(T_B_CREDITS,"CREDITS",AR(B_CREDITS)) X(T_LEVELS,"LEVELS",AR(LEVELS)) X(T_SETTINGS,"SETTINGS",AR(SETTINGS)) \
    X(T_CREDITS,"CREDITS",AR(CREDITS)) X(T_EXIT,"EXIT",AR(EXIT)) X(T_BACK,"BACK",AR(BACK)) \
    X(T_CTRL_A,"CONTROLS: ANALOG",AR(CTRL_ANALOG)) X(T_CTRL_B,"CONTROLS: ARROWS",AR(CTRL_ARROWS)) \
    X(T_LANG,"LANGUAGE: ENGLISH",AR(LANG)) X(T_H_SELECT,"X: SELECT",AR(SELECT)) \
    X(T_H_LEVELS,"X: PLAY   O: BACK",AR(LEVELS_HINT)) X(T_H_BACK,"O: BACK",AR(BACK_HINT)) \
    X(T_TIP,"LEFT RIGHT OR X: CHANGE",AR(CHANGE_TIP)) X(T_H_STORY,"X: NEXT   OPTIONS: SKIP",AR(STORY_HINT)) \
    X(T_PLAYX,"PRESS X TO PLAY",AR(PLAYX)) X(T_LOCKED,"LOCKED",AR(LOCKED)) X(T_DESIGNERS,"GAME DESIGNERS",AR(DESIGNERS)) \
    X(T_KAREEM,"KAREEM",AR(KAREEM)) X(T_CLAUDE,"CLAUDE",AR(CLAUDE)) X(T_GAME,"MEAT CUBE",AR(GAME_NAME)) \
    X(T_DEATHS,"DEATHS",AR(DEATHS)) X(T_BOSS_HP,"BOSS HP",AR(BOSS_HP)) X(T_COMPLETE,"LEVEL COMPLETE",AR(COMPLETE)) \
    X(T_GREAT,"GREAT JOB",AR(GREAT)) \
    X(T_B_SHOP,"SHOP AND SKINS",AR(B_SHOP)) X(T_B_ACH,"ACHIEVEMENTS",AR(B_ACH)) \
    X(T_SHOP,"SHOP AND SKINS",AR(SHOP)) X(T_ACH,"ACHIEVEMENTS",AR(ACH)) \
    X(T_COINS,"COINS",AR(COINS)) X(T_TIME_LBL,"TIME",AR(TIME_LBL)) X(T_SECRETS_LBL,"SECRETS",AR(SECRETS_LBL)) \
    X(T_KING_FROGUS,"KING FROGUS",AR(KING_FROGUS)) X(T_CHOMP_BEAST,"BEHEMOTH CHOMPER",AR(CHOMP_BEAST)) \
    X(T_FROST_LORD,"FROST CITADEL LORD",AR(FROST_LORD)) X(T_MAGMA_CRUSHER,"MAGMA CRUSHER",AR(MAGMA_CRUSHER)) \
    X(T_SAW_OVERLORD,"DARK SAW OVERLORD",AR(SAW_OVERLORD)) \
    X(T_H_SHOP,"X: BUY OR EQUIP   O: BACK",AR(SHOP_HINT)) X(T_BOUGHT,"EQUIPPED",AR(BOUGHT)) X(T_EQUIP,"PRESS X TO EQUIP",AR(EQUIP)) \
    X(T_SKIN0,"CLASSIC MEAT CUBE",AR(SKIN0)) X(T_SKIN1,"ROYAL GOLDEN CUBE",AR(SKIN1)) \
    X(T_SKIN2,"SHADOW NINJA CUBE",AR(SKIN2)) X(T_SKIN3,"FROST GLACIER CUBE",AR(SKIN3)) \
    X(T_SKIN4,"BLAZING MAGMA CUBE",AR(SKIN4)) X(T_SKIN5,"EMERALD SLIME CUBE",AR(SKIN5)) \
    X(T_UPG0,"UPGRADE: START STAGE WITH SHIELD",AR(UPG0)) X(T_UPG1,"UPGRADE: FAST DASH COOLDOWN",AR(UPG1)) \
    X(T_UPG2,"UPGRADE: COIN MAGNET AND 2X GOLD",AR(UPG2)) \
    X(T_ACH0,"FIRST STEP: CLEAR STAGE 1",AR(ACH0)) X(T_ACH1,"TREASURE HUNTER: OPEN 3 SECRET CHESTS",AR(ACH1)) \
    X(T_ACH2,"FROG SLAYER: DEFEAT KING FROGUS",AR(ACH2)) X(T_ACH3,"GOLD COLLECTOR: COLLECT 100 COINS",AR(ACH3)) \
    X(T_ACH4,"SPEEDRUNNER: CLEAR A STAGE UNDER 35S",AR(ACH4)) X(T_ACH5,"LEGEND: UNLOCK 15 STAGES",AR(ACH5)) \
    X(T_NPC0,"ELDER: PRESS X IN AIR FOR DOUBLE JUMP, DOWN+O FOR SLAM!",AR(NPC_MSG0)) \
    X(T_NPC1,"SCOUT: CRACKED WALLS HIDE SECRET TREASURE ROOMS!",AR(NPC_MSG1)) \
    X(T_NPC2,"GUARD: BEWARE KING FROGUS TONGUE AND HIDING MUSHROOMS!",AR(NPC_MSG2)) \
    X(T_SECRET_FOUND,"SECRET AREA DISCOVERED!",AR(SECRET_FOUND)) X(T_CHEST_OPEN,"TREASURE CHEST! +25 GOLD COINS",AR(CHEST_OPEN))

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
static const Page PAGES[11] = {
    {0,2,{"MEAT CUBE AND BANDAGE GIRL","LIVED IN THE OLD FOREST.",""},{AR(P0L0),AR(P0L1),-1}},
    {1,2,{"ONE NIGHT THE GRINDER CAME.","HIS SAWS TOOK HER AWAY.",""},{AR(P1L0),AR(P1L1),-1}},
    {2,2,{"MEAT CUBE WOKE UP ANGRY.","I WILL BRING YOU BACK!",""},{AR(P2L0),AR(P2L1),-1}},
    {3,3,{"MEAT CUBE REACHED THE TOWER.","BUT SHE WAS NOT THERE.","ONLY A NOTE: FOLLOW THE SAWS."},{AR(P3L0),AR(P3L1),AR(P3L2)}},
    {1,1,{"TO BE CONTINUED.","",""},{AR(P4L0),-1,-1}},
    {10,3,{"STAGE 1: LIVING FOREST - DOUBLE JUMP & SLAM","TAP X TO JUMP, TAP X IN AIR FOR DOUBLE JUMP.","PRESS DOWN + O IN AIR FOR GROUND SLAM!"},{AR(T1L0),AR(T1L1),AR(T1L2)}},
    {11,3,{"STAGE 2: COUNTRYSIDE - DASH & PLATFORMS","USE DASH TO CROSS GAPS AND MOVING PLATFORMS.","FIND SECRET WALLS AND TREASURE CHESTS!"},{AR(T2L0),AR(T2L1),AR(T2L2)}},
    {12,3,{"STAGE 3: GOLDEN DESERT - TWO-WALL JUMP","ENTER BETWEEN TWO WALLS AND SLIDE SLOWLY.","PRESS X TO CLIMB AND WATCH FOR BATS!"},{AR(T3L0),AR(T3L1),AR(T3L2)}},
    {13,3,{"STAGE 4: SNOWY PEAKS - LEDGE TIMING","WATCH SPINNING SAWS AND DISAPPEARING LEDGES.","COLLECT SHIELDS AND GOLD COINS TO UPGRADE!"},{AR(T4L0),AR(T4L1),AR(T4L2)}},
    {14,3,{"STAGE 5: BOSS FIGHT - KING FROGUS!","DODGE THE SPIKED TONGUE AND STOMP HIS CROWN.","WATCH OUT FOR SLIMES AND QUAKE JUMPS!"},{AR(T5L0),AR(T5L1),AR(T5L2)}},
    {15,3,{"STAGE 10: BOSS FIGHT - BEHEMOTH & OVERLORD!","BOUNCE ON THE BOSS HEAD USING DOUBLE JUMP.","DODGE DOUBLE SHOTS AND SAVE THE REALM!"},{AR(T10L0),AR(T10L1),AR(T10L2)}}
};
static const int STORY_FIRST[8] = {0,3,5,6,7,8,9,10}, STORY_LAST[8] = {3,5,6,7,8,9,10,11};

struct Input {
    bool up, down, left, right, dleft, dright, cross, circle, options, square, triangle, r1, l1;
    bool crossNew, circleNew, optionsNew, squareNew, triangleNew, dashBtnNew;
    float ana;
};
static Input in;
static bool prevCross = false, prevCircle = false, prevOptions = false, prevSquare = false, prevTriangle = false, prevDashBtn = false;
static int holdCnt[4] = {0, 0, 0, 0};

static inline int iabs(int a) { return a < 0 ? -a : a; }
static inline int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int rnd(int n) { rng = rng * 1103515245u + 12345u; return (int)((rng >> 16) % (unsigned int)n); }
static inline Color mk(int r, int g, int b) { Color c = {(unsigned char)r, (unsigned char)g, (unsigned char)b}; return c; }
static inline bool useAr() { return lang == 1 && ARABIC_OK; }
static inline bool isBossStage(int lvl) { return ((lvl + 1) % 5) == 0; }

struct SaveData {
    unsigned int magic;
    int unlockedLevels, totalCoins, curSkin, totalChestsOpened, lang, controlMode;
    bool skinOwned[NUM_SKINS], upgOwned[NUM_UPGS], achUnlocked[NUM_ACHS];
    int bestTime[NUM_LEVELS];
};

static void saveGame() {
    SaveData sd;
    sd.magic = 0x4D435542u; sd.unlockedLevels = unlockedLevels; sd.totalCoins = totalCoins;
    sd.curSkin = curSkin; sd.totalChestsOpened = totalChestsOpened; sd.lang = lang; sd.controlMode = controlMode;
    for (int i = 0; i < NUM_SKINS; i++) sd.skinOwned[i] = skinOwned[i];
    for (int i = 0; i < NUM_UPGS; i++) sd.upgOwned[i] = upgOwned[i];
    for (int i = 0; i < NUM_ACHS; i++) sd.achUnlocked[i] = achUnlocked[i];
    for (int i = 0; i < NUM_LEVELS; i++) sd.bestTime[i] = bestTime[i];
    FILE *f = fopen("/data/meatcube_save.dat", "wb");
    if (f) { fwrite(&sd, sizeof(SaveData), 1, f); fclose(f); }
}

static void loadGame() {
    for (int i = 0; i < NUM_LEVELS; i++) bestTime[i] = 0;
    FILE *f = fopen("/data/meatcube_save.dat", "rb");
    if (!f) return;
    SaveData sd;
    if (fread(&sd, sizeof(SaveData), 1, f) == 1 && sd.magic == 0x4D435542u) {
        unlockedLevels = iclamp(sd.unlockedLevels, 1, NUM_LEVELS);
        totalCoins = iclamp(sd.totalCoins, 0, 999999);
        curSkin = iclamp(sd.curSkin, 0, NUM_SKINS - 1);
        totalChestsOpened = iclamp(sd.totalChestsOpened, 0, 9999);
        lang = iclamp(sd.lang, 0, 1); controlMode = iclamp(sd.controlMode, 0, 1);
        for (int i = 0; i < NUM_SKINS; i++) skinOwned[i] = sd.skinOwned[i];
        skinOwned[0] = true;
        for (int i = 0; i < NUM_UPGS; i++) upgOwned[i] = sd.upgOwned[i];
        for (int i = 0; i < NUM_ACHS; i++) achUnlocked[i] = sd.achUnlocked[i];
        for (int i = 0; i < NUM_LEVELS; i++) bestTime[i] = sd.bestTime[i];
    }
    fclose(f);
}

static void showBanner(int txtId, int dur) { bannerTextId = txtId; bannerTimer = dur; hudNeed[0] = hudNeed[1] = true; }
static void unlockAch(int idx) {
    if (idx < 0 || idx >= NUM_ACHS || achUnlocked[idx]) return;
    achUnlocked[idx] = true; totalCoins += 30; showBanner(T_ACH0 + idx, 140); saveGame();
}

// ---------- Controller ----------
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
    in.cross = (b & ORBIS_PAD_BUTTON_CROSS) != 0;
    in.circle = (b & ORBIS_PAD_BUTTON_CIRCLE) != 0;
    in.options = (b & ORBIS_PAD_BUTTON_OPTIONS) != 0;
    in.square = (b & ORBIS_PAD_BUTTON_SQUARE) != 0;
    in.triangle = (b & ORBIS_PAD_BUTTON_TRIANGLE) != 0;
    in.r1 = (b & ORBIS_PAD_BUTTON_R1) != 0;
    in.l1 = (b & ORBIS_PAD_BUTTON_L1) != 0;
    bool dashBtn = in.r1 || in.l1 || in.triangle;
    in.crossNew = in.cross && !prevCross;
    in.circleNew = in.circle && !prevCircle;
    in.optionsNew = in.options && !prevOptions;
    in.squareNew = in.square && !prevSquare;
    in.triangleNew = in.triangle && !prevTriangle;
    in.dashBtnNew = dashBtn && !prevDashBtn;
    prevCross = in.cross; prevCircle = in.circle; prevOptions = in.options;
    prevSquare = in.square; prevTriangle = in.triangle; prevDashBtn = dashBtn;
}

static bool navEvent(int i, bool p) {
    if (!p) { holdCnt[i] = 0; return false; }
    holdCnt[i]++;
    return holdCnt[i] == 1 || (holdCnt[i] > 20 && ((holdCnt[i] - 20) % 6) == 0);
}

// ---------- PS4 Halal Audio Engine ----------
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
        if (k == 0) playToneImmediate(880, 25, 16000);
        else if (k == 1) { playToneImmediate(660, 35, 18000); playToneImmediate(990, 55, 18000); }
        else if (k == 2) playToneImmediate(440, 45, 16000);
        else playToneImmediate(220, 80, 16000);
        return;
    }
    static const int LENS[14] = {1400, 4000, 2600, 3500, 2800, 3400, 6500, 7200, 13000, 2200, 2600, 5400, 8200, 7600};
    curSfx = iclamp(k, 0, 13); sfxPos = 0; sfxPhase = 0; sfxLen = LENS[curSfx];
}

static void updateGameplayAudio() {
#ifndef NO_AUDIO
    if (audioHandle < 0 || state != S_PLAY) return;
    static short outBuf[512];
    static const int BIOME_BASS[6][4] = {
        {110, 130, 146, 130}, {123, 146, 164, 146}, {116, 138, 155, 130},
        {130, 164, 196, 164}, {98,  110, 123, 104}, {92,  110, 138, 104}
    };
    static const int BIOME_HARM[6][4] = {
        {165, 195, 220, 195}, {185, 220, 246, 220}, {174, 207, 233, 195},
        {196, 246, 293, 246}, {146, 165, 185, 155}, {138, 165, 207, 155}
    };
    static const unsigned char PAT[16] = {1,0,3,2,1,0,2,0,1,1,3,2,0,2,3,2};
    static const unsigned char BOSS_PAT[16] = {1,3,2,3,1,1,2,3,1,3,2,1,2,3,2,2};
    bool playBgm = (winTimer == 0);
    int stepS = bossActive ? 4600 : (curBiome == 4 || curBiome == 5 ? 5600 : 6400);
    int bm = iclamp(curBiome, 0, 5);
    for (int blk = 0; blk < 2; blk++) {
        for (int i = 0; i < 256; i++) {
            int mix = 0;
            if (playBgm) {
                int step = (bgmSample / stepS) & 15, inS = bgmSample % stepS, bar = (bgmSample / (stepS * 16)) & 3;
                bgmSample++;
                unsigned char hit = bossActive ? BOSS_PAT[step] : PAT[step];
                if (hit == 1 && inS < 3200) {
                    int env = 3200 - inS;
                    if ((duffP += 68 + (env >> 6)) >= 48000) duffP -= 48000;
                    mix += ((duffP < 24000 ? duffP - 12000 : 36000 - duffP) * env) / 4200;
                } else if (hit == 2 && inS < 1400) {
                    int env = 1400 - inS;
                    noiseRng = noiseRng * 1664525u + 1013904223u;
                    int nz = ((int)(noiseRng >> 20) & 511) - 256;
                    if ((duffP += 280) >= 48000) duffP -= 48000;
                    mix += ((nz + (duffP < 24000 ? 280 : -280)) * env) / 280;
                } else if (hit == 3 && inS < 750) {
                    noiseRng = noiseRng * 1664525u + 1013904223u;
                    mix += ((((int)(noiseRng >> 22) & 255) - 128) * (750 - inS)) / 320;
                }
                int f1 = BIOME_BASS[bm][bar] + (bossActive ? 24 : 0);
                int f2 = BIOME_HARM[bm][bar] + (bossActive ? 36 : 0);
                if ((humP1 += f1) >= 48000) humP1 -= 48000;
                if ((humP2 += f2) >= 48000) humP2 -= 48000;
                int v1 = humP1 < 24000 ? humP1 - 12000 : 36000 - humP1;
                int v2 = humP2 < 24000 ? humP2 - 12000 : 36000 - humP2;
                int br = inS < (stepS / 2) ? inS : (stepS - inS);
                mix += ((v1 + v2) * (1400 + br / 6)) / 10000;
            }
            if (curSfx >= 0) {
                int rem = sfxLen - sfxPos;
                if (rem <= 0) curSfx = -1;
                else {
                    int f = 440, vol = 14000;
                    if (curSfx == 0) { f = 720; vol = 12000; }
                    else if (curSfx == 1) f = sfxPos < 1800 ? 580 : 880;
                    else if (curSfx == 4) { f = 260 + (sfxPos >> 3); vol = 13000; }
                    else if (curSfx == 5) { f = 350 + (sfxPos >> 2); vol = 14500; }
                    else if (curSfx == 6) { f = iclamp(165 - (sfxPos >> 6), 55, 200); vol = 16000; }
                    else if (curSfx == 7) { f = iclamp(220 - (sfxPos >> 6), 65, 250); vol = 17500; }
                    else if (curSfx == 8) { f = sfxPos < 4200 ? 440 : (sfxPos < 8400 ? 550 : 660); vol = 16000; }
                    else if (curSfx == 9) { f = sfxPos < 900 ? 987 : 1318; vol = 15500; }
                    else if (curSfx == 10) { f = iclamp(640 - (sfxPos >> 2), 180, 680); vol = 15000; }
                    else if (curSfx == 11) { f = iclamp(140 - (sfxPos >> 6), 45, 160); vol = 18500; }
                    else if (curSfx == 12) { f = sfxPos < 2600 ? 523 : (sfxPos < 5200 ? 659 : 1046); vol = 16500; }
                    else if (curSfx == 13) { f = iclamp(115 + ((sfxPos >> 5) & 31), 85, 180); vol = 17500; }
                    if ((sfxPhase += f) >= 48000) sfxPhase -= 48000;
                    int tri = sfxPhase < 24000 ? sfxPhase : 48000 - sfxPhase;
                    int s = ((tri - 12000) * vol) / 12000;
                    if (curSfx == 6 || curSfx == 7 || curSfx == 10 || curSfx == 11 || curSfx == 13) {
                        noiseRng = noiseRng * 1664525u + 1013904223u;
                        s += (((int)(noiseRng >> 19) & 1023) - 512) * 6;
                    }
                    if (rem < 1200) s = (s * rem) / 1200;
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
        int g = glyphIndex(*s);
        if (g == 40) continue;
        for (int r = 0; r < 7; r++)
            for (int col = 0; col < 5; col++)
                if (FONT[g][r] & (0x10 >> col)) fillRect(x + col * sc, y + r * sc, sc, sc, c);
    }
}
static int textWidth(const char *s, int sc) { int n = (int)strlen(s); return n > 0 ? n * 6 * sc - sc : 0; }
static void drawTextC(int cx, int y, const char *s, int sc, Color c) { drawText(cx - textWidth(s, sc) / 2, y, s, sc, c); }
static void intToStr(int v, char *o) {
    if (v < 0) v = 0;
    if (v >= 1000) {
        o[0] = '0' + (v / 1000) % 10; o[1] = '0' + (v / 100) % 10;
        o[2] = '0' + (v / 10) % 10;   o[3] = '0' + v % 10; o[4] = 0;
    } else if (v >= 100) {
        o[0] = '0' + (v / 100) % 10; o[1] = '0' + (v / 10) % 10; o[2] = '0' + v % 10; o[3] = 0;
    } else if (v >= 10) {
        o[0] = '0' + v / 10; o[1] = '0' + v % 10; o[2] = 0;
    } else {
        o[0] = '0' + v; o[1] = 0;
    }
}

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

// ---------- Level, Enemy, Secrets, Platforms, Plant & Ambient Builders ----------
static int baseCol = 0;
static void put(int r, int c, char ch) { int g = baseCol + c; if (r >= 0 && r < ROWS && g >= 0 && g < COLS) level[r][g] = ch; }
static void plat(int r, int a, int b) { for (int c = a; c <= b; c++) put(r, c, '#'); }
static void wall(int c, int r0, int r1) { for (int r = r0; r <= r1; r++) put(r, c, '#'); }
static void spk(int r, int a, int b) { for (int c = a; c <= b; c++) put(r, c, '^'); }
static void sawAt(int r, int c) { put(r, c, 'O'); }
static void crackFloor(int r, int a, int b) { for (int c = a; c <= b; c++) put(r, c, 'B'); }
static void addCP(int c, int r) { if (checkCount < MAX_CHECKS) { checkC[checkCount] = baseCol + c; checkR[checkCount] = r; checkAct[checkCount++] = false; } }

static void addCoin(int c, int r, bool sec = false) {
    if (coinCount >= MAX_COINS) return;
    int gc = baseCol + c;
    coins[coinCount++] = {gc / RCOLS, (gc % RCOLS) * TILE + 18, r * TILE + 16, false, sec};
}
static void addCoinArc(int c0, int r0, int cnt) {
    for (int i = 0; i < cnt; i++) addCoin(c0 + i, r0 - ((i > 0 && i < cnt - 1) ? 1 : 0), false);
}
static void addChest(int c, int r) {
    if (chestCount >= MAX_CHESTS) return;
    int gc = baseCol + c;
    chests[chestCount++] = {gc / RCOLS, (gc % RCOLS) * TILE + 8, r * TILE - 38, false};
}
static void addMPlat(int c, int r, int cMin, int cMax, float spd) {
    if (mplatCount >= MAX_MPLATS) return;
    int gc = baseCol + c, rm = gc / RCOLS;
    MPlat &mp = mplats[mplatCount++];
    mp.room = rm; mp.x = (float)((gc % RCOLS) * TILE); mp.y = (float)(r * TILE);
    mp.minX = (float)(cMin * TILE); mp.maxX = (float)(cMax * TILE);
    mp.minY = mp.maxY = mp.y; mp.vx = spd; mp.vy = 0; mp.w = TILE * 2;
}
static void addDPlat(int c, int r, int cnt) {
    for (int i = 0; i < cnt && dplatCount < MAX_DPLATS; i++) {
        int gc = baseCol + c + i; put(r, c + i, '#');
        dplats[dplatCount++] = {gc / RCOLS, gc, r, 0, false};
    }
}
static void addPowerUp(int c, int r, int kind) {
    if (powerupCount >= MAX_POWERUPS) return;
    int gc = baseCol + c;
    powerups[powerupCount++] = {gc / RCOLS, (gc % RCOLS) * TILE + 14, r * TILE - 44, kind, true};
}
static void addNPC(int c, int r, int msgId) {
    if (npcCount >= MAX_NPCS) return;
    int gc = baseCol + c;
    npcs[npcCount++] = {gc / RCOLS, (gc % RCOLS) * TILE + 10, r * TILE - 46, msgId};
}

static void addE(int k, int c, int r, int a, int b) {
    if (enemyCount >= MAX_ENEMIES) return;
    Enemy &e = enemies[enemyCount++];
    e.kind = k; e.x = (float)((baseCol + c) * TILE + 8);
    e.y = e.baseY = (float)(r * TILE - (k == E_CHOMPER ? 68 : (k == E_WASP ? 58 : 44)));
    e.minX = (float)((baseCol + a) * TILE + 4); e.maxX = (float)((baseCol + b) * TILE + 12);
    e.dir = 1; e.vx = (k == E_SLIME) ? 2.6f : (k == E_BAT ? 3.1f : (k == E_CHOMPER ? 4.4f : (k == E_WASP ? 2.8f : 2.3f)));
    e.vy = 0; e.hp = e.maxHp = (k == E_CHOMPER || k == E_WASP) ? 2 : 1;
    e.hurt = 0; e.timer = (c * 17) & 63; e.state = 0; e.deathTimer = 0; e.active = true;
}

static void initWorldDetails() {
    plantCount = 0;
    for (int r = 2; r < ROWS; r++) {
        for (int c = 1; c < levelCols - 1; c++) {
            if (level[r][c] != '#' || level[r - 1][c] != '.') continue;
            bool skip = false;
            for (int dr = -2; dr <= 0 && !skip; dr++)
                for (int dc = -1; dc <= 1; dc++)
                    if (r + dr >= 0 && c + dc >= 0 && c + dc < COLS && (level[r + dr][c + dc] == 'O' || level[r + dr][c + dc] == 'G')) skip = true;
            if (skip) continue;
            for (int s = 0; s < 2 && plantCount < MAX_PLANTS; s++) {
                Plant &p = plants[plantCount++];
                p.room = (short)(c / RCOLS); p.x = (short)((c % RCOLS) * TILE + 15 + s * 28 + ((c * 5 + r * 3 + s * 7) % 7));
                p.y = (short)(r * TILE); p.angle = p.vel = 0; p.h = (unsigned char)(11 + ((c * 3 + r * 7 + s * 5) % 6));
                p.style = (unsigned char)((c + r + s) % 3); p.seed = (unsigned char)((c * 5 + r * 9 + s * 11) & 15);
                p.drawn[0] = p.drawn[1] = false; p.dBend[0] = p.dBend[1] = 99;
            }
        }
    }
    static const float AVX[6] = {0.6f, 0.9f, 3.2f, -0.5f, -0.3f, 0.3f};
    static const float AVY[6] = {0.8f, 0.3f, 0.4f, 1.4f, -1.3f, -0.4f};
    static const unsigned char ASZ[6] = {6, 5, 4, 5, 5, 5};
    for (int i = 0; i < MAX_AMB; i++) {
        Amb &a = amb[i]; a.x = (float)((i * 173 + 47) % FW); a.y = (float)(110 + ((i * 113 + 31) % (FH - 260)));
        a.seed = (unsigned char)i; a.sz = ASZ[curBiome];
        a.kind = (unsigned char)((curBiome == 0 || curBiome == 5) ? (i & 1) : 0);
        a.vx = AVX[curBiome] + (i % 3) * (curBiome == 2 ? 0.6f : 0.3f);
        a.vy = AVY[curBiome] + (i % 2) * (curBiome == 4 ? -0.4f : 0.3f);
    }
}

static void buildLevel() {
    for (int r = 0; r < ROWS; r++) { memset(level[r], '.', COLS); level[r][COLS] = 0; level[r][0] = level[r][COLS - 1] = '#'; }
    memset(level[0], '#', COLS); memset(level[ROWS - 1], '#', COLS);
    enemyCount = checkCount = coinCount = chestCount = mplatCount = dplatCount = powerupCount = npcCount = 0;
    bossActive = false; activeNpcMsg = -1;
    cpX = (float)(TILE + 16); cpY = (float)((ROWS - 1) * TILE - PH);
    baseCol = 0; levelCols = COLS; levelPW = WW;

    if (isBossStage(curLvl)) {
        int tier = (curLvl + 1) / 5;
        bossKind = (tier - 1) % 5;
        static const int BOSS_BIOMES[5] = {0, 1, 3, 4, 5};
        curBiome = BOSS_BIOMES[bossKind];
        levelCols = RCOLS; levelPW = FW;
        for (int r = 0; r < ROWS; r++) level[r][RCOLS - 1] = '#';
        wall(2, 5, 16); wall(29, 5, 16);
        plat(12, 5, 9); plat(7, 13, 18); plat(12, 22, 26);
        addPowerUp(7, 12, PU_SHIELD); addCoinArc(14, 6, 4);
        bossActive = true;
        bossW = (bossKind == B_KING_FROGUS) ? 156 : 124;
        bossH = (bossKind == B_KING_FROGUS) ? 140 : 116;
        bossX = (float)((bossKind == B_KING_FROGUS) ? 4 * TILE : 21 * TILE);
        bossY = (float)((ROWS - 1) * TILE - bossH);
        bossVX = (bossKind == B_KING_FROGUS) ? 3.5f : (tier >= 2 ? -5.0f : -3.8f);
        bossVY = 0; bossMaxHP = bossHP = 6 + tier * 2;
        bossHurt = bossTimer = bossSwCd = bossBeam = bossPhase = bossAttackState = bossTongueLen = 0;
        bossTongueDir = 1;
        for (int i = 0; i < MAX_SHOTS; i++) shots[i].active = false;
        initWorldDetails(); return;
    }

    static const int BIOMES[12] = {0, 1, 2, 3, 4, 5, 3, 1, 0, 5, 2, 4};
    int idx = curLvl % 12; curBiome = BIOMES[idx];

    if (idx == 0) {
        baseCol = 0 * RCOLS; addNPC(3, 17, T_NPC0);
        plat(15,7,13); addCoinArc(8,14,4); addE(E_SLIME,10,17,6,15); addE(E_SLIME,10,15,7,13);
        plat(14,16,22); addE(E_MUSHROOM,19,14,16,22);
        plat(10,24,30); crackFloor(10,26,27); addChest(28, 10); addCoin(25,9,true); addCoin(29,9,true);
        plat(15,25,30); addE(E_SLIME,27,17,24,30);
        baseCol = 1 * RCOLS; spk(16,11,13); addMPlat(11, 14, 9, 15, 2.2f);
        plat(14,16,21); addCoinArc(17,13,4); addE(E_SLIME,18,17,14,23); addE(E_BAT,18,10,15,22);
        plat(13,24,30); addE(E_MUSHROOM,27,13,24,30); addCP(26,13); addPowerUp(28,13,PU_SHIELD);
        baseCol = 2 * RCOLS; addNPC(3, 17, T_NPC1);
        wall(9,8,14); wall(13,8,16); plat(8,13,20); addCoinArc(14,7,5);
        addE(E_SLIME,16,8,13,20); addE(E_CHOMPER,24,17,20,29); plat(13,23,30); addE(E_MUSHROOM,26,13,23,30);
        baseCol = 3 * RCOLS; plat(14,4,10); sawAt(15,13); addDPlat(11,13,4);
        plat(14,16,22); addE(E_SLIME,19,17,15,24); addE(E_BAT,19,9,16,23);
        plat(12,25,30); addE(E_TURRET,29,12,25,30); addCP(26,12); addPowerUp(6,14,PU_SPEED);
        baseCol = 4 * RCOLS; plat(15,4,10); plat(13,13,19); addCoinArc(14,12,4);
        addE(E_WASP,16,11,12,21); addE(E_SLIME,15,17,11,21); plat(11,22,30); addE(E_MUSHROOM,25,11,22,30);
        crackFloor(17, 26, 27); addChest(28, 17);
        baseCol = 5 * RCOLS; plat(14,4,10); plat(12,13,19); addCoinArc(14,11,5);
        addE(E_WASP,16,9,12,21); addE(E_CHOMPER,16,17,11,22); plat(10,22,30); put(9,27,'G');
    } else if (idx == 1) {
        baseCol = 0 * RCOLS; addNPC(3, 17, T_NPC2);
        plat(15,7,13); addCoinArc(8,14,4); addE(E_SLIME,10,15,7,13); addE(E_CHOMPER,18,17,14,22);
        spk(16,15,17); plat(14,20,26); addE(E_MUSHROOM,23,14,20,26); plat(15,28,30);
        baseCol = 1 * RCOLS; plat(14,4,10); addMPlat(11,12,10,16,2.4f); plat(12,14,19); addE(E_BAT,16,8,13,20);
        plat(14,22,30); addE(E_SLIME,25,14,22,30); addCP(24,14); addPowerUp(28,14,PU_MAGNET);
        baseCol = 2 * RCOLS; wall(9,8,14); wall(13,8,16); plat(8,13,20); addE(E_TURRET,19,8,13,20);
        addE(E_CHOMPER,24,17,21,29); plat(13,23,30); addE(E_MUSHROOM,26,13,23,30); crackFloor(8,16,17); addChest(18, 8);
        baseCol = 3 * RCOLS; plat(14,4,10); sawAt(15,13); addDPlat(12,13,3); plat(14,16,22); addE(E_SLIME,19,14,16,22);
        plat(12,25,30); addCP(26,12); addCoinArc(17,13,4);
        baseCol = 4 * RCOLS; plat(15,4,10); plat(12,13,19); addE(E_WASP,16,8,13,20); addE(E_SLIME,16,17,12,21);
        plat(10,22,30); addE(E_MUSHROOM,25,10,22,30);
        baseCol = 5 * RCOLS; plat(14,4,10); plat(12,13,19); addE(E_WASP,16,9,13,20); addE(E_CHOMPER,16,17,11,22);
        plat(10,22,30); put(9,27,'G');
    } else if (idx == 2) {
        baseCol = 0 * RCOLS; wall(9,8,14); wall(13,8,16); plat(8,13,20); addCoinArc(14,7,5);
        addE(E_SLIME,16,8,13,20); addE(E_CHOMPER,18,17,15,22); plat(13,23,30); addE(E_MUSHROOM,26,13,23,30);
        baseCol = 1 * RCOLS; plat(14,4,10); plat(11,13,19); addE(E_TURRET,18,11,13,19);
        plat(14,22,30); addE(E_BAT,25,10,22,30); addCP(24,14); addPowerUp(7,14,PU_SHIELD);
        baseCol = 2 * RCOLS; wall(8,8,14); wall(12,8,16); plat(8,12,18); addMPlat(18,10,18,23,2.5f);
        plat(13,21,30); addE(E_SLIME,25,13,21,30); addChest(28, 13);
        baseCol = 3 * RCOLS; plat(14,4,10); sawAt(15,13); plat(13,16,22); addE(E_MUSHROOM,19,13,16,22);
        addE(E_CHOMPER,18,17,14,24); plat(11,25,30); addE(E_TURRET,29,11,25,30); addCP(26,11);
        baseCol = 4 * RCOLS; plat(15,4,10); wall(12,7,13); wall(16,7,15); plat(7,16,22); addE(E_WASP,19,5,16,23); plat(12,25,30);
        baseCol = 5 * RCOLS; plat(14,4,10); plat(12,13,19); addE(E_CHOMPER,16,12,13,19); plat(10,22,30); put(9,27,'G');
    } else if (idx == 3) {
        baseCol = 0 * RCOLS; plat(14,7,13); sawAt(15,15); addDPlat(14,13,3); plat(14,18,24);
        addE(E_SLIME,21,14,18,24); addE(E_SLIME,10,17,6,14); plat(12,26,30); addCoinArc(8,13,4);
        baseCol = 1 * RCOLS; plat(14,4,10); wall(12,7,13); wall(16,7,15); plat(7,16,22);
        addE(E_MUSHROOM,19,7,16,22); plat(12,25,30); addCP(26,12); addPowerUp(28,12,PU_SPEED);
        baseCol = 2 * RCOLS; plat(14,4,10); sawAt(15,12); addMPlat(11,12,10,15,2.6f);
        plat(13,15,21); addE(E_TURRET,20,13,15,21); plat(14,24,30); addChest(28, 14);
        baseCol = 3 * RCOLS; plat(14,4,10); plat(12,13,19); addE(E_BAT,16,8,13,20); addE(E_CHOMPER,16,17,12,21);
        plat(11,22,30); addCP(23,11);
        baseCol = 4 * RCOLS; plat(14,4,10); wall(12,7,13); wall(16,7,15); plat(7,16,22);
        plat(11,25,30); addE(E_WASP,27,9,24,30);
        baseCol = 5 * RCOLS; plat(14,4,10); plat(12,13,19); addE(E_CHOMPER,16,12,13,19); plat(10,22,30); put(9,27,'G');
    } else {
        for (int rm = 0; rm < ROOMS; rm++) {
            baseCol = rm * RCOLS; plat(14,5,11); plat(12,14,20); plat(14,23,29); addCoinArc(6, 13, 4);
            if (rm == 0) addNPC(3, 17, (curBiome == 4) ? T_NPC2 : T_NPC1);
            if (rm == 1 || rm == 3) { wall(9,8,13); wall(13,8,14); addCP(25,14); addPowerUp(27, 14, (rm == 1) ? PU_SHIELD : PU_MAGNET); }
            if (rm == 2) { addMPlat(11, 11, 10, 15, 2.5f); addChest(28, 14); }
            if (rm == 4) { addDPlat(11, 13, 3); sawAt(15, 13); }
            if (rm % 2 == 0) { addE(E_SLIME,8,14,5,11); addE(E_CHOMPER,14,17,10,21); addE(E_MUSHROOM,17,12,14,20); }
            else { addE(E_BAT,17,8,13,22); addE(E_WASP,15,9,12,21); addE(E_TURRET,28,14,23,29); }
        }
        baseCol = 5 * RCOLS; plat(10,22,30); put(9,27,'G');
    }
    baseCol = 0;
    initWorldDetails();
}

// ---------- Collision & Physics ----------
static inline char tileAt(int r, int c) { return (r < 0 || r >= ROWS || c < 0 || c >= COLS) ? '#' : level[r][c]; }
static inline char staticChar(int r, int c) { char ch = tileAt(r, c); return ch == 'O' ? '.' : ch; }
static inline bool hiAt(int r, int c) { return r > 0 && (staticChar(r, c) == '#' || staticChar(r, c) == 'B') && staticChar(r - 1, c) != '#'; }
static inline bool solidAt(int x, int y) {
    if (x < 0 || y < 0 || x / TILE >= COLS || y / TILE >= ROWS) return true;
    char ch = level[y / TILE][x / TILE]; return ch == '#' || ch == 'B';
}
static inline bool rectSolid(int x, int y, int w, int h) { return solidAt(x, y) || solidAt(x + w - 1, y) || solidAt(x, y + h - 1) || solidAt(x + w - 1, y + h - 1); }
static inline bool rectOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) { return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by; }
static inline bool circleHit(int cx, int cy, int rad, int rx, int ry, int rw, int rh) {
    int dx = cx - iclamp(cx, rx, rx + rw), dy = cy - iclamp(cy, ry, ry + rh);
    return dx * dx + dy * dy < rad * rad;
}

static void triggerShake(int mag, int dur) { if (mag >= shakeMag || shakeTimer == 0) { shakeMag = mag; shakeTimer = dur; } }

static void addSplat(int wx, int wy, int w, int h) {
    if (wx < 0 || wy < 0 || wx >= levelPW) return;
    int idx = splatCount < MAX_SPLATS ? splatCount++ : -1;
    if (idx < 0) for (int i = 0; i < MAX_SPLATS; i++) if (splats[i].stage >= 4 && !splats[i].drawn[0] && !splats[i].drawn[1]) { idx = i; break; }
    if (idx < 0) return;
    int rm = iclamp(wx / FW, 0, ROOMS - 1);
    Splat &s = splats[idx];
    s.room = (short)rm; s.x = (short)(wx - rm * FW - w / 2); s.y = (short)(wy - h / 2); s.w = (short)w; s.h = (short)h;
    s.born = tick; s.stage = 0; s.drawn[0] = s.drawn[1] = false; s.dStage[0] = s.dStage[1] = 99;
}

static void spawnPart(float x, float y, float pvx, float pvy, int life, int sz, int kind) {
    for (int i = 0; i < MAX_PARTS; i++) if (parts[i].life <= 0) {
        parts[i] = {x, y, pvx, pvy, (short)life, (short)life, (unsigned char)kind, (unsigned char)sz}; return;
    }
}
static void spawnDust(float x, float y, int n, float sp, int kind = 0) {
    for (int i = 0; i < n; i++)
        spawnPart(x + (rnd(20) - 10), y - rnd(4), ((rnd(100) - 50) / 50.0f) * sp, -(rnd(100) / 100.0f) * 2.0f, 16 + rnd(14), 6 + rnd(6), kind);
}

static void breakBlocksNear(int wx, int wy, int rad) {
    int c0 = iclamp((wx - rad) / TILE, 1, COLS - 2), c1 = iclamp((wx + rad) / TILE, 1, COLS - 2);
    int r0 = iclamp((wy - rad) / TILE, 1, ROWS - 2), r1 = iclamp((wy + rad) / TILE, 1, ROWS - 2);
    bool broke = false;
    for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++) if (level[r][c] == 'B') {
        level[r][c] = '.'; broke = true; spawnDust((float)(c * TILE + 30), (float)(r * TILE + 30), 8, 4.0f, 2);
    }
    if (broke) { secretsFound++; needFull[0] = needFull[1] = true; snd(12); triggerShake(7, 14); showBanner(T_SECRET_FOUND, 110); }
}

static void respawn() {
    px = cpX; py = cpY;
    roomIdx = iclamp((int)(px / FW), 0, ROOMS - 1);
    vx = vy = 0; squashX = squashY = 1.0f; onGround = false; wallLock = coyote = 0;
    jumpsLeft = 1; dashTimer = dashCd = 0; isSlamming = false; slamWaveTimer = 0;
    shieldHits = upgOwned[0] ? 1 : 0; speedBoostTimer = 0; magnetTimer = upgOwned[2] ? 999999 : 0;
    for (int i = 0; i < MAX_TRAIL; i++) trails[i].life = 0;
    for (int i = 0; i < dplatCount; i++) { dplats[i].timer = 0; dplats[i].triggered = false; level[dplats[i].r][dplats[i].c] = '#'; }
    if (bossActive && bossHP > 0) {
        bossX = (float)((bossKind == B_KING_FROGUS) ? 4 * TILE : 21 * TILE);
        bossY = (float)((ROWS - 1) * TILE - bossH);
        bossTongueLen = bossAttackState = 0;
        for (int i = 0; i < MAX_SHOTS; i++) shots[i].active = false;
    }
    needFull[0] = needFull[1] = true;
}

static void die() {
    if (dashTimer > 0) return;
    if (shieldHits > 0) {
        shieldHits--; vy = -12.5f; vx = -facing * 6.5f; dashTimer = 10;
        snd(5); triggerShake(6, 12); spawnDust(px + PW * 0.5f, py + PH * 0.5f, 8, 4.0f, 3);
        hudNeed[0] = hudNeed[1] = true; return;
    }
    deaths++; hudNeed[0] = hudNeed[1] = true; snd(6); triggerShake(8, 14);
    float cx = px + PW * 0.5f, cy = py + PH * 0.5f;
    for (int i = 0; i < 10; i++) spawnPart(cx + rnd(16) - 8, cy + rnd(16) - 8, (rnd(160) - 80) / 10.0f, (rnd(130) - 110) / 10.0f, 85, 5 + rnd(4), 1);
    for (int i = 0; i < 2; i++) addSplat((int)cx + rnd(30) - 15, (int)cy + rnd(30) - 15, 8 + rnd(8), 8 + rnd(8));
    vx = vy = 0; deadTimer = 28;
}

static void damageBoss(int dmg) {
    if (bossHurt > 0 || bossHP <= 0) return;
    bossHP = iclamp(bossHP - dmg, 0, bossMaxHP); bossHurt = 26; hudNeed[0] = hudNeed[1] = true; snd(7); triggerShake(9, 16);
    for (int i = 0; i < 10; i++) spawnPart(bossX + bossW * 0.5f, bossY + bossH * 0.5f, (rnd(180) - 90) / 10.0f, -(40 + rnd(90)) / 10.0f, 85, 6 + rnd(4), 1);
    if (bossHP <= 0) {
        for (int i = 0; i < MAX_SHOTS; i++) shots[i].active = false;
        level[16][15] = 'G'; totalCoins += 50;
        if (bossKind == B_KING_FROGUS) unlockAch(2);
        saveGame(); needFull[0] = needFull[1] = true;
    }
}

static void spawnShot(float sx, float sy, float svx, float svy, int kind = 0) {
    for (int i = 0; i < MAX_SHOTS; i++) if (!shots[i].active) { shots[i] = {sx, sy, svx, svy, kind, true}; return; }
}

static void update() {
    stageTimer++;
    if (bannerTimer > 0 && --bannerTimer == 0) needFull[0] = needFull[1] = true;
    if (shakeTimer > 0) {
        shakeTimer--;
        shakeX = rnd(shakeMag * 2 + 1) - shakeMag; shakeY = rnd(shakeMag * 2 + 1) - shakeMag;
        if (shakeTimer == 0) { shakeX = shakeY = shakeMag = 0; needFull[0] = needFull[1] = true; }
    }
    if (speedBoostTimer > 0) speedBoostTimer--;
    if (magnetTimer > 0 && !upgOwned[2]) magnetTimer--;
    if (slamWaveTimer > 0) slamWaveTimer--;
    for (int i = 0; i < MAX_TRAIL; i++) if (trails[i].life > 0) trails[i].life--;

    for (int i = 0; i < MAX_PARTS; i++) {
        Part &p = parts[i]; if (p.life <= 0) continue; p.life--;
        if (p.kind != 1) { p.x += p.vx; p.y += p.vy; p.vx *= 0.9f; p.vy *= 0.92f; }
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
        MPlat &mp = mplats[i]; if (mp.room != roomIdx) continue;
        float oldX = mp.x; mp.x += mp.vx;
        if (mp.x <= mp.minX) { mp.x = mp.minX; mp.vx = -mp.vx; } else if (mp.x + mp.w >= mp.maxX) { mp.x = mp.maxX - mp.w; mp.vx = -mp.vx; }
        float wx = mp.room * FW + oldX;
        if (deadTimer == 0 && vy >= 0 && px + PW > wx + 4 && px < wx + mp.w - 4 && py + PH >= mp.y - 6 && py + PH <= mp.y + 14) {
            px += (mp.x - oldX); py = mp.y - PH; vy = 0; onGround = true; jumpsLeft = 1;
        }
    }

    for (int i = 0; i < dplatCount; i++) {
        DPlat &dp = dplats[i]; if (dp.room != roomIdx) continue;
        int wx = dp.c * TILE, wy = dp.r * TILE;
        if (!dp.triggered && deadTimer == 0 && onGround && px + PW > wx + 2 && px < wx + TILE - 2 && iabs((int)(py + PH) - wy) <= 6) { dp.triggered = true; dp.timer = 1; }
        if (dp.triggered) {
            dp.timer++;
            if (dp.timer == 35) { level[dp.r][dp.c] = '.'; spawnDust((float)(wx + 30), (float)(wy + 12), 6, 2.5f, 0); }
            else if (dp.timer >= 175) { dp.triggered = false; dp.timer = 0; level[dp.r][dp.c] = '#'; }
        }
    }

    bool burst = false;
    if (deadTimer > 0) { if (--deadTimer == 0) respawn(); }
    else {
        bool wasG = onGround;
        if (dashCd > 0) dashCd--;

        float k = onGround ? 0.0f : (iabs((int)vy) / 18.0f > 1.0f ? 1.0f : iabs((int)vy) / 18.0f);
        squashX += (1.0f - 0.22f * k - squashX) * 0.3f; squashY += (1.0f + 0.30f * k - squashY) * 0.3f;
        int dir = controlMode == 1 ? (in.ana != 0.0f ? (in.ana > 0 ? 1 : -1) : 0) : ((in.dright ? 1 : 0) - (in.dleft ? 1 : 0));
        float sc = controlMode == 1 && in.ana != 0.0f ? (fabsf(in.ana) < 0.5f ? 0.5f : fabsf(in.ana)) : 1.0f;
        bool wL = rectSolid((int)px - 1, (int)py, 1, PH), wR = rectSolid((int)px + PW, (int)py, 1, PH);
        float maxS = (in.square ? 8.4f : 6.0f) * sc * (speedBoostTimer > 0 ? 1.28f : 1.0f);
        if (dir && dashTimer == 0) facing = dir;

        bool wantSlam = !onGround && in.down && (in.circleNew || in.squareNew);
        bool wantDash = !wantSlam && (in.dashBtnNew || (in.circleNew && !in.down));
        if (wantDash && dashCd == 0 && dashTimer == 0 && !isSlamming) {
            dashTimer = 10; dashCd = upgOwned[1] ? 24 : 38; dashDir = dir != 0 ? dir : facing; facing = dashDir;
            vx = dashDir * 15.5f; vy = 0; squashX = 1.38f; squashY = 0.68f; snd(10); spawnDust(px + PW * 0.5f, py + PH * 0.5f, 6, 3.5f, 3);
        }
        if (wantSlam && !isSlamming) { isSlamming = true; dashTimer = 0; vx = 0; vy = 22.0f; squashX = 0.65f; squashY = 1.42f; snd(10); }

        if (dashTimer > 0) {
            dashTimer--; vx = dashDir * 15.0f; vy = 0;
            if ((dashTimer & 1) == 0) for (int i = 0; i < MAX_TRAIL; i++) if (trails[i].life <= 0) { trails[i] = {px, py, 8, facing}; break; }
            breakBlocksNear((int)(px + PW * 0.5f + dashDir * 24), (int)(py + PH * 0.5f), 36);
        } else if (isSlamming) {
            vx *= 0.5f; vy = 22.0f;
        } else {
            if (wallLock > 0) wallLock--;
            else if (dir) { vx += dir * (onGround ? 1.32f : 0.78f); if (vx > maxS) vx = maxS; if (vx < -maxS) vx = -maxS; }
            else { float fr = onGround ? 1.05f : 0.32f; vx = vx > fr ? vx - fr : (vx < -fr ? vx + fr : 0); }
            vy = vy + 0.9f > 18.0f ? 18.0f : vy + 0.9f;
        }

        bool slide = !onGround && !isSlamming && dashTimer == 0 && vy > 0 && ((wL && dir < 0) || (wR && dir > 0));
        if (slide && vy > 2.8f) vy = 2.8f;
        if (slide && (tick & 4) == 0) spawnDust(wL ? px : px + PW, py + PH - 6.0f, 1, 1.0f);
        if (onGround) { coyote = 7; jumpsLeft = 1; } else if (coyote > 0) coyote--;

        // Jump on fresh press only (prevents holding X from causing unintended midair flight/spam)
        if (in.crossNew) {
            if (coyote > 0) {
                vy = -17.2f; coyote = 0; jumpsLeft = 1; isSlamming = false;
                squashX = 0.72f; squashY = 1.30f; spawnDust(px + PW * 0.5f, py + PH, 5, 3.0f); burst = true; snd(4);
            } else if (wL || wR) {
                vy = -16.4f; vx = wL ? 8.4f : -8.4f; wallLock = 9; facing = wL ? 1 : -1;
                jumpsLeft = 1; isSlamming = false;
                squashX = 0.72f; squashY = 1.30f; spawnDust(wL ? px : px + PW, py + PH * 0.5f, 4, 2.2f); burst = true; snd(5);
            } else if (hasDoubleJump && jumpsLeft > 0) {
                vy = -15.4f; jumpsLeft = 0; isSlamming = false; dashTimer = 0;
                squashX = 0.72f; squashY = 1.32f; spawnDust(px + PW * 0.5f, py + PH, 6, 3.5f, 3); burst = true; snd(5);
            }
        }
        if (!in.cross && vy < -6.0f && !isSlamming) vy = -6.0f;

        px += vx;
        if (rectSolid((int)px, (int)py, PW, PH)) {
            if (dashTimer > 0) breakBlocksNear((int)(px + PW * 0.5f + dashDir * 24), (int)(py + PH * 0.5f), 40);
            if (rectSolid((int)px, (int)py, PW, PH)) {
                px = (float)(vx > 0 ? ((((int)px + PW - 1) / TILE) * TILE - PW) : ((((int)px) / TILE + 1) * TILE));
                vx = 0; dashTimer = 0;
            }
        }
        py += vy;
        if (!onGround) onGround = false;
        if (rectSolid((int)px, (int)py, PW, PH)) {
            if (vy > 0) {
                if (isSlamming) breakBlocksNear((int)(px + PW * 0.5f), (int)(py + PH + 16), 52);
                if (rectSolid((int)px, (int)py, PW, PH)) {
                    py = (float)((((int)py + PH - 1) / TILE) * TILE - PH);
                    onGround = true; jumpsLeft = 1;
                    if (isSlamming) {
                        isSlamming = false; slamWaveTimer = 14; slamWaveX = (int)(px + PW * 0.5f); slamWaveY = (int)(py + PH);
                        squashY = 0.52f; squashX = 1.50f; triggerShake(8, 14); snd(11); burst = true;
                        for (int eIdx = 0; eIdx < enemyCount; eIdx++) {
                            Enemy &en = enemies[eIdx];
                            if (en.active && en.deathTimer == 0 && fabsf((en.x + 22) - slamWaveX) < 155.0f && fabsf((en.y + 22) - slamWaveY) < 95.0f) {
                                en.hp -= 2; en.hurt = 16;
                                if (en.hp <= 0) {
                                    en.deathTimer = (en.kind == E_BAT) ? 16 : 0; if (en.deathTimer == 0) en.active = false; snd(7);
                                    for (int k2 = 0; k2 < 8; k2++) spawnPart(en.x + 22, en.y + 22, (rnd(160) - 80) / 10.0f, -(45 + rnd(90)) / 10.0f, 90, 6 + rnd(4), 1);
                                }
                            }
                        }
                    } else if (!wasG && vy > 5.0f) {
                        float lk = vy / 18.0f > 1.0f ? 1.0f : vy / 18.0f;
                        squashY = 1.0f - 0.35f * lk; squashX = 1.0f + 0.40f * lk;
                        spawnDust(px + PW * 0.5f, py + PH, 4 + (int)(lk * 5), 3.5f); burst = true;
                    }
                }
            } else if (vy < 0) { py = (float)((((int)py) / TILE + 1) * TILE); isSlamming = false; }
            if (onGround || vy < 0) vy = 0;
        }

        for (int i = 0; i < checkCount; i++) if (!checkAct[i] && (int)px + PW >= checkC[i] * TILE) {
            checkAct[i] = true; cpX = (float)(checkC[i] * TILE); cpY = (float)(checkR[i] * TILE - PH);
            snd(1); spawnDust(cpX + 20, cpY + 4, 8, 3.2f, 3);
        }

        for (int i = 0; i < coinCount; i++) {
            Coin &cn = coins[i]; if (cn.taken || cn.room != roomIdx) continue;
            float wx = (float)(cn.room * FW + cn.x), wy = (float)cn.y;
            float dx = (px + PW * 0.5f) - (wx + 12), dy = (py + PH * 0.5f) - (wy + 12);
            if ((magnetTimer > 0 || upgOwned[2]) && dx * dx + dy * dy < 190.0f * 190.0f) {
                cn.x += (int)(dx * 0.14f); cn.y += (int)(dy * 0.14f); wx = (float)(cn.room * FW + cn.x); wy = (float)cn.y;
            }
            if (rectOverlap((int)px, (int)py, PW, PH, (int)wx - 4, (int)wy - 4, 32, 32)) {
                cn.taken = true; int gain = upgOwned[2] ? 2 : 1; totalCoins += gain; stageCoins += gain;
                hudNeed[0] = hudNeed[1] = true; snd(9); if (totalCoins >= 100) unlockAch(3);
            }
        }

        for (int i = 0; i < chestCount; i++) {
            Chest &ch = chests[i]; if (ch.opened || ch.room != roomIdx) continue;
            int wx = ch.room * FW + ch.x, wy = ch.y;
            if (rectOverlap((int)px, (int)py, PW, PH, wx - 8, wy - 8, 60, 48)) {
                ch.opened = true; int bonus = upgOwned[2] ? 50 : 25; totalCoins += bonus; stageCoins += bonus; totalChestsOpened++;
                hudNeed[0] = hudNeed[1] = true; snd(12); triggerShake(6, 12); showBanner(T_CHEST_OPEN, 120);
                if (totalChestsOpened >= 3) unlockAch(1); if (totalCoins >= 100) unlockAch(3); saveGame();
            }
        }

        for (int i = 0; i < powerupCount; i++) {
            PowerUp &pu = powerups[i]; if (!pu.active || pu.room != roomIdx) continue;
            int wx = pu.room * FW + pu.x, wy = pu.y;
            if (rectOverlap((int)px, (int)py, PW, PH, wx - 4, wy - 4, 40, 40)) {
                pu.active = false;
                if (pu.kind == PU_SHIELD) shieldHits = 1; else if (pu.kind == PU_SPEED) speedBoostTimer = 420; else magnetTimer = 600;
                hudNeed[0] = hudNeed[1] = true; snd(12);
            }
        }

        int prevNpc = activeNpcMsg; activeNpcMsg = -1;
        for (int i = 0; i < npcCount; i++) if (npcs[i].room == roomIdx) {
            int wx = npcs[i].room * FW + npcs[i].x;
            if (iabs((int)(px + PW * 0.5f) - (wx + 22)) < 130 && iabs((int)(py + PH) - (npcs[i].y + 46)) < 110) activeNpcMsg = npcs[i].msgId;
        }
        if (prevNpc != activeNpcMsg && prevNpc != -1) needFull[0] = needFull[1] = true;
    }

    float pcx = px - roomIdx * FW + PW * 0.5f, pby = py + PH;
    for (int i = 0; i < plantCount; i++) {
        Plant &p = plants[i]; if (p.room != roomIdx) continue;
        float wind = SN[((tick / 12) + (p.x / 60) + p.seed) & 15] / 256.0f * 1.8f;
        if (deadTimer == 0) {
            float dx = pcx - p.x, dy = pby - p.y, adx = fabsf(dx);
            if (adx < 46.0f && dy > -22.0f && dy < 32.0f) {
                float pr = 1.0f - adx / 46.0f; p.vel += vx * 0.40f * pr; wind += (dx < 0 ? 7.0f : -7.0f) * pr;
                if (burst) p.vel += (dx < 0 ? 5.0f : -5.0f) * pr;
            }
        }
        p.vel = (p.vel + (wind - p.angle) * 0.18f) * 0.78f; p.angle = iclamp((int)((p.angle + p.vel) * 10), -120, 120) / 10.0f;
    }
    for (int i = 0; i < MAX_AMB; i++) {
        Amb &a = amb[i]; a.x += a.vx + CS[((tick / 8) + a.seed * 3) & 15] / 256.0f * 0.4f; a.y += a.vy;
        if (a.x > FW - 24) a.x = 24; else if (a.x < 24) a.x = FW - 28;
        if (a.y > FH - 130) a.y = 110; else if (a.y < 105) a.y = FH - 140;
    }
    if (deadTimer > 0) return;

    int rmX0 = roomIdx * FW - 240, rmX1 = roomIdx * FW + FW + 240;
    for (int i = 0; i < enemyCount; i++) {
        Enemy &e = enemies[i]; if (!e.active || (int)e.x < rmX0 || (int)e.x > rmX1) continue;
        if (e.deathTimer > 0) { e.y += 3.5f; if (--e.deathTimer == 0) e.active = false; continue; }
        if (e.hurt > 0) e.hurt--; e.timer++;
        float dx = px - e.x, adx = fabsf(dx), ady = fabsf(py - e.y);
        int ew = (e.kind == E_CHOMPER) ? 68 : (e.kind == E_WASP ? 58 : 44);
        int eh = (e.kind == E_CHOMPER) ? 68 : (e.kind == E_WASP ? 58 : 44);

        if (e.kind == E_SLIME) {
            if (adx < 360 && ady < 150) e.dir = dx >= 0 ? 1 : -1;
            if (e.timer % 44 == 0 && e.vy == 0) e.vy = (adx < 260) ? -9.8f : -7.2f;
            float nx = e.x + ((adx < 340 && ady < 140) ? e.vx * 1.35f : e.vx) * e.dir;
            if (nx <= e.minX) { nx = e.minX; e.dir = 1; } else if (nx >= e.maxX) { nx = e.maxX; e.dir = -1; }
            int prX = e.dir > 0 ? (int)nx + 46 : (int)nx - 4;
            if (solidAt(prX, (int)e.y + 20)) { if (e.vy == 0) e.vy = -11.5f; else e.dir = -e.dir; }
            if (!rectSolid((int)nx, (int)e.y, 44, 44)) e.x = nx; else e.dir = -e.dir;
            e.vy += 0.65f; e.y += e.vy;
            if (rectSolid((int)e.x, (int)e.y, 44, 44)) { if (e.vy > 0) e.y = (float)((((int)e.y + 43) / TILE) * TILE - 44); e.vy = 0; }
        } else if (e.kind == E_BAT) {
            if (adx < 380) e.dir = dx >= 0 ? 1 : -1;
            e.x += e.vx * e.dir;
            if (e.x <= e.minX) { e.x = e.minX; e.dir = 1; } else if (e.x >= e.maxX) { e.x = e.maxX; e.dir = -1; }
            if (adx < 240 && py > e.baseY) { e.state = 1; e.y += (iclamp((int)py - 10, (int)e.baseY, (int)e.baseY + 135) - e.y) * 0.08f; }
            else { e.state = 0; e.y = e.baseY + (SN[(e.timer / 4) & 15] * 26) / 256; }
        } else if (e.kind == E_MUSHROOM) {
            if (adx < 280 && ady < 160) {
                if (e.state == 0) { e.state = 1; e.vy = -11.5f; spawnDust(e.x + 22, e.y + 44, 4, 2.2f); }
                e.dir = dx >= 0 ? 1 : -1; e.x += e.vx * 1.2f * e.dir;
                if (e.x <= e.minX) { e.x = e.minX; e.dir = 1; } else if (e.x >= e.maxX) { e.x = e.maxX; e.dir = -1; }
                if (e.timer % 52 == 0 && e.y >= e.baseY - 2) e.vy = -12.0f;
            } else e.state = 0;
            e.vy += 0.65f; e.y += e.vy; if (e.y >= e.baseY) { e.y = e.baseY; e.vy = 0; }
        } else if (e.kind == E_WASP) {
            e.dir = dx < 0 ? -1 : 1; e.x += e.vx * ((e.timer & 64) ? 1 : -1);
            if (e.x <= e.minX) e.x = e.minX; else if (e.x >= e.maxX) e.x = e.maxX;
            e.y = e.baseY + (SN[(e.timer / 5) & 15] * 22) / 256;
            if (e.timer % 85 == 0 && adx < 620) spawnShot(e.x + 28, e.y + 38, e.dir * 6.5f, 2.2f, 1);
        } else if (e.kind == E_CHOMPER) {
            bool chg = (adx < 380 && ady < 110);
            if (chg) e.dir = dx >= 0 ? 1 : -1;
            float nx = e.x + (chg ? e.vx * 1.45f : e.vx * 0.75f) * e.dir;
            if (nx <= e.minX) { nx = e.minX; e.dir = 1; } else if (nx >= e.maxX) { nx = e.maxX; e.dir = -1; }
            if (!rectSolid((int)nx, (int)e.y, ew, eh)) e.x = nx; else e.dir = -e.dir;
            e.vy += 0.65f; e.y += e.vy;
            if (rectSolid((int)e.x, (int)e.y, ew, eh)) { if (e.vy > 0) e.y = (float)((((int)e.y + eh - 1) / TILE) * TILE - eh); e.vy = 0; }
        } else {
            e.dir = dx < 0 ? -1 : 1;
            if (e.timer % 90 == 0 && adx < 680) spawnShot(e.x + 22, e.y + 18, e.dir * 6.8f, 0, 0);
        }

        if (rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, (int)e.x + 4, (int)e.y + 4, ew - 8, eh - 8)) {
            if ((vy > 0 && py + PH < e.y + 32) || isSlamming) {
                if (!isSlamming) { py = e.y - PH; vy = -15.8f; jumpsLeft = 1; }
                squashX = 0.72f; squashY = 1.28f; e.hp--; e.hurt = 16; snd(7); triggerShake(5, 9);
                if (e.hp <= 0) {
                    e.deathTimer = (e.kind == E_BAT) ? 16 : 0; if (e.deathTimer == 0) e.active = false;
                    totalCoins += (e.maxHp > 1) ? 3 : 1; hudNeed[0] = hudNeed[1] = true;
                    for (int k2 = 0; k2 < 10; k2++) spawnPart(e.x + ew * 0.5f, e.y + eh * 0.5f, (rnd(160) - 80) / 10.0f, -(45 + rnd(90)) / 10.0f, 90, 6 + rnd(4), 1);
                }
            } else if (e.hurt == 0) { die(); return; }
        }
    }

    for (int i = 0; i < MAX_SHOTS; i++) {
        Shot &s = shots[i]; if (!s.active) continue; s.x += s.vx; s.y += s.vy;
        if (s.x < TILE || s.x > levelPW - TILE || s.y < TILE || s.y > FH - TILE || solidAt((int)s.x, (int)s.y)) { s.active = false; continue; }
        if (rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, (int)s.x - 14, (int)s.y - 14, 28, 28)) { s.active = false; die(); return; }
    }

    if (bossActive && bossHP > 0) {
        if (bossHurt > 0) bossHurt--; if (bossSwCd > 0) bossSwCd--; if (bossBeam > 0) bossBeam--; bossTimer++;
        int tier = (curLvl + 1) / 5; float floorY = (float)((ROWS - 1) * TILE - bossH); bool enraged = (bossHP * 2 <= bossMaxHP);
        if (bossKind == B_KING_FROGUS) {
            bossTongueDir = (px >= bossX + bossW * 0.5f) ? 1 : -1;
            int cycle = bossTimer % (enraged ? 150 : 190);
            if (cycle < 50) { bossAttackState = (cycle > 30) ? 1 : 0; bossTongueLen = 0; }
            else if (cycle < 95) {
                bossAttackState = 2; int tStep = cycle - 50;
                bossTongueLen = (tStep < 22) ? (tStep * 14) : iclamp((45 - tStep) * 14, 0, 320);
                if (tStep == 10) snd(13);
                int tx = (bossTongueDir > 0) ? ((int)bossX + bossW - 10) : ((int)bossX - bossTongueLen + 10);
                if (bossTongueLen > 24 && rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, tx, (int)bossY + 40, bossTongueLen, 28)) { die(); return; }
            } else if (cycle == 105 && bossY >= floorY - 2) {
                bossAttackState = 0; bossTongueLen = 0; bossVY = -16.5f; bossVX = (px > bossX) ? 5.2f : -5.2f; snd(4);
            } else if (cycle == 145) {
                float d = (px < bossX) ? -1.0f : 1.0f;
                spawnShot(bossX + bossW * 0.5f, bossY + 58, d * 7.2f, -1.2f, 1);
                if (enraged) spawnShot(bossX + bossW * 0.5f, bossY + 38, d * 6.4f, -3.4f, 1);
            }
            if (bossY < floorY) {
                bossX += bossVX;
                if (bossX < 3 * TILE) { bossX = 3 * TILE; bossVX = -bossVX; } else if (bossX > 24 * TILE) { bossX = 24 * TILE; bossVX = -bossVX; }
            }
            float prevBY = bossY; bossVY += 0.65f; bossY += bossVY;
            if (bossY >= floorY) {
                bossY = floorY;
                if (prevBY < floorY - 4 && bossVY > 8.0f) { triggerShake(9, 15); snd(11); spawnDust(bossX + bossW * 0.5f, bossY + bossH, 8, 5.0f); }
                bossVY = 0;
            }
        } else {
            float spdM = (enraged ? 1.35f : 1.0f) * (tier >= 2 ? 1.2f : 1.0f);
            bossX += bossVX * spdM;
            if (bossX < 4 * TILE) { bossX = 4 * TILE; bossVX = 4.4f; } else if (bossX > 24 * TILE) { bossX = 24 * TILE; bossVX = -4.4f; }
            if (bossTimer % (enraged ? 85 : 105) == 50 && bossY >= floorY - 2) bossVY = -15.0f;
            bossVY += 0.65f; bossY += bossVY; if (bossY >= floorY) { bossY = floorY; bossVY = 0; }
            if (bossTimer % (enraged ? 55 : 75) == 0) {
                float d = px < bossX ? -1.0f : 1.0f; int sk = (bossKind == B_FROST_LORD) ? 2 : 0;
                spawnShot(bossX + bossW * 0.5f, bossY + bossH - 32, d * 7.2f, 0, sk);
                if (tier >= 2 || enraged) spawnShot(bossX + bossW * 0.5f, bossY + 24, d * 6.2f, -2.5f, sk);
            }
        }
        if (bossSwCd == 0 && rectOverlap((int)px, (int)py, PW, PH, 15 * TILE, 6 * TILE + 20, 120, 40)) { bossSwCd = 130; bossBeam = 25; vy = -12.0f; damageBoss(2); return; }
        if (rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, (int)bossX + 10, (int)bossY + 10, bossW - 20, bossH - 16)) {
            if ((vy > 0 && py + PH < bossY + 52) || isSlamming) {
                isSlamming = false; py = bossY - PH; vy = -16.8f; jumpsLeft = 1; squashX = 0.7f; squashY = 1.32f; damageBoss(2);
            } else if (bossHurt == 0) { die(); return; }
        }
    }

    int c0 = iclamp((int)px / TILE - 2, 0, COLS - 1), c1 = iclamp(((int)px + PW) / TILE + 2, 0, COLS - 1);
    for (int r = 0; r < ROWS; r++) for (int c = c0; c <= c1; c++) {
        char ch = level[r][c];
        if ((ch == '^' && rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, c * TILE + 14, r * TILE + 26, 32, 34)) ||
            (ch == 'O' && circleHit(c * TILE + 30, r * TILE + 30, 24, (int)px + 4, (int)py + 4, PW - 8, PH - 8))) { die(); return; }
        if (ch == 'G' && rectOverlap((int)px, (int)py, PW, PH, c * TILE + 8, r * TILE + 16, PW, PH)) {
            winTimer = 150; needWin[0] = needWin[1] = true; snd(8);
            int secElapsed = stageTimer / 60;
            if (bestTime[curLvl] == 0 || secElapsed < bestTime[curLvl]) bestTime[curLvl] = secElapsed;
            if (curLvl == 0) unlockAch(0); if (secElapsed <= 35) unlockAch(4); if (curLvl + 2 >= 15) unlockAch(5);
            saveGame(); return;
        }
    }
    roomIdx = iclamp(((int)px + PW / 2) / FW, 0, ROOMS - 1);
}

// ---------- High-Detail Graphics & Dirty-Rect Renderer ----------
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

struct SkinPal { Color outC, mainC, hiC, mouthC; };
static const SkinPal SkinPalSKINS[NUM_SKINS] = {
    {{125,0,14},   {208,26,36},  {242,85,95},  {120,0,12}},
    {{145,98,12},  {245,195,42}, {255,242,145},{135,75,10}},
    {{22,18,38},   {52,44,82},   {115,98,175}, {235,45,75}},
    {{24,82,128},  {68,178,242}, {185,240,255},{18,58,96}},
    {{92,18,12},   {238,85,24},  {255,210,75}, {70,10,8}},
    {{18,108,42},  {48,225,88},  {155,255,165},{14,78,30}}
};

static void drawPlayerFace(int dx, int dy, int dw, int dh, int d) {
    int ew = dw * 10 / 44 < 4 ? 4 : dw * 10 / 44, eh = dh * 14 / 44 < 6 ? 6 : dh * 14 / 44;
    int pw = dw * 5 / 44 < 2 ? 2 : dw * 5 / 44, ph = dh * 8 / 44 < 3 ? 3 : dh * 8 / 44;
    int e1 = dx + dw * 9 / 44, e2 = dx + dw * 25 / 44, ey = dy + dh * 12 / 44, po = d > 0 ? dw * 5 / 44 : 0, pyy = dy + dh * 16 / 44;
    if (curSkin == 2) fillRect(dx + 2, dy + dh * 7 / 44, dw - 4, dh * 5 / 44 < 3 ? 3 : dh * 5 / 44, mk(225, 35, 55));
    else if (curSkin == 1) fillRect(dx + dw / 4, dy - 5, dw / 2, 5, mk(255, 225, 75));
    fillRect(e1, ey, ew, eh, mk(255, 255, 255)); fillRect(e2, ey, ew, eh, mk(255, 255, 255));
    fillRect(e1 + po, pyy, pw, ph, mk(0, 0, 0)); fillRect(e2 + po, pyy, pw, ph, mk(0, 0, 0));
    fillRect(dx + dw * 15 / 44, dy + dh * 32 / 44, dw * 14 / 44, dh * 4 / 44 < 2 ? 2 : dh * 4 / 44, SkinPalSKINS[curSkin].mouthC);
}

struct TilePal { Color mainC, litC, topC, topHi, botC, spHi, spMid, spShd; };
static const TilePal TPAL[6] = {
    {{112,68,42},{142,88,54},{62,218,54},{145,252,105},{64,38,22},{245,248,255},{195,200,215},{125,132,152}},
    {{122,74,44},{146,92,56},{236,184,56},{255,224,115},{76,44,24},{245,248,255},{195,200,215},{125,132,152}},
    {{184,124,66},{208,146,84},{248,208,98},{255,236,155},{122,76,38},{255,232,185},{224,184,128},{162,124,78}},
    {{72,98,134},{94,124,165},{232,248,255},{255,255,255},{42,58,86},{240,252,255},{175,224,250},{105,165,205}},
    {{62,36,44},{86,50,60},{245,98,36},{255,178,85},{34,18,24},{255,195,115},{240,112,52},{165,52,26}},
    {{64,52,92},{84,70,118},{165,122,205},{205,168,238},{36,28,56},{245,248,255},{195,200,215},{125,132,152}}
};

static void drawTileCell(int r, int cx, char ch, bool hi) {
    int x = cx * TILE, y = r * TILE; const TilePal &tp = TPAL[curBiome];
    if (ch == '#' || ch == 'B') {
        fillRect(x, y, TILE, TILE, tp.mainC);
        fillRect(x + 6, y + 14, TILE - 12, TILE - 22, tp.litC);
        fillRect(x + 10, y + 18, 14, 8, tp.mainC); fillRect(x + 32, y + 32, 16, 10, tp.botC);
        fillRect(x + 6, y + 38, 10, 8, tp.botC); fillRect(x + 22, y + 26, 8, 8, tp.botC);
        if (ch == 'B') {
            fillRect(x + 16, y + 12, 4, 24, tp.botC); fillRect(x + 20, y + 24, 18, 4, tp.botC); fillRect(x + 34, y + 28, 4, 18, tp.botC);
        }
        if (hi) {
            fillRect(x, y, TILE, 10, tp.topC); fillRect(x, y, TILE, 4, tp.topHi);
            fillRect(x + 4, y + 10, 10, 5, tp.topC); fillRect(x + 22, y + 10, 12, 7, tp.topC); fillRect(x + 42, y + 10, 12, 5, tp.topC);
            fillRect(x + 14, y + 10, 6, 9, tp.topHi);
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
        fillRect(gx - 2, gy - 2, PW + 4, PH + 4, mk(255, 225, 130));
        fillRect(gx, gy, PW, PH, mk(255, 140, 170)); fillRect(gx, gy + 3, PW, 7, mk(240, 230, 200));
        drawFace(gx, gy, -1, mk(255, 255, 255));
    }
}

static void drawPlantTuft(int bx, int by, int h, int bend, int bm, int st) {
    if (bm == 2) {
        int s1 = bend / 2; Color sc = mk(82, 168, 56);
        fillRect(bx - 2, by - h - 2, 4, h + 2, sc);
        fillRect(bx - 8 + s1, by - h * 2 / 3, 6, 4, sc); fillRect(bx - 8 + s1, by - h + 1, 3, 6, sc);
        fillRect(bx + 2 + s1, by - h * 3 / 4, 6, 4, sc); fillRect(bx + 5 + s1, by - h, 3, 6, sc);
        if (st == 0) fillRect(bx - 2 + s1, by - h - 5, 4, 3, mk(255, 125, 145));
        return;
    }
    static const Color PD[6] = {{34,142,48},{185,132,38},{82,168,56},{105,175,220},{95,32,32},{78,52,128}};
    static const Color PM[6] = {{78,218,76},{248,205,72},{82,168,56},{185,230,255},{242,102,32},{138,98,205}};
    static const Color PT[6] = {{255,235,80},{255,232,115},{255,125,145},{255,255,255},{255,225,75},{185,235,255}};
    Color tip = bm == 0 ? (st == 0 ? mk(255,105,145) : (st == 1 ? mk(80,235,255) : mk(255,235,80))) : PT[bm];
    for (int b = -1; b <= 1; b++) {
        int ox = bx + b * 5, bh = h + (bm == 1 ? 2 : 0) - (b ? (bm == 3 ? 3 : 2) : 0), sh = bh / 2, w = bm == 1 ? 2 : 3;
        int off = bend / 2 + b * (bm == 4 ? 2 : 1);
        fillRect(ox - 1, by - sh, w, sh, PD[bm]);
        fillRect(ox - 1 + off, by - bh, w, bh - sh, PM[bm]);
        if (bm == 1 || bm == 3 || bm == 4 || b == 0) fillRect(ox - 2 + off, by - bh - (bm == 4 ? 2 : 4), bm == 4 ? 3 : 4, bm == 4 ? 3 : 4, tip);
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

static inline void starPos(int i, int &sx, int &sy) { sx = (i * 271 + 91) % FW; sy = (i * 137 + 31) % 570; }
static inline bool starDim(int i, int ph) { return ((ph + i) % 4) == 0; }

static void drawCastle(int cx, int by, Color wc, Color rc, Color fc) {
    fillRect(cx - 65, by - 120, 130, 120, wc); fillRect(cx - 105, by - 155, 44, 155, wc);
    fillRect(cx + 61, by - 155, 44, 155, wc); fillRect(cx - 26, by - 175, 52, 175, wc);
    for (int j = 0; j < 26; j++) {
        int h = (j * 28) / 25 + 4;
        fillRect(cx - h, by - 200 + j, h * 2 + 1, 1, rc);
        fillRect(cx - 83 - j / 2, by - 175 + j, j + 10, 1, rc); fillRect(cx + 83 - j / 2, by - 175 + j, j + 10, 1, rc);
    }
    fillRect(cx - 18, by - 52, 36, 52, mk(45, 32, 48));
    fillRect(cx - 45, by - 95, 16, 22, mk(250, 215, 90)); fillRect(cx + 29, by - 95, 16, 22, mk(250, 215, 90));
    fillRect(cx, by - 216, 18, 10, fc); fillRect(cx - 83, by - 190, 14, 8, fc); fillRect(cx + 83, by - 190, 14, 8, fc);
}

static void drawAncientTotemAndSpears(int ox) {
    int tx = 960 - ox / 4;
    fillRect(tx - 58, 510, 116, 190, mk(24, 58, 48)); fillRect(tx - 48, 520, 96, 170, mk(34, 82, 68));
    for (int k = -2; k <= 2; k++) fillRect(tx + k * 20 - 6, 488 + iabs(k) * 4, 12, 24, mk(42, 98, 80));
    fillRect(tx - 78, 545, 22, 14, mk(34, 82, 68)); fillRect(tx - 78, 530, 12, 18, mk(42, 98, 80));
    fillRect(tx + 56, 545, 22, 14, mk(34, 82, 68)); fillRect(tx + 66, 530, 12, 18, mk(42, 98, 80));
    fillRect(tx - 26, 552, 18, 18, mk(15, 32, 26)); fillRect(tx + 8, 552, 18, 18, mk(15, 32, 26));
    fillRect(tx - 20, 558, 8, 8, mk(175, 245, 195)); fillRect(tx + 14, 558, 8, 8, mk(175, 245, 195));
    fillRect(tx - 32, 584, 64, 8, mk(18, 42, 34));
    for (int s = 0; s < 7; s++) {
        int sx = ((s * 270 + 140 - ox / 2) % (FW - 120)) + 60, sy = 640 + (s & 1) * 35;
        fillRect(sx, sy, 5, 75, mk(62, 46, 54)); fillRect(sx - 5, sy - 14, 15, 14, mk(145, 175, 205)); fillRect(sx - 2, sy - 22, 9, 8, mk(205, 232, 255));
    }
}

static void paintBackground() {
    int pxOff = roomIdx * 80;
    if (curBiome == 0) {
        if (bossActive && bossKind == B_KING_FROGUS) {
            fillRect(0, 0, FW, FH, mk(18, 48, 28));
            for (int i = 0; i < 6; i++) {
                int bx = i * 330 + 50;
                fillRect(bx, 60, 74, FH - 120, mk(48, 38, 28)); fillRect(bx + 10, 60, 54, FH - 120, mk(68, 52, 36));
                fillRect(bx + 110, 60, 14, 140 + (i & 1) * 55, mk(42, 148, 52)); fillRect(bx + 165, 60, 10, 95 + (i % 3) * 40, mk(75, 195, 72));
            }
            for (int b = 0; b < 3; b++) { int sx = 380 + b * 480; fillRect(sx, 60, 110, FH - 160, mk(34, 82, 48)); fillRect(sx + 25, 60, 60, FH - 160, mk(48, 112, 64)); }
            return;
        }
        fillRect(0, 0, FW, 260, mk(42,148,218)); fillRect(0, 260, FW, 240, mk(105,198,235));
        fillRect(0, 500, FW, 110, mk(48,138,82)); fillRect(0, 610, FW, 130, mk(32,102,58)); fillRect(0, 740, FW, 340, mk(22,68,42));
        fillRect(1540 - pxOff / 4, 72, 136, 136, mk(255,242,155)); fillRect(1556 - pxOff / 4, 88, 104, 104, mk(255,224,72));
        for (int i = 0; i < 4; i++) {
            int cx = ((150 + i * 380 - pxOff / 3) % (FW - 180)) + 40, cy = 115 + (i & 1) * 48;
            fillRect(cx, cy, 165, 36, mk(236,248,255)); fillRect(cx + 28, cy - 20, 105, 24, mk(248,253,255));
        }
        drawCastle(1260 - pxOff / 3, 500, mk(218,212,224), mk(195,55,65), mk(255,215,65));
        drawAncientTotemAndSpears(pxOff);
        fillRect(360, 430, 96, 250, mk(72,178,245)); fillRect(376, 430, 64, 250, mk(145,224,255)); fillRect(394, 430, 28, 250, mk(225,248,255));
        fillRect(260, 655, 420, 65, mk(62,165,235)); fillRect(310, 670, 310, 28, mk(135,218,252));
        for (int i = 0; i < 6; i++) {
            int x = i * 320 + 40 - (pxOff % 60);
            fillRect(x + 42, 480, 34, 195, mk(78,48,30));
            fillRect(x, 420, 118, 115, mk(26,92,48)); fillRect(x + 18, 385, 82, 55, mk(48,148,74));
        }
    } else if (curBiome == 1) {
        Color fC = mk(132,88,78), nC = mk(88,54,48);
        fillRect(0, 0, FW, 300, mk(188,158,162)); fillRect(0, 300, FW, 270, mk(218,188,182)); fillRect(0, 570, FW, 130, fC); fillRect(0, 700, FW, 380, nC);
        for (int m = 0; m < 5; m++) {
            int mx = m * 380 + 110 - (pxOff / 3);
            fillRect(mx + 38, 450, 34, 130, mk(148, 122, 118)); fillRect(mx, 410, 110, 45, mk(118, 96, 94)); fillRect(mx + 16, 392, 78, 22, mk(142, 116, 112));
        }
        drawAncientTotemAndSpears(pxOff);
        for (int i = 0; i < 4; i++) {
            int tx = i * 480 + 180;
            fillRect(tx, 160, 78, 540, mk(68, 44, 38)); fillRect(tx + 12, 160, 54, 540, mk(88, 58, 48)); fillRect(tx - 60, 140, 198, 55, mk(58, 86, 48));
        }
    } else if (curBiome == 2) {
        fillRect(0, 0, FW, 270, mk(68,168,238)); fillRect(0, 270, FW, 250, mk(185,228,248)); fillRect(0, 520, FW, 140, mk(218,165,86)); fillRect(0, 660, FW, 420, mk(178,122,58));
        fillRect(1495, 82, 132, 132, mk(255,242,165)); fillRect(1511, 98, 100, 100, mk(255,225,95));
        for (int p = 0; p < 2; p++) for (int s = 0; s < 9; s++) { int w = (s + 1) * 34; fillRect(1120 + p * 360 - w / 2, 340 + s * 20, w, 20, mk(226,174,92)); }
        fillRect(425, 450, 190, 110, mk(216,162,88)); fillRect(455, 405, 130, 45, mk(216,162,88)); fillRect(490, 375, 60, 30, mk(242,198,105));
        fillRect(496, 504, 48, 56, mk(62,38,22)); fillRect(440, 465, 18, 32, mk(195,45,35)); fillRect(582, 465, 18, 32, mk(195,45,35));
        fillRect(343, 425, 14, 135, mk(118,72,36)); fillRect(316, 416, 68, 10, mk(58,168,52)); fillRect(324, 404, 52, 9, mk(85,198,68)); fillRect(338, 394, 24, 9, mk(110,215,78));
        fillRect(760, 625, 310, 58, mk(62,175,235)); fillRect(800, 638, 220, 24, mk(142,222,252));
    } else if (curBiome == 3) {
        Color fC = mk(95,135,178), nC = mk(62,95,135);
        fillRect(0, 0, FW, 300, mk(32,56,96)); fillRect(0, 300, FW, 270, mk(62,100,148)); fillRect(0, 570, FW, 130, fC); fillRect(0, 700, FW, 380, nC);
        for (int i = 0; i < 6; i++) fillRect(120 + i * 280, 140 + (i & 1) * 18, 220, 18, mk(65,215,175));
        fillRect(1500, 110, 96, 96, mk(220,240,255));
        for (int i = 0; i < 3; i++) { int x = i * 660; fillRect(x+80,410,200,160,fC); fillRect(x+110,370,140,50,mk(230,245,255)); fillRect(x+340,440,180,130,fC); fillRect(x+365,410,130,40,mk(230,245,255)); }
        for (int i = 0; i < 4; i++) { int x = i * 520; fillRect(x+70,550,200,150,nC); fillRect(x+105,520,130,40,mk(210,232,250)); }
    } else if (curBiome == 4) {
        Color fC = mk(58,22,30), nC = mk(38,15,22);
        fillRect(0, 0, FW, 300, mk(42,14,24)); fillRect(0, 300, FW, 270, mk(78,24,32)); fillRect(0, 570, FW, 130, fC); fillRect(0, 700, FW, 380, nC);
        fillRect(1500, 115, 100, 100, mk(235,95,50)); fillRect(1520, 130, 65, 70, mk(255,155,70));
        for (int i = 0; i < 3; i++) { int x = i * 680; fillRect(x+120,400,220,170,fC); fillRect(x+180,392,100,20,mk(245,95,35)); fillRect(x+220,412,18,158,mk(235,75,28)); }
        for (int i = 0; i < 4; i++) fillRect(i * 540 + 80, 540, 200, 160, nC);
    } else {
        Color fC = mk(35,20,75), nC = mk(25,14,55);
        fillRect(0, 0, FW, 300, mk(24,12,55)); fillRect(0, 300, FW, 270, mk(31,15,66)); fillRect(0, 570, FW, 130, fC); fillRect(0, 700, FW, 380, nC);
        fillRect(1500, 115, 100, 100, mk(145,125,205)); fillRect(1515, 100, 70, 130, mk(145,125,205)); fillRect(1520, 125, 65, 75, mk(185,170,220));
        int ph = tick / 20;
        for (int i = 0; i < NUM_STARS; i++) {
            int sx, sy; starPos(i, sx, sy);
            if (i % 7 == 0) fillRect(sx - 2, sy - 2, 7, 7, mk(70, 55, 110));
            fillRect(sx, sy, 3, 3, starDim(i, ph) ? mk(130, 115, 185) : mk(220, 210, 255));
        }
        drawCastle(960 - pxOff / 4, 570, mk(48, 34, 88), mk(95, 28, 52), mk(255, 85, 110));
        for (int i = 0; i < 3; i++) { int x = i * 700; fillRect(x+80,450,180,130,fC); fillRect(x+220,390,180,190,fC); fillRect(x+390,470,180,110,fC); }
        for (int i = 0; i < 4; i++) { int x = i * 560; fillRect(x+70,590,160,120,nC); fillRect(x+220,530,180,180,nC); fillRect(x+380,630,130,80,nC); }
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
                drawPlantTuft(p.x, p.y, p.h, (int)p.angle, curBiome, p.style); p.drawn[b] = true; p.dBend[b] = (signed char)p.angle;
            }
        }
    }
    resetClip();
}

static inline void addDirty(int b, int x, int y, int w, int h) {
    if (dirtyCnt[b] >= MAX_DIRTY) return;
    int x0 = iclamp(x, 0, FW), y0 = iclamp(y, 0, FH);
    int x1 = iclamp(x + w, 0, FW), y1 = iclamp(y + h, 0, FH);
    if (x1 > x0 && y1 > y0) dirty[b][dirtyCnt[b]++] = {x0, y0, x1 - x0, y1 - y0};
}
static int progressNow() { return bossActive ? (bossMaxHP > 0 ? bossHP * 520 / bossMaxHP : 0) : iclamp((int)(px / levelPW * 520.0f), 0, 520); }

static void drawHUDFull(int b) {
    int hn = deaths < 20 ? deaths : 20, pr = progressNow();
    if (useAr() && TXT_AR[T_DEATHS] >= 0) drawArCols(TXT_AR[T_DEATHS], 64, 18, 0, arW(TXT_AR[T_DEATHS]), mk(235, 225, 255));
    else drawText(64, 24, "DEATHS", 3, mk(235, 225, 255));
    for (int i = 0; i < hn; i++) fillRect(64 + i * 18, 56, 12, 12, mk(215, 28, 42));

    static const int BOSS_NAMES[5] = {T_KING_FROGUS, T_CHOMP_BEAST, T_FROST_LORD, T_MAGMA_CRUSHER, T_SAW_OVERLORD};
    if (bossActive) drawLabel(BOSS_NAMES[iclamp(bossKind, 0, 4)], 960, 8, 28, 3, mk(255, 225, 105));
    fillRect(690, 36, 540, 26, mk(135, 86, 38));
    fillRect(694, 39, 532, 20, mk(34, 22, 46));
    fillRect(700, 42, 520, 14, bossActive ? mk(72, 24, 34) : mk(42, 34, 72));
    fillRect(700, 42, pr, 14, bossActive ? mk(242, 72, 85) : mk(58, 175, 245));
    fillRect(700, 42, pr, 4, bossActive ? mk(255, 145, 155) : mk(145, 225, 255));
    if (bossActive) {
        fillRect(948, 28, 24, 18, mk(235, 232, 210));
        fillRect(952, 33, 5, 5, mk(25, 15, 20)); fillRect(963, 33, 5, 5, mk(25, 15, 20));
    }

    int s1x = 585, s2x = 1265, sy = 24;
    fillRect(s1x, sy, 48, 44, mk(155, 108, 45)); fillRect(s1x + 4, sy + 4, 40, 36, mk(28, 22, 42));
    fillRect(s1x + 14, sy + 14, 20, 8, mk(85, 235, 145)); fillRect(s1x + 18, sy + 22, 14, 8, mk(165, 255, 205));
    fillRect(s2x, sy, 48, 44, mk(155, 108, 45)); fillRect(s2x + 4, sy + 4, 40, 36, mk(28, 22, 42));
    Color pCol = shieldHits > 0 ? mk(85, 215, 255) : (speedBoostTimer > 0 ? mk(255, 215, 65) : mk(145, 155, 185));
    fillRect(s2x + 16, sy + 14, 16, 18, pCol); fillRect(s2x + 20, sy + 9, 8, 5, mk(220, 225, 240));

    fillRect(1560, 22, 290, 46, mk(32, 22, 52));
    fillRect(1572, 33, 22, 22, mk(255, 205, 45)); fillRect(1577, 38, 12, 12, mk(255, 240, 135));
    char cBuf[16]; intToStr(totalCoins, cBuf); drawText(1606, 34, cBuf, 3, mk(255, 232, 115));
    char tBuf[16]; intToStr(stageTimer / 60, tBuf); strcat(tBuf, "S"); drawText(1735, 34, tBuf, 3, mk(185, 225, 255));

    hudSq[b] = hn; hudProg[b] = pr; hudCoins[b] = totalCoins; hudNeed[b] = false;
}

static void drawEnemyChar(const Enemy &e, int sx, int sy) {
    bool fl = (e.hurt & 2) != 0;
    if (e.kind == E_SLIME) {
        int sq = (e.vy != 0) ? -4 : ((e.timer / 8) & 1) * 3;
        int w = 44 + sq, h = 38 - sq, x = sx - sq / 2, y = sy + 6 + sq;
        fillRect(x + 2, y, w - 4, h, mk(12, 65, 22));
        fillRect(x + 4, y + 3, w - 8, h - 5, fl ? mk(255, 255, 255) : mk(32, 228, 65));
        fillRect(x + 6, y + 4, w - 14, 10, mk(115, 255, 128));
        fillRect(x + 8, y + 6, 8, 6, mk(255, 255, 255));
        int eo = e.dir > 0 ? 4 : -2;
        fillRect(x + 10 + eo, y + 16, 10, 10, mk(255, 255, 255)); fillRect(x + 26 + eo, y + 16, 10, 10, mk(255, 255, 255));
        fillRect(x + 13 + eo + (e.dir > 0 ? 2 : 0), y + 19, 5, 5, mk(15, 20, 15)); fillRect(x + 29 + eo + (e.dir > 0 ? 2 : 0), y + 19, 5, 5, mk(15, 20, 15));
    } else if (e.kind == E_BAT) {
        Color bodyC = (e.hurt > 0) ? mk(245, 58, 52) : mk(78, 52, 64);
        Color wingC = (e.hurt > 0) ? mk(255, 105, 95) : mk(128, 64, 76);
        Color outC  = mk(38, 22, 32);
        if (e.deathTimer > 0) { fillRect(sx + 8, sy + 24, 28, 12, outC); fillRect(sx + 10, sy + 26, 24, 8, wingC); return; }
        int frame = (e.timer / 4) & 3;
        int wy = (e.state == 1) ? 4 : ((frame == 0) ? -10 : (frame == 1 ? -2 : (frame == 2 ? 8 : 0)));
        int ww = (frame == 1) ? 20 : 16;
        fillRect(sx - ww + 2, sy + 14 + wy, ww, 12, outC); fillRect(sx + 42, sy + 14 + wy, ww, 12, outC);
        fillRect(sx - ww + 4, sy + 16 + wy, ww - 4, 8, wingC); fillRect(sx + 42, sy + 16 + wy, ww - 4, 8, wingC);
        fillRect(sx + 10, sy + 8, 24, 24, outC); fillRect(sx + 12, sy + 4, 6, 6, outC); fillRect(sx + 26, sy + 4, 6, 6, outC);
        fillRect(sx + 12, sy + 10, 20, 20, fl ? mk(255, 255, 255) : bodyC);
        fillRect(sx + 15, sy + 16, 4, 4, mk(255, 220, 75)); fillRect(sx + 25, sy + 16, 4, 4, mk(255, 220, 75));
        fillRect(sx + 17, sy + 24, 3, 4, mk(255, 255, 255)); fillRect(sx + 24, sy + 24, 3, 4, mk(255, 255, 255));
    } else if (e.kind == E_MUSHROOM) {
        if (e.state == 0) {
            fillRect(sx + 14, sy + 32, 16, 12, mk(225, 215, 195)); fillRect(sx + 6, sy + 20, 32, 14, mk(215, 45, 65));
            fillRect(sx + 12, sy + 23, 6, 6, mk(255, 245, 230)); fillRect(sx + 26, sy + 24, 5, 5, mk(255, 245, 230));
        } else {
            fillRect(sx + 8, sy + 18, 28, 26, mk(235, 220, 195)); fillRect(sx + 2, sy, 40, 20, fl ? mk(255, 255, 255) : mk(215, 42, 62));
            fillRect(sx + 8, sy + 4, 8, 8, mk(255, 245, 230)); fillRect(sx + 26, sy + 5, 8, 8, mk(255, 245, 230));
            int eo = e.dir > 0 ? 3 : -3;
            fillRect(sx + 14 + eo, sy + 24, 5, 8, mk(25, 15, 20)); fillRect(sx + 25 + eo, sy + 24, 5, 8, mk(25, 15, 20));
        }
    } else if (e.kind == E_WASP) {
        int y = sy + (SN[(e.timer / 4) & 15] * 5) / 256;
        for (int l = 0; l < 3; l++) { fillRect(sx - 16, y + 2 + l * 10, 20, 5, mk(45, 52, 62)); fillRect(sx + 52, y + 2 + l * 10, 20, 5, mk(45, 52, 62)); }
        fillRect(sx + 14, y + 4, 30, 22, fl ? mk(255, 255, 255) : mk(95, 138, 38));
        fillRect(sx + 12, y + 10, 10, 10, mk(195, 22, 32)); fillRect(sx + 36, y + 10, 10, 10, mk(195, 22, 32));
        fillRect(sx + 10, y + 26, 38, 28, fl ? mk(255, 255, 255) : mk(185, 118, 52));
        fillRect(sx + 12, y + 32, 34, 5, mk(115, 68, 28)); fillRect(sx + 14, y + 42, 30, 5, mk(115, 68, 28));
        fillRect(sx + 24, y + 54, 10, 8, mk(42, 35, 45));
    } else if (e.kind == E_CHOMPER) {
        Color bC = fl ? mk(255, 255, 255) : mk(188, 108, 128), sC = mk(128, 68, 86);
        fillRect(sx, sy + 8, 68, 60, sC); fillRect(sx + 4, sy + 12, 60, 52, bC);
        fillRect(sx + 22, sy - 6, 10, 18, bC); fillRect(sx + 36, sy - 4, 10, 16, bC);
        fillRect(sx + 10, sy + 24, 10, 10, sC); fillRect(sx + 18, sy + 42, 12, 10, sC);
        int mx = (e.dir > 0) ? (sx + 34) : (sx + 4), ex = (e.dir > 0) ? (sx + 36) : (sx + 22);
        fillRect(ex, sy + 18, 10, 10, mk(255, 220, 45)); fillRect(ex + 3, sy + 21, 4, 4, mk(20, 15, 15));
        fillRect(mx, sy + 30, 30, 32, mk(75, 22, 32));
        for (int t = 0; t < 3; t++) { fillRect(mx + 2 + t * 9, sy + 30, 7, 10, mk(245, 245, 238)); fillRect(mx + 2 + t * 9, sy + 52, 7, 10, mk(245, 245, 238)); }
    } else {
        fillRect(sx, sy + 6, 44, 38, mk(48, 54, 68)); fillRect(sx + 4, sy + 10, 36, 30, fl ? mk(255,255,255) : mk(102, 114, 136));
        fillRect(sx + 6, sy + 12, 32, 7, mk(152, 166, 192));
        fillRect(e.dir < 0 ? sx - 14 : sx + 38, sy + 18, 18, 12, mk(235, 75, 45)); fillRect(sx + 14, sy + 16, 16, 12, mk(255, 215, 70));
    }
}

static void drawBossChar(int bx, int by, int bw, int bh, bool hurt) {
    if (bossKind == B_KING_FROGUS) {
        Color bodyC = hurt ? mk(255, 255, 255) : mk(64, 218, 72), bellyC = hurt ? mk(255, 250, 210) : mk(158, 252, 168), capeC = mk(168, 32, 38);
        int capeX = (bossTongueDir > 0) ? bx : (bx + bw - 54);
        fillRect(capeX, by + 48, 54, bh - 48, capeC); fillRect(capeX, by + 48, 54, 10, mk(240, 238, 232));
        fillRect(bx + 8, by + 18, bw - 16, bh - 18, mk(24, 118, 36)); fillRect(bx + 14, by + 24, bw - 28, bh - 28, bodyC); fillRect(bx + 24, by + 76, bw - 48, bh - 80, bellyC);
        int crX = bx + 34, crY = by - 14;
        fillRect(crX, crY + 12, 56, 16, mk(252, 212, 55)); fillRect(crX, crY, 12, 14, mk(252, 212, 55)); fillRect(crX + 22, crY - 4, 12, 18, mk(252, 212, 55)); fillRect(crX + 44, crY, 12, 14, mk(252, 212, 55));
        fillRect(crX + 24, crY + 14, 8, 8, mk(225, 35, 55));
        int eyeX = (bossTongueDir > 0) ? (bx + 92) : (bx + 36);
        fillRect(eyeX, by + 28, 24, 24, mk(255, 255, 255)); fillRect(eyeX + (bossTongueDir > 0 ? 10 : 4), by + 34, 10, 10, mk(20, 28, 20));
        int scX = (bossTongueDir > 0) ? (bx + bw + 8) : (bx - 20);
        fillRect(scX + 4, by + 56, 6, 80, mk(245, 205, 62)); fillRect(scX - 4, by + 40, 22, 20, mk(255, 225, 85)); fillRect(scX + 1, by + 45, 12, 10, mk(215, 32, 48));
        if (bossAttackState >= 1) {
            int mX = (bossTongueDir > 0) ? (bx + 76) : (bx + 12);
            fillRect(mX, by + 46, 68, 44, mk(118, 28, 36));
            if (bossTongueLen > 0) {
                int tx = (bossTongueDir > 0) ? (bx + bw - 14) : (bx - bossTongueLen + 14), ty = by + 58;
                fillRect(tx, ty, bossTongueLen, 16, mk(198, 96, 96)); fillRect(tx, ty + 3, bossTongueLen, 6, mk(232, 132, 132));
                int tipX = (bossTongueDir > 0) ? (tx + bossTongueLen - 22) : tx;
                fillRect(tipX, ty - 8, 26, 32, mk(185, 82, 82)); fillRect(tipX + 8, ty - 14, 10, 6, mk(235, 135, 135)); fillRect(tipX + 8, ty + 24, 10, 6, mk(235, 135, 135));
            }
        }
        return;
    }
    Color dC = hurt ? mk(255,240,180) : (bossKind == B_FROST_LORD ? mk(28,72,118) : (bossKind == B_CHOMP_TOTEM ? mk(128,56,82) : mk(85,22,32)));
    Color mC = hurt ? mk(255,255,255) : (bossKind == B_FROST_LORD ? mk(68,165,235) : (bossKind == B_CHOMP_TOTEM ? mk(205,105,138) : mk(175,35,45)));
    Color cC = (tick & 4) ? mk(255,215,65) : (bossKind == B_FROST_LORD ? mk(165,245,255) : mk(255,110,30));
    fillRect(bx, by, bw, bh, dC); fillRect(bx + 8, by + 8, bw - 16, bh - 16, mC); fillRect(bx + 12, by + 12, bw - 24, 22, cC);
    fillRect(bx + 20, by - 8, bw - 40, 10, cC);
    fillRect(bx + 18, by + 26, 24, 20, mk(255,240,90)); fillRect(bx + bw - 42, by + 26, 24, 20, mk(255,240,90));
    fillRect(bx + 24, by + 32, 12, 12, mk(20,0,0)); fillRect(bx + bw - 36, by + 32, 12, 12, mk(20,0,0));
    fillRect(bx + 14, by + 20, 32, 6, dC); fillRect(bx + bw - 46, by + 20, 32, 6, dC);
    fillRect(bx + 22, by + 68, bw - 44, 24, mk(35,10,18));
    for (int k = 0; k < 4; k++) fillRect(bx + 28 + k * 18, by + 70, 8, 18, mk(245, 245, 240));
}

static void renderGame(int b) {
    resetClip();
    if (winTimer > 0) {
        if (needWin[b]) {
            fillRect(0, 0, FW, FH, mk(20, 90, 60));
            drawLabel(T_COMPLETE, 960, 310, 110, 10, mk(255, 255, 255));
            drawLabel(T_GREAT, 960, 440, 80, 6, mk(255, 220, 180));
            fillRect(580, 560, 760, 220, mk(15, 58, 40));
            char line1[64], n1[16]; strcpy(line1, "COINS COLLECTED: "); intToStr(stageCoins, n1); strcat(line1, n1); drawTextC(960, 595, line1, 4, mk(255, 225, 95));
            char line2[64], n2[16], n3[16]; strcpy(line2, "CLEAR TIME: "); intToStr(stageTimer / 60, n2); strcat(line2, n2);
            strcat(line2, "S   BEST: "); intToStr(bestTime[curLvl], n3); strcat(line2, n3); strcat(line2, "S"); drawTextC(960, 665, line2, 4, mk(185, 240, 255));
            char line3[64], n4[16]; strcpy(line3, "SECRETS FOUND: "); intToStr(secretsFound, n4); strcat(line3, n4); drawTextC(960, 730, line3, 3, mk(165, 255, 185));
            needWin[b] = false;
        }
        return;
    }
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
            if (p.drawn[b]) { drawPlantTuft(p.x, p.y, p.h, (int)p.angle, curBiome, p.style); p.dBend[b] = (signed char)p.angle; }
        }
        bufStar[b] = tick / 20; dirtyCnt[b] = 0; drawHUDFull(b); needFull[b] = false;
    } else {
        bool trans = (bufRoom[b] != roomIdx);
        for (int i = 0; i < dirtyCnt[b]; i++) { if (dirty[b][i].y < 100) hudNeed[b] = true; restoreRect(b, dirty[b][i].x, dirty[b][i].y, dirty[b][i].w, dirty[b][i].h, !trans); }
        dirtyCnt[b] = 0;
        if (trans) {
            for (int i = 0; i < splatCount; i++) if (splats[i].drawn[b]) { restoreRect(b, splats[i].x - 1, splats[i].y - 1, splats[i].w + 2, splats[i].h + 2, false); splats[i].drawn[b] = false; }
            for (int i = 0; i < plantCount; i++) if (plants[i].drawn[b]) { restoreRect(b, plants[i].x - 12, plants[i].y - plants[i].h - 5, 24, plants[i].h + 5, false); plants[i].drawn[b] = false; }
            for (int r = 0; r < ROWS; r++) for (int c = 0; c < RCOLS; c++) {
                int wc = roomIdx * RCOLS + c; char nc = staticChar(r, wc); bool nh = hiAt(r, wc);
                if (nc == bufGrid[b][r][c] && nh == bufHi[b][r][c]) continue;
                if (bufGrid[b][r][c] != '.' && nc != '#' && nc != 'B') { setClip(c * TILE, r * TILE, TILE, TILE); paintBackground(); resetClip(); }
                bufGrid[b][r][c] = nc; bufHi[b][r][c] = nh;
                if (nc != '.') drawTileCell(r, c, nc, nh);
                if (r <= 1) hudNeed[b] = true;
            }
            bufRoom[b] = roomIdx;
        } else {
            for (int i = 0; i < dplatCount; i++) {
                DPlat &dp = dplats[i]; if (dp.room != roomIdx) continue;
                int c = dp.c % RCOLS, r = dp.r; char nc = staticChar(r, dp.c); bool nh = hiAt(r, dp.c);
                if (nc != bufGrid[b][r][c]) { bufGrid[b][r][c] = nc; bufHi[b][r][c] = nh; restoreRect(b, c * TILE, r * TILE, TILE, TILE, true); }
            }
        }
        for (int i = 0; i < splatCount; i++) {
            Splat &s = splats[i]; if (s.room != bufRoom[b]) continue;
            if (s.drawn[b] && (s.stage >= 4 || s.dStage[b] != s.stage)) { restoreRect(b, s.x - 1, s.y - 1, s.w + 2, s.h + 2, false); s.drawn[b] = false; }
            if (!s.drawn[b] && s.stage < 4) { drawSplat(s); s.drawn[b] = true; s.dStage[b] = s.stage; }
        }
        for (int i = 0; i < plantCount; i++) {
            Plant &p = plants[i]; if (p.room != bufRoom[b]) continue;
            int bd = (int)p.angle;
            if (p.drawn[b] && p.dBend[b] != bd) { restoreRect(b, p.x - 12, p.y - p.h - 5, 24, p.h + 5, false); p.drawn[b] = false; }
            if (!p.drawn[b]) { drawPlantTuft(p.x, p.y, p.h, bd, curBiome, p.style); p.drawn[b] = true; p.dBend[b] = (signed char)bd; }
        }
        int ph = tick / 20;
        if (curBiome == 5 && bufStar[b] != ph) {
            for (int i = 0; i < NUM_STARS; i++) if (starDim(i, bufStar[b]) != starDim(i, ph)) { int sx, sy; starPos(i, sx, sy); restoreRect(b, sx - 3, sy - 3, 9, 9, true); }
            bufStar[b] = ph;
        }
        if ((tick % 60) == 0 || hudCoins[b] != totalCoins) hudNeed[b] = true;
        int hn = deaths < 20 ? deaths : 20, pr = progressNow();
        if (hudNeed[b]) drawHUDFull(b);
        else {
            for (int i = hudSq[b]; i < hn; i++) fillRect(64 + i * 18, 56, 12, 12, mk(215, 28, 42));
            hudSq[b] = hn;
            if (pr > hudProg[b]) fillRect(700 + hudProg[b], 42, pr - hudProg[b], 14, bossActive ? mk(242,72,85) : mk(58,175,245));
            else if (pr < hudProg[b]) fillRect(700 + pr, 42, hudProg[b] - pr, 14, bossActive ? mk(72,24,34) : mk(42,34,72));
            hudProg[b] = pr;
        }
    }
    resetClip();
    int roomX = roomIdx * FW - shakeX, shY = shakeY;

    if (curBiome == 0 && !bossActive) {
        int wf = (tick * 4) % 44;
        for (int k = 0; k < 5; k++) { int wy = 435 + wf + k * 48; if (wy < 660) { fillRect(398, wy, 20, 18, mk(245,252,255)); addDirty(b, 396, wy - 2, 24, 22); } }
    }
    if (curBiome <= 2 && !bossActive) {
        Color bc = curBiome == 0 ? mk(26,75,52) : mk(95,42,30); int wg = ((tick / 8) & 1) ? -6 : 5;
        for (int i = 0; i < MAX_BIRDS; i++) {
            int bx = (i * 390 + tick * 2) % (FW - 80) + 40, by = 125 + (i % 3) * 48 + (SN[((tick / 6) + i * 3) & 15] >> 5);
            fillRect(bx, by, 10, 4, bc); fillRect(bx - 7, by + wg, 8, 4, bc); fillRect(bx + 9, by + wg, 8, 4, bc); addDirty(b, bx - 9, by - 8, 28, 18);
        }
    }

    static const Color AMBC[6] = {{140,235,110},{255,230,130},{245,215,145},{240,250,255},{255,145,45},{175,155,235}};
    for (int i = 0; i < MAX_AMB; i++) {
        int ax = (int)amb[i].x, ay = (int)amb[i].y, sz = amb[i].sz;
        if (amb[i].kind == 1) {
            bool bright = (((tick / 6) + i) & 3) != 0;
            if (bright) fillRect(ax - 2, ay - 2, sz + 4, sz + 4, mk(115, 225, 95));
            fillRect(ax, ay, sz, sz, bright ? mk(245, 255, 145) : mk(145, 215, 95));
            addDirty(b, ax - 3, ay - 3, sz + 6, sz + 6);
        } else { fillRect(ax, ay, sz, sz, AMBC[curBiome]); addDirty(b, ax - 1, ay - 1, sz + 2, sz + 2); }
    }

    for (int i = 0; i < mplatCount; i++) {
        MPlat &mp = mplats[i]; if (mp.room != roomIdx) continue;
        int mx = (int)mp.x + shakeX, my = (int)mp.y + shY;
        fillRect(mx, my, mp.w, 18, TPAL[curBiome].botC); fillRect(mx + 2, my + 2, mp.w - 4, 12, TPAL[curBiome].litC); fillRect(mx, my, mp.w, 5, TPAL[curBiome].topC);
        addDirty(b, mx - 4, my - 2, mp.w + 8, 22);
    }
    for (int i = 0; i < dplatCount; i++) {
        DPlat &dp = dplats[i]; if (dp.room != roomIdx || !dp.triggered || dp.timer >= 35) continue;
        int dx = (dp.c % RCOLS) * TILE + ((tick & 1) ? 2 : -2), dy = dp.r * TILE;
        fillRect(dx + 4, dy + 2, TILE - 8, 8, mk(255, 195, 75)); addDirty(b, (dp.c % RCOLS) * TILE - 4, dy, TILE + 8, 16);
    }
    for (int i = 0; i < coinCount; i++) {
        Coin &cn = coins[i]; if (cn.taken || cn.room != roomIdx) continue;
        int bob = (SN[((tick / 5) + i * 3) & 15] * 4) / 256, cx = cn.x + shakeX, cy = cn.y + bob + shY;
        int cw = (((tick / 6) + i) & 3) == 1 ? 12 : 20, ox = cx + (20 - cw) / 2;
        fillRect(ox - 2, cy - 2, cw + 4, 24, cn.secret ? mk(45, 145, 225) : mk(185, 115, 20));
        fillRect(ox, cy, cw, 20, cn.secret ? mk(125, 225, 255) : mk(255, 215, 55));
        fillRect(ox + 2, cy + 3, cw > 6 ? 4 : 2, 10, mk(255, 250, 195)); addDirty(b, cx - 4, cn.y - 8, 28, 36);
    }
    for (int i = 0; i < chestCount; i++) {
        Chest &ch = chests[i]; if (ch.room != roomIdx) continue;
        int cx = ch.x + shakeX, cy = ch.y + shY;
        fillRect(cx, cy + 8, 44, 30, mk(92, 48, 22)); fillRect(cx + 3, cy + 11, 38, 24, mk(152, 86, 38));
        fillRect(cx, cy + (ch.opened ? -4 : 2), 44, 10, mk(215, 165, 45));
        fillRect(cx + 18, cy + 12, 8, 10, ch.opened ? mk(90, 90, 100) : mk(255, 232, 95)); addDirty(b, cx - 4, cy - 8, 52, 48);
    }
    for (int i = 0; i < powerupCount; i++) {
        PowerUp &pu = powerups[i]; if (!pu.active || pu.room != roomIdx) continue;
        int bob = (SN[((tick / 4) + i * 5) & 15] * 5) / 256, px0 = pu.x + shakeX, py0 = pu.y + bob + shY;
        Color cOut = pu.kind == PU_SHIELD ? mk(35, 145, 235) : (pu.kind == PU_SPEED ? mk(225, 135, 25) : mk(185, 45, 215));
        Color cIn  = pu.kind == PU_SHIELD ? mk(145, 235, 255) : (pu.kind == PU_SPEED ? mk(255, 235, 95) : mk(245, 155, 255));
        fillRect(px0, py0, 32, 32, cOut); fillRect(px0 + 4, py0 + 4, 24, 24, cIn); fillRect(px0 + 10, py0 + 10, 12, 12, mk(255, 255, 255));
        addDirty(b, px0 - 4, pu.y - 8, 40, 48);
    }
    for (int i = 0; i < npcCount; i++) {
        NPC &np = npcs[i]; if (np.room != roomIdx) continue;
        int nx = np.x + shakeX, ny = np.y + shY;
        fillRect(nx + 4, ny + 14, 34, 32, mk(58, 95, 165)); fillRect(nx + 8, ny + 2, 26, 18, mk(240, 205, 165));
        fillRect(nx + 4, ny - 4, 34, 8, mk(225, 185, 65)); fillRect(nx + 12, ny + 18, 18, 14, mk(235, 235, 242));
        fillRect(nx + 40, ny - 2, 5, 48, mk(135, 86, 42)); fillRect(nx + 38, ny - 8, 9, 9, mk(95, 245, 195));
        addDirty(b, nx - 2, ny - 10, 52, 60);
    }
    if (activeNpcMsg >= 0) {
        fillRect(410, 105, 1100, 58, mk(235, 190, 65)); fillRect(414, 109, 1092, 50, mk(28, 20, 48));
        drawLabel(activeNpcMsg, 960, 109, 50, 3, mk(245, 242, 255)); addDirty(b, 408, 103, 1104, 62);
    }

    for (int i = 0; i < checkCount; i++) if (checkC[i] / RCOLS == roomIdx) {
        int fx = (checkC[i] % RCOLS) * TILE + 24 + shakeX, fy = checkR[i] * TILE - 64 + shY;
        fillRect(fx, fy, 6, 64, mk(220,220,235)); fillRect(fx + 6, fy + 4, 28, 20, checkAct[i] ? mk(65,235,110) : mk(235,190,55)); addDirty(b, fx - 4, fy - 4, 42, 70);
    }
    for (int r = 0; r < ROWS; r++) for (int c = roomIdx * RCOLS; c < roomIdx * RCOLS + RCOLS && c < COLS; c++)
        if (level[r][c] == 'O') { int cx = c * TILE + 30 - roomX, cy = r * TILE + 30 + shY; drawSaw(cx, cy); addDirty(b, cx - 28, cy - 28, 58, 58); }

    for (int i = 0; i < enemyCount; i++) if (enemies[i].active) {
        int sx = (int)enemies[i].x - roomX, sy = (int)enemies[i].y + shY;
        if (sx >= -80 && sx <= FW + 80) { drawEnemyChar(enemies[i], sx, sy); addDirty(b, sx - 24, sy - 12, 104, 88); }
    }

    if (bossActive && bossHP > 0) {
        fillRect(15 * TILE + 20, 7 * TILE - 18, 80, 18, bossSwCd == 0 ? ((tick & 8) ? mk(80,255,120) : mk(40,200,85)) : mk(90,80,100));
        addDirty(b, 15 * TILE + 18, 7 * TILE - 20, 84, 22);
        int bx = (int)bossX + shakeX, by = (int)bossY + shY;
        if (bossBeam > 0) { fillRect(bx + bossW / 2 - 16, 7 * TILE, 32, by - 7 * TILE + 20, mk(120,255,220)); addDirty(b, bx + bossW / 2 - 20, 7 * TILE, 40, by - 7 * TILE + 24); }
        drawBossChar(bx, by, bossW, bossH, (bossHurt & 2) != 0);
        int dirtyX = (bossKind == B_KING_FROGUS && bossTongueDir < 0) ? (bx - bossTongueLen - 28) : (bx - 28);
        addDirty(b, dirtyX, by - 24, bossW + bossTongueLen + 64, bossH + 36);
    }

    for (int i = 0; i < MAX_SHOTS; i++) if (shots[i].active) {
        int sx = (int)shots[i].x - roomX, sy = (int)shots[i].y + shY;
        if (sx >= -30 && sx <= FW + 30) {
            Color c1 = shots[i].kind == 1 ? mk(95, 235, 65) : (shots[i].kind == 2 ? mk(85, 205, 255) : mk(255, 95, 35));
            Color c2 = shots[i].kind == 1 ? mk(215, 255, 145) : (shots[i].kind == 2 ? mk(225, 250, 255) : mk(255, 235, 90));
            fillRect(sx - 14, sy - 14, 28, 28, c1); fillRect(sx - 8, sy - 8, 16, 16, c2); addDirty(b, sx - 16, sy - 16, 32, 32);
        }
    }

    if (slamWaveTimer > 0) {
        int rad = (15 - slamWaveTimer) * 12, wx = slamWaveX - roomX, wy = slamWaveY + shY - 10;
        fillRect(wx - rad, wy, 22, 10, mk(255, 230, 120)); fillRect(wx + rad - 22, wy, 22, 10, mk(255, 230, 120));
        addDirty(b, wx - rad - 4, wy - 4, rad * 2 + 8, 18);
    }

    for (int i = 0; i < MAX_TRAIL; i++) {
        if (trails[i].life <= 0) continue;
        int tx = (int)trails[i].x - roomX, ty = (int)trails[i].y + shY;
        fillRect(tx + 8, ty + 8, PW - 16, PH - 16, SkinPalSKINS[curSkin].mainC); addDirty(b, tx + 4, ty + 4, PW - 8, PH - 8);
    }

    for (int i = 0; i < MAX_PARTS; i++) {
        Part &p = parts[i]; if (p.life <= 0) continue;
        int sx = (int)p.x - roomX, sy = (int)p.y + shY; if (sx < -20 || sx > FW + 20 || sy < -20 || sy > FH + 20) continue;
        int sz = (p.kind != 1) ? 2 + (p.size - 2) * p.life / p.maxLife : p.size;
        Color col = p.kind == 0 ? (p.life * 2 > p.maxLife ? mk(205,195,230) : mk(150,140,185)) :
                   (p.kind == 2 ? mk(255, 220, 85) : (p.kind == 3 ? mk(125, 235, 255) : mk(210, 18, 32)));
        fillRect(sx, sy, sz, sz, col); addDirty(b, sx - 1, sy - 1, sz + 2, sz + 2);
    }

    if (deadTimer == 0) {
        int x = (int)px - roomX, y = (int)py + shY, dw = iclamp((int)(PW * squashX), 20, 80), dh = iclamp((int)(PH * squashY), 20, 80);
        int dx = x + (PW - dw) / 2, dy = y + (PH - dh);
        const SkinPal &sp = SkinPalSKINS[curSkin];
        fillRect(dx, dy, dw, dh, shieldHits > 0 ? mk(85, 215, 255) : sp.outC);
        fillRect(dx + 3, dy + 3, dw - 6, dh - 6, sp.mainC);
        fillRect(dx + 5, dy + 5, dw - 10, 7, sp.hiC);
        drawPlayerFace(dx, dy, dw, dh, facing);
        addDirty(b, dx - 6, dy - 10, dw + 12, dh + 14);
    }

    if (bannerTimer > 0 && bannerTextId >= 0) {
        fillRect(510, 112, 900, 62, mk(255, 215, 75)); fillRect(514, 116, 892, 54, mk(38, 24, 64));
        drawLabel(bannerTextId, 960, 116, 54, 4, mk(255, 245, 145)); addDirty(b, 508, 110, 904, 66);
    }
}

// ---------- Cutscenes & Menus ----------
static void drawCubeK(int x, int y, int k) {
    const SkinPal &sp = SkinPalSKINS[curSkin];
    fillRect(x, y, 88*k, 88*k, sp.outC); fillRect(x+6*k, y+6*k, 76*k, 76*k, sp.mainC);
    fillRect(x+18*k, y+24*k, 20*k, 28*k, mk(255,255,255)); fillRect(x+50*k, y+24*k, 20*k, 28*k, mk(255,255,255));
    fillRect(x+28*k, y+32*k, 10*k, 16*k, mk(0,0,0)); fillRect(x+60*k, y+32*k, 10*k, 16*k, mk(0,0,0));
    fillRect(x+30*k, y+64*k, 28*k, 8*k, sp.mouthC);
}
static void drawGirlK(int x, int y, int k) {
    fillRect(x, y, 88*k, 88*k, mk(255,140,170)); fillRect(x, y+8*k, 88*k, 12*k, mk(240,230,200));
    fillRect(x+18*k, y+30*k, 20*k, 26*k, mk(255,255,255)); fillRect(x+50*k, y+30*k, 20*k, 26*k, mk(255,255,255));
    fillRect(x+26*k, y+38*k, 10*k, 16*k, mk(0,0,0)); fillRect(x+58*k, y+38*k, 10*k, 16*k, mk(0,0,0));
    fillRect(x+34*k, y+66*k, 20*k, 6*k, mk(170,60,90));
}
static void drawPadPrompt(int x, int y, bool dL, bool dR, bool sq, bool xB, const char *lbl) {
    fillRect(x, y, 420, 74, mk(22,16,38)); fillRect(x+3, y+3, 414, 68, mk(38,28,62));
    fillRect(x+18, y+16, 42, 42, dL ? mk(90,200,255) : mk(55,45,80)); drawTextC(x+39, y+27, "<", 3, mk(255,255,255));
    fillRect(x+68, y+16, 42, 42, dR ? mk(90,200,255) : mk(55,45,80)); drawTextC(x+89, y+27, ">", 3, mk(255,255,255));
    fillRect(x+130, y+16, 54, 42, sq ? mk(235,65,145) : mk(55,45,80)); fillRect(x+147, y+25, 20, 20, mk(255,255,255)); fillRect(x+151, y+29, 12, 12, sq ? mk(235,65,145) : mk(55,45,80));
    fillRect(x+196, y+16, 54, 42, xB ? mk(65,165,255) : mk(55,45,80)); drawTextC(x+223, y+27, "X", 3, mk(255,255,255));
    drawTextC(x+332, y+29, lbl, 2, (xB || sq) ? mk(255,235,110) : mk(190,180,215));
}
static void drawMiniDemo(int x, int y, int dir) {
    const SkinPal &sp = SkinPalSKINS[curSkin];
    fillRect(x, y, 44, 44, sp.outC); fillRect(x+3, y+3, 38, 38, sp.mainC); drawPlayerFace(x, y, 44, 44, dir);
}

static void drawStoryArt(int art) {
    int ax = 460, ay = 90, aw = 1000, ah = 500, t = storyAnimTick;
    if (art == 0) {
        fillRect(ax, ay, aw, ah, mk(20,25,60)); fillRect(ax+780, ay+60, 80, 80, mk(230,225,190)); fillRect(ax, ay+380, aw, 120, mk(25,70,45));
        for (int i = 0; i < 4; i++) { fillRect(ax+90+i*240, ay+300, 24, 90, mk(70,45,30)); fillRect(ax+60+i*240, ay+210, 84, 100, mk(15,55,35)); }
        for (int i = 0; i < 16; i++) drawPlantTuft(ax+50+i*60, ay+380, 15, SN[(t/5)&15]/32, 0, i%3);
        int bob = (t / 12) & 1 ? -6 : 0; drawCubeK(ax+300, ay+292+bob, 1); drawGirlK(ax+560, ay+292-bob, 1);
    } else if (art == 1) {
        fillRect(ax, ay, aw, ah, mk(45,10,25)); fillRect(ax, ay+420, aw, 80, mk(30,12,22));
        drawSaw(ax+500+(t%80)-40, ay+230); drawCubeK(ax+120, ay+332, 1); drawGirlK(ax+800, ay+100+SN[(t/4)&15]/32, 1);
    } else if (art == 2) {
        fillRect(ax, ay, aw, ah, mk(35,15,55)); fillRect(ax, ay+420, aw, 80, mk(25,12,40));
        int sh = ((t/3)&1) ? 3 : -3; drawCubeK(ax+368+sh, ay+150, 3);
        fillRect(ax+410+sh, ay+190, 84, 14, mk(60,0,10)); fillRect(ax+518+sh, ay+190, 84, 14, mk(60,0,10));
    } else if (art == 3) {
        fillRect(ax, ay, aw, ah, mk(20,18,40)); fillRect(ax, ay+420, aw, 80, mk(25,25,35));
        fillRect(ax+400, ay+40, 200, 400, mk(55,45,80)); fillRect(ax+470, ay+90, 60, 60, mk(240,200,90));
        fillRect(ax+470, ay+190, 60, 60, mk(240,200,90)); fillRect(ax+440, ay+330, 120, 110, mk(30,25,50));
        drawCubeK(ax+160, ay+332, 1); fillRect(ax+720, ay+340, 70, 90, mk(235,230,215));
    } else if (art == 10) {
        fillRect(ax, ay, aw, ah, mk(65,160,225)); fillRect(ax+820, ay+35, 70, 70, mk(255,235,120));
        fillRect(ax, ay+400, aw, 100, mk(82,54,38)); fillRect(ax, ay+400, aw, 8, mk(65,190,75));
        fillRect(ax+700, ay+290, 240, 110, mk(82,54,38)); fillRect(ax+700, ay+290, 240, 8, mk(65,190,75));
        int cyc = t % 180, cx = iclamp(ax+90+cyc*4, ax, ax+820), cy = ay+356; bool xOn = false; const char *lbl = "WALK";
        if (cyc >= 45 && cyc < 90) { int j = cyc-45; cy = ay+356 - j*(45-j)/4; xOn = j < 14; lbl = "2X JUMP!"; }
        else if (cyc >= 110 && cyc < 160) { int j = cyc-110; cy = ay+356 - j*(55-j)/4; if (j > 32 && cy > ay+246) cy = ay+246; xOn = j < 34; lbl = "SLAM O"; }
        else if (cyc >= 160) { cy = ay+246; lbl = "GREAT!"; }
        if (cyc < 72) { Enemy de = {0,0,0,0,0,0,0,E_SLIME,1,1,0,t,-1,0,0,true}; drawEnemyChar(de, ax+480, ay+356); }
        for (int i = 0; i < 14; i++) { int gx = ax+60+i*45; drawPlantTuft(gx, ay+400, 15, (iabs(cx+22-gx)<42 && cy>ay+320) ? 12 : SN[(t/5+i)&15]/32, 0, i%3); }
        drawMiniDemo(cx, cy, 1); drawPadPrompt(ax+290, ay+20, false, cyc < 165, false, xOn, lbl);
    } else if (art == 11) {
        fillRect(ax, ay, aw, ah, mk(215,125,75)); fillRect(ax+820, ay+120, 90, 90, mk(255,215,95));
        fillRect(ax, ay+400, 420, 100, mk(102,62,40)); fillRect(ax, ay+400, 420, 8, mk(225,175,55));
        fillRect(ax+660, ay+400, 340, 100, mk(102,62,40)); fillRect(ax+660, ay+400, 340, 8, mk(225,175,55));
        int cyc = t % 160, cx = iclamp(ax+70+cyc*6, ax, ax+850), cy = (cyc>=50&&cyc<102) ? ay+356-(cyc-50)*(102-cyc)/5 : ay+356;
        drawMiniDemo(cx, cy, 1); drawPadPrompt(ax+290, ay+20, false, cyc < 140, cyc < 140, cyc >= 50 && cyc < 85, cyc >= 50 ? "DASH+X" : "HOLD SQ");
    } else if (art == 12) {
        fillRect(ax, ay, aw, ah, mk(235,165,85)); fillRect(ax, ay+420, aw, 80, mk(165,118,68)); fillRect(ax, ay+420, aw, 8, mk(240,200,115));
        fillRect(ax+340, ay+150, 55, 200, mk(150,105,58)); fillRect(ax+590, ay+120, 55, 300, mk(150,105,58)); fillRect(ax+590, ay+120, 220, 45, mk(165,118,68));
        int cyc = t % 190, cx = ax+640, cy = ay+76, d = 1; bool dL = false, dR = false, xOn = false; const char *lbl = "CLIMBED!";
        if (cyc < 35) { cx = ax+220+cyc*326/35; cy = ay+376-cyc*140/35; dR = true; xOn = cyc < 10; lbl = "JUMP"; }
        else if (cyc < 75) { cx = ax+546; cy = ay+236+(cyc-35)*60/40; dR = true; lbl = "SLIDE"; }
        else if (cyc < 115) { int w = cyc-75; cx = ax+546-w*151/40; cy = ay+296-w*120/40; d = -1; dL = true; xOn = w < 12; lbl = "WALL X!"; }
        else if (cyc < 155) { int w = cyc-115; cx = ax+395+w*225/40; cy = iclamp(ay+176-w*100/40, ay+76, ay+376); dR = true; xOn = w < 12; lbl = "WALL X!"; }
        drawMiniDemo(cx, cy, d); drawPadPrompt(ax+290, ay+20, dL, dR, false, xOn, lbl);
    } else if (art == 13) {
        fillRect(ax, ay, aw, ah, mk(52,85,130)); fillRect(ax, ay+400, 410, 100, mk(65,88,118)); fillRect(ax+590, ay+400, 410, 100, mk(65,88,118));
        drawSaw(ax+500, ay+370);
        int cyc = t % 160, cx = iclamp(ax+110+cyc*5, ax, ax+820), cy = (cyc>=50&&cyc<106) ? ay+356-(cyc-50)*(106-cyc)/4 : ay+356;
        drawMiniDemo(cx, cy, 1); drawPadPrompt(ax+290, ay+20, false, cyc < 140, false, cyc >= 50 && cyc < 80, cyc >= 50 ? "EDGE JUMP" : "TIMING");
    } else {
        fillRect(ax, ay, aw, ah, art == 14 ? mk(24,58,34) : mk(28,22,58)); fillRect(ax, ay+410, aw, 90, mk(55,36,46));
        fillRect(ax+400, ay+180, 200, 32, mk(55,36,46)); fillRect(ax+460, ay+164, 80, 16, (t&8) ? mk(80,255,120) : mk(40,200,85));
        int cyc = t % 160, bx = ax+620 - (cyc < 80 ? cyc*2 : (160-cyc)*2), cx = ax+180, cy = ay+366;
        int prevKind = bossKind; bossKind = (art == 14) ? B_KING_FROGUS : B_CHOMP_TOTEM;
        bossTongueDir = -1; bossAttackState = (cyc < 65) ? 2 : 0; bossTongueLen = (cyc < 65) ? 95 : 0;
        drawBossChar(bx, ay+276, 140, 134, cyc >= 70 && cyc < 110 && (t&2));
        bossKind = prevKind;
        if (cyc < 25) cx = ax+160+cyc*5;
        else if (cyc < 72) { int j = cyc-25; cx = ax+285+j*235/47; cy = ay+366-j*(47-j)/3-j*104/47; }
        else { int j = cyc-72; cx = ax+520+j*240/88; cy = iclamp(ay+262-j*(70-j)/6, ay, ay+366); }
        drawMiniDemo(cx, cy, 1); drawPadPrompt(ax+290, ay+20, false, true, false, cyc >= 25 && cyc < 55, cyc >= 72 ? "BOSS HIT!" : "STOMP CROWN!");
    }
}

static int storyTotal() {
    const Page &p = PAGES[storyPage]; int c = 0;
    if (useAr()) { for (int i = 0; i < p.n; i++) if (p.ar[i] >= 0) c += arW(p.ar[i]); return c / 14 + 1; }
    for (int i = 0; i < p.n; i++) c += (int)strlen(p.en[i]); return c * 2;
}

static void renderStory(int b) {
    resetClip();
    if (!storyBgDone[b]) { fillRect(0, 0, FW, FH, mk(12,8,24)); storyBgDone[b] = true; storyDrawnPage[b] = -1; }
    if (storyDrawnPage[b] != storyPage) {
        fillRect(460, 630, 1000, 300, mk(30,22,48)); fillRect(460, 950, 1000, 60, mk(12,8,24));
        drawLabel(T_H_STORY, 960, 950, 60, 3, mk(200,190,220)); storyDrawnPage[b] = storyPage; storyShown[b] = 0;
    }
    drawStoryArt(PAGES[storyPage].art);
    if (typed > storyShown[b]) {
        const Page &p = PAGES[storyPage]; bool ar = useAr();
        int f = ar ? storyShown[b] * 14 : storyShown[b] / 2, t = ar ? typed * 14 : typed / 2, bef = 0;
        for (int i = 0; i < p.n; i++) {
            int y = 660 + i * 80;
            if (ar) {
                int id = p.ar[i]; if (id < 0) continue; int w = arW(id), a = iclamp(f - bef, 0, w), b2 = iclamp(t - bef, 0, w);
                if (b2 > a) drawArCols(id, 960 - w / 2, y + (70 - arH(id)) / 2, w - b2, w - a, mk(255,255,255)); bef += w;
            } else {
                int len = (int)strlen(p.en[i]), a = iclamp(f - bef, 0, len), b2 = iclamp(t - bef, 0, len), x0 = 960 - textWidth(p.en[i], 4) / 2;
                for (int k = a; k < b2; k++) { char tmp[2] = {p.en[i][k], 0}; drawText(x0 + k * 24, y + 21, tmp, 4, mk(255,255,255)); }
                bef += len;
            }
        }
        storyShown[b] = typed;
    }
}

static void markPend(int i) { if (i >= 0 && i < 64) pend[0][i] = pend[1][i] = true; }
static void setState(int s) { state = s; uiFull[0] = uiFull[1] = true; }
static void startStory(int id) { storyId = id; storyPage = STORY_FIRST[id]; typed = storyAnimTick = 0; storyBgDone[0] = storyBgDone[1] = false; state = S_STORY; }
static void startLevel() {
    curLvl = levelSel; buildLevel(); roomIdx = 0; respawn();
    deaths = splatCount = winTimer = deadTimer = stageCoins = stageTimer = secretsFound = 0;
    for (int i = 0; i < MAX_PARTS; i++) parts[i].life = 0;
    for (int b = 0; b < 2; b++) { for (int i = 0; i < MAX_SPLATS; i++) splats[i].drawn[b] = false; needFull[b] = true; needWin[b] = hudNeed[b] = false; dirtyCnt[b] = 0; }
    state = S_PLAY;
}
static void launchLevel(int lvl) {
    levelSel = curLvl = lvl;
    if (lvl == 0 && !introSeen) { introSeen = true; startStory(0); }
    else if (lvl >= 0 && lvl <= 4) startStory(2 + lvl);
    else if (lvl == 9 || isBossStage(lvl)) startStory(7);
    else startLevel();
}
static void endStory() { if (storyId == 0) startStory(2); else if (storyId >= 2 && storyId <= 7) startLevel(); else setState(S_LEVELS); }

static void drawBanner(int tid, int sc, int y, int h, bool cubes) {
    fillRect(456, y - 4, 1008, h + 8, mk(245,190,65)); fillRect(460, y, 1000, h, mk(35,25,55));
    drawLabel(tid, 960, y, h, sc, mk(255,75,115));
    if (cubes) { drawCubeK(500, y + (h - 88) / 2, 1); drawCubeK(1332, y + (h - 88) / 2, 1); }
}
static void drawHint(int tid) {
    int w = labelWidth(tid, 3) + 80;
    fillRect(960 - w / 2 - 3, 977, w + 6, 66, mk(235,185,65)); fillRect(960 - w / 2, 980, w, 60, mk(35,25,55));
    drawLabel(tid, 960, 980, 60, 3, mk(220,212,238));
}

static const int SHOP_COSTS[NUM_SKINS + NUM_UPGS] = {0, 25, 40, 50, 65, 80, 45, 55, 75};
static const int SHOP_NAMES[NUM_SKINS + NUM_UPGS] = {
    T_SKIN0, T_SKIN1, T_SKIN2, T_SKIN3, T_SKIN4, T_SKIN5, T_UPG0, T_UPG1, T_UPG2
};

static void drawItem(int i) {
    static const int MENU_T[6] = {T_LEVELS, T_SHOP, T_ACH, T_SETTINGS, T_CREDITS, T_EXIT};
    if (state == S_MENU && i >= 0 && i <= 5) {
        int y = 325 + i * 102; bool sel = (i == menuSel);
        if (sel) fillRect(606, y - 4, 708, 88, mk(255,210,85));
        fillRect(610, y, 700, 80, sel ? mk(215,30,45) : mk(48,36,68));
        drawLabel(MENU_T[i], 960, y, 80, 5, sel ? mk(255,255,255) : mk(215,205,230));
    } else if (state == S_LEVELS) {
        if (i >= 0 && i < NUM_LEVELS) {
            int x = 375 + (i % LCOLS) * 108, y = 330 + (i / LCOLS) * 108;
            bool sel = (i == levelSel), unl = (i < unlockedLevels), bs = isBossStage(i);
            if (sel) fillRect(x, y, 90, 90, mk(245,245,255)); else if (bs && unl) fillRect(x, y, 90, 90, mk(255,195,55));
            int ins = (sel || (bs && unl)) ? 6 : 0;
            fillRect(x + ins, y + ins, 90 - 2 * ins, 90 - 2 * ins, unl ? (bs ? mk(165,15,45) : mk(205,25,35)) : mk(50,40,62));
            if (unl) {
                if (bs) { fillRect(x+27,y+10,36,6,mk(255,215,70)); fillRect(x+27,y+6,8,6,mk(255,215,70)); fillRect(x+41,y+4,8,8,mk(255,215,70)); fillRect(x+55,y+6,8,6,mk(255,215,70)); }
                char buf[8]; intToStr(i + 1, buf); drawTextC(x + 45, y + (bs ? 28 : 24), buf, 6, mk(255,255,255));
                if (bestTime[i] > 0) fillRect(x + 68, y + 68, 12, 12, bestTime[i] <= 35 ? mk(255, 225, 65) : mk(145, 225, 255));
            } else {
                Color g = mk(140,140,155); int cx = x + 45, cy = y + 45;
                fillRect(cx-18,cy-2,36,28,g); fillRect(cx-12,cy-26,24,6,g); fillRect(cx-12,cy-22,6,20,g); fillRect(cx+6,cy-22,6,20,g); fillRect(cx-3,cy+6,6,10,mk(40,40,50));
            }
        } else if (i == NUM_LEVELS) {
            fillRect(510, 880, 900, 60, mk(35,25,55));
            bool ok = (levelSel < unlockedLevels); Color c = ok ? mk(120,230,140) : mk(255,170,60);
            if (useAr()) drawLabel(ok ? T_PLAYX : T_LOCKED, 960, 880, 60, 4, c);
            else {
                char buf[80], n[8]; strcpy(buf, isBossStage(levelSel) ? "BOSS LEVEL " : "LEVEL "); intToStr(levelSel + 1, n); strcat(buf, n);
                if (ok && bestTime[levelSel] > 0) { char tn[8]; intToStr(bestTime[levelSel], tn); strcat(buf, " [BEST "); strcat(buf, tn); strcat(buf, "S]"); }
                else strcat(buf, ok ? ": PRESS X TO PLAY" : ": LOCKED");
                drawTextC(960, 896, buf, 4, c);
            }
        }
    } else if (state == S_SHOP && i >= 0 && i < (NUM_SKINS + NUM_UPGS)) {
        int y = 265 + i * 74; bool sel = (i == shopSel);
        bool owned = (i < NUM_SKINS) ? skinOwned[i] : upgOwned[i - NUM_SKINS];
        bool equipped = (i < NUM_SKINS && curSkin == i) || (i >= NUM_SKINS && owned);
        if (sel) fillRect(416, y - 3, 1088, 68, mk(255, 215, 75));
        fillRect(420, y, 1080, 62, sel ? mk(195, 32, 52) : mk(42, 32, 66));
        if (i < NUM_SKINS) {
            const SkinPal &sp = SkinPalSKINS[i];
            fillRect(442, y + 9, 44, 44, sp.outC); fillRect(446, y + 13, 36, 36, sp.mainC); fillRect(448, y + 15, 32, 6, sp.hiC);
        } else { fillRect(442, y + 9, 44, 44, mk(65, 185, 245)); fillRect(450, y + 17, 28, 28, mk(195, 245, 255)); }
        drawLabel(SHOP_NAMES[i], 880, y, 62, 4, mk(255, 255, 255));
        if (equipped) drawText(1310, y + 20, "EQUIPPED", 3, mk(115, 255, 145));
        else if (owned) drawText(1330, y + 20, "OWNED", 3, mk(185, 230, 255));
        else { char cBuf[16]; intToStr(SHOP_COSTS[i], cBuf); strcat(cBuf, "G"); drawText(1345, y + 20, cBuf, 3, mk(255, 225, 85)); }
    } else if (state == S_ACH && i >= 0 && i < NUM_ACHS) {
        int y = 310 + i * 96; bool unl = achUnlocked[i];
        fillRect(436, y - 3, 1048, 82, unl ? mk(255, 210, 75) : mk(85, 75, 110));
        fillRect(440, y, 1040, 76, unl ? mk(45, 88, 64) : mk(38, 28, 58));
        fillRect(464, y + 16, 44, 44, unl ? mk(255, 215, 55) : mk(75, 65, 95));
        drawLabel(T_ACH0 + i, 960, y, 76, 4, unl ? mk(255, 255, 255) : mk(165, 155, 185));
    } else if (state == S_SETTINGS && i >= 0 && i <= 2) {
        int y = 380 + i * 110; bool sel = (i == settingsSel);
        if (sel) fillRect(506, y - 4, 908, 98, mk(255,210,85));
        fillRect(510, y, 900, 90, sel ? mk(215,30,45) : mk(48,36,68));
        drawLabel(i == 0 ? (controlMode ? T_CTRL_A : T_CTRL_B) : (i == 1 ? T_LANG : T_BACK), 960, y, 90, 6, sel ? mk(255,255,255) : mk(215,205,230));
    }
}

static void drawUI(int b) {
    resetClip();
    if (uiFull[b]) {
        fillRect(0,0,FW,320,mk(32,22,68)); fillRect(0,320,FW,360,mk(48,30,88)); fillRect(0,680,FW,260,mk(34,78,58));
        fillRect(0,940,FW,140,mk(78,48,30)); fillRect(0,940,FW,10,mk(68,198,72));
        fillRect(1560,85,110,110,mk(255,210,85)); fillRect(1576,101,78,78,mk(255,238,140));
        for (int i = 0; i < 4; i++) { fillRect(i*520+40,580,260,100,mk(42,98,72)); fillRect(i*520+90,545,160,40,mk(42,98,72)); }
        for (int gx = 90; gx < FW - 80; gx += 75) drawPlantTuft(gx, 940, 14, SN[(tick/8)&15]/36, 0, (gx/75)%3);

        fillRect(60, 42, 260, 54, mk(245, 195, 65)); fillRect(64, 46, 252, 46, mk(35, 25, 58));
        fillRect(80, 57, 24, 24, mk(255, 215, 55)); char cBuf[16]; intToStr(totalCoins, cBuf); drawText(120, 58, cBuf, 4, mk(255, 235, 115));

        if (state == S_MENU) { drawBanner(T_TITLE, 12, 120, 150, true); drawHint(T_H_SELECT); }
        else if (state == S_LEVELS) { drawBanner(T_B_LEVELS, 8, 130, 110, false); drawHint(T_H_LEVELS); }
        else if (state == S_SHOP) { drawBanner(T_B_SHOP, 8, 120, 110, false); drawHint(T_H_SHOP); }
        else if (state == S_ACH) { drawBanner(T_B_ACH, 8, 130, 110, false); drawHint(T_H_BACK); }
        else if (state == S_SETTINGS) { drawBanner(T_B_SETTINGS, 8, 130, 110, false); fillRect(607,737,706,66,mk(235,185,65)); fillRect(610,740,700,60,mk(35,25,55)); drawLabel(T_TIP,960,740,60,3,mk(220,212,238)); drawHint(T_H_BACK); }
        else if (state == S_CREDITS) { drawBanner(T_B_CREDITS, 8, 130, 110, false); fillRect(456,296,1008,548,mk(235,185,65)); fillRect(460,300,1000,540,mk(35,25,55)); drawLabel(T_DESIGNERS,960,340,60,5,mk(220,212,238)); drawLabel(T_KAREEM,960,450,110,10,mk(255,75,115)); drawLabel(T_CLAUDE,960,580,110,10,mk(255,75,115)); drawLabel(T_GAME,960,740,60,4,mk(165,165,185)); drawHint(T_H_BACK); }

        int cnt = state == S_MENU ? 6 : (state == S_LEVELS ? NUM_LEVELS + 1 : (state == S_SHOP ? (NUM_SKINS + NUM_UPGS) : (state == S_ACH ? NUM_ACHS : (state == S_SETTINGS ? 3 : 0))));
        for (int i = 0; i < cnt; i++) drawItem(i);
        uiFull[b] = false; memset(pend[b], 0, 64);
    } else {
        for (int i = 0; i < 64; i++) if (pend[b][i]) { drawItem(i); pend[b][i] = false; }
    }
}

static void updateUI() {
    bool nu = navEvent(0, in.up), nd = navEvent(1, in.down), nl = navEvent(2, in.left), nr = navEvent(3, in.right);
    if (state == S_MENU) {
        int o = menuSel; if (nu) menuSel = (menuSel + 5) % 6; if (nd) menuSel = (menuSel + 1) % 6;
        if (menuSel != o) { markPend(o); markPend(menuSel); snd(0); }
        if (in.crossNew) {
            if (menuSel == 0) { snd(1); setState(S_LEVELS); }
            else if (menuSel == 1) { snd(1); shopSel = 0; setState(S_SHOP); }
            else if (menuSel == 2) { snd(1); achSel = 0; setState(S_ACH); }
            else if (menuSel == 3) { snd(1); settingsSel = 0; setState(S_SETTINGS); }
            else if (menuSel == 4) { snd(1); setState(S_CREDITS); }
            else { snd(2); saveGame(); exit(0); }
        }
    } else if (state == S_LEVELS) {
        int o = levelSel, n = levelSel;
        if (nl && n > 0) n--; if (nr && n < NUM_LEVELS - 1) n++;
        if (nu && n >= LCOLS) n -= LCOLS; if (nd && n + LCOLS < NUM_LEVELS) n += LCOLS;
        if (n != o) { levelSel = n; markPend(o); markPend(n); markPend(NUM_LEVELS); snd(0); }
        if (in.crossNew && levelSel < unlockedLevels) { snd(1); launchLevel(levelSel); }
        else if (in.crossNew) snd(3);
        else if (in.circleNew) { snd(2); setState(S_MENU); }
    } else if (state == S_SHOP) {
        int totalItems = NUM_SKINS + NUM_UPGS, o = shopSel;
        if (nu) shopSel = (shopSel + totalItems - 1) % totalItems; if (nd) shopSel = (shopSel + 1) % totalItems;
        if (shopSel != o) { markPend(o); markPend(shopSel); snd(0); }
        if (in.crossNew) {
            if (shopSel < NUM_SKINS) {
                if (skinOwned[shopSel]) { curSkin = shopSel; snd(1); saveGame(); setState(S_SHOP); }
                else if (totalCoins >= SHOP_COSTS[shopSel]) { totalCoins -= SHOP_COSTS[shopSel]; skinOwned[shopSel] = true; curSkin = shopSel; snd(1); saveGame(); setState(S_SHOP); }
                else snd(3);
            } else {
                int uIdx = shopSel - NUM_SKINS;
                if (!upgOwned[uIdx] && totalCoins >= SHOP_COSTS[shopSel]) { totalCoins -= SHOP_COSTS[shopSel]; upgOwned[uIdx] = true; snd(1); saveGame(); setState(S_SHOP); }
                else snd(3);
            }
        } else if (in.circleNew) { snd(2); setState(S_MENU); }
    } else if (state == S_ACH && (in.crossNew || in.circleNew)) { snd(2); setState(S_MENU); }
    else if (state == S_SETTINGS) {
        int o = settingsSel; if (nu) settingsSel = (settingsSel + 2) % 3; if (nd) settingsSel = (settingsSel + 1) % 3;
        if (settingsSel != o) { markPend(o); markPend(settingsSel); snd(0); }
        if (settingsSel == 0 && (nl || nr || in.crossNew)) { controlMode = 1 - controlMode; saveGame(); markPend(0); snd(0); }
        else if (settingsSel == 1 && (nl || nr || in.crossNew)) { if (ARABIC_OK) { lang = 1 - lang; saveGame(); snd(1); setState(S_SETTINGS); } else snd(3); }
        else if (in.crossNew || in.circleNew) { snd(2); setState(S_MENU); }
    } else if (state == S_CREDITS && (in.crossNew || in.circleNew)) { snd(2); setState(S_MENU); }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    scene = new Scene2D(FW, FH, 4);
    if (!scene->Init(0xC000000, 2) || !initPad()) for (;;);
    loadGame(); initAudio(); buildLevel(); roomIdx = 0; respawn(); setState(S_MENU); snd(1);
    for (;;) {
        int b = frameID & 1; readInput();
        if (state == S_PLAY) {
            if (in.optionsNew) { saveGame(); setState(S_LEVELS); }
            else if (winTimer > 0) {
                if (--winTimer == 0) {
                    if (curLvl + 2 > unlockedLevels && unlockedLevels < NUM_LEVELS) unlockedLevels = curLvl + 2;
                    saveGame(); if (curLvl < 11) launchLevel(curLvl + 1); else startStory(1);
                }
            } else update();
        } else if (state == S_STORY) {
            int tot = storyTotal(); storyAnimTick++; if (typed < tot) typed++;
            if (in.crossNew) {
                if (typed < tot) typed = tot;
                else { snd(0); storyPage++; typed = storyAnimTick = 0; if (storyPage >= STORY_LAST[storyId]) endStory(); }
            }
            if (state == S_STORY && in.optionsNew) endStory();
        } else updateUI();

        if (state == S_PLAY) renderGame(b); else if (state == S_STORY) renderStory(b); else drawUI(b);
        scene->SubmitFlip(frameID); updateGameplayAudio(); scene->FrameWait(frameID); scene->FrameBufferSwap();
        frameID++; tick++;
    }
    return 0;
}
