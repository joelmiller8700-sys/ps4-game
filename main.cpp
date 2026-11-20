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
#define MAX_SPLATS 280
#define MAX_PARTS 120
#define MAX_PLANTS 640
#define MAX_AMB 18
#define MAX_BIRDS 5
#define MAX_SHOTS 14
#define MAX_ENEMIES 56
#define MAX_CHECKS 6
#define MAX_DIRTY 440
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
static bool onGround = false;
static unsigned int rng = 12345;
static int checkC[MAX_CHECKS], checkR[MAX_CHECKS], checkCount = 0;
static bool checkAct[MAX_CHECKS];

enum { E_WALKER = 0, E_FLYER = 1, E_JUMPER = 2, E_TURRET = 3 };
struct Enemy { float x, y, vx, vy, minX, maxX, baseY; int kind, hp, hurt, timer, dir; bool active; };
static Enemy enemies[MAX_ENEMIES];
static int enemyCount = 0;

static bool bossActive = false;
static float bossX, bossY, bossVX, bossVY;
static int bossW = 104, bossH = 104, bossHP = 5, bossMaxHP = 5, bossHurt = 0, bossTimer = 0, bossSwCd = 0, bossBeam = 0;
struct Shot { float x, y, vx, vy; bool active; };
static Shot shots[MAX_SHOTS];

struct Splat { short room, x, y, w, h; int born; unsigned char stage, dStage[2]; bool drawn[2]; };
static Splat splats[MAX_SPLATS];
static int splatCount = 0;

struct Part { float x, y, vx, vy; short life, maxLife; unsigned char kind, size; };
static Part parts[MAX_PARTS];

struct Plant { short room, x, y; float angle, vel; unsigned char h, style, seed; signed char dBend[2]; bool drawn[2]; };
static Plant plants[MAX_PLANTS];
static int plantCount = 0;

struct Amb { float x, y, vx, vy; unsigned char sz, seed; };
static Amb amb[MAX_AMB];

struct Rect { int x, y, w, h; };
static Rect dirty[2][MAX_DIRTY];
static int dirtyCnt[2] = {0, 0}, bufRoom[2] = {0, 0}, bufStar[2] = {0, 0}, hudSq[2] = {0, 0}, hudProg[2] = {0, 0};
static bool needFull[2] = {true, true}, needWin[2] = {false, false}, hudNeed[2] = {false, false};
static char bufGrid[2][ROWS][RCOLS];
static bool bufHi[2][ROWS][RCOLS];
static int clipX0 = 0, clipY0 = 0, clipX1 = FW, clipY1 = FH;

static const int CS[16] = {256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98,0,98,181,237};
static const int SN[16] = {0,98,181,237,256,237,181,98,0,-98,-181,-237,-256,-237,-181,-98};

// ---------- UI & Story State ----------
enum { S_MENU = 0, S_LEVELS = 1, S_SETTINGS = 2, S_CREDITS = 3, S_PLAY = 4, S_STORY = 5 };
static int state = S_MENU, menuSel = 0, levelSel = 0, settingsSel = 0, controlMode = 1, lang = 0, unlockedLevels = 1;
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
    X(T_GREAT,"GREAT JOB",AR(GREAT))

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
    {10,3,{"STAGE 1: LIVING FOREST - STOMP ENEMIES","TAP X FOR SHORT JUMP, HOLD X FOR HIGH JUMP.","JUMP ON TOP OF ENEMIES TO DEFEAT THEM!"},{AR(T1L0),AR(T1L1),AR(T1L2)}},
    {11,3,{"STAGE 2: COUNTRYSIDE - SPRINT & STOMP","HOLD SQUARE TO SPRINT ACROSS WIDE BRIDGES.","STOMP ENEMIES FROM ABOVE TO CLEAR THE PATH!"},{AR(T2L0),AR(T2L1),AR(T2L2)}},
    {12,3,{"STAGE 3: GOLDEN DESERT - TWO-WALL JUMP","ENTER BETWEEN TWO WALLS AND SLIDE SLOWLY.","PRESS X TO CLIMB AND STOMP DESERT FOES!"},{AR(T3L0),AR(T3L1),AR(T3L2)}},
    {13,3,{"STAGE 4: SNOWY PEAKS - LEDGE TIMING","WATCH SPINNING SAWS AND TIME YOUR JUMPS.","STOMP ENEMIES AND CROSS FROZEN LEDGES!"},{AR(T4L0),AR(T4L1),AR(T4L2)}},
    {14,3,{"STAGE 5: BOSS FIGHT - MAGMA CRUSHER!","STOMP ON THE BOSS HEAD OR HIT THE TOP SWITCH.","DODGE FIREBALLS AND DEFEAT THE BOSS!"},{AR(T5L0),AR(T5L1),AR(T5L2)}},
    {15,3,{"STAGE 10: BOSS FIGHT - SAW OVERLORD!","BOUNCE ON THE BOSS HEAD USING SIDE WALLS.","DODGE DOUBLE SHOTS AND SAVE THE FOREST!"},{AR(T10L0),AR(T10L1),AR(T10L2)}}
};
static const int STORY_FIRST[8] = {0,3,5,6,7,8,9,10}, STORY_LAST[8] = {3,5,6,7,8,9,10,11};

struct Input { bool up, down, left, right, dleft, dright, cross, circle, options, square, crossNew, circleNew, optionsNew; float ana; };
static Input in;
static bool prevCross = false, prevCircle = false, prevOptions = false;
static int holdCnt[4] = {0, 0, 0, 0};

static inline int iabs(int a) { return a < 0 ? -a : a; }
static inline int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int rnd(int n) { rng = rng * 1103515245u + 12345u; return (int)((rng >> 16) % (unsigned int)n); }
static inline Color mk(int r, int g, int b) { Color c = {(unsigned char)r, (unsigned char)g, (unsigned char)b}; return c; }
static inline bool useAr() { return lang == 1 && ARABIC_OK; }
static inline bool isBossStage(int lvl) { return ((lvl + 1) % 5) == 0; }

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
    in.cross = (b & ORBIS_PAD_BUTTON_CROSS) != 0; in.circle = (b & ORBIS_PAD_BUTTON_CIRCLE) != 0;
    in.options = (b & ORBIS_PAD_BUTTON_OPTIONS) != 0; in.square = (b & ORBIS_PAD_BUTTON_SQUARE) != 0;
    in.crossNew = in.cross && !prevCross; in.circleNew = in.circle && !prevCircle; in.optionsNew = in.options && !prevOptions;
    prevCross = in.cross; prevCircle = in.circle; prevOptions = in.options;
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
    static const int LENS[9] = {1400, 4000, 2600, 3500, 2800, 3400, 6500, 7200, 13000};
    curSfx = k; sfxPos = 0; sfxPhase = 0; sfxLen = LENS[iclamp(k, 0, 8)];
}

static void updateGameplayAudio() {
#ifndef NO_AUDIO
    if (audioHandle < 0 || state != S_PLAY) return;
    static short outBuf[512];
    static const int HB[4] = {110, 130, 146, 130}, HH[4] = {165, 195, 220, 195};
    static const unsigned char PAT[16] = {1,0,3,2,1,0,2,0,1,1,3,2,0,2,3,2};
    bool playBgm = (winTimer == 0);
    int stepS = bossActive ? 5200 : 6400;
    for (int blk = 0; blk < 2; blk++) {
        for (int i = 0; i < 256; i++) {
            int mix = 0;
            if (playBgm) {
                int step = (bgmSample / stepS) & 15, inS = bgmSample % stepS, bar = (bgmSample / (stepS * 16)) & 3;
                bgmSample++;
                unsigned char hit = PAT[step];
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
                int f1 = HB[bar] + (bossActive ? 22 : 0), f2 = HH[bar] + (bossActive ? 33 : 0);
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
                    else { f = sfxPos < 4200 ? 440 : (sfxPos < 8400 ? 550 : 660); vol = 16000; }
                    if ((sfxPhase += f) >= 48000) sfxPhase -= 48000;
                    int tri = sfxPhase < 24000 ? sfxPhase : 48000 - sfxPhase;
                    int s = ((tri - 12000) * vol) / 12000;
                    if (curSfx == 6 || curSfx == 7) {
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
static void intToStr(int v, char *o) { if (v >= 10) { o[0] = '0' + v / 10; o[1] = '0' + v % 10; o[2] = 0; } else { o[0] = '0' + v; o[1] = 0; } }

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

// ---------- Level, Enemy, Plant & Ambient Builders ----------
static int baseCol = 0;
static void put(int r, int c, char ch) { int g = baseCol + c; if (r >= 0 && r < ROWS && g >= 0 && g < COLS) level[r][g] = ch; }
static void plat(int r, int a, int b) { for (int c = a; c <= b; c++) put(r, c, '#'); }
static void wall(int c, int r0, int r1) { for (int r = r0; r <= r1; r++) put(r, c, '#'); }
static void spk(int r, int a, int b) { for (int c = a; c <= b; c++) put(r, c, '^'); }
static void sawAt(int r, int c) { put(r, c, 'O'); }
static void addCP(int c, int r) { if (checkCount < MAX_CHECKS) { checkC[checkCount] = baseCol + c; checkR[checkCount] = r; checkAct[checkCount++] = false; } }
static void addE(int k, int c, int r, int a, int b) {
    if (enemyCount >= MAX_ENEMIES) return;
    Enemy &e = enemies[enemyCount++];
    e.kind = k; e.x = (float)((baseCol + c) * TILE + 8); e.y = e.baseY = (float)(r * TILE - 44);
    e.minX = (float)((baseCol + a) * TILE + 4); e.maxX = (float)((baseCol + b) * TILE + 12);
    e.dir = 1; e.vx = k == E_WALKER ? 2.7f : (k == E_FLYER ? 2.9f : 2.2f); e.vy = 0;
    e.hp = 1; e.hurt = 0; e.timer = (c * 17) & 63; e.active = true;
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
    static const unsigned char ASZ[6] = {6, 5, 4, 5, 5, 4};
    for (int i = 0; i < MAX_AMB; i++) {
        Amb &a = amb[i]; a.x = (float)((i * 173 + 47) % FW); a.y = (float)(110 + ((i * 113 + 31) % (FH - 260)));
        a.seed = (unsigned char)i; a.sz = ASZ[curBiome];
        a.vx = AVX[curBiome] + (i % 3) * (curBiome == 2 ? 0.6f : 0.3f);
        a.vy = AVY[curBiome] + (i % 2) * (curBiome == 4 ? -0.4f : 0.3f);
    }
}

static void buildLevel() {
    for (int r = 0; r < ROWS; r++) { memset(level[r], '.', COLS); level[r][COLS] = 0; level[r][0] = level[r][COLS - 1] = '#'; }
    memset(level[0], '#', COLS); memset(level[ROWS - 1], '#', COLS);
    enemyCount = checkCount = 0; bossActive = false;
    cpX = (float)(TILE + 16); cpY = (float)((ROWS - 1) * TILE - PH);
    baseCol = 0; levelCols = COLS; levelPW = WW;

    if (isBossStage(curLvl)) {
        int tier = (curLvl + 1) / 5; curBiome = (tier & 1) ? 4 : 5;
        levelCols = RCOLS; levelPW = FW;
        for (int r = 0; r < ROWS; r++) level[r][RCOLS - 1] = '#';
        wall(3, 7, 14); wall(28, 7, 14); plat(12, 5, 9); plat(7, 13, 18); plat(12, 22, 26);
        bossActive = true; bossX = (float)(21 * TILE); bossY = (float)((ROWS - 1) * TILE - bossH);
        bossVX = tier >= 2 ? -5.0f : -3.8f; bossVY = 0; bossMaxHP = bossHP = tier >= 2 ? 10 : 6;
        bossHurt = bossTimer = bossSwCd = bossBeam = 0;
        for (int i = 0; i < MAX_SHOTS; i++) shots[i].active = false;
        initWorldDetails(); return;
    }

    static const int BIOMES[12] = {0, 1, 2, 3, 4, 5, 3, 1, 0, 5, 2, 4};
    int idx = curLvl % 12; curBiome = BIOMES[idx];

    if (idx == 0) {
        baseCol = 0 * RCOLS; plat(15,7,13); addE(0,10,17,6,15); addE(0,10,15,7,13); plat(14,16,22); addE(2,19,14,16,22); plat(15,25,30); addE(0,27,17,24,30);
        baseCol = 1 * RCOLS; spk(16,11,13); plat(14,15,21); addE(0,18,17,14,23); addE(1,18,10,15,22); plat(13,24,30); addE(0,27,13,24,30); addCP(26,13);
        baseCol = 2 * RCOLS; wall(9,8,14); wall(13,8,16); plat(8,13,20); addE(0,16,8,13,20); addE(0,24,17,21,29); plat(13,23,30); addE(2,26,13,23,30);
        baseCol = 3 * RCOLS; plat(14,4,10); sawAt(15,13); plat(14,16,22); addE(0,19,17,15,24); addE(1,19,10,16,23); plat(12,25,30); addE(3,29,12,25,30); addCP(26,12);
        baseCol = 4 * RCOLS; plat(15,4,10); plat(13,13,19); addE(0,16,13,13,19); addE(0,15,17,11,21); plat(11,22,30); addE(2,25,11,22,30);
        baseCol = 5 * RCOLS; plat(14,4,10); plat(12,13,19); addE(1,16,8,13,20); addE(0,16,17,12,21); plat(10,22,30); put(9,27,'G');
    } else if (idx == 1) {
        baseCol = 0 * RCOLS; plat(15,7,13); addE(0,10,15,7,13); addE(0,18,17,14,22); spk(16,15,17); plat(14,20,26); addE(2,23,14,20,26); plat(15,28,30);
        baseCol = 1 * RCOLS; plat(14,4,10); plat(12,13,19); addE(1,16,8,13,20); plat(14,22,30); addE(0,25,14,22,30); addCP(24,14);
        baseCol = 2 * RCOLS; wall(9,8,14); wall(13,8,16); plat(8,13,20); addE(3,19,8,13,20); addE(0,24,17,21,29); plat(13,23,30); addE(2,26,13,23,30);
        baseCol = 3 * RCOLS; plat(14,4,10); sawAt(15,13); plat(14,16,22); addE(0,19,14,16,22); plat(12,25,30); addCP(26,12);
        baseCol = 4 * RCOLS; plat(15,4,10); plat(12,13,19); addE(1,16,8,13,20); addE(0,16,17,12,21); plat(10,22,30); addE(2,25,10,22,30);
        baseCol = 5 * RCOLS; plat(14,4,10); plat(12,13,19); addE(0,16,12,13,19); plat(10,22,30); put(9,27,'G');
    } else if (idx == 2) {
        baseCol = 0 * RCOLS; wall(9,8,14); wall(13,8,16); plat(8,13,20); addE(0,16,8,13,20); addE(0,18,17,15,22); plat(13,23,30); addE(2,26,13,23,30);
        baseCol = 1 * RCOLS; plat(14,4,10); plat(11,13,19); addE(3,18,11,13,19); plat(14,22,30); addE(1,25,10,22,30); addCP(24,14);
        baseCol = 2 * RCOLS; wall(8,8,14); wall(12,8,16); plat(8,12,18); plat(13,21,30); addE(0,25,13,21,30);
        baseCol = 3 * RCOLS; plat(14,4,10); sawAt(15,13); plat(13,16,22); addE(2,19,13,16,22); addE(0,18,17,14,24); plat(11,25,30); addE(3,29,11,25,30); addCP(26,11);
        baseCol = 4 * RCOLS; plat(15,4,10); wall(12,7,13); wall(16,7,15); plat(7,16,22); addE(1,19,4,16,23); plat(12,25,30);
        baseCol = 5 * RCOLS; plat(14,4,10); plat(12,13,19); addE(0,16,12,13,19); plat(10,22,30); put(9,27,'G');
    } else if (idx == 3) {
        baseCol = 0 * RCOLS; plat(14,7,13); sawAt(15,15); plat(14,18,24); addE(0,21,14,18,24); addE(0,10,17,6,14); plat(12,26,30);
        baseCol = 1 * RCOLS; plat(14,4,10); wall(12,7,13); wall(16,7,15); plat(7,16,22); addE(2,19,7,16,22); plat(12,25,30); addCP(26,12);
        baseCol = 2 * RCOLS; plat(14,4,10); sawAt(15,12); plat(13,15,21); addE(3,20,13,15,21); plat(14,24,30);
        baseCol = 3 * RCOLS; plat(14,4,10); plat(12,13,19); addE(1,16,8,13,20); addE(0,16,17,12,21); plat(11,22,30); addCP(23,11);
        baseCol = 4 * RCOLS; plat(14,4,10); wall(12,7,13); wall(16,7,15); plat(7,16,22); plat(11,25,30); addE(0,27,11,25,30);
        baseCol = 5 * RCOLS; plat(14,4,10); plat(12,13,19); addE(2,16,12,13,19); plat(10,22,30); put(9,27,'G');
    } else {
        for (int rm = 0; rm < ROOMS; rm++) {
            baseCol = rm * RCOLS; plat(14,5,11); plat(12,14,20); plat(14,23,29);
            if (rm == 1 || rm == 3) { wall(9,8,13); wall(13,8,14); addCP(25,14); }
            if (rm % 2 == 0) { addE(0,8,14,5,11); addE(0,14,17,11,21); addE(2,17,12,14,20); }
            else { addE(0,9,17,5,14); addE(1,17,8,14,21); addE(3,28,14,23,29); }
        }
        baseCol = 5 * RCOLS; plat(10,22,30); put(9,27,'G');
    }
    baseCol = 0;
    initWorldDetails();
}

// ---------- Collision & Physics ----------
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
static void spawnDust(float x, float y, int n, float sp) {
    for (int i = 0; i < n; i++)
        spawnPart(x + (rnd(20) - 10), y - rnd(4), ((rnd(100) - 50) / 50.0f) * sp, -(rnd(100) / 100.0f) * 1.6f, 16 + rnd(12), 7 + rnd(5), 0);
}

static void respawn() {
    px = (float)(roomIdx * FW + TILE + 8); py = (float)((ROWS - 1) * TILE - PH);
    vx = vy = 0; squashX = squashY = 1.0f; onGround = false; wallLock = coyote = 0;
    if (bossActive && bossHP > 0) {
        bossX = (float)(21 * TILE); bossY = (float)((ROWS - 1) * TILE - bossH);
        for (int i = 0; i < MAX_SHOTS; i++) shots[i].active = false;
    }
}

static void die() {
    deaths++; hudNeed[0] = hudNeed[1] = true; snd(6);
    float cx = px + PW * 0.5f, cy = py + PH * 0.5f;
    for (int i = 0; i < 26; i++) spawnPart(cx + rnd(20) - 10, cy + rnd(20) - 10, (rnd(180) - 90) / 10.0f, (rnd(140) - 120) / 10.0f, 90, 5 + rnd(5), 1);
    for (int i = 0; i < 4; i++) addSplat((int)cx + rnd(40) - 20, (int)cy + rnd(40) - 20, 8 + rnd(10), 8 + rnd(10));
    vx = vy = 0; deadTimer = 28;
}

static void damageBoss(int dmg) {
    if (bossHurt > 0 || bossHP <= 0) return;
    bossHP = iclamp(bossHP - dmg, 0, bossMaxHP); bossHurt = 24; hudNeed[0] = hudNeed[1] = true; snd(7);
    for (int i = 0; i < 22; i++) spawnPart(bossX + bossW * 0.5f, bossY + bossH * 0.5f, (rnd(200) - 100) / 10.0f, -(40 + rnd(100)) / 10.0f, 90, 6 + rnd(5), 1);
    if (bossHP <= 0) { for (int i = 0; i < MAX_SHOTS; i++) shots[i].active = false; level[16][15] = 'G'; needFull[0] = needFull[1] = true; }
}

static void spawnShot(float sx, float sy, float svx, float svy) {
    for (int i = 0; i < MAX_SHOTS; i++) if (!shots[i].active) { shots[i] = {sx, sy, svx, svy, true}; return; }
}

static void update() {
    for (int i = 0; i < MAX_PARTS; i++) {
        Part &p = parts[i]; if (p.life <= 0) continue; p.life--;
        if (p.kind == 0) { p.x += p.vx; p.y += p.vy; p.vx *= 0.9f; p.vy *= 0.92f; }
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
    bool burst = false;
    if (deadTimer > 0) { if (--deadTimer == 0) respawn(); }
    else {
        bool wasG = onGround;
        float k = onGround ? 0.0f : (iabs((int)vy) / 18.0f > 1.0f ? 1.0f : iabs((int)vy) / 18.0f);
        squashX += (1.0f - 0.22f * k - squashX) * 0.3f; squashY += (1.0f + 0.30f * k - squashY) * 0.3f;
        int dir = controlMode == 1 ? (in.ana != 0.0f ? (in.ana > 0 ? 1 : -1) : 0) : ((in.dright ? 1 : 0) - (in.dleft ? 1 : 0));
        float sc = controlMode == 1 && in.ana != 0.0f ? (fabsf(in.ana) < 0.5f ? 0.5f : fabsf(in.ana)) : 1.0f;
        bool wL = rectSolid((int)px - 1, (int)py, 1, PH), wR = rectSolid((int)px + PW, (int)py, 1, PH);
        float maxS = (in.square ? 8.2f : 5.8f) * sc;
        if (dir) facing = dir;
        if (wallLock > 0) wallLock--;
        else if (dir) { vx += dir * (onGround ? 1.25f : 0.72f); if (vx > maxS) vx = maxS; if (vx < -maxS) vx = -maxS; }
        else { float fr = onGround ? 1.0f : 0.3f; vx = vx > fr ? vx - fr : (vx < -fr ? vx + fr : 0); }
        vy = vy + 0.9f > 18.0f ? 18.0f : vy + 0.9f;
        bool slide = !onGround && vy > 0 && ((wL && dir < 0) || (wR && dir > 0));
        if (slide && vy > 3.0f) vy = 3.0f;
        if (slide && (tick & 3) == 0) spawnDust(wL ? px : px + PW, py + PH - 6.0f, 1, 1.0f);
        if (onGround) coyote = 6; else if (coyote > 0) coyote--;
        if (in.crossNew) {
            if (coyote > 0) { vy = -17.2f; coyote = 0; squashX = 0.72f; squashY = 1.30f; spawnDust(px + PW * 0.5f, py + PH, 6, 3.0f); burst = true; snd(4); }
            else if (wL || wR) { vy = -16.2f; vx = wL ? 8.0f : -8.0f; wallLock = 9; facing = wL ? 1 : -1; squashX = 0.72f; squashY = 1.30f; spawnDust(wL ? px : px + PW, py + PH * 0.5f, 4, 2.0f); burst = true; snd(5); }
        }
        if (!in.cross && vy < -6.0f) vy = -6.0f;
        px += vx;
        if (rectSolid((int)px, (int)py, PW, PH)) { px = (float)(vx > 0 ? ((((int)px + PW - 1) / TILE) * TILE - PW) : ((((int)px) / TILE + 1) * TILE)); vx = 0; }
        py += vy; onGround = false;
        if (rectSolid((int)px, (int)py, PW, PH)) {
            if (vy > 0) {
                py = (float)((((int)py + PH - 1) / TILE) * TILE - PH); onGround = true;
                if (!wasG && vy > 5.0f) { float lk = vy / 18.0f > 1.0f ? 1.0f : vy / 18.0f; squashY = 1.0f - 0.35f * lk; squashX = 1.0f + 0.40f * lk; spawnDust(px + PW * 0.5f, py + PH, 4 + (int)(lk * 6), 4.0f); burst = true; }
            } else if (vy < 0) py = (float)((((int)py) / TILE + 1) * TILE);
            vy = 0;
        }
        for (int i = 0; i < checkCount; i++) if (!checkAct[i] && (int)px + PW >= checkC[i] * TILE) {
            checkAct[i] = true; cpX = (float)(checkC[i] * TILE); cpY = (float)(checkR[i] * TILE - PH); snd(1); spawnDust(cpX + 20, cpY + 4, 10, 3.5f);
        }
    }
    // Plants & ambient
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

    // Smart Enemies (Stomp from above ONLY!)
    int rmX0 = roomIdx * FW - 240, rmX1 = roomIdx * FW + FW + 240;
    for (int i = 0; i < enemyCount; i++) {
        Enemy &e = enemies[i]; if (!e.active || (int)e.x < rmX0 || (int)e.x > rmX1) continue;
        if (e.hurt > 0) e.hurt--; e.timer++;
        float dx = px - e.x, adx = fabsf(dx), ady = fabsf(py - e.y);
        if (e.kind == E_WALKER) {
            if (adx < 340 && ady < 140) e.dir = dx >= 0 ? 1 : -1;
            float nx = e.x + ((adx < 340 && ady < 140) ? e.vx * 1.25f : e.vx) * e.dir;
            if (nx <= e.minX) { nx = e.minX; e.dir = 1; } else if (nx >= e.maxX) { nx = e.maxX; e.dir = -1; }
            int prX = e.dir > 0 ? (int)nx + 46 : (int)nx - 4;
            if (solidAt(prX, (int)e.y + 20)) { if (e.vy == 0) e.vy = -11.5f; else e.dir = -e.dir; }
            if (!rectSolid((int)nx, (int)e.y, 44, 44)) e.x = nx; else e.dir = -e.dir;
            e.vy += 0.65f; e.y += e.vy;
            if (rectSolid((int)e.x, (int)e.y, 44, 44)) { if (e.vy > 0) e.y = (float)((((int)e.y + 43) / TILE) * TILE - 44); e.vy = 0; }
        } else if (e.kind == E_FLYER) {
            if (adx < 360) e.dir = dx >= 0 ? 1 : -1;
            e.x += e.vx * e.dir; if (e.x <= e.minX) { e.x = e.minX; e.dir = 1; } else if (e.x >= e.maxX) { e.x = e.maxX; e.dir = -1; }
            e.y = e.baseY + (SN[(e.timer / 4) & 15] * 28) / 256;
        } else if (e.kind == E_JUMPER) {
            if (adx < 320) e.dir = dx >= 0 ? 1 : -1;
            e.x += e.vx * e.dir; if (e.x <= e.minX) { e.x = e.minX; e.dir = 1; } else if (e.x >= e.maxX) { e.x = e.maxX; e.dir = -1; }
            if (e.timer % 50 == 0 && e.y >= e.baseY - 2) e.vy = -12.5f;
            e.vy += 0.65f; e.y += e.vy; if (e.y >= e.baseY) { e.y = e.baseY; e.vy = 0; }
        } else {
            e.dir = dx < 0 ? -1 : 1;
            if (e.timer % 90 == 0 && adx < 680) spawnShot(e.x + 22, e.y + 18, e.dir * 6.8f, 0);
        }
        if (rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, (int)e.x + 4, (int)e.y + 4, 36, 36)) {
            if (vy > 0 && py + PH < e.y + 28) {
                py = e.y - PH; vy = -15.5f; squashX = 0.72f; squashY = 1.28f;
                e.active = false; snd(7);
                for (int k = 0; k < 24; k++) spawnPart(e.x + 22, e.y + 22, (rnd(180) - 90) / 10.0f, -(45 + rnd(95)) / 10.0f, 110, 6 + rnd(4), 1);
            } else { die(); return; }
        }
    }
    for (int i = 0; i < MAX_SHOTS; i++) {
        Shot &s = shots[i]; if (!s.active) continue; s.x += s.vx; s.y += s.vy;
        if (s.x < TILE || s.x > levelPW - TILE || s.y < TILE || s.y > FH - TILE || solidAt((int)s.x, (int)s.y)) { s.active = false; continue; }
        if (rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, (int)s.x - 14, (int)s.y - 14, 28, 28)) { s.active = false; die(); return; }
    }
    if (bossActive && bossHP > 0) {
        if (bossHurt > 0) bossHurt--; if (bossSwCd > 0) bossSwCd--; if (bossBeam > 0) bossBeam--; bossTimer++;
        int tier = (curLvl + 1) / 5; float floorY = (float)((ROWS - 1) * TILE - bossH);
        bossX += bossVX * (tier >= 2 ? 1.3f : 1.0f);
        if (bossX < 5 * TILE) { bossX = 5 * TILE; bossVX = 4.2f; } else if (bossX > 24 * TILE) { bossX = 24 * TILE; bossVX = -4.2f; }
        if (bossTimer % 105 == 50 && bossY >= floorY - 2) bossVY = -14.5f;
        bossVY += 0.65f; bossY += bossVY; if (bossY >= floorY) { bossY = floorY; bossVY = 0; }
        if (bossTimer % (tier >= 2 ? 60 : 80) == 0) {
            float d = px < bossX ? -1.0f : 1.0f;
            spawnShot(bossX + bossW * 0.5f, bossY + bossH - 28, d * 7.0f, 0);
            if (tier >= 2) spawnShot(bossX + bossW * 0.5f, bossY + 24, d * 6.0f, -2.5f);
        }
        if (bossSwCd == 0 && rectOverlap((int)px, (int)py, PW, PH, 15 * TILE, 6 * TILE + 20, 120, 40)) { bossSwCd = 130; bossBeam = 25; vy = -12.0f; damageBoss(2); return; }
        if (rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, (int)bossX + 8, (int)bossY + 8, bossW - 16, bossH - 16)) {
            if (vy > 0 && py + PH < bossY + 46) { py = bossY - PH; vy = -16.5f; squashX = 0.7f; squashY = 1.32f; damageBoss(2); }
            else if (bossHurt == 0) { die(); return; }
        }
    }
    // Hazards & Goal
    int c0 = iclamp((int)px / TILE - 2, 0, COLS - 1), c1 = iclamp(((int)px + PW) / TILE + 2, 0, COLS - 1);
    for (int r = 0; r < ROWS; r++) for (int c = c0; c <= c1; c++) {
        char ch = level[r][c];
        if ((ch == '^' && rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, c * TILE + 14, r * TILE + 26, 32, 34)) ||
            (ch == 'O' && circleHit(c * TILE + 30, r * TILE + 30, 24, (int)px + 4, (int)py + 4, PW - 8, PH - 8))) { die(); return; }
        if (ch == 'G' && rectOverlap((int)px, (int)py, PW, PH, c * TILE + 8, r * TILE + 16, PW, PH)) {
            winTimer = 150; needWin[0] = needWin[1] = true; snd(8); return;
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

static void drawPlayerFace(int dx, int dy, int dw, int dh, int d) {
    int ew = dw * 10 / 44 < 4 ? 4 : dw * 10 / 44, eh = dh * 14 / 44 < 6 ? 6 : dh * 14 / 44;
    int pw = dw * 5 / 44 < 2 ? 2 : dw * 5 / 44, ph = dh * 8 / 44 < 3 ? 3 : dh * 8 / 44;
    int e1 = dx + dw * 9 / 44, e2 = dx + dw * 25 / 44, ey = dy + dh * 12 / 44, po = d > 0 ? dw * 5 / 44 : 0, pyy = dy + dh * 16 / 44;
    fillRect(e1, ey, ew, eh, mk(255, 255, 255)); fillRect(e2, ey, ew, eh, mk(255, 255, 255));
    fillRect(e1 + po, pyy, pw, ph, mk(0, 0, 0)); fillRect(e2 + po, pyy, pw, ph, mk(0, 0, 0));
    fillRect(dx + dw * 15 / 44, dy + dh * 32 / 44, dw * 14 / 44, dh * 4 / 44 < 2 ? 2 : dh * 4 / 44, mk(120, 0, 12));
}

struct TilePal { Color mainC, litC, topC, topHi, botC, spHi, spMid, spShd; };
static const TilePal TPAL[6] = {
    {{124,76,44},{148,94,56},{76,208,68},{135,240,115},{74,44,24},{245,248,255},{195,200,215},{125,132,152}},
    {{122,74,44},{146,92,56},{236,184,56},{255,224,115},{76,44,24},{245,248,255},{195,200,215},{125,132,152}},
    {{184,124,66},{208,146,84},{248,208,98},{255,236,155},{122,76,38},{255,232,185},{224,184,128},{162,124,78}},
    {{72,98,134},{94,124,165},{232,248,255},{255,255,255},{42,58,86},{240,252,255},{175,224,250},{105,165,205}},
    {{62,36,44},{86,50,60},{245,98,36},{255,178,85},{34,18,24},{255,195,115},{240,112,52},{165,52,26}},
    {{64,52,92},{84,70,118},{165,122,205},{205,168,238},{36,28,56},{245,248,255},{195,200,215},{125,132,152}}
};

static void drawTileCell(int r, int cx, char ch, bool hi) {
    int x = cx * TILE, y = r * TILE; const TilePal &tp = TPAL[curBiome];
    if (ch == '#') {
        fillRect(x, y, TILE, TILE, tp.mainC);
        fillRect(x + 6, y + 14, TILE - 12, TILE - 22, tp.litC);
        fillRect(x + 10, y + 18, 14, 8, tp.mainC); fillRect(x + 32, y + 32, 16, 10, tp.botC);
        if (hi) {
            fillRect(x, y, TILE, 9, tp.topC); fillRect(x, y, TILE, 3, tp.topHi);
            fillRect(x + 4, y + 9, 10, 4, tp.topC); fillRect(x + 22, y + 9, 12, 5, tp.topC); fillRect(x + 42, y + 9, 12, 4, tp.topC);
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
    Color tip = bm == 0 ? (st == 0 ? mk(255,105,145) : (st == 1 ? mk(255,235,80) : mk(245,245,255))) : PT[bm];
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

static void paintBackground() {
    if (curBiome == 0) {
        fillRect(0, 0, FW, 260, mk(52,162,238)); fillRect(0, 260, FW, 240, mk(125,215,250));
        fillRect(0, 500, FW, 110, mk(58,155,92)); fillRect(0, 610, FW, 130, mk(38,118,68)); fillRect(0, 740, FW, 340, mk(28,88,52));
        fillRect(1540, 72, 136, 136, mk(255,242,155)); fillRect(1556, 88, 104, 104, mk(255,224,72));
        for (int i = 0; i < 4; i++) { int cx = 150 + i * 380, cy = 115 + (i & 1) * 48; fillRect(cx, cy, 165, 36, mk(236,248,255)); fillRect(cx + 28, cy - 20, 105, 24, mk(248,253,255)); }
        drawCastle(1260, 500, mk(218,212,224), mk(195,55,65), mk(255,215,65));
        fillRect(360, 430, 96, 250, mk(72,178,245)); fillRect(376, 430, 64, 250, mk(145,224,255)); fillRect(394, 430, 28, 250, mk(225,248,255));
        fillRect(260, 655, 420, 65, mk(62,165,235)); fillRect(310, 670, 310, 28, mk(135,218,252));
        for (int i = 0; i < 6; i++) { int x = i * 320 + 40; fillRect(x + 42, 580, 26, 95, mk(92,56,32)); fillRect(x, 470, 110, 115, mk(28,88,52)); fillRect(x + 20, 430, 70, 50, mk(45,132,74)); }
    } else if (curBiome == 1) {
        Color fC = mk(142,86,52), nC = mk(94,56,36);
        fillRect(0, 0, FW, 300, mk(198,96,72)); fillRect(0, 300, FW, 270, mk(246,162,86)); fillRect(0, 570, FW, 130, fC); fillRect(0, 700, FW, 380, nC);
        fillRect(1420, 195, 146, 146, mk(255,200,85)); fillRect(1434, 209, 118, 118, mk(255,232,125));
        drawCastle(1180, 570, mk(215,175,135), mk(175,58,48), mk(255,225,90));
        for (int i = 0; i < 4; i++) { fillRect(i * 500 + 40, 485, 260, 85, fC); fillRect(i * 500 + 90, 458, 160, 32, fC); }
        fillRect(360, 570, 140, 130, nC); fillRect(385, 540, 90, 35, nC); fillRect(1480, 560, 76, 140, nC); fillRect(1492, 538, 52, 26, nC);
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

static inline void addDirty(int b, int x, int y, int w, int h) { if (dirtyCnt[b] < MAX_DIRTY) dirty[b][dirtyCnt[b]++] = {x, y, w, h}; }
static int progressNow() { return bossActive ? (bossMaxHP > 0 ? bossHP * 520 / bossMaxHP : 0) : iclamp((int)(px / levelPW * 520.0f), 0, 520); }

static void drawHUDFull(int b) {
    int hn = deaths < 30 ? deaths : 30, pr = progressNow();
    if (useAr() && TXT_AR[T_DEATHS] >= 0) drawArCols(TXT_AR[T_DEATHS], 70, 22, 0, arW(TXT_AR[T_DEATHS]), mk(235, 225, 255));
    else drawText(70, 28, "DEATHS", 3, mk(235, 225, 255));
    for (int i = 0; i < hn; i++) fillRect(70 + i * 20, 60, 13, 13, mk(205, 25, 35));
    if (bossActive) drawLabel(T_BOSS_HP, 960, 10, 30, 3, mk(255, 215, 90));
    fillRect(696, 40, 528, 20, mk(30, 20, 50));
    fillRect(700, 43, 520, 14, bossActive ? mk(65, 25, 35) : mk(45, 35, 70));
    fillRect(700, 43, pr, 14, bossActive ? mk(245, 55, 40) : mk(205, 40, 100));
    hudSq[b] = hn; hudProg[b] = pr; hudNeed[b] = false;
}

static void drawEnemyChar(const Enemy &e, int sx, int sy) {
    bool fl = (e.hurt & 2) != 0;
    if (e.kind == E_WALKER) {
        fillRect(sx, sy, 44, 44, mk(82, 16, 102)); fillRect(sx + 4, sy + 4, 36, 36, fl ? mk(255,255,255) : mk(158, 42, 182));
        fillRect(sx + 6, sy + 6, 32, 10, mk(198, 86, 224)); fillRect(sx + 10, sy - 6, 8, 6, mk(235,238,248)); fillRect(sx + 26, sy - 6, 8, 6, mk(235,238,248));
        drawFace(sx, sy, e.dir, mk(255, 235, 90));
    } else if (e.kind == E_FLYER) {
        int wy = ((tick / 4) & 1) ? -10 : 8;
        fillRect(sx - 12, sy + 14 + wy, 14, 10, mk(175, 45, 25)); fillRect(sx + 42, sy + 14 + wy, 14, 10, mk(175, 45, 25));
        fillRect(sx + 4, sy + 4, 36, 36, fl ? mk(255,255,255) : mk(232, 92, 38)); fillRect(sx + 6, sy + 6, 32, 8, mk(255, 155, 85));
        drawFace(sx, sy, e.dir, mk(255, 245, 120));
    } else if (e.kind == E_JUMPER) {
        fillRect(sx, sy, 44, 44, mk(24, 112, 78)); fillRect(sx + 4, sy + 4, 36, 36, fl ? mk(255,255,255) : mk(48, 195, 142));
        fillRect(sx + 6, sy + 6, 32, 8, mk(115, 235, 185)); fillRect(sx + 12, sy + 42, 20, 6, mk(215, 224, 236));
        drawFace(sx, sy, e.dir, mk(255, 255, 255));
    } else {
        fillRect(sx, sy + 6, 44, 38, mk(48, 54, 68)); fillRect(sx + 4, sy + 10, 36, 30, fl ? mk(255,255,255) : mk(102, 114, 136));
        fillRect(sx + 6, sy + 12, 32, 7, mk(152, 166, 192));
        fillRect(e.dir < 0 ? sx - 14 : sx + 38, sy + 18, 18, 12, mk(235, 75, 45)); fillRect(sx + 14, sy + 16, 16, 12, mk(255, 215, 70));
    }
}

static void drawBossChar(int bx, int by, int bw, int bh, bool hurt) {
    Color dC = hurt ? mk(255,240,180) : mk(85,22,32), mC = hurt ? mk(255,255,255) : mk(175,35,45), cC = (tick & 4) ? mk(255,215,65) : mk(255,110,30);
    fillRect(bx, by, bw, bh, dC); fillRect(bx + 8, by + 8, bw - 16, bh - 16, mC); fillRect(bx + 12, by + 12, bw - 24, 22, mk(220, 68, 82));
    fillRect(bx + 20, by - 8, bw - 40, 10, cC);
    fillRect(bx + 18, by + 26, 24, 20, mk(255,240,90)); fillRect(bx + bw - 42, by + 26, 24, 20, mk(255,240,90));
    fillRect(bx + 24, by + 32, 12, 12, mk(20,0,0)); fillRect(bx + bw - 36, by + 32, 12, 12, mk(20,0,0));
    fillRect(bx + 14, by + 20, 32, 6, dC); fillRect(bx + bw - 46, by + 20, 32, 6, dC);
    fillRect(bx + 22, by + 68, bw - 44, 20, mk(35,10,18));
    for (int k = 0; k < 4; k++) fillRect(bx + 28 + k * 14, by + 70, 6, 16, cC);
}

static void renderGame(int b) {
    resetClip();
    if (winTimer > 0) {
        if (needWin[b]) {
            fillRect(0, 0, FW, FH, mk(20, 90, 60));
            drawLabel(T_COMPLETE, 960, 370, 110, 10, mk(255, 255, 255));
            drawLabel(T_GREAT, 960, 520, 80, 6, mk(255, 220, 180));
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
                if (bufGrid[b][r][c] != '.' && nc != '#') { setClip(c * TILE, r * TILE, TILE, TILE); paintBackground(); resetClip(); }
                bufGrid[b][r][c] = nc; bufHi[b][r][c] = nh;
                if (nc != '.') drawTileCell(r, c, nc, nh);
                if (r <= 1) hudNeed[b] = true;
            }
            bufRoom[b] = roomIdx;
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
        int hn = deaths < 30 ? deaths : 30, pr = progressNow();
        if (hudNeed[b]) drawHUDFull(b);
        else {
            for (int i = hudSq[b]; i < hn; i++) fillRect(70 + i * 20, 60, 13, 13, mk(205, 25, 35));
            hudSq[b] = hn;
            if (pr > hudProg[b]) fillRect(700 + hudProg[b], 43, pr - hudProg[b], 14, bossActive ? mk(245,55,40) : mk(205,40,100));
            else if (pr < hudProg[b]) fillRect(700 + pr, 43, hudProg[b] - pr, 14, bossActive ? mk(65,25,35) : mk(45,35,70));
            hudProg[b] = pr;
        }
    }
    resetClip();
    int roomX = roomIdx * FW;
    if (curBiome == 0) {
        int wf = (tick * 4) % 44;
        for (int k = 0; k < 5; k++) { int wy = 435 + wf + k * 48; if (wy < 660) { fillRect(398, wy, 20, 18, mk(245,252,255)); addDirty(b, 396, wy - 2, 24, 22); } }
    }
    if (curBiome <= 2) {
        Color bc = curBiome == 0 ? mk(26,75,52) : mk(95,42,30); int wg = ((tick / 8) & 1) ? -6 : 5;
        for (int i = 0; i < MAX_BIRDS; i++) {
            int bx = (i * 390 + tick * 2) % (FW - 80) + 40, by = 125 + (i % 3) * 48 + (SN[((tick / 6) + i * 3) & 15] >> 5);
            fillRect(bx, by, 10, 4, bc); fillRect(bx - 7, by + wg, 8, 4, bc); fillRect(bx + 9, by + wg, 8, 4, bc); addDirty(b, bx - 9, by - 8, 28, 18);
        }
    }
    static const Color AMBC[6] = {{140,235,110},{255,230,130},{245,215,145},{240,250,255},{255,145,45},{175,155,235}};
    for (int i = 0; i < MAX_AMB; i++) { fillRect((int)amb[i].x, (int)amb[i].y, amb[i].sz, amb[i].sz, AMBC[curBiome]); addDirty(b, (int)amb[i].x - 1, (int)amb[i].y - 1, amb[i].sz + 2, amb[i].sz + 2); }
    for (int i = 0; i < checkCount; i++) if (checkC[i] / RCOLS == roomIdx) {
        int fx = (checkC[i] % RCOLS) * TILE + 24, fy = checkR[i] * TILE - 64;
        fillRect(fx, fy, 6, 64, mk(220,220,235)); fillRect(fx + 6, fy + 4, 28, 20, checkAct[i] ? mk(65,235,110) : mk(235,190,55)); addDirty(b, fx - 2, fy - 2, 38, 66);
    }
    for (int r = 0; r < ROWS; r++) for (int c = roomIdx * RCOLS; c < roomIdx * RCOLS + RCOLS && c < COLS; c++)
        if (level[r][c] == 'O') { int cx = c * TILE + 30 - roomX, cy = r * TILE + 30; drawSaw(cx, cy); addDirty(b, cx - 27, cy - 27, 55, 55); }
    for (int i = 0; i < enemyCount; i++) if (enemies[i].active) {
        int sx = (int)enemies[i].x - roomX, sy = (int)enemies[i].y;
        if (sx >= -60 && sx <= FW + 60) { drawEnemyChar(enemies[i], sx, sy); addDirty(b, sx - 16, sy - 8, 76, 60); }
    }
    if (bossActive && bossHP > 0) {
        fillRect(15 * TILE + 20, 7 * TILE - 18, 80, 18, bossSwCd == 0 ? ((tick & 8) ? mk(80,255,120) : mk(40,200,85)) : mk(90,80,100));
        addDirty(b, 15 * TILE + 18, 7 * TILE - 20, 84, 22);
        int bx = (int)bossX, by = (int)bossY;
        if (bossBeam > 0) { fillRect(bx + bossW / 2 - 16, 7 * TILE, 32, by - 7 * TILE + 20, mk(120,255,220)); addDirty(b, bx + bossW / 2 - 20, 7 * TILE, 40, by - 7 * TILE + 24); }
        drawBossChar(bx, by, bossW, bossH, (bossHurt & 2) != 0); addDirty(b, bx - 6, by - 12, bossW + 12, bossH + 16);
    }
    for (int i = 0; i < MAX_SHOTS; i++) if (shots[i].active) {
        int sx = (int)shots[i].x - roomX, sy = (int)shots[i].y;
        if (sx >= -30 && sx <= FW + 30) { fillRect(sx - 14, sy - 14, 28, 28, mk(255,95,35)); fillRect(sx - 8, sy - 8, 16, 16, mk(255,235,90)); addDirty(b, sx - 16, sy - 16, 32, 32); }
    }
    for (int i = 0; i < MAX_PARTS; i++) {
        Part &p = parts[i]; if (p.life <= 0) continue;
        int sx = (int)p.x - roomX, sy = (int)p.y; if (sx < -20 || sx > FW + 20 || sy < -20 || sy > FH + 20) continue;
        int sz = p.kind == 0 ? 2 + (p.size - 2) * p.life / p.maxLife : p.size;
        Color col = p.kind == 0 ? (p.life * 2 > p.maxLife ? mk(205,195,230) : mk(150,140,185)) : mk(210,18,32);
        fillRect(sx, sy, sz, sz, col); addDirty(b, sx - 1, sy - 1, sz + 2, sz + 2);
    }
    if (deadTimer == 0) {
        int x = (int)px - roomX, y = (int)py, dw = iclamp((int)(PW * squashX), 20, 80), dh = iclamp((int)(PH * squashY), 20, 80);
        int dx = x + (PW - dw) / 2, dy = y + (PH - dh);
        fillRect(dx, dy, dw, dh, mk(125,0,14)); fillRect(dx + 3, dy + 3, dw - 6, dh - 6, mk(208,26,36)); fillRect(dx + 5, dy + 5, dw - 10, 7, mk(242,85,95));
        drawPlayerFace(dx, dy, dw, dh, facing); addDirty(b, x - 14, y - 18, PW + 28, PH + 20);
    }
}

// ---------- Cutscenes & Menus ----------
static void drawCubeK(int x, int y, int k) {
    fillRect(x, y, 88*k, 88*k, mk(140,0,15)); fillRect(x+6*k, y+6*k, 76*k, 76*k, mk(205,25,35));
    fillRect(x+18*k, y+24*k, 20*k, 28*k, mk(255,255,255)); fillRect(x+50*k, y+24*k, 20*k, 28*k, mk(255,255,255));
    fillRect(x+28*k, y+32*k, 10*k, 16*k, mk(0,0,0)); fillRect(x+60*k, y+32*k, 10*k, 16*k, mk(0,0,0));
    fillRect(x+30*k, y+64*k, 28*k, 8*k, mk(120,0,12));
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
static void drawMiniDemo(int x, int y, int dir) { fillRect(x, y, 44, 44, mk(140,0,15)); fillRect(x+3, y+3, 38, 38, mk(205,25,35)); drawPlayerFace(x, y, 44, 44, dir); }

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
        if (cyc >= 45 && cyc < 90) { int j = cyc-45; cy = ay+356 - j*(45-j)/4; xOn = j < 14; lbl = "STOMP!"; }
        else if (cyc >= 110 && cyc < 160) { int j = cyc-110; cy = ay+356 - j*(55-j)/4; if (j > 32 && cy > ay+246) cy = ay+246; xOn = j < 34; lbl = "HOLD X"; }
        else if (cyc >= 160) { cy = ay+246; lbl = "GREAT!"; }
        if (cyc < 72) { Enemy de = {0,0,0,0,0,0,0,E_WALKER,1,1,0,t,-1,true}; drawEnemyChar(de, ax+480, ay+356); }
        for (int i = 0; i < 14; i++) { int gx = ax+60+i*45; drawPlantTuft(gx, ay+400, 15, (iabs(cx+22-gx)<42 && cy>ay+320) ? 12 : SN[(t/5+i)&15]/32, 0, i%3); }
        drawMiniDemo(cx, cy, 1); drawPadPrompt(ax+290, ay+20, false, cyc < 165, false, xOn, lbl);
    } else if (art == 11) {
        fillRect(ax, ay, aw, ah, mk(215,125,75)); fillRect(ax+820, ay+120, 90, 90, mk(255,215,95));
        fillRect(ax, ay+400, 420, 100, mk(102,62,40)); fillRect(ax, ay+400, 420, 8, mk(225,175,55));
        fillRect(ax+660, ay+400, 340, 100, mk(102,62,40)); fillRect(ax+660, ay+400, 340, 8, mk(225,175,55));
        int cyc = t % 160, cx = iclamp(ax+70+cyc*6, ax, ax+850), cy = (cyc>=50&&cyc<102) ? ay+356-(cyc-50)*(102-cyc)/5 : ay+356;
        drawMiniDemo(cx, cy, 1); drawPadPrompt(ax+290, ay+20, false, cyc < 140, cyc < 140, cyc >= 50 && cyc < 85, cyc >= 50 ? "SPRINT+X" : "HOLD SQ");
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
        fillRect(ax, ay, aw, ah, art == 14 ? mk(55,20,30) : mk(28,22,58)); fillRect(ax, ay+410, aw, 90, mk(55,36,46));
        fillRect(ax+400, ay+180, 200, 32, mk(55,36,46)); fillRect(ax+460, ay+164, 80, 16, (t&8) ? mk(80,255,120) : mk(40,200,85));
        int cyc = t % 160, bx = ax+620 - (cyc < 80 ? cyc*2 : (160-cyc)*2), cx = ax+180, cy = ay+366;
        drawBossChar(bx, ay+306, 104, 104, cyc >= 70 && cyc < 110 && (t&2));
        if (cyc < 25) cx = ax+160+cyc*5;
        else if (cyc < 72) { int j = cyc-25; cx = ax+285+j*235/47; cy = ay+366-j*(47-j)/3-j*104/47; }
        else { int j = cyc-72; cx = ax+520+j*240/88; cy = iclamp(ay+262-j*(70-j)/6, ay, ay+366); }
        drawMiniDemo(cx, cy, 1); drawPadPrompt(ax+290, ay+20, false, true, false, cyc >= 25 && cyc < 55, cyc >= 72 ? "BOSS HIT!" : "STOMP HEAD!");
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
    curLvl = levelSel; buildLevel(); roomIdx = 0; respawn(); deaths = splatCount = winTimer = deadTimer = 0;
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

static void drawItem(int i) {
    static const int MENU_T[4] = {T_LEVELS, T_SETTINGS, T_CREDITS, T_EXIT};
    if (state == S_MENU && i >= 0 && i <= 3) {
        int y = 420 + i * 120; bool sel = (i == menuSel);
        if (sel) fillRect(606, y - 4, 708, 98, mk(255,210,85));
        fillRect(610, y, 700, 90, sel ? mk(215,30,45) : mk(48,36,68));
        drawLabel(MENU_T[i], 960, y, 90, 6, sel ? mk(255,255,255) : mk(215,205,230));
    } else if (state == S_LEVELS) {
        if (i >= 0 && i < NUM_LEVELS) {
            int x = 375 + (i % LCOLS) * 108, y = 330 + (i / LCOLS) * 108;
            bool sel = (i == levelSel), unl = (i < unlockedLevels), bs = isBossStage(i);
            if (sel) fillRect(x, y, 90, 90, mk(245,245,255)); else if (bs && unl) fillRect(x, y, 90, 90, mk(255,195,55));
            int ins = (sel || (bs && unl)) ? 6 : 0;
            fillRect(x + ins, y + ins, 90 - 2 * ins, 90 - 2 * ins, unl ? (bs ? mk(165,15,45) : mk(205,25,35)) : mk(50,40,62));
            if (unl) {
                if (bs) { fillRect(x+27,y+10,36,6,mk(255,215,70)); fillRect(x+27,y+6,8,6,mk(255,215,70)); fillRect(x+41,y+4,8,8,mk(255,215,70)); fillRect(x+55,y+6,8,6,mk(255,215,70)); }
                char buf[4]; intToStr(i + 1, buf); drawTextC(x + 45, y + (bs ? 28 : 24), buf, 6, mk(255,255,255));
            } else {
                Color g = mk(140,140,155); int cx = x + 45, cy = y + 45;
                fillRect(cx-18,cy-2,36,28,g); fillRect(cx-12,cy-26,24,6,g); fillRect(cx-12,cy-22,6,20,g); fillRect(cx+6,cy-22,6,20,g); fillRect(cx-3,cy+6,6,10,mk(40,40,50));
            }
        } else if (i == NUM_LEVELS) {
            fillRect(610, 880, 700, 60, mk(35,25,55));
            bool ok = (levelSel < unlockedLevels); Color c = ok ? mk(120,230,140) : mk(255,170,60);
            if (useAr()) drawLabel(ok ? T_PLAYX : T_LOCKED, 960, 880, 60, 4, c);
            else { char buf[64], n[4]; strcpy(buf, isBossStage(levelSel) ? "BOSS LEVEL " : "LEVEL "); intToStr(levelSel + 1, n); strcat(buf, n); strcat(buf, ok ? ": PRESS X TO PLAY" : ": LOCKED"); drawTextC(960, 896, buf, 4, c); }
        }
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
        if (state == S_MENU) { drawBanner(T_TITLE, 12, 140, 150, true); drawHint(T_H_SELECT); }
        else if (state == S_LEVELS) { drawBanner(T_B_LEVELS, 8, 130, 110, false); drawHint(T_H_LEVELS); }
        else if (state == S_SETTINGS) { drawBanner(T_B_SETTINGS, 8, 130, 110, false); fillRect(607,737,706,66,mk(235,185,65)); fillRect(610,740,700,60,mk(35,25,55)); drawLabel(T_TIP,960,740,60,3,mk(220,212,238)); drawHint(T_H_BACK); }
        else if (state == S_CREDITS) { drawBanner(T_B_CREDITS, 8, 130, 110, false); fillRect(456,296,1008,548,mk(235,185,65)); fillRect(460,300,1000,540,mk(35,25,55)); drawLabel(T_DESIGNERS,960,340,60,5,mk(220,212,238)); drawLabel(T_KAREEM,960,450,110,10,mk(255,75,115)); drawLabel(T_CLAUDE,960,580,110,10,mk(255,75,115)); drawLabel(T_GAME,960,740,60,4,mk(165,165,185)); drawHint(T_H_BACK); }
        int cnt = state == S_MENU ? 4 : (state == S_LEVELS ? NUM_LEVELS + 1 : (state == S_SETTINGS ? 3 : 0));
        for (int i = 0; i < cnt; i++) drawItem(i);
        uiFull[b] = false; memset(pend[b], 0, 64);
    } else {
        for (int i = 0; i < 64; i++) if (pend[b][i]) { drawItem(i); pend[b][i] = false; }
    }
}

static void updateUI() {
    bool nu = navEvent(0, in.up), nd = navEvent(1, in.down), nl = navEvent(2, in.left), nr = navEvent(3, in.right);
    if (state == S_MENU) {
        int o = menuSel; if (nu) menuSel = (menuSel + 3) % 4; if (nd) menuSel = (menuSel + 1) % 4;
        if (menuSel != o) { markPend(o); markPend(menuSel); snd(0); }
        if (in.crossNew) {
            if (menuSel == 0) { snd(1); setState(S_LEVELS); }
            else if (menuSel == 1) { snd(1); settingsSel = 0; setState(S_SETTINGS); }
            else if (menuSel == 2) { snd(1); setState(S_CREDITS); }
            else { snd(2); exit(0); }
        }
    } else if (state == S_LEVELS) {
        int o = levelSel, n = levelSel;
        if (nl && n > 0) n--; if (nr && n < NUM_LEVELS - 1) n++;
        if (nu && n >= LCOLS) n -= LCOLS; if (nd && n + LCOLS < NUM_LEVELS) n += LCOLS;
        if (n != o) { levelSel = n; markPend(o); markPend(n); markPend(NUM_LEVELS); snd(0); }
        if (in.crossNew && levelSel < unlockedLevels) { snd(1); launchLevel(levelSel); }
        else if (in.crossNew) snd(3);
        else if (in.circleNew) { snd(2); setState(S_MENU); }
    } else if (state == S_SETTINGS) {
        int o = settingsSel; if (nu) settingsSel = (settingsSel + 2) % 3; if (nd) settingsSel = (settingsSel + 1) % 3;
        if (settingsSel != o) { markPend(o); markPend(settingsSel); snd(0); }
        if (settingsSel == 0 && (nl || nr || in.crossNew)) { controlMode = 1 - controlMode; markPend(0); snd(0); }
        else if (settingsSel == 1 && (nl || nr || in.crossNew)) { if (ARABIC_OK) { lang = 1 - lang; snd(1); setState(S_SETTINGS); } else snd(3); }
        else if (in.crossNew || in.circleNew) { snd(2); setState(S_MENU); }
    } else if (state == S_CREDITS && (in.crossNew || in.circleNew)) { snd(2); setState(S_MENU); }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    scene = new Scene2D(FW, FH, 4);
    if (!scene->Init(0xC000000, 2) || !initPad()) for (;;);
    initAudio(); buildLevel(); roomIdx = 0; respawn(); setState(S_MENU); snd(1);
    for (;;) {
        int b = frameID & 1; readInput();
        if (state == S_PLAY) {
            if (in.optionsNew) setState(S_LEVELS);
            else if (winTimer > 0) {
                if (--winTimer == 0) {
                    if (curLvl + 2 > unlockedLevels && unlockedLevels < NUM_LEVELS) unlockedLevels = curLvl + 2;
                    if (curLvl < 11) launchLevel(curLvl + 1); else startStory(1);
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
