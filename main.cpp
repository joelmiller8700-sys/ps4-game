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

extern "C" {
    int sceKernelUsleep(unsigned int microseconds);
    int scePadSetVibration(int handle, const void *param);
#ifndef NO_AUDIO
    int sceAudioOutInit(void);
    int sceAudioOutOpen(int userId, int type, int index, unsigned int len, unsigned int freq, unsigned int param);
    int sceAudioOutOutput(int handle, const void *ptr);
    int sceAudioOutSetVolume(int handle, int flag, int *vol);
#endif
}

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

#define TOTAL_STAGES 16   // 8 Worlds x 2 Stages (Action/Puzzle/Escape + Boss/Challenge)
#define MAX_ENEMIES 64
#define MAX_SHOTS 28
#define MAX_PBULLETS 20
#define MAX_COINS 96
#define MAX_CHESTS 12
#define MAX_POWERUPS 16
#define MAX_MPLAT 16
#define MAX_DPLAT 24
#define MAX_NPCS 6
#define MAX_CHECKS 6
#define MAX_PARTS 150
#define MAX_SPLATS 240
#define MAX_PLANTS 620
#define MAX_LEAVES 24
#define MAX_FIREFLIES 18
#define MAX_DMGNUM 20
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

// ---------- Persistent Settings System (/data/meat_cube_settings.dat) ----------
struct SettingsData {
    unsigned int magic;
    // Graphics (0=Very Low, 1=Low, 2=Medium, 3=Ultra)
    int grassDensity, worldDetails, waterAnim, bloodParts, grassAnim;
    int lightingQual, shadowQual, particleFx, ambientFx;
    // Video
    int fpsLimit;     // 30 or 60
    int brightness;   // 50..150 (default 100)
    // Audio (0..100 independent channels)
    int masterVol, musicVol, sfxVol, ambVol, playerVol, enemyVol;
    int muteAll, muteMusic, muteSfx;
    // Language (0=AR, 1=EN, 2=FR, 3=ES, 4=DE, 5=TR)
    int lang;
    // Gameplay
    int camShake, rumble, showDmgNum, showDmgInd, difficulty, showHints, ngPlus;
    // Controls (0=Analog, 1=Arrows; button scheme 0=Default, 1=Alt)
    int moveMode, btnScheme;
};

static SettingsData cfg;
static unsigned char brLUT[256];

static void rebuildBrightnessLUT() {
    int br = cfg.brightness < 50 ? 50 : (cfg.brightness > 150 ? 150 : cfg.brightness);
    for (int i = 0; i < 256; i++) {
        int v = (i * br) / 100;
        brLUT[i] = (unsigned char)(v > 255 ? 255 : v);
    }
}

static void resetDefaultSettings() {
    cfg.magic = 0x53455432u;
    cfg.grassDensity = 2; cfg.worldDetails = 3; cfg.waterAnim = 2; cfg.bloodParts = 2; cfg.grassAnim = 2;
    cfg.lightingQual = 2; cfg.shadowQual = 2; cfg.particleFx = 2; cfg.ambientFx = 2;
    cfg.fpsLimit = 60; cfg.brightness = 100;
    cfg.masterVol = 85; cfg.musicVol = 75; cfg.sfxVol = 85; cfg.ambVol = 70; cfg.playerVol = 85; cfg.enemyVol = 85;
    cfg.muteAll = 0; cfg.muteMusic = 0; cfg.muteSfx = 0;
    cfg.lang = 0; // Arabic default
    cfg.camShake = 1; cfg.rumble = 1; cfg.showDmgNum = 1; cfg.showDmgInd = 1; cfg.difficulty = 1; cfg.showHints = 1; cfg.ngPlus = 0;
    cfg.moveMode = 0; cfg.btnScheme = 0;
    rebuildBrightnessLUT();
}

static void loadSettings() {
    resetDefaultSettings();
    FILE *f = fopen("/data/meat_cube_settings.dat", "rb");
    if (!f) return;
    SettingsData tmp;
    if (fread(&tmp, sizeof(SettingsData), 1, f) == 1 && tmp.magic == 0x53455432u) cfg = tmp;
    fclose(f);
    rebuildBrightnessLUT();
}

static void writeSettings() {
    FILE *f = fopen("/data/meat_cube_settings.dat", "wb");
    if (!f) return;
    fwrite(&cfg, sizeof(SettingsData), 1, f);
    fclose(f);
}

// ---------- Game Progression Save Data (/data/meat_cube_save.dat) ----------
struct SaveData {
    unsigned int magic;
    int unlockedStages, coins, xp, charLevel, skillPoints;
    int upHp, upDash, upMagnet, upSlam;
    unsigned int skillMask;    // Bit0=DoubleDash, Bit1=FireBlade, Bit2=Vampiric, Bit3=ShieldStart
    unsigned int skinsOwned;
    int selectedSkin;          // 0..4=Cubes, 5..9=Secret Naruto HD
    unsigned int achMask;      // 6 achievements
    int bestTime[TOTAL_STAGES];
    int secretsFound, enemiesKilled, ngPlusUnlocked;
};

static SaveData save = {
    0x4D433330u, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0u, 1u, 0, 0u,
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, 0, 0, 0
};

static void loadSave() {
    FILE *f = fopen("/data/meat_cube_save.dat", "rb");
    if (!f) return;
    SaveData tmp;
    if (fread(&tmp, sizeof(SaveData), 1, f) == 1 && tmp.magic == 0x4D433330u) {
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

// ---------- Game World & Entities State ----------
static float px, py, vx, vy, squashX = 1.0f, squashY = 1.0f, cpX = 80.0f, cpY = 900.0f;
static int roomIdx = 0, levelCols = COLS, levelPW = WW, curStage = 0, curWorld = 0;
static int hp = 3, maxHp = 3, invulnTimer = 0, jumpsLeft = 2, dashTimer = 0, dashCd = 0;
static bool slamActive = false, onGround = false, inWater = false, timeTrialMode = false, gateOpen = false;
static int curWeapon = 0, atkCd = 0; // 0=Stomp/Pulse, 1=Energy Bolt, 2=Fire Wave
static int slamShockTimer = 0, shieldTimer = 0, speedTimer = 0, animTick = 0, vibeTimer = 0;
static int coyote = 0, wallLock = 0, facing = 1, winTimer = 0, deadTimer = 0, tick = 0, frameID = 0;
static int stageTimeFrames = 0, escapeTimer = 0, shakeTimer = 0, shakeMag = 0;
static int toastTid = -1, toastTimer = 0, npcDialogTid = -1, npcDialogTimer = 0;
static int questKills = 0, questDone = 0;
static unsigned int rng = 12345;

struct Ghost { float x, y; int life, dir; };
static Ghost ghosts[8];

struct DmgNum { float x, y, vy; int val, life; Color col; };
static DmgNum dmgNums[MAX_DMGNUM];

static int checkC[MAX_CHECKS], checkR[MAX_CHECKS], checkCount = 0;
static bool checkAct[MAX_CHECKS];

// 6 Radically Distinct Enemies
enum { E_SPIDER = 0, E_BEETLE = 1, E_GHOST = 2, E_LIZARD = 3, E_DRONE = 4, E_MIMIC = 5 };
struct Enemy {
    float x, y, vx, vy, minX, maxX, baseY;
    int kind, hp, maxHp, hurt, timer, dir, state;
    bool active;
};
static Enemy enemies[MAX_ENEMIES]; static int enemyCount = 0;

// 4 Unique 3-Phase Bosses: 0=Spider King, 1=Lava Titan, 2=Ice Dragon, 3=Cursed Knight
static bool bossActive = false;
static int bossType = 0, bossPhase = 1, bossW = 128, bossH = 116, bossHP = 12, bossMaxHP = 12;
static int bossHurt = 0, bossTimer = 0, bossIntro = 0, bossSwCd = 0;
static float bossX, bossY, bossVX, bossVY;

struct Shot { float x, y, vx, vy; int kind; bool active; };
static Shot shots[MAX_SHOTS];

struct PBullet { float x, y, vx, vy; int kind, dmg, life; bool active; };
static PBullet pbullets[MAX_PBULLETS];

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

// ---------- UI State ----------
enum { S_MENU = 0, S_LEVELS = 1, S_SKILLS = 2, S_SHOP = 3, S_SKINS = 4, S_ACH = 5, S_SETTINGS = 6, S_CREDITS = 7, S_PLAY = 8, S_SECRET = 9 };
static int state = S_MENU, menuSel = 0, levelSel = 0, skillSel = 0, shopSel = 0, skinSel = 0, secretSel = 0;
static int setTab = 0, setRow = 0; // Pro Settings: 6 Tabs (0..5) + Row inside Tab
static bool uiFull[2] = {true, true}, pend[2][64];

// ---------- 6-Language Localization Table (0=AR via Bitmap, 1=EN, 2=FR, 3=ES, 4=DE, 5=TR) ----------
#define TEXT_LIST \
    X(T_TITLE,"MEAT CUBE","MEAT CUBE","MEAT CUBE","MEAT CUBE","MEAT CUBE",AR(TITLE)) \
    X(T_PLAY,"PLAY ADVENTURE","JOUER","JUGAR","SPIELEN","OYNA",AR(PLAY)) \
    X(T_SKILLTREE,"SKILL TREE","COMPETENCES","HABILIDADES","FAHIGKEITEN","YETENEK AGACI",AR(SKILLTREE)) \
    X(T_SHOP,"UPGRADE SHOP","BOUTIQUE","TIENDA","LADEN","MAGAZA",AR(SHOP)) \
    X(T_SKINS,"SKINS","APPARENCES","ASPECTOS","SKINS","GORUNUMLER",AR(SKINS)) \
    X(T_ACH,"ACHIEVEMENTS","SUCCES","LOGROS","ERFOLGE","BASARIMLAR",AR(ACH)) \
    X(T_SETTINGS,"SETTINGS","PARAMETRES","AJUSTES","EINSTELLUNGEN","AYARLAR",AR(SETTINGS)) \
    X(T_CREDITS,"CREDITS","CREDITS","CREDITOS","MITWIRKENDE","YAPIMCILAR",AR(CREDITS)) \
    X(T_EXIT,"EXIT","QUITTER","SALIR","BEENDEN","CIKIS",AR(EXIT)) \
    X(T_BACK,"BACK","RETOUR","VOLVER","ZURUCK","GERI",AR(BACK)) \
    X(T_B_LEVELS,"8 WORLDS MAP","CARTE DES MONDES","MAPA DE MUNDOS","WELTENKARTE","DUNYA HARITASI",AR(B_LEVELS)) \
    X(T_B_SKILLS,"SKILL TREE","ARBRE DE TALENTS","ARBOL DE SKILLS","SKILLBAUM","YETENEK AGACI",AR(B_SKILLS)) \
    X(T_B_SHOP,"UPGRADE SHOP","BOUTIQUE","TIENDA","LADEN","MAGAZA",AR(B_SHOP)) \
    X(T_B_SKINS,"CHARACTER SKINS","APPARENCES","ASPECTOS","SKINS","GORUNUMLER",AR(B_SKINS)) \
    X(T_B_ACH,"ACHIEVEMENTS","SUCCES & QUETES","LOGROS Y MISIONES","ERFOLGE","BASARIMLAR",AR(B_ACH)) \
    X(T_B_SETTINGS,"SETTINGS","PARAMETRES","CONFIGURACION","EINSTELLUNGEN","AYARLAR",AR(B_SETTINGS)) \
    X(T_B_CREDITS,"CREDITS","CREDITS","CREDITOS","MITWIRKENDE","YAPIMCILAR",AR(B_CREDITS)) \
    X(T_TAB_GFX,"GRAPHICS","GRAPHIQUES","GRAFICOS","GRAFIK","GRAFIK",AR(TAB_GFX)) \
    X(T_TAB_VID,"VIDEO","VIDEO","VIDEO","VIDEO","VIDEO",AR(TAB_VID)) \
    X(T_TAB_AUD,"AUDIO","AUDIO","AUDIO","AUDIO","SES",AR(TAB_AUD)) \
    X(T_TAB_LANG,"LANGUAGE","LANGUE","IDIOMA","SPRACHE","DIL",AR(TAB_LANG)) \
    X(T_TAB_GAME,"GAMEPLAY","JEU","JUGABILIDAD","GAMEPLAY","OYNANIS",AR(TAB_GAME)) \
    X(T_TAB_CTRL,"CONTROLS","COMMANDES","CONTROLES","STEUERUNG","KONTROLLER",AR(TAB_CTRL)) \
    X(T_G_GRASSD,"GRASS DENSITY","DENSITE HERBE","DENSIDAD HIERBA","GRASDICHTE","CIM YOGUNLUGU",AR(G_GRASSD)) \
    X(T_G_WORLD,"WORLD DETAILS","DETAILS MONDE","DETALLES MUNDO","WELTDETAILS","DUNYA DETAYI",AR(G_WORLD)) \
    X(T_G_WATER,"WATER ANIMATION","ANIMATION EAU","ANIMACION AGUA","WASSERANIMATION","SU ANIMASYONU",AR(G_WATER)) \
    X(T_G_BLOOD,"BLOOD PARTICLES","PARTICULES SANG","SANGRE","BLUTPARTIKEL","KAN EFEKTI",AR(G_BLOOD)) \
    X(T_G_GRASSA,"GRASS ANIMATION","ANIMATION HERBE","MOV. HIERBA","GRASANIMATION","CIM HAREKETI",AR(G_GRASSA)) \
    X(T_G_LIGHT,"LIGHTING QUALITY","ECLAIRAGE","ILUMINACION","BELEUCHTUNG","ISIK KALITESI",AR(G_LIGHT)) \
    X(T_G_SHADOW,"SHADOW QUALITY","OMBRES","SOMBRAS","SCHATTEN","GOLGE KALITESI",AR(G_SHADOW)) \
    X(T_G_PART,"PARTICLE EFFECTS","PARTICULES","PARTICULAS","PARTIKEL","PARCACIK EFEKTI",AR(G_PART)) \
    X(T_G_AMB,"AMBIENT EFFECTS","EFFETS AMBIANTS","AMBIENTE","UMGEBUNG","ORTAM EFEKTI",AR(G_AMB)) \
    X(T_VLOW,"VERY LOW","TRES BAS","MUY BAJO","SEHR NIEDRIG","COK DUSUK",AR(LVL_VLOW)) \
    X(T_LOW,"LOW","BAS","BAJO","NIEDRIG","DUSUK",AR(LVL_LOW)) \
    X(T_MED,"MEDIUM","MOYEN","MEDIO","MITTEL","ORTA",AR(LVL_MED)) \
    X(T_ULTRA,"ULTRA","ULTRA","ULTRA","ULTRA","ULTRA",AR(LVL_ULTRA)) \
    X(T_APPLY,"APPLY & SAVE","APPLIQUER","APLICAR","ANWENDEN","UYGULA",AR(APPLY)) \
    X(T_RESTORE,"RESTORE DEFAULTS","REINITIALISER","RESTAURAR","STANDARD","VARSAYILAN",AR(RESTORE)) \
    X(T_RESTORE_ALL,"RESTORE ALL DEFAULTS","TOUT REINITIALISER","RESTAURAR TODO","ALLES ZURUCK","TUMUNU SIFIRLA",AR(RESTORE_ALL)) \
    X(T_V_FPS,"FRAME RATE LIMIT","LIMITE FPS","LIMITE FPS","FPS LIMIT","FPS SINIRI",AR(V_FPS)) \
    X(T_V_BRIGHT,"BRIGHTNESS","LUMINOSITE","BRILLO","HELLIGKEIT","PARLAKLIK",AR(V_BRIGHT)) \
    X(T_V_RES,"RESOLUTION: 1080P TV","RES: 1080P TV","RES: 1080P TV","AUFLOSUNG: 1080P","COZUNURLUK: 1080P",AR(V_RES)) \
    X(T_V_VSYNC,"V-SYNC: ON (PS4 HW)","V-SYNC: OUI (PS4)","V-SYNC: SI (PS4)","V-SYNC: AN (PS4)","V-SYNC: ACIK",AR(V_VSYNC)) \
    X(T_A_MASTER,"MASTER VOLUME","VOLUME GENERAL","VOLUMEN GENERAL","GESAMTLAUTSTARKE","ANA SES",AR(A_MASTER)) \
    X(T_A_MUSIC,"MUSIC VOLUME","MUSIQUE","MUSICA","MUSIK","MUZIK",AR(A_MUSIC)) \
    X(T_A_SFX,"SFX VOLUME","EFFETS SONORES","EFECTOS","EFFEKTE","EFEKT SESI",AR(A_SFX)) \
    X(T_A_AMB,"AMBIENT VOLUME","AMBIANCE","AMBIENTE","UMGEBUNG","ORTAM SESI",AR(A_AMB)) \
    X(T_A_PLAYER,"PLAYER SOUNDS","SONS JOUEUR","SONIDOS JUGADOR","SPIELERSOUNDS","OYUNCU SESI",AR(A_PLAYER)) \
    X(T_A_ENEMY,"ENEMY SOUNDS","SONS ENNEMIS","SONIDOS ENEMIGOS","GEGNERSOUNDS","DUSMAN SESI",AR(A_ENEMY)) \
    X(T_A_MUTEALL,"MUTE ALL","TOUT COUPER","SILENCIAR TODO","ALLES STUMM","TUMUNU SUSTUR",AR(A_MUTEALL)) \
    X(T_A_MUTEMUS,"MUTE MUSIC","COUPER MUSIQUE","SILENCIAR MUSICA","MUSIK STUMM","MUZIGI SUSTUR",AR(A_MUTEMUS)) \
    X(T_A_MUTESFX,"MUTE SFX","COUPER EFFETS","SILENCIAR SFX","SFX STUMM","EFEKTI SUSTUR",AR(A_MUTESFX)) \
    X(T_GP_SHAKE,"CAMERA SHAKE","TREMBLEMENT CAM","VIBRACION CAM","KAMERAWACKELN","KAMERA SARSINTISI",AR(GP_SHAKE)) \
    X(T_GP_VIBE,"CONTROLLER RUMBLE","VIBRATION MANETTE","VIBRACION MANDO","VIBRATION","TITRESIM",AR(GP_VIBE)) \
    X(T_GP_DMGNUM,"DAMAGE NUMBERS","CHIFFRES DEGATS","NUMEROS DANO","SCHADENSZAHLEN","HASAR SAYILARI",AR(GP_DMGNUM)) \
    X(T_GP_DMGIND,"DAMAGE FLASH","INDICATEUR DEGATS","INDICADOR DANO","SCHADENSBLITZ","HASAR GOSTERGESI",AR(GP_DMGIND)) \
    X(T_GP_DIFF,"DIFFICULTY","DIFFICULTE","DIFICULTAD","SCHWIERIGKEIT","ZORLUK",AR(GP_DIFF)) \
    X(T_GP_HINTS,"SHOW HINTS","AFFICHER ASTUCES","MOSTRAR PISTAS","HINWEISE","IPUCLARI",AR(GP_HINTS)) \
    X(T_GP_NGPLUS,"NEW GAME PLUS","NEW GAME PLUS","NUEVA PARTIDA+","NEW GAME PLUS","NEW GAME PLUS",AR(GP_NGPLUS)) \
    X(T_D_EASY,"EASY","FACILE","FACIL","EINFACH","KOLAY",AR(D_EASY)) \
    X(T_D_NORM,"NORMAL","NORMAL","NORMAL","NORMAL","NORMAL",AR(D_NORM)) \
    X(T_D_HARD,"HARD","DIFFICILE","DIFICIL","SCHWER","ZOR",AR(D_HARD)) \
    X(T_ON,"ON","OUI","SI","AN","ACIK",AR(ON)) \
    X(T_OFF,"OFF","NON","NO","AUS","KAPALI",AR(OFF)) \
    X(T_W0,"1: HAUNTED FOREST","1: FORET HANTEE","1: BOSQUE","1: WALD","1: ORMAN",AR(W0)) \
    X(T_W1,"2: VOLCANO CORE","2: VOLCAN","2: VOLCAN","2: VULKAN","2: VOLKAN",AR(W1)) \
    X(T_W2,"3: FROZEN PEAKS","3: GLACE","3: HIELO","3: EISWELT","3: BUZ DUNYASI",AR(W2)) \
    X(T_W3,"4: DARK CASTLE","4: CHATEAU NOIR","4: CASTILLO","4: SCHLOSS","4: KARANLIK KALE",AR(W3)) \
    X(T_W4,"5: CRYSTAL CAVES","5: GROTTES","5: CUEVAS","5: HOHLEN","5: MAGARALAR",AR(W4)) \
    X(T_W5,"6: MECH FACTORY","6: USINE MECA","6: FABRICA","6: FABRIK","6: FABRIKA",AR(W5)) \
    X(T_W6,"7: SKY ISLANDS","7: ILES VOLANTES","7: ISLAS CIELO","7: HIMMELSINSELN","7: UCAN ADALAR",AR(W6)) \
    X(T_W7,"8: WATER RUINS","8: MONDE AQUATIQUE","8: MUNDO AGUA","8: WASSERWELT","8: SU DUNYASI",AR(W7)) \
    X(T_B0,"SPIDER KING","ROI ARAIGNEE","REY ARANA","SPINNENKONIG","ORUMCEK KRAL",AR(B_SPIDERK)) \
    X(T_B1,"LAVA TITAN","TITAN DE LAVE","TITAN DE LAVA","LAVATITAN","LAV TITANI",AR(B_LAVAT)) \
    X(T_B2,"ICE DRAGON","DRAGON DE GLACE","DRAGON HIELO","EISDRACHE","BUZ EJDERHASI",AR(B_ICEDRAG)) \
    X(T_B3,"CURSED KNIGHT","CHEVALIER MAUDIT","CABALLERO","FLUCHRITTER","LANETLI SOVALYE",AR(B_CURSEDK)) \
    X(T_COMPLETE,"STAGE COMPLETE!","NIVEAU TERMINE!","NIVEL COMPLETO!","LEVEL GESCHAFFT!","BOLUM TAMAM!",AR(COMPLETE)) \
    X(T_GREAT,"LEGENDARY!","LEGENDAIRE!","LEGENDARIO!","LEGENDAR!","EFSANEVI!",AR(GREAT)) \
    X(T_SEC_FND,"SECRET ROOM FOUND!","SALLE SECRETE!","SALA SECRETA!","GEHEIMRAUM!","GIZLI ODA!",AR(SECRET_FOUND)) \
    X(T_CHEST,"TREASURE! +25 COINS","TRESOR! +25","TESORO! +25","SCHATZ! +25","HAZINE! +25",AR(CHEST_OPEN)) \
    X(T_ACH_UNL,"ACHIEVEMENT UNLOCKED!","SUCCES DEBLOQUE!","LOGRO!","ERFOLG!","BASARIM ACILDI!",AR(ACH_UNL)) \
    X(T_Q_DONE,"SIDE QUEST DONE! +40C","QUETE FINIE! +40C","MISION LISTA! +40C","QUEST FERTIG! +40C","GOREV BITTI! +40C",AR(QUEST_DONE)) \
    X(T_NPC_H,"TRIANGLE: TALK","TRIANGLE: PARLER","TRIANGULO: HABLAR","DREIECK: REDEN","UCGEN: KONUS",AR(NPC_H)) \
    X(T_NPC0,"SAGE: DOUBLE JUMP (X) & DASH (O) OVER BRIDGES!","SAGE: DOUBLE SAUT (X) ET DASH (O)!","SABIO: DOBLE SALTO (X) Y DASH (O)!","WEISER: DOPPELSPRUNG & DASH!","BILGE: CIFT ZIPLAMA VE DASH KULLAN!",AR(NPC0)) \
    X(T_NPC1,"SCOUT: DOWN + SQUARE IN AIR BREAKS ARMOR!","ECLAIREUR: BAS+CARRE BRISE ARMURE!","EXPLORADOR: ABAJO+CUADRADO ROMPE ARMADURA!","SPAHER: UNTEN+QUADRAT!","KASIF: HAVADA ASAGI+KARE ZIRH KIRAR!",AR(NPC1)) \
    X(T_NPC2,"GUARD: BEWARE! SOME CHESTS ARE MIMICS!","GARDE: ATTENTION AUX COFFRES MIMICS!","GUARDIA: CUIDADO CON LOS MIMICS!","WACHE: VORSICHT VOR MIMICS!","MUHAFIZ: BAZI SANDIKLAR MIMIC!",AR(NPC2)) \
    X(T_NPC3,"MERCHANT: SWITCH WEAPON WITH L1 & UPGRADE SKILLS!","MARCHAND: ARME L1 ET COMPETENCES!","COMERCIANTE: ARMA L1 Y HABILIDADES!","HANDLER: WAFFE L1 & SKILLS!","TUCCAR: L1 ILE SILAH DEGISTIR!",AR(NPC3)) \
    X(T_H_SEL,"X: SELECT   O: BACK","X: CHOISIR   O: RETOUR","X: ELEGIR   O: VOLVER","X: WAHLEN   O: ZURUCK","X: SEC   O: GERI",AR(SELECT)) \
    X(T_H_BACK,"O: BACK","O: RETOUR","O: VOLVER","O: ZURUCK","O: GERI",AR(BACK_HINT)) \
    X(T_H_LVL,"X: PLAY   SQ: TIME TRIAL   O: BACK","X: JOUER  SQ: CHRONO  O: RETOUR","X: JUGAR  SQ: TIEMPO  O: VOLVER","X: SPIELEN  SQ: ZEIT  O: ZURUCK","X: OYNA  SQ: ZAMAN  O: GERI",AR(LEVELS_HINT))

enum {
#define X(id, en, fr, es, de, tr, ar) id,
    TEXT_LIST
#undef X
    T_COUNT
};
static const char *TXT_LANG[5][T_COUNT] = {
    {
#define X(id, en, fr, es, de, tr, ar) en,
        TEXT_LIST
#undef X
    }, {
#define X(id, en, fr, es, de, tr, ar) fr,
        TEXT_LIST
#undef X
    }, {
#define X(id, en, fr, es, de, tr, ar) es,
        TEXT_LIST
#undef X
    }, {
#define X(id, en, fr, es, de, tr, ar) de,
        TEXT_LIST
#undef X
    }, {
#define X(id, en, fr, es, de, tr, ar) tr,
        TEXT_LIST
#undef X
    }
};
static const int TXT_AR[T_COUNT] = {
#define X(id, en, fr, es, de, tr, ar) ar,
    TEXT_LIST
#undef X
};

// ---------- Input & Rumble ----------
struct Input {
    bool up, down, left, right, dleft, dright;
    bool cross, circle, square, triangle, options, r1, l1, l2, r2;
    bool crossNew, circleNew, squareNew, triNew, optionsNew, dashNew, l1New, r1New;
    float ana;
};
static Input in;
static bool pCross=0, pCircle=0, pSquare=0, pTri=0, pOpt=0, pDash=0, pL1=0, pR1=0;
static int holdCnt[4] = {0, 0, 0, 0};

static inline int iabs(int a) { return a < 0 ? -a : a; }
static inline int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int rnd(int n) { rng = rng * 1103515245u + 12345u; return (int)((rng >> 16) % (unsigned int)n); }
static inline Color mk(int r, int g, int b) { Color c = {brLUT[iclamp(r,0,255)], brLUT[iclamp(g,0,255)], brLUT[iclamp(b,0,255)]}; return c; }
static inline bool useAr() { return cfg.lang == 0 && ARABIC_OK; }
static inline bool isBossStage(int s) { return (s & 1) == 1; }

static void triggerRumble(int frames, unsigned char lg, unsigned char sm) {
    if (!cfg.rumble || padHandle < 0) return;
    struct { unsigned char large, small; } vp = {lg, sm};
    scePadSetVibration(padHandle, &vp);
    if (frames > vibeTimer) vibeTimer = frames;
}

static bool initPad() {
    if (scePadInit() != 0) return false;
    OrbisUserServiceInitializeParams param; param.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    sceUserServiceInitialize(&param); sceUserServiceGetInitialUser(&padUserID);
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
    if (a > 1.0f) a = 1.0f; else if (a < -1.0f) a = -1.0f; if (a > -0.2f && a < 0.2f) a = 0.0f;
    in.ana = a;
    in.cross = (b & ORBIS_PAD_BUTTON_CROSS) != 0; in.circle = (b & ORBIS_PAD_BUTTON_CIRCLE) != 0;
    in.square = (b & ORBIS_PAD_BUTTON_SQUARE) != 0; in.triangle = (b & ORBIS_PAD_BUTTON_TRIANGLE) != 0;
    in.options = (b & ORBIS_PAD_BUTTON_OPTIONS) != 0;
    in.r1 = (b & ORBIS_PAD_BUTTON_R1) != 0; in.l1 = (b & ORBIS_PAD_BUTTON_L1) != 0;
    in.l2 = ((b & ORBIS_PAD_BUTTON_L2) != 0) || (padData.analogButtons.l2 > 35);
    in.r2 = ((b & ORBIS_PAD_BUTTON_R2) != 0) || (padData.analogButtons.r2 > 35);
    bool dashBtn = cfg.btnScheme == 0 ? (in.circle || in.r1) : (in.square || in.r1);
    in.crossNew = in.cross && !pCross; in.circleNew = in.circle && !pCircle; in.squareNew = in.square && !pSquare;
    in.triNew = in.triangle && !pTri; in.optionsNew = in.options && !pOpt; in.dashNew = dashBtn && !pDash;
    in.l1New = in.l1 && !pL1; in.r1New = in.r1 && !pR1;
    pCross = in.cross; pCircle = in.circle; pSquare = in.square; pTri = in.triangle; pOpt = in.options; pDash = dashBtn; pL1 = in.l1; pR1 = in.r1;
    if (vibeTimer > 0 && --vibeTimer == 0 && padHandle >= 0) { struct { unsigned char l, s; } z = {0, 0}; scePadSetVibration(padHandle, &z); }
}

static bool navEvent(int i, bool p) {
    if (!p) { holdCnt[i] = 0; return false; }
    holdCnt[i]++; return holdCnt[i] == 1 || (holdCnt[i] > 20 && ((holdCnt[i] - 20) % 6) == 0);
}

// ---------- 6-Channel Independent Halal Audio Engine ----------
// Channels: Master, Music, SFX, Ambient (wind/water), Player, Enemy
static int curSfx = -1, sfxChan = 0, sfxPos = 0, sfxLen = 0, sfxPhase = 0, bgmSample = 0, humP1 = 0, humP2 = 0, duffP = 0;
static unsigned int noiseRng = 987654321u;

static void playToneImmediate(int freq, int ms, int vol) {
#ifndef NO_AUDIO
    if (audioHandle < 0 || cfg.muteAll || cfg.muteSfx) return;
    int effVol = (vol * cfg.masterVol * cfg.sfxVol) / 10000;
    if (effVol <= 0) return;
    static short buf[512]; int total = 48 * ms, ph = 0, done = 0;
    while (done < total) {
        for (int i = 0; i < 256; i++) {
            int idx = done + i, s = 0;
            if (idx < total) {
                if ((ph += freq) >= 48000) ph -= 48000;
                s = (((ph < 24000 ? ph : 48000 - ph) - 12000) * effVol) / 12000;
                if (total - idx < 1200) s = (s * (total - idx)) / 1200;
            }
            buf[i * 2] = buf[i * 2 + 1] = (short)s;
        }
        sceAudioOutOutput(audioHandle, buf); done += 256;
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
    if (audioHandle >= 0) { int vol[8] = {32768,32768,32768,32768,32768,32768,32768,32768}; sceAudioOutSetVolume(audioHandle, 3, vol); }
#endif
}

// sfxChan: 0=general SFX, 1=Player, 2=Enemy
static void snd(int k, int chan = 0) {
    if (state != S_PLAY) {
        if (k == 0) playToneImmediate(880, 20, 15000);
        else if (k == 1) { playToneImmediate(660, 28, 17000); playToneImmediate(990, 44, 17000); }
        else if (k == 2) playToneImmediate(440, 36, 15000);
        else playToneImmediate(220, 65, 15000);
        return;
    }
    static const int LENS[12] = {1200, 3600, 2400, 3200, 2600, 3200, 6200, 5200, 12000, 2200, 5800, 650};
    curSfx = k; sfxChan = chan; sfxPos = 0; sfxPhase = 0; sfxLen = LENS[iclamp(k, 0, 11)];
}

static void updateGameplayAudio() {
#ifndef NO_AUDIO
    if (audioHandle < 0 || state != S_PLAY) return;
    static short outBuf[512];
    static const int W_HB[8][4] = {
        {110,130,146,130},{98,116,130,110},{130,146,165,146},{92,104,116,98},
        {123,146,165,138},{104,116,130,104},{146,165,195,165},{116,130,146,138}
    };
    static const int W_HH[8][4] = {
        {165,195,220,195},{146,174,195,165},{195,220,247,220},{138,155,174,146},
        {185,220,247,207},{155,174,195,155},{220,247,293,247},{174,195,220,207}
    };
    static const unsigned char PAT[4][16] = {
        {1,0,3,2,1,0,2,0,1,1,3,2,0,2,3,2},{1,1,2,0,1,0,2,2,1,1,2,0,1,2,3,2},
        {1,0,3,0,2,0,3,0,1,0,3,2,0,3,2,0},{1,0,2,1,0,2,1,0,1,2,0,2,1,1,2,2}
    };
    int w = iclamp(curWorld, 0, 7);
    int stepS = bossActive ? 4600 : (w == 1 || w == 5 ? 5400 : 6200);
    int mGain = (cfg.muteAll || cfg.muteMusic || winTimer > 0) ? 0 : (cfg.masterVol * cfg.musicVol) / 100;
    int aGain = cfg.muteAll ? 0 : (cfg.masterVol * cfg.ambVol) / 100;
    int sBase = (cfg.muteAll || cfg.muteSfx) ? 0 : (sfxChan == 1 ? cfg.playerVol : (sfxChan == 2 ? cfg.enemyVol : cfg.sfxVol));
    int sGain = (cfg.masterVol * sBase) / 100;

    for (int blk = 0; blk < 2; blk++) {
        for (int i = 0; i < 256; i++) {
            int mix = 0;
            if (mGain > 0) {
                int step = (bgmSample / stepS) & 15, inS = bgmSample % stepS, bar = (bgmSample / (stepS * 16)) & 3;
                bgmSample++; int m = 0;
                unsigned char hit = PAT[w & 3][step];
                if (hit == 1 && inS < 3000) { int env = 3000 - inS; if ((duffP += 66 + (env >> 6)) >= 48000) duffP -= 48000; m += ((duffP < 24000 ? duffP - 12000 : 36000 - duffP) * env) / 4200; }
                else if (hit == 2 && inS < 1300) { int env = 1300 - inS; noiseRng = noiseRng * 1664525u + 1013904223u; int nz = ((int)(noiseRng >> 20) & 511) - 256; if ((duffP += 290) >= 48000) duffP -= 48000; m += ((nz + (duffP < 24000 ? 260 : -260)) * env) / 300; }
                else if (hit == 3 && inS < 700) { noiseRng = noiseRng * 1664525u + 1013904223u; m += ((((int)(noiseRng >> 22) & 255) - 128) * (700 - inS)) / 340; }
                int f1 = W_HB[w][bar] + (bossActive ? 24 : 0), f2 = W_HH[w][bar] + (bossActive ? 36 : 0);
                if ((humP1 += f1) >= 48000) humP1 -= 48000; if ((humP2 += f2) >= 48000) humP2 -= 48000;
                int v1 = humP1 < 24000 ? humP1 - 12000 : 36000 - humP1, v2 = humP2 < 24000 ? humP2 - 12000 : 36000 - humP2;
                int br = inS < (stepS / 2) ? inS : (stepS - inS);
                m += ((v1 + v2) * (1200 + br / 6)) / 10000;
                mix += (m * mGain) / 10000;
            }
            if (aGain > 0) {
                noiseRng = noiseRng * 1664525u + 1013904223u;
                int breeze = (((int)(noiseRng >> 22) & 255) - 128) * (w == 7 || w == 1 ? 6 : 3);
                mix += (breeze * aGain) / 10000;
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
                    int s = (((sfxPhase < 24000 ? sfxPhase : 48000 - sfxPhase) - 12000) * vol) / 12000;
                    if (curSfx == 5 || curSfx == 6 || curSfx == 7 || curSfx == 10) { noiseRng = noiseRng * 1664525u + 1013904223u; s += (((int)(noiseRng >> 19) & 1023) - 512) * (curSfx == 10 ? 9 : 5); }
                    if (rem < 1000) s = (s * rem) / 1000;
                    if (sGain > 0) mix += (s * sGain) / 10000;
                    sfxPos++;
                }
            }
            outBuf[i * 2] = outBuf[i * 2 + 1] = (short)iclamp(mix, -30000, 30000);
        }
        sceAudioOutOutput(audioHandle, outBuf);
    }
#endif
}

// ---------- Drawing Primitives & Multi-Language Text ----------
static inline void setClip(int x, int y, int w, int h) { clipX0 = x < 0 ? 0 : x; clipY0 = y < 0 ? 0 : y; clipX1 = (x + w) > FW ? FW : (x + w); clipY1 = (y + h) > FH ? FH : (y + h); }
static inline void resetClip() { clipX0 = 0; clipY0 = 0; clipX1 = FW; clipY1 = FH; }
static void fillRect(int x, int y, int w, int h, Color c) {
    if (w <= 0 || h <= 0) return;
    int x1 = x + w, y1 = y + h;
    if (x < clipX0) x = clipX0; if (y < clipY0) y = clipY0; if (x1 > clipX1) x1 = clipX1; if (y1 > clipY1) y1 = clipY1;
    for (int j = y; j < y1; j++) for (int i = x; i < x1; i++) scene->DrawPixel(i, j, c);
}
static void fillCircle(int cx, int cy, int r, Color c) {
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        int py0 = cy + dy; if (py0 < clipY0 || py0 >= clipY1) continue;
        for (int dx = -r; dx <= r; dx++) { int px0 = cx + dx; if (px0 >= clipX0 && px0 < clipX1 && dx * dx + dy * dy <= r2) scene->DrawPixel(px0, py0, c); }
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
    if (ch >= 'a' && ch <= 'z') return ch - 'a';
    if (ch >= '0' && ch <= '9') return 26 + ch - '0';
    static const char *sym = ":-<> .!,?";
    for (int i = 0; sym[i]; i++) if (ch == sym[i]) return 36 + i;
    return 40;
}
static void drawText(int x, int y, const char *s, int sc, Color c) {
    for (; *s; s++, x += 6 * sc) {
        int g = glyphIndex(*s); if (g == 40) continue;
        for (int r = 0; r < 7; r++) for (int col = 0; col < 5; col++) if (FONT[g][r] & (0x10 >> col)) fillRect(x + col * sc, y + r * sc, sc, sc, c);
    }
}
static int textWidth(const char *s, int sc) { int n = (int)strlen(s); return n > 0 ? n * 6 * sc - sc : 0; }
static void drawTextC(int cx, int y, const char *s, int sc, Color c) { drawText(cx - textWidth(s, sc) / 2, y, s, sc, c); }
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
    for (int cx = c0; cx < c1; cx++) for (int ry = 0; ry < im.h; ry++) if (im.bits[ry * rb + (cx >> 3)] & (0x80 >> (cx & 7))) scene->DrawPixel(x + cx, y + ry, col);
#else
    (void)id; (void)x; (void)y; (void)c0; (void)c1; (void)col;
#endif
}
static const char *getLocStr(int tid) { int li = iclamp(cfg.lang - 1, 0, 4); return TXT_LANG[li][tid]; }
static void drawLabel(int tid, int cx, int by, int bh, int sc, Color c) {
    if (useAr() && TXT_AR[tid] >= 0) { int id = TXT_AR[tid]; drawArCols(id, cx - arW(id) / 2, by + (bh - arH(id)) / 2, 0, arW(id), c); }
    else drawTextC(cx, by + (bh - 7 * sc) / 2, getLocStr(tid), sc, c);
}
static int labelWidth(int tid, int sc) { return (useAr() && TXT_AR[tid] >= 0) ? arW(TXT_AR[tid]) : textWidth(getLocStr(tid), sc); }

static void unlockAch(int idx) {
    if (idx < 0 || idx >= 6) return;
    if (!(save.achMask & (1u << idx))) { save.achMask |= (1u << idx); toastTid = T_ACH_UNL; toastTimer = 180; writeSave(); snd(1, 0); }
}
static void spawnDmgNum(float x, float y, int val, Color col) {
    if (!cfg.showDmgNum) return;
    for (int i = 0; i < MAX_DMGNUM; i++) if (dmgNums[i].life <= 0) { dmgNums[i] = {x, y, -2.0f, val, 36, col}; return; }
}

// ---------- 8-World Stage Builder (Bridges '=', Water '~', Switches 'S', Gates 'D', Caves, Puzzles) ----------
static int baseCol = 0;
static void put(int r, int c, char ch) { int g = baseCol + c; if (r >= 0 && r < ROWS && g >= 0 && g < COLS) level[r][g] = ch; }
static void plat(int r, int a, int b) { for (int c = a; c <= b; c++) put(r, c, '#'); }
static void bridge(int r, int a, int b) { for (int c = a; c <= b; c++) put(r, c, '='); }
static void waterPool(int r0, int r1, int a, int b) { for (int r = r0; r <= r1; r++) for (int c = a; c <= b; c++) put(r, c, '~'); }
static void wall(int c, int r0, int r1) { for (int r = r0; r <= r1; r++) put(r, c, '#'); }
static void spk(int r, int a, int b) { for (int c = a; c <= b; c++) put(r, c, '^'); }
static void sawAt(int r, int c) { put(r, c, 'O'); }
static void addCP(int c, int r) { if (checkCount < MAX_CHECKS) { checkC[checkCount] = baseCol + c; checkR[checkCount] = r; checkAct[checkCount++] = false; } }
static void addCoin(int c, int r) { if (coinCount < MAX_COINS) { int g = baseCol + c; coins[coinCount++] = {(short)(g / RCOLS), (short)((g % RCOLS) * TILE + 30), (short)(r * TILE + 26), true}; } }
static void addCoinRow(int r, int a, int b) { for (int c = a; c <= b; c += 2) addCoin(c, r); }
static void addChest(int c, int r, bool sec) { if (chestCount < MAX_CHESTS) { int g = baseCol + c; chests[chestCount++] = {(short)(g / RCOLS), (short)((g % RCOLS) * TILE + 8), (short)(r * TILE - 34), false, sec}; } }
static void addPU(int c, int r, int k) { if (puCount < MAX_POWERUPS) { int g = baseCol + c; powerups[puCount++] = {(short)(g / RCOLS), (short)((g % RCOLS) * TILE + 18), (short)(r * TILE - 32), k, true}; } }
static void addMPlat(int r, int a, int b, int wT, float spd) { if (mplatCount < MAX_MPLAT) { int g = baseCol + a; mplats[mplatCount++] = {(short)(g / RCOLS), (float)((g % RCOLS) * TILE), (float)(r * TILE), (float)((g % RCOLS) * TILE), (float)(((baseCol + b) % RCOLS) * TILE), spd, (short)(wT * TILE)}; } }
static void addDPlat(int r, int c, int wT) { if (dplatCount < MAX_DPLAT) { int g = baseCol + c; dplats[dplatCount++] = {(short)(g / RCOLS), (short)((g % RCOLS) * TILE), (short)(r * TILE), (short)(wT * TILE), 0, 0}; } }
static void addNPC(int c, int r, int tid) { if (npcCount < MAX_NPCS) { int g = baseCol + c; npcs[npcCount++] = {(short)(g / RCOLS), (short)((g % RCOLS) * TILE + 10), (short)(r * TILE - 48), tid}; } }
static void addE(int k, int c, int r, int a, int b) {
    if (enemyCount >= MAX_ENEMIES) return;
    Enemy &e = enemies[enemyCount++];
    e.kind = k; e.x = (float)((baseCol + c) * TILE + 8); e.y = e.baseY = (float)(r * TILE - 44);
    e.minX = (float)((baseCol + a) * TILE + 4); e.maxX = (float)((baseCol + b) * TILE + 12);
    e.dir = 1; e.vx = k == E_DRONE ? 3.2f : (k == E_BEETLE ? 2.1f : (k == E_GHOST ? 2.4f : (k == E_SPIDER ? 2.5f : 0.0f)));
    e.vy = 0; int hm = cfg.difficulty == 2 || cfg.ngPlus ? 2 : 1;
    e.maxHp = e.hp = (k == E_BEETLE || k == E_MIMIC ? 2 : 1) * hm; e.hurt = 0; e.timer = (c * 17) & 63; e.state = 0; e.active = true;
}

static void initWorldDetails() {
    plantCount = 0;
    int maxPerTile = cfg.grassDensity == 0 ? 0 : (cfg.grassDensity == 1 ? 1 : (cfg.grassDensity == 2 ? 2 : 3));
    if (maxPerTile > 0) {
        for (int r = 2; r < ROWS; r++) for (int c = 1; c < levelCols - 1; c++) {
            if (level[r][c] != '#' || level[r - 1][c] != '.') continue;
            if (cfg.grassDensity == 1 && (c % 2) != 0) continue;
            for (int s = 0; s < maxPerTile && plantCount < MAX_PLANTS; s++) {
                Plant &p = plants[plantCount++];
                p.room = (short)(c / RCOLS); p.x = (short)((c % RCOLS) * TILE + 10 + s * (50 / maxPerTile));
                p.y = (short)(r * TILE); p.angle = p.vel = 0; p.h = (unsigned char)(11 + ((c * 3 + r * 7 + s * 5) % 7));
                p.style = (unsigned char)((c + r + s) % 3); p.seed = (unsigned char)((c * 5 + r * 9 + s * 11) & 15);
                p.drawn[0] = p.drawn[1] = false; p.dBend[0] = p.dBend[1] = 99;
            }
        }
    }
    for (int i = 0; i < MAX_LEAVES; i++) {
        leaves[i].x = (float)((i * 173 + 47) % FW); leaves[i].y = (float)(100 + ((i * 113 + 31) % (FH - 240)));
        leaves[i].vx = (curWorld == 1 ? -0.4f : 0.6f) + (i % 3) * 0.2f; leaves[i].vy = (curWorld == 1 ? -1.3f : 1.0f);
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
    bossActive = false; gateOpen = false; baseCol = 0; levelCols = COLS; levelPW = WW; escapeTimer = 0;
    curWorld = iclamp(st / 2, 0, 7);
    cpX = (float)(TILE + 16); cpY = (float)((ROWS - 1) * TILE - PH);

    if (isBossStage(st)) {
        levelCols = RCOLS; levelPW = FW; bossActive = true; bossType = curWorld % 4; bossPhase = 1; bossIntro = 50;
        for (int r = 0; r < ROWS; r++) level[r][RCOLS - 1] = '#';
        wall(2, 6, 14); wall(29, 6, 14); bridge(12, 5, 10); plat(7, 13, 18); bridge(12, 21, 26);
        if (curWorld == 7) waterPool(15, 16, 4, 27);
        addPU(7, 12, PU_HEART); addPU(24, 12, PU_SHIELD);
        bossW = 128; bossH = 116; bossX = (float)(21 * TILE); bossY = (float)((ROWS - 1) * TILE - bossH);
        bossVX = -4.0f - bossType * 0.4f; bossVY = 0;
        int dMul = cfg.difficulty == 0 ? 8 : (cfg.difficulty == 2 || cfg.ngPlus ? 16 : 12);
        bossMaxHP = bossHP = dMul + bossType * 3; bossHurt = bossTimer = bossSwCd = 0;
        for (int i = 0; i < MAX_SHOTS; i++) shots[i].active = false;
        initWorldDetails(); return;
    }

    // Room 0: World-themed terrain (Arched Bridges, Water Pools, Rocky Steps, NPC & Secret High Room)
    baseCol = 0 * RCOLS;
    addNPC(4, 17, T_NPC0 + (curWorld & 3));
    if (curWorld == 0 || curWorld == 7) { waterPool(15, 16, 9, 20); bridge(14, 8, 21); addCoinRow(13, 10, 19); }
    else { plat(14, 7, 14); addCoinRow(13, 8, 13); plat(12, 17, 23); }
    addE(E_SPIDER, 13, 6, 9, 20); addE(E_BEETLE, 18, 14, 15, 23);
    plat(6, 4, 9); addChest(6, 6, true); addCoinRow(5, 5, 8); // Secret upper ledge!

    // Room 1: Moving & Disappearing Bridges + Mimic Ambush + Ghost
    baseCol = 1 * RCOLS;
    if (curWorld == 1 || curWorld == 5) spk(16, 6, 25); else waterPool(15, 16, 6, 25);
    plat(14, 2, 6); addMPlat(13, 8, 14, 3, 2.2f); addDPlat(12, 18, 3); addDPlat(12, 22, 3);
    addE(E_GHOST, 15, 9, 8, 22);
    plat(13, 26, 30); addE(E_MIMIC, 28, 13, 26, 30); addCP(27, 13);

    // Room 2: Environmental Puzzle (Switch 'S' opens Gate 'D') + Wall Jump Cave
    baseCol = 2 * RCOLS;
    wall(9, 6, 14); wall(13, 6, 16); plat(6, 13, 20); put(5, 18, 'S'); addCoinRow(5, 14, 17);
    plat(6, 2, 7); addChest(4, 6, true);
    wall(24, 1, 12); for (int r = 13; r <= 16; r++) put(r, 24, 'D'); // Locked gate!
    addE(E_LIZARD, 16, 17, 14, 22); addPU(26, 17, PU_HEART);

    // Room 3: Mechanical/Cave/Sky Gauntlet with Flying Drones & Saws
    baseCol = 3 * RCOLS;
    if (curWorld == 5) escapeTimer = 1800; // 30s Escape Sequence in Factory!
    plat(14, 2, 8); bridge(12, 10, 17); sawAt(11, 13); addE(E_DRONE, 15, 7, 8, 24);
    addDPlat(11, 19, 3); plat(13, 24, 30); addE(E_BEETLE, 27, 13, 24, 30); addCP(25, 13);

    // Room 4: Rocky Cavern / Water Ruin Climb + Fire Lizard + Chest
    baseCol = 4 * RCOLS;
    waterPool(15, 16, 4, 26); plat(14, 3, 8); bridge(12, 10, 16); addE(E_LIZARD, 14, 12, 10, 16);
    addMPlat(10, 18, 23, 3, 2.4f); plat(10, 25, 30); addChest(28, 10, false); addE(E_GHOST, 20, 7, 12, 26);

    // Room 5: Final Bridge & Goal 'G'
    baseCol = 5 * RCOLS;
    plat(14, 3, 9); bridge(12, 11, 21); addCoinRow(11, 12, 20);
    addE(E_SPIDER, 16, 6, 11, 21); addE(E_DRONE, 18, 8, 11, 24);
    plat(10, 24, 30); put(9, 28, 'G');
    baseCol = 0; initWorldDetails();
}

// ---------- Collision, Weapons, Physics, 6 Enemies & 3-Phase Bosses ----------
static inline char tileAt(int r, int c) { return (r < 0 || r >= ROWS || c < 0 || c >= COLS) ? '#' : level[r][c]; }
static inline char staticChar(int r, int c) { char ch = tileAt(r, c); return (ch == 'O' || (ch == 'D' && gateOpen)) ? '.' : ch; }
static inline bool hiAt(int r, int c) { return r > 0 && staticChar(r, c) == '#' && staticChar(r - 1, c) != '#'; }
static inline bool solidAt(int x, int y) {
    if (x < 0 || y < 0 || x / TILE >= COLS || y / TILE >= ROWS) return true;
    char ch = level[y / TILE][x / TILE];
    return ch == '#' || (ch == 'D' && !gateOpen);
}
static inline bool rectSolid(int x, int y, int w, int h) { return solidAt(x, y) || solidAt(x + w - 1, y) || solidAt(x, y + h - 1) || solidAt(x + w - 1, y + h - 1); }
static inline bool rectOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) { return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by; }
static inline bool circleHit(int cx, int cy, int rad, int rx, int ry, int rw, int rh) {
    int dx = cx - iclamp(cx, rx, rx + rw), dy = cy - iclamp(cy, ry, ry + rh); return dx * dx + dy * dy < rad * rad;
}
static void triggerShake(int f, int m) { if (!cfg.camShake) return; if (f > shakeTimer) shakeTimer = f; if (m > shakeMag) shakeMag = m; }
static void showToast(int tid) { toastTid = tid; toastTimer = 150; hudNeed[0] = hudNeed[1] = true; }

static void addSplat(int wx, int wy, int w, int h) {
    if (cfg.bloodParts == 0 || wx < 0 || wy < 0 || wx >= levelPW) return;
    int idx = splatCount < MAX_SPLATS ? splatCount++ : -1;
    if (idx < 0) for (int i = 0; i < MAX_SPLATS; i++) if (splats[i].stage >= 4 && !splats[i].drawn[0] && !splats[i].drawn[1]) { idx = i; break; }
    if (idx < 0) return;
    int rm = iclamp(wx / FW, 0, ROOMS - 1); Splat &s = splats[idx];
    s.room = (short)rm; s.x = (short)(wx - rm * FW - w / 2); s.y = (short)(wy - h / 2); s.w = (short)w; s.h = (short)h;
    s.born = tick; s.stage = 0; s.drawn[0] = s.drawn[1] = false; s.dStage[0] = s.dStage[1] = 99;
}
static void spawnPart(float x, float y, float pvx, float pvy, int life, int sz, int kind) {
    if (kind != 1 && cfg.particleFx == 0) return;
    if (kind == 1 && cfg.bloodParts == 0) return;
    for (int i = 0; i < MAX_PARTS; i++) if (parts[i].life <= 0) { parts[i] = {x, y, pvx, pvy, (short)life, (short)life, (unsigned char)kind, (unsigned char)sz}; return; }
}
static void spawnDust(float x, float y, int n, float sp, int kind = 0) {
    int cnt = (n * cfg.particleFx) / 3; if (cnt < 1 && cfg.particleFx > 0) cnt = 1;
    for (int i = 0; i < cnt; i++) spawnPart(x + (rnd(20) - 10), y - rnd(4), ((rnd(100) - 50) / 50.0f) * sp, -(rnd(100) / 100.0f) * 1.8f, 16 + rnd(12), 6 + rnd(5), kind);
}
static void spawnShot(float sx, float sy, float svx, float svy, int kind) {
    for (int i = 0; i < MAX_SHOTS; i++) if (!shots[i].active) { shots[i] = {sx, sy, svx, svy, kind, true}; return; }
}

static void respawn() {
    px = cpX; py = cpY; vx = vy = 0; squashX = squashY = 1.0f; onGround = inWater = false; wallLock = coyote = dashTimer = dashCd = 0; slamActive = false; jumpsLeft = 2;
    maxHp = (cfg.difficulty == 0 ? 5 : (cfg.difficulty == 2 ? 2 : 3)) + save.upHp; hp = maxHp; invulnTimer = 40;
    if (save.skillMask & 8u) shieldTimer = 600;
    roomIdx = iclamp(((int)px + PW / 2) / FW, 0, ROOMS - 1);
    if (bossActive && bossHP > 0) { bossX = (float)(21 * TILE); bossY = (float)((ROWS - 1) * TILE - bossH); for (int i = 0; i < MAX_SHOTS; i++) shots[i].active = false; }
    hudNeed[0] = hudNeed[1] = true;
}

static void hurtPlayer(int dmg) {
    if (invulnTimer > 0 || deadTimer > 0) return;
    if (shieldTimer > 0) { shieldTimer = 0; invulnTimer = 45; snd(5, 1); triggerShake(8, 5); hudNeed[0] = hudNeed[1] = true; return; }
    hp -= dmg; invulnTimer = 60; hudNeed[0] = hudNeed[1] = true; triggerShake(12, 8); triggerRumble(14, 180, 90);
    spawnDmgNum(px + 10, py - 10, dmg, mk(255, 65, 65));
    if (hp <= 0) {
        snd(6, 1); int bc = cfg.bloodParts * 7;
        for (int i = 0; i < bc; i++) spawnPart(px + 22, py + 22, (rnd(180) - 90) / 10.0f, (rnd(140) - 120) / 10.0f, 90, 5 + rnd(5), 1);
        vx = vy = 0; deadTimer = 30;
    } else { vy = -10.0f; vx = -facing * 5.0f; snd(6, 1); }
}

static void grantXP(int amt) {
    save.xp += amt;
    if (save.xp >= save.charLevel * 50) { save.xp -= save.charLevel * 50; save.charLevel++; save.skillPoints++; snd(1, 0); }
}

static void killEnemy(Enemy &e) {
    e.active = false; snd(7, 2); triggerShake(6, 4); triggerRumble(8, 90, 40);
    save.enemiesKilled++; grantXP(15);
    if ((save.skillMask & 4u) && hp < maxHp && (save.enemiesKilled % 3) == 0) hp++;
    if (!questDone && ++questKills >= 5) { questDone = 1; save.coins += 40; showToast(T_Q_DONE); writeSave(); }
    int bc = cfg.bloodParts * 7;
    for (int k = 0; k < bc; k++) spawnPart(e.x + 22, e.y + 22, (rnd(180) - 90) / 10.0f, -(45 + rnd(95)) / 10.0f, 110, 6 + rnd(4), 1);
}

static void damageBoss(int dmg) {
    if (bossHurt > 0 || bossHP <= 0) return;
    bossHP = iclamp(bossHP - dmg, 0, bossMaxHP); bossHurt = 22; hudNeed[0] = hudNeed[1] = true; snd(7, 2); triggerShake(10, 7); triggerRumble(12, 160, 80);
    spawnDmgNum(bossX + bossW * 0.5f, bossY - 10, dmg, mk(255, 225, 75));
    bossPhase = (bossHP * 3 <= bossMaxHP) ? 3 : ((bossHP * 3 <= bossMaxHP * 2) ? 2 : 1);
    int bc = cfg.bloodParts * 7;
    for (int i = 0; i < bc; i++) spawnPart(bossX + bossW * 0.5f, bossY + bossH * 0.5f, (rnd(200) - 100) / 10.0f, -(40 + rnd(100)) / 10.0f, 90, 6 + rnd(5), 1);
    if (bossHP <= 0) {
        for (int i = 0; i < MAX_SHOTS; i++) shots[i].active = false;
        save.coins += 60; grantXP(80); unlockAch(2); if (save.coins >= 50) unlockAch(3);
        if (curStage >= 15) save.ngPlusUnlocked = 1;
        writeSave(); level[16][15] = 'G'; needFull[0] = needFull[1] = true; triggerShake(24, 12);
    }
}

static void firePlayerWeapon() {
    if (atkCd > 0) return;
    atkCd = curWeapon == 1 ? 16 : 24; snd(5, 1);
    for (int i = 0; i < MAX_PBULLETS; i++) if (!pbullets[i].active) {
        pbullets[i] = {px + (facing > 0 ? PW : -12), py + 18, facing * 13.5f, 0, curWeapon, curWeapon == 2 ? 2 : 1, 45, true};
        return;
    }
}

static void update() {
    stageTimeFrames++; animTick++;
    if (invulnTimer > 0) invulnTimer--; if (dashCd > 0) dashCd--; if (atkCd > 0) atkCd--;
    if (slamShockTimer > 0) slamShockTimer--; if (shieldTimer > 0) shieldTimer--; if (speedTimer > 0) speedTimer--;
    if (shakeTimer > 0) shakeTimer--;
    if (escapeTimer > 0 && --escapeTimer == 0) hurtPlayer(99);
    if (toastTimer > 0 && --toastTimer == 0) hudNeed[0] = hudNeed[1] = true;
    if (npcDialogTimer > 0 && --npcDialogTimer == 0) hudNeed[0] = hudNeed[1] = true;
    for (int i = 0; i < 8; i++) if (ghosts[i].life > 0) ghosts[i].life--;
    for (int i = 0; i < MAX_DMGNUM; i++) if (dmgNums[i].life > 0) { dmgNums[i].y += dmgNums[i].vy; dmgNums[i].life--; }

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
        if (in.l1New) { curWeapon = (curWeapon + 1) % ((save.skillMask & 2u) ? 3 : 2); hudNeed[0] = hudNeed[1] = true; snd(0, 1); }
        if (in.squareNew && onGround && curWeapon > 0) firePlayerWeapon();

        inWater = tileAt((int)(py + PH * 0.5f) / TILE, (int)(px + PW * 0.5f) / TILE) == '~';
        bool wasG = onGround;
        float k = onGround ? 0.0f : (iabs((int)vy) / 18.0f > 1.0f ? 1.0f : iabs((int)vy) / 18.0f);
        squashX += (1.0f - 0.22f * k - squashX) * 0.3f; squashY += (1.0f + 0.30f * k - squashY) * 0.3f;
        int dir = cfg.moveMode == 0 ? (in.ana != 0.0f ? (in.ana > 0 ? 1 : -1) : 0) : ((in.dright ? 1 : 0) - (in.dleft ? 1 : 0));
        float sc = cfg.moveMode == 0 && in.ana != 0.0f ? (fabsf(in.ana) < 0.5f ? 0.5f : fabsf(in.ana)) : 1.0f;
        bool wL = rectSolid((int)px - 1, (int)py, 1, PH), wR = rectSolid((int)px + PW, (int)py, 1, PH);
        if (dir) facing = dir;

        if (in.dashNew && dashCd == 0 && !slamActive) {
            dashTimer = 11; dashCd = (save.upDash || (save.skillMask & 1u)) ? 20 : 38;
            vx = (float)(facing * 15.5f); vy = 0.0f; squashX = 1.38f; squashY = 0.68f; snd(5, 1); spawnDust(px + 22, py + 22, 8, 3.5f, 2);
        }
        if (!onGround && in.down && in.squareNew && !slamActive) {
            slamActive = true; dashTimer = 0; vx = 0; vy = 22.0f; squashX = 0.65f; squashY = 1.40f; snd(5, 1);
        }
        if (dashTimer > 0) {
            dashTimer--; vy = 0.0f;
            if (cfg.particleFx >= 2 && (dashTimer & 1) == 0) for (int g = 0; g < 8; g++) if (ghosts[g].life <= 0) { ghosts[g] = {px, py, 10, facing}; break; }
        } else if (slamActive) {
            vy = 22.0f; vx = 0;
        } else {
            float maxS = ((in.square ? 8.0f : 6.0f) + (speedTimer > 0 ? 2.2f : 0.0f)) * (inWater ? 0.65f : 1.0f) * sc;
            if (wallLock > 0) wallLock--;
            else if (dir) { vx += dir * (onGround ? 1.35f : 0.78f); if (vx > maxS) vx = maxS; if (vx < -maxS) vx = -maxS; }
            else { float fr = onGround ? 1.1f : 0.3f; vx = vx > fr ? vx - fr : (vx < -fr ? vx + fr : 0); }
            float grav = inWater ? 0.35f : 0.88f, maxFall = inWater ? 6.0f : 18.0f;
            vy = vy + grav > maxFall ? maxFall : vy + grav;
            bool slide = !onGround && !inWater && vy > 0 && ((wL && dir < 0) || (wR && dir > 0));
            if (slide && vy > 3.0f) vy = 3.0f;
            if (slide && (tick & 3) == 0) spawnDust(wL ? px : px + PW, py + PH - 6.0f, 1, 1.0f);
        }
        if (onGround || inWater) { coyote = 6; jumpsLeft = 2; if (onGround && dir != 0 && (tick % 14) == 0) snd(11, 1); }
        else if (coyote > 0) coyote--;

        if (in.crossNew && !slamActive) {
            if (coyote > 0 || inWater) { vy = inWater ? -10.5f : -16.8f; coyote = 0; jumpsLeft = 1; squashX = 0.72f; squashY = 1.30f; spawnDust(px + 22, py + PH, 6, 3.0f); burst = true; snd(4, 1); }
            else if (wL || wR) { vy = -16.0f; vx = wL ? 8.2f : -8.2f; wallLock = 9; facing = wL ? 1 : -1; jumpsLeft = 1; squashX = 0.72f; squashY = 1.30f; spawnDust(wL ? px : px + PW, py + 22, 5, 2.2f); burst = true; snd(4, 1); }
            else if (jumpsLeft > 0) { vy = -15.6f; jumpsLeft--; dashTimer = 0; squashX = 0.70f; squashY = 1.32f; spawnDust(px + 22, py + PH, 9, 4.0f, 2); burst = true; snd(4, 1); }
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
        // One-way Bridges '='
        if (vy >= 0 && !in.down) {
            int brR = (int)(py + PH) / TILE;
            if (brR >= 0 && brR < ROWS && (tileAt(brR, (int)(px + 8) / TILE) == '=' || tileAt(brR, (int)(px + PW - 8) / TILE) == '=')) {
                if (prevBot <= brR * TILE + 10) { py = (float)(brR * TILE - PH); vy = 0; onGround = true; }
            }
        }
        int rmX = roomIdx * FW;
        if (vy >= 0) {
            for (int i = 0; i < mplatCount; i++) { MPlat &m = mplats[i]; if (m.room == roomIdx && px + PW > rmX + m.x + 2 && px < rmX + m.x + m.w - 2 && prevBot <= m.y + 10 && py + PH >= m.y) { py = m.y - PH; vy = 0; onGround = true; px += m.vx; } }
            for (int i = 0; i < dplatCount; i++) { DPlat &d = dplats[i]; if (d.room == roomIdx && d.state < 2 && px + PW > rmX + d.x + 2 && px < rmX + d.x + d.w - 2 && prevBot <= d.y + 10 && py + PH >= d.y) { py = (float)(d.y - PH); vy = 0; onGround = true; if (d.state == 0) { d.state = 1; d.timer = 0; } } }
        }
        if (onGround && slamActive) {
            slamActive = false; slamShockTimer = 16; triggerShake(12, 9); triggerRumble(12, 180, 90); snd(10, 1); spawnDust(px + 22, py + PH, 14, 6.0f, 2); burst = true;
            int sRad = save.upSlam ? 210 : 140;
            for (int i = 0; i < enemyCount; i++) if (enemies[i].active && iabs((int)(enemies[i].x - px)) < sRad && iabs((int)(enemies[i].y - py)) < 90) { spawnDmgNum(enemies[i].x, enemies[i].y, 2, mk(255,230,90)); killEnemy(enemies[i]); }
            if (bossActive && bossHP > 0 && iabs((int)(bossX - px)) < sRad + 40 && iabs((int)(bossY - py)) < 110) damageBoss(2);
        } else if (onGround && !wasG && vy > 4.0f) { squashY = 0.72f; squashX = 1.32f; spawnDust(px + 22, py + PH, 6, 3.5f); burst = true; }
    }

    // Living Grass (Respects Grass Animation setting!), Leaves & Fireflies
    float pcx = px - roomIdx * FW + PW * 0.5f, pby = py + PH;
    if (cfg.grassAnim > 0) {
        for (int i = 0; i < plantCount; i++) {
            Plant &p = plants[i]; if (p.room != roomIdx) continue;
            float wind = (cfg.grassAnim >= 1) ? SN[((tick / 12) + (p.x / 60) + p.seed) & 15] / 256.0f * 1.8f : 0.0f;
            if (cfg.grassAnim >= 2 && deadTimer == 0) {
                float dx = pcx - p.x, dy = pby - p.y, adx = fabsf(dx);
                if (adx < 46.0f && dy > -22.0f && dy < 32.0f) { float pr = 1.0f - adx / 46.0f; p.vel += vx * 0.40f * pr; wind += (dx < 0 ? 7.0f : -7.0f) * pr; if (burst) p.vel += (dx < 0 ? 5.0f : -5.0f) * pr; }
            }
            p.vel = (p.vel + (wind - p.angle) * 0.18f) * 0.78f; p.angle = iclamp((int)((p.angle + p.vel) * 10), -120, 120) / 10.0f;
        }
    }
    if (cfg.ambientFx > 0) {
        for (int i = 0; i < MAX_LEAVES; i++) { Leaf &a = leaves[i]; a.x += a.vx + CS[((tick / 7) + a.seed * 3) & 15] / 256.0f * 0.6f; a.y += a.vy; if (a.x > FW - 20) a.x = 20; else if (a.x < 20) a.x = FW - 24; if (a.y > FH - 120) a.y = 95; else if (a.y < 90) a.y = FH - 130; }
        for (int i = 0; i < MAX_FIREFLIES; i++) { Firefly &f = flies[i]; f.x += f.vx + SN[((tick / 9) + f.phase) & 15] / 256.0f * 0.8f; f.y += f.vy + CS[((tick / 11) + f.phase) & 15] / 256.0f * 0.6f; if (f.x < 40 || f.x > FW - 40) f.vx = -f.vx; if (f.y < 180 || f.y > FH - 160) f.vy = -f.vy; }
    }
    if (deadTimer > 0) return;

    int rmX = roomIdx * FW;
    // Player Bullets
    for (int i = 0; i < MAX_PBULLETS; i++) if (pbullets[i].active) {
        PBullet &b = pbullets[i]; b.x += b.vx; b.y += b.vy;
        if (--b.life <= 0 || solidAt((int)b.x, (int)b.y)) { b.active = false; continue; }
        for (int j = 0; j < enemyCount; j++) if (enemies[j].active && rectOverlap((int)b.x - 8, (int)b.y - 8, 16, 16, (int)enemies[j].x, (int)enemies[j].y, 44, 44)) {
            if (enemies[j].kind == E_BEETLE && (b.vx * enemies[j].dir < 0)) { snd(3, 2); b.active = false; break; } // Armored front blocks!
            enemies[j].hp -= b.dmg; enemies[j].hurt = 10; spawnDmgNum(enemies[j].x, enemies[j].y, b.dmg, mk(125,245,255));
            if (enemies[j].hp <= 0) killEnemy(enemies[j]); else snd(7, 2);
            b.active = false; break;
        }
        if (b.active && bossActive && bossHP > 0 && rectOverlap((int)b.x - 8, (int)b.y - 8, 16, 16, (int)bossX, (int)bossY, bossW, bossH)) { damageBoss(b.dmg); b.active = false; }
    }

    // Checkpoints, Coins, Chests, Powerups, NPCs & Puzzle Switch 'S'
    for (int i = 0; i < checkCount; i++) if (!checkAct[i] && (int)px + PW >= checkC[i] * TILE) { checkAct[i] = true; cpX = (float)(checkC[i] * TILE); cpY = (float)(checkR[i] * TILE - PH); snd(1, 0); }
    int magRad = save.upMagnet ? 160 : 36;
    for (int i = 0; i < coinCount; i++) if (coins[i].active && coins[i].room == roomIdx) {
        float dx = (px + 22) - (rmX + coins[i].x), dy = (py + 22) - coins[i].y;
        if (fabsf(dx) < magRad && fabsf(dy) < magRad) {
            if (save.upMagnet && (fabsf(dx) > 26 || fabsf(dy) > 26)) { coins[i].x += (short)(dx > 0 ? 5 : -5); coins[i].y += (short)(dy > 0 ? 5 : -5); }
            else { coins[i].active = false; save.coins++; snd(9, 0); hudNeed[0] = hudNeed[1] = true; if (save.coins >= 50) unlockAch(3); }
        }
    }
    for (int i = 0; i < chestCount; i++) if (!chests[i].opened && chests[i].room == roomIdx && rectOverlap((int)px, (int)py, PW, PH, rmX + chests[i].x, chests[i].y, 44, 34)) {
        chests[i].opened = true; save.coins += 25; snd(1, 0); triggerShake(8, 5); unlockAch(1);
        if (chests[i].secret) { save.secretsFound++; unlockAch(4); showToast(T_SEC_FND); } else showToast(T_CHEST);
        if (save.coins >= 50) unlockAch(3); writeSave();
    }
    for (int i = 0; i < puCount; i++) if (powerups[i].active && powerups[i].room == roomIdx && rectOverlap((int)px, (int)py, PW, PH, rmX + powerups[i].x, powerups[i].y, 28, 28)) {
        powerups[i].active = false; snd(1, 0); if (powerups[i].kind == PU_HEART && hp < maxHp) hp++; else if (powerups[i].kind == PU_SHIELD) shieldTimer = 600; else speedTimer = 480; hudNeed[0] = hudNeed[1] = true;
    }
    for (int i = 0; i < npcCount; i++) if (npcs[i].room == roomIdx && in.triNew && iabs((int)px - (rmX + npcs[i].x)) < 90 && iabs((int)py - npcs[i].y) < 80) {
        npcDialogTid = npcs[i].dialogTid; npcDialogTimer = 240; snd(0, 0); hudNeed[0] = hudNeed[1] = true;
    }

    // 6 Distinct Enemy AI Behaviors
    int rmX0 = rmX - 240, rmX1 = rmX + FW + 240;
    for (int i = 0; i < enemyCount; i++) {
        Enemy &e = enemies[i]; if (!e.active || (int)e.x < rmX0 || (int)e.x > rmX1) continue;
        if (e.hurt > 0) e.hurt--; e.timer++;
        float dx = px - e.x, adx = fabsf(dx), ady = fabsf(py - e.y);
        if (e.kind == E_SPIDER) {
            // Climbs/drops from ceiling on silk web thread when player walks underneath!
            if (adx < 160 && e.y < e.baseY + 260) e.y += 4.5f; else if (e.y > e.baseY) e.y -= 2.2f;
        } else if (e.kind == E_BEETLE) {
            // Armored Beetle: patrols steadily; front shell blocks attacks, weak from above/slam/behind
            e.x += e.vx * e.dir; if (e.x <= e.minX) { e.x = e.minX; e.dir = 1; } else if (e.x >= e.maxX) { e.x = e.maxX; e.dir = -1; }
        } else if (e.kind == E_GHOST) {
            // Ghost: fades invisible & phases toward player
            e.state = ((e.timer / 75) & 1); // 0=visible, 1=faded
            e.dir = dx >= 0 ? 1 : -1; e.x += e.dir * 1.4f; e.y = e.baseY + (SN[(e.timer / 5) & 15] * 36) / 256;
        } else if (e.kind == E_LIZARD) {
            // Fire Lizard: spits fireballs at player
            e.dir = dx >= 0 ? 1 : -1;
            if (e.timer % 85 == 0 && adx < 520) spawnShot(e.x + 22, e.y + 18, e.dir * 6.5f, -1.2f, 0);
        } else if (e.kind == E_DRONE) {
            // Flying Drone: patrols high & drops laser shots from above
            e.x += e.vx * e.dir; if (e.x <= e.minX) { e.x = e.minX; e.dir = 1; } else if (e.x >= e.maxX) { e.x = e.maxX; e.dir = -1; }
            if (adx < 90 && e.timer % 48 == 0) spawnShot(e.x + 22, e.y + 38, 0, 7.2f, 2);
        } else if (e.kind == E_MIMIC) {
            // Mimic: disguised as treasure chest until player gets close, then lunges!
            if (e.state == 0 && adx < 170 && ady < 100) { e.state = 1; snd(6, 2); }
            if (e.state == 1) {
                e.dir = dx >= 0 ? 1 : -1; e.x = iclamp((int)(e.x + e.dir * 3.0f), (int)e.minX, (int)e.maxX);
                if (e.timer % 45 == 0 && e.y >= e.baseY - 2) e.vy = -11.5f;
                e.vy += 0.65f; e.y += e.vy; if (e.y >= e.baseY) { e.y = e.baseY; e.vy = 0; }
            }
        }
        if (e.kind == E_GHOST && e.state == 1) continue; // Intangible when faded
        if (rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, (int)e.x + 4, (int)e.y + 4, 36, 36)) {
            if ((vy > 0 && py + PH < e.y + 30) || dashTimer > 0 || slamActive) {
                if (vy > 0 && !slamActive) { py = e.y - PH; vy = -15.5f; jumpsLeft = 1; squashX = 0.72f; squashY = 1.28f; }
                e.hp--; e.hurt = 14; spawnDmgNum(e.x, e.y, 1, mk(255,230,90));
                if (e.hp <= 0) killEnemy(e); else snd(7, 2);
            } else { hurtPlayer(1); return; }
        }
    }
    for (int i = 0; i < MAX_SHOTS; i++) {
        Shot &s = shots[i]; if (!s.active) continue; s.x += s.vx; s.y += s.vy; if (s.kind == 1) s.vy += 0.14f;
        if (s.x < TILE || s.x > levelPW - TILE || s.y < TILE || s.y > FH - TILE || solidAt((int)s.x, (int)s.y)) { s.active = false; continue; }
        if (rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, (int)s.x - 14, (int)s.y - 14, 28, 28)) { s.active = false; hurtPlayer(1); return; }
    }

    // 4 Unique 3-Phase Bosses (Spider King, Lava Titan, Ice Dragon, Cursed Knight)
    if (bossActive && bossHP > 0) {
        if (bossIntro > 0) { bossIntro--; triggerShake(2, 3); return; }
        if (bossHurt > 0) bossHurt--; if (bossSwCd > 0) bossSwCd--; bossTimer++;
        float floorY = (float)((ROWS - 1) * TILE - bossH), pSpd = 0.9f + bossPhase * 0.28f;
        if (bossType == 0) { // Spider King: Phase1 ceiling drop, Phase2 web spread, Phase3 rapid swoop
            bossX += bossVX * pSpd; if (bossX < 4 * TILE) { bossX = 4 * TILE; bossVX = 4.2f; } else if (bossX > 24 * TILE) { bossX = 24 * TILE; bossVX = -4.2f; }
            bossY = 230.0f + (SN[(bossTimer / (6 - bossPhase)) & 15] * (120 + bossPhase * 30)) / 256;
            if (bossTimer % (78 - bossPhase * 12) == 0) {
                spawnShot(bossX + 64, bossY + bossH, (px < bossX ? -5.0f : 5.0f), 3.0f, 1);
                if (bossPhase >= 2) spawnShot(bossX + 64, bossY + bossH, 0, 5.5f, 1);
            }
        } else if (bossType == 1) { // Lava Titan: Phase1 stomps, Phase2 lava arcs, Phase3 meteor rain
            bossX += bossVX * (pSpd * 0.85f); if (bossX < 4 * TILE) { bossX = 4 * TILE; bossVX = 3.8f; } else if (bossX > 24 * TILE) { bossX = 24 * TILE; bossVX = -3.8f; }
            if (bossTimer % (95 - bossPhase * 10) == 20 && bossY >= floorY - 2) { bossVY = -15.5f; triggerShake(10, 6); }
            bossVY += 0.65f; bossY += bossVY; if (bossY >= floorY) { bossY = floorY; bossVY = 0; }
            if (bossTimer % (70 - bossPhase * 10) == 0) {
                spawnShot(bossX + 64, bossY + 20, (px < bossX ? -6.2f : 6.2f), -3.5f, 1);
                if (bossPhase == 3) spawnShot((float)(TILE * (6 + rnd(20))), 80, 0, 6.2f, 0);
            }
        } else if (bossType == 2) { // Ice Dragon: Phase1 high swoops, Phase2 frost breath, Phase3 dive blitz
            bossX += bossVX * (pSpd * 1.1f); if (bossX < 3 * TILE) { bossX = 3 * TILE; bossVX = 4.8f; } else if (bossX > 25 * TILE) { bossX = 25 * TILE; bossVX = -4.8f; }
            bossY = (bossPhase == 3 && (bossTimer % 100 > 65)) ? floorY - 40 : 210.0f + (SN[(bossTimer / 5) & 15] * 90) / 256;
            if (bossTimer % (68 - bossPhase * 10) == 0) spawnShot(bossX + 64, bossY + 56, (px < bossX ? -7.5f : 7.5f), 1.8f, 2);
        } else { // Cursed Knight: Phase1 dash slash, Phase2 shadow clones/shots, Phase3 teleport strike
            if (bossPhase == 3 && bossTimer % 110 == 0) { bossX = iclamp((int)px + (px > 960 ? -260 : 260), 4 * TILE, 24 * TILE); }
            bossX += bossVX * pSpd; if (bossX < 4 * TILE) { bossX = 4 * TILE; bossVX = 4.6f; } else if (bossX > 24 * TILE) { bossX = 24 * TILE; bossVX = -4.6f; }
            bossVY += 0.65f; bossY += bossVY; if (bossY >= floorY) { bossY = floorY; bossVY = 0; }
            if (bossTimer % (65 - bossPhase * 10) == 0) { float d = px < bossX ? -1.0f : 1.0f; spawnShot(bossX + 64, bossY + 60, d * 7.5f, 0, 0); if (bossPhase >= 2) spawnShot(bossX + 64, bossY + 28, d * 6.0f, -2.5f, 0); }
        }
        if (bossSwCd == 0 && rectOverlap((int)px, (int)py, PW, PH, 15 * TILE, 6 * TILE + 20, 120, 40)) { bossSwCd = 130; vy = -12.0f; damageBoss(2); return; }
        if (rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, (int)bossX + 8, (int)bossY + 8, bossW - 16, bossH - 16)) {
            if ((vy > 0 && py + PH < bossY + 52) || slamActive) { py = bossY - PH; vy = -16.5f; jumpsLeft = 1; slamActive = false; damageBoss(2); }
            else if (bossHurt == 0) { hurtPlayer(1); return; }
        }
    }

    int c0 = iclamp((int)px / TILE - 2, 0, COLS - 1), c1 = iclamp(((int)px + PW) / TILE + 2, 0, COLS - 1);
    for (int r = 0; r < ROWS; r++) for (int c = c0; c <= c1; c++) {
        char ch = level[r][c];
        if (ch == 'S' && !gateOpen && rectOverlap((int)px, (int)py, PW, PH, c * TILE, r * TILE, TILE, TILE)) {
            gateOpen = true; snd(1, 0); triggerShake(10, 6); needFull[0] = needFull[1] = true;
        }
        if ((ch == '^' && rectOverlap((int)px + 4, (int)py + 4, PW - 8, PH - 8, c * TILE + 14, r * TILE + 26, 32, 34)) ||
            (ch == 'O' && circleHit(c * TILE + 30, r * TILE + 30, 24, (int)px + 4, (int)py + 4, PW - 8, PH - 8))) { hurtPlayer(1); return; }
        if (ch == 'G' && rectOverlap((int)px, (int)py, PW, PH, c * TILE + 8, r * TILE + 16, PW, PH)) {
            winTimer = 140; needWin[0] = needWin[1] = true; snd(8, 0); grantXP(40);
            if (curStage == 0) unlockAch(0);
            if (timeTrialMode) { unlockAch(5); if (save.bestTime[curStage] == 0 || stageTimeFrames < save.bestTime[curStage]) save.bestTime[curStage] = stageTimeFrames; }
            if (curStage + 2 > save.unlockedStages && save.unlockedStages < TOTAL_STAGES) save.unlockedStages = curStage + 2;
            writeSave(); return;
        }
    }
    roomIdx = iclamp(((int)px + PW / 2) / FW, 0, ROOMS - 1);
}
// ====== نهاية الجزء 1 (الصق الجزء 2 أسفل هذا السطر مباشرة) ======
// ====== بداية الجزء 2: الجرافيكس، القائمة السرية، واجهة Settings الاحترافية و main() ======
static void drawNarutoHD(int idx, int x, int y, int ps, int dirFace) {
    static const char *SPR[5][32] = {
        { "......KKKKKKKK......",".....KNNNNNNNNK.....","....KNNKOOOOOKNK....","....KNKOoYYoOOKK....","...KNNKOoKKoOOKK....","...KNNKOYOOoOOKK....","....KNKOoOOoOOKK....",".....KKOOOOOOKK.....",
          "......KKKKKKKK......",".....KNNNRRNNNK.....","....KNNNNRRNNNNK....","...KNNnNNRRNNnNNK...","...KNNnNNRRNNnNNK...","..KNNnNWWWWWNNnNNK..","..KNNnWRRRRRWNnNNK..",".KNNnNWRRRRRWNnNNK..",
          ".KNNnNNWWWWWNNnNNK..",".KRRnNNNNRRNNNNnNK..",".KNNnNNNNRRNNNNnNK..",".KNNnNNNNRRNNWWNnK..","..KNnNNNNRRNWRRNnK..","..KNnNNWWWRNWRRNnK..","..KnnNWRRRRNWWNnK...","..KnnNWRRRRNNNnNK...",
          "..KNNNNWWWNNNNnNK...","...KNNNKKKKNNnNK....","....KKK....KKKK.....",".....KWWK..KWWK.....",".....KWWK..KWWK.....","....KNNNK..KNNNK....","....KSSSK..KSSSK....","....KKKKK..KKKKK...." },
        { ".......K..K..K......","......KOKKOKKOK.....",".....KOOOKOOOKOK....","....KOOYOOOYOOOK....","...KOOYOOOOOOYOOK...","....KOOOOOOOOOOK....","....KKBBBBBBBBKK....","....KKSBKBBKBSKK....",
          "....KSSPPSSPPSSK....",".....KSSKSSKSSK.....","......KSSSSSSK......",".....KNNNRRNNNK.....","....KNNNNRRNNNNK....","...KNNnNNRRNNnNNK...","...KNNnNNRRNNnNNK...","..KNNnNWWWWWNNnNNK..",
          "..KNNnWRRRRRWNnNNK..",".KNNnNWRRRRRWNnNNK..",".KRRnNNWWWWWNNnNNK..",".KNNnNNNNRRNNNNnNK..","..KNnNNNNRRNNWWNnK..","..KNnNNWWWRNWRRNnK..","..KnnNWRRRRNWWNnK...","..KnnNWRRRRNNNnNK...",
          "..KNNNNWWWNNNNnNK...","...KNNNKKKKNNnNK....","....KKK....KKKK.....",".....KWWK..KWWK.....",".....KWWK..KWWK.....","....KNNNK..KNNNK....","....KSSSK..KSSSK....","....KKKKK..KKKKK...." },
        { "......KKKKKKKK......",".....KNNNNNNNNK.....","....KNNnNNNNnNNK....","....KNNBBBBBBNNK....","...KNNnBKBBKBnNNK...","...KNNKSSSSSSKNNK...","...KNNKSRSSRSKNNK...","...KNNKSSSSSSKNNK...",
          "....KNKKSSSSKKNK....",".....KNNNRRNNNK.....","....KNNNNRRNNNNK....","...KNNnNNRRNNnNNK...","...KNNnNNRRNNnNNK...","..KNNnNWWWWWNNnNNK..","..KNNnWRRRRRWNnNNK..",".KNNnNWRRRRRWNnNNK..",
          ".KRRnNNWWWWWNNnNNK..",".KSSnNNNNRRNNNNnNK..",".KKKnNNNNRRNNWWNnK..","..KNnNNNNRRNWRRNnK..","..KNnNNWWWRNWRRNnK..","..KnnNWRRRRNWWNnK...","..KnnNWRRRRNNNnNK...","..KNNNNWWWNNNNnNK...",
          "...KNNNKKKKNNnNK....","....KKK....KKKK.....",".....KWWK..KWWK.....",".....KWWK..KWWK.....",".....KWWK..KWWK.....","....KNNNK..KNNNK....","....KSSSK..KSSSK....","....KKKKK..KKKKK...." },
        { ".....K..KKKK..K.....","....KNKNNNNNNKNK....","...KNNNNNNNNNNNNK...","..KNNnNNNNNNNNnNNK..",".KNNnNNKSSSSKNNnNNK.","..KNNnKSRSSRSKnNNK..",".KNNnNKSSSSSSKnNNK..","..KNNnKKSSSSKKnNNK..",
          ".KNNnKMMMMMMMMKnNK..","..KNnKMmMMMMmMKnNK..",".KNNnKMMMMMMMMKnNK..","..KNnKMMKMMKMMKnNK..",".KNNnKMMMMMMMMKnNK..","..KNnKNNMMMMNNKnNK..",".KNNnKSKMMMMKKnNNK..","..KNnKKKmmmmKKnNNK..",
          ".KNNnKMMMMMMMMKnNK..","..KNnKMmMMMMmMKnNK..","..KNNKMMMMMMMMKNNK..","...KKKMmMMMMmMKKK...","....KKMMMMMMMMKK....","....KNNKMMMMKNNK....","....KNNKmmmmKNNK....","....KNNK.KK.KNNK....",
          "....KNNK....KNNK....","....KNNK....KNNK....",".....KWWK..KWWK.....",".....KWWK..KWWK.....",".....KWWK..KWWK.....","....KNNNK..KNNNK....","....KSSSK..KSSSK....","....KKKKK..KKKKK...." },
        { "........K..K........","......KWKWWKWK......",".....KWWWWWWWWK.....","....KWWwWWWWwWWK....","...KWWWWWWWWWWWWK...","....KwwWWWWWWwwK....","....KKNNNNBBBBKK....","....KNKSSKBBKBNK....",
          "....KNKSSKNNNNNK....",".....KKNNNNNNNKK....",".....KGGGGGGGGGK....","....KGGgGGGGgGGGK...","...KNNKGGGGGGGKNNK..","..KNRRKGGKKGGKKNNK..","..KNRRKGgKKgGGKNNK..","..KNNNKGGGGGGGKNNK..",
          "..KNNNKGGggGGGKNNK..","..KNNNKGGGGGGGKNNK..","...KSSKNNNNNNNKSSK..","....KKKNNNNNNNKKK...","......KNNNNNNNK.....",".....KNNWWWWWNNK....",".....KNNNNNNNNNK....",".....KNNWWWWWNNK....",
          ".....KNNKK..KNNK....",".....KNNK....KNNK...",".....KWWK....KWWK...",".....KWWK....KWWK...",".....KWWK....KWWK...","....KNNNK....KNNNK..","....KSSSK....KSSSK..","....KKKKK....KKKKK.." }
    };
    int id = iclamp(idx, 0, 4);
    for (int r = 0; r < 32; r++) for (int c = 0; c < 20; c++) {
        char ch = SPR[id][r][c]; if (ch == '.') continue;
        int dc = (dirFace >= 0) ? c : (19 - c); Color col = mk(255,255,255);
        if (ch == 'K') col = mk(12,12,16); else if (ch == 'N') col = mk(24,26,38); else if (ch == 'n') col = mk(44,48,66);
        else if (ch == 'W') col = mk(228,232,244); else if (ch == 'w') col = mk(168,174,192); else if (ch == 'B') col = mk(182,190,206);
        else if (ch == 'S') col = mk(238,188,152); else if (ch == 'R') col = mk(206,36,44); else if (ch == 'M') col = mk(164,34,42);
        else if (ch == 'm') col = mk(112,22,28); else if (ch == 'O') col = mk(242,112,24); else if (ch == 'o') col = mk(188,62,16);
        else if (ch == 'Y') col = mk(255,178,52); else if (ch == 'P') col = mk(168,124,224); else if (ch == 'G') col = mk(98,118,82); else if (ch == 'g') col = mk(68,84,56);
        fillRect(x + dc * ps, y + r * ps, ps, ps, col);
    }
}

static void drawSaw(int cx, int cy) {
    int a = (tick / 2) & 15, cc = CS[a], ss = SN[a];
    for (int dy = -26; dy <= 26; dy++) for (int dx = -26; dx <= 26; dx++) {
        int d2 = dx * dx + dy * dy; if (d2 > 676) continue;
        int u = (dx * cc + dy * ss) >> 8, v = (-dx * ss + dy * cc) >> 8;
        if (d2 < 36) scene->DrawPixel(cx + dx, cy + dy, mk(45,45,52));
        else if (d2 <= 400) scene->DrawPixel(cx + dx, cy + dy, (dx + dy < 0) ? mk(195,200,215) : mk(135,140,155));
        else if (iabs(u) < 6 || iabs(v) < 6 || iabs(u - v) < 8 || iabs(u + v) < 8) scene->DrawPixel(cx + dx, cy + dy, mk(235,238,248));
    }
}

static void drawFace(int x, int y, int d, Color ew) {
    fillRect(x + 9, y + 12, 10, 14, ew); fillRect(x + 25, y + 12, 10, 14, ew);
    int o = d > 0 ? 5 : 0; fillRect(x + 9 + o, y + 16, 5, 8, mk(0,0,0)); fillRect(x + 25 + o, y + 16, 5, 8, mk(0,0,0));
}

static void drawSkinCube(int dx, int dy, int dw, int dh, int dir, int skin) {
    if (skin >= 5 && skin <= 9) { drawNarutoHD(skin - 5, dx + (dw - 40) / 2, dy + dh - 64, 2, dir); return; }
    static const int BDR[5][3]={{125,0,14},{165,112,18},{32,24,52},{42,108,162},{120,24,12}};
    static const int BDY[5][3]={{208,26,36},{245,192,48},{62,48,96},{98,205,248},{242,92,28}};
    static const int HI[5][3] ={{248,96,108},{255,236,135},{112,92,162},{198,242,255},{255,186,88}};
    int s = iclamp(skin, 0, 4);
    fillRect(dx, dy, dw, dh, mk(BDR[s][0],BDR[s][1],BDR[s][2]));
    fillRect(dx + 3, dy + 3, dw - 6, dh - 6, mk(BDY[s][0],BDY[s][1],BDY[s][2]));
    fillRect(dx + 5, dy + 5, dw - 10, 7, mk(HI[s][0],HI[s][1],HI[s][2]));
    drawFace(dx + (dw - 44) / 2, dy + (dh - 44) / 2, dir, mk(255,255,255));
}

static void drawTileCell(int r, int cx, char ch, bool hi) {
    int x = cx * TILE, y = r * TILE, w = iclamp(curWorld, 0, 7);
    static const int MC[8][3]={{88,58,46},{62,36,44},{72,98,134},{58,46,84},{52,38,68},{78,84,96},{122,88,54},{54,88,92}};
    static const int TC[8][3]={{64,198,58},{245,98,36},{232,248,255},{158,112,205},{98,225,185},{215,175,55},{88,215,92},{84,215,195}};
    if (ch == '#') {
        fillRect(x, y, TILE, TILE, mk(MC[w][0], MC[w][1], MC[w][2]));
        if (cfg.worldDetails >= 1) fillRect(x + 6, y + 14, TILE - 12, TILE - 22, mk(MC[w][0] + 22, MC[w][1] + 18, MC[w][2] + 18));
        if (hi) {
            fillRect(x, y, TILE, 10, mk(TC[w][0], TC[w][1], TC[w][2]));
            if (cfg.worldDetails >= 2) { fillRect(x + 6, y + 10, 12, 4, mk(TC[w][0], TC[w][1], TC[w][2])); fillRect(x + 34, y + 10, 14, 5, mk(TC[w][0], TC[w][1], TC[w][2])); }
        }
        if (cfg.shadowQual >= 2) fillRect(x, y + TILE - 6, TILE, 6, mk(MC[w][0] / 2, MC[w][1] / 2, MC[w][2] / 2));
    } else if (ch == '=') {
        // Arched Ornamental Bridge (Inspired by Image 4!)
        fillRect(x, y, TILE, 12, mk(118, 82, 52)); fillRect(x, y, TILE, 4, mk(168, 124, 82));
        if (cfg.worldDetails >= 2) { fillRect(x + 6, y - 14, 5, 14, mk(72, 58, 48)); fillRect(x + 48, y - 14, 5, 14, mk(72, 58, 48)); fillRect(x, y - 14, TILE, 4, mk(98, 82, 68)); }
    } else if (ch == '~') {
        // Water Zone
        fillRect(x, y, TILE, TILE, mk(32, 128, 175));
        if (r > 0 && tileAt(r - 1, cx) != '~') fillRect(x, y, TILE, 7, mk(115, 225, 245));
    } else if (ch == 'S') {
        fillRect(x + 12, y + 40, 36, 20, gateOpen ? mk(65,235,115) : mk(235,65,55));
    } else if (ch == 'D' && !gateOpen) {
        fillRect(x + 16, y, 28, TILE, mk(98,108,125)); fillRect(x + 22, y + 10, 16, 40, mk(185,65,75));
    } else if (ch == '^') {
        int ctr = x + TILE / 2, top = y + 18;
        for (int j = 0; j < 42; j++) { int h = (j * 26) / 41; fillRect(ctr - h, top + j, h * 2 + 1, 1, mk(210, 215, 228)); }
    } else if (ch == 'G') {
        fillRect(x + 5, y + 13, PW + 6, PH + 6, mk(255,228,130)); fillRect(x + 8, y + 16, PW, PH, mk(255,140,170));
        fillRect(x + 8, y + 19, PW, 7, mk(240,230,200)); drawFace(x + 8, y + 16, -1, mk(255,255,255));
    }
}

static void drawPlantTuft(int bx, int by, int h, int bend, int bm, int st) {
    Color pd = bm == 1 ? mk(95,32,32) : (bm == 2 ? mk(105,175,220) : (bm == 4 ? mk(42,145,115) : mk(32,138,48)));
    Color pm = bm == 1 ? mk(242,102,32) : (bm == 2 ? mk(185,230,255) : (bm == 4 ? mk(105,245,195) : mk(78,218,76)));
    Color tip = st == 0 ? mk(255,95,135) : mk(255,235,85);
    for (int b = -1; b <= 1; b++) {
        int ox = bx + b * 5, bh = h - (b ? 2 : 0), sh = bh / 2, off = bend / 2 + b;
        fillRect(ox - 1, by - sh, 3, sh, pd); fillRect(ox - 1 + off, by - bh, 3, bh - sh, pm);
        if (b == 0 && cfg.worldDetails >= 2) fillRect(ox - 2 + off, by - bh - 4, 4, 4, tip);
    }
}

static void drawSplat(const Splat &s) {
    if (s.stage >= 4) return;
    int div = s.stage + 1, dw = iclamp((s.w * (4 - s.stage)) / 4, 2, 40), dh = iclamp((s.h * (4 - s.stage)) / 4, 2, 40);
    fillRect(s.x + (s.w - dw) / 2, s.y + (s.h - dh) / 2, dw, dh, mk(140 / div + 20, 10, 20));
}

// 8 Distinct World Backgrounds (Respects World Details & Lighting Quality!)
static void paintBackground() {
    int pFar = roomIdx * 85, pNear = roomIdx * 170;
    if (curWorld == 0) { // Haunted Forest (Image 1 & 4 style: moon, purple mist, gnarled trees)
        fillRect(0,0,FW,280,mk(38,44,78)); fillRect(0,280,FW,260,mk(78,64,108)); fillRect(0,540,FW,540,mk(44,32,58));
        if (cfg.lightingQual >= 1) fillCircle(1480, 160, 74, mk(225,238,242));
        if (cfg.worldDetails >= 1) for (int i = 0; i < 6; i++) {
            int tx = ((i * 340 - pNear) % FW + FW) % FW;
            fillRect(tx, 160, 56, 600, mk(52,32,64)); fillRect(tx + 10, 160, 14, 600, mk(78,52,92));
            if (cfg.worldDetails >= 2) { fillRect(tx - 40, 240, 120, 16, mk(52,32,64)); fillRect(tx + 35, 320, 90, 14, mk(52,32,64)); }
        }
    } else if (curWorld == 1) { // Volcano
        fillRect(0,0,FW,280,mk(42,14,24)); fillRect(0,280,FW,260,mk(86,26,32)); fillRect(0,540,FW,540,mk(38,14,20));
        if (cfg.worldDetails >= 1) for (int i = 0; i < 4; i++) { int x = ((i * 520 - pFar) % FW + FW) % FW; fillRect(x, 360, 200, 360, mk(54,18,26)); fillRect(x + 88, 380, 24, 340, mk(242,88,28)); }
    } else if (curWorld == 2) { // Ice
        fillRect(0,0,FW,280,mk(26,48,88)); fillRect(0,280,FW,260,mk(56,94,142)); fillRect(0,540,FW,540,mk(48,78,120));
        if (cfg.lightingQual >= 2) for (int i = 0; i < 6; i++) fillRect(i * 320 + 20, 120 + (i & 1) * 24, 240, 18, mk(72,224,182));
        if (cfg.worldDetails >= 1) for (int i = 0; i < 4; i++) { int x = ((i * 510 - pFar) % FW + FW) % FW; fillRect(x + 40, 390, 210, 330, mk(86,126,172)); fillRect(x + 70, 350, 150, 50, mk(236,248,255)); }
    } else if (curWorld == 3) { // Dark Castle
        fillRect(0,0,FW,300,mk(22,12,48)); fillRect(0,300,FW,780,mk(34,18,68));
        if (cfg.worldDetails >= 1) for (int i = 0; i < 6; i++) { int x = ((i * 340 - pNear) % FW + FW) % FW; fillRect(x, 220, 68, 680, mk(46,32,74)); fillRect(x + 140, 330, 56, 100, mk(145,58,185)); }
    } else if (curWorld == 4) { // Crystal Caves (Image 3 style: dark purple cavern + glowing teal crystals!)
        fillRect(0,0,FW,300,mk(22,16,38)); fillRect(0,300,FW,300,mk(42,24,68)); fillRect(0,600,FW,480,mk(28,18,46));
        if (cfg.worldDetails >= 1) for (int i = 0; i < 5; i++) {
            int cx = ((i * 420 - pFar) % FW + FW) % FW;
            fillRect(cx + 20, 320, 38, 220, mk(58,205,175)); fillRect(cx + 28, 320, 14, 220, mk(155,255,235));
            if (cfg.worldDetails >= 2) { fillRect(cx - 10, 400, 28, 140, mk(42,165,145)); fillRect(cx + 62, 380, 28, 160, mk(48,185,155)); }
        }
    } else if (curWorld == 5) { // Mechanical Factory
        fillRect(0,0,FW,300,mk(38,42,50)); fillRect(0,300,FW,780,mk(28,32,38));
        if (cfg.worldDetails >= 1) for (int i = 0; i < 6; i++) { int x = ((i * 340 - pNear) % FW + FW) % FW; fillRect(x, 100, 48, 800, mk(62,68,78)); fillCircle(x + 160, 360, 58, mk(98,86,62)); fillCircle(x + 160, 360, 28, mk(28,32,38)); }
    } else if (curWorld == 6) { // Floating Sky Islands
        fillRect(0,0,FW,400,mk(52,158,238)); fillRect(0,400,FW,680,mk(145,224,252));
        if (cfg.worldDetails >= 1) for (int i = 0; i < 5; i++) { int x = ((i * 410 - pFar) % FW + FW) % FW; fillRect(x, 260 + (i & 1) * 90, 180, 42, mk(105,74,48)); fillRect(x, 260 + (i & 1) * 90, 180, 12, mk(82,215,86)); }
    } else { // Water World & Sunken Arches (Image 2 style: teal pillars, arched ruins, waterfalls!)
        fillRect(0,0,FW,320,mk(18,52,68)); fillRect(0,320,FW,760,mk(24,78,92));
        if (cfg.worldDetails >= 1) for (int i = 0; i < 5; i++) {
            int x = ((i * 410 - pNear) % FW + FW) % FW;
            fillRect(x, 180, 54, 620, mk(36,118,126)); fillRect(x + 140, 180, 54, 620, mk(36,118,126)); fillRect(x, 180, 194, 48, mk(46,142,148));
            if (cfg.worldDetails >= 2) fillRect(x + 68, 420, 36, 380, mk(72,215,205));
        }
    }
}

static void restoreRect(int b, int x, int y, int w, int h, bool det) {
    if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
    if (x + w > FW) w = FW - x; if (y + h > FH) h = FH - y; if (w <= 0 || h <= 0) return;
    setClip(x, y, w, h); paintBackground();
    for (int r = y / TILE; r <= (y + h - 1) / TILE; r++) for (int c = x / TILE; c <= (x + w - 1) / TILE; c++)
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
    bool fl = cfg.showDmgInd && ((e.hurt & 2) != 0);
    if (e.kind == E_SPIDER) {
        fillRect(sx + 21, 60, 2, sy - 50, mk(220,225,235)); // Silk web thread!
        int lg = ((tick / 5) & 1) ? 4 : -4;
        fillRect(sx - 10, sy + 20 + lg, 14, 5, mk(32,36,44)); fillRect(sx + 40, sy + 20 - lg, 14, 5, mk(32,36,44));
        fillCircle(sx + 22, sy + 22, 16, fl ? mk(255,255,255) : mk(148,88,42)); fillRect(sx + 14, sy + 24, 6, 6, mk(235,24,36)); fillRect(sx + 24, sy + 24, 6, 6, mk(235,24,36));
    } else if (e.kind == E_BEETLE) {
        fillRect(sx, sy + 12, 44, 32, fl ? mk(255,255,255) : mk(78,92,135)); fillRect(sx + 4, sy + 6, 36, 12, mk(145,165,215)); // Heavy shell
        fillRect(e.dir > 0 ? sx + 38 : sx - 6, sy + 20, 10, 6, mk(235,195,75)); // Horn
        drawFace(sx, sy + 8, e.dir, mk(255,235,90));
    } else if (e.kind == E_GHOST) {
        if (e.state == 1) { fillRect(sx + 10, sy + 10, 24, 24, mk(135,145,185)); return; }
        fillCircle(sx + 22, sy + 20, 18, fl ? mk(255,255,255) : mk(225,235,255)); fillRect(sx + 4, sy + 20, 36, 22, mk(215,225,248));
        drawFace(sx, sy + 4, e.dir, mk(45,25,85));
    } else if (e.kind == E_LIZARD) {
        fillRect(sx, sy + 14, 44, 30, fl ? mk(255,255,255) : mk(215,62,32)); fillRect(sx + 6, sy + 8, 28, 10, mk(255,165,45));
        drawFace(sx, sy + 8, e.dir, mk(255,245,95));
    } else if (e.kind == E_DRONE) {
        fillRect(sx - 10, sy + 8, 64, 8, mk(115,125,145)); fillCircle(sx + 22, sy + 24, 15, fl ? mk(255,255,255) : mk(68,78,96));
        fillCircle(sx + 22, sy + 24, 7, mk(245,45,65));
    } else { // Mimic
        fillRect(sx, sy + 10, 44, 34, fl ? mk(255,255,255) : mk(168,104,42)); fillRect(sx, sy + 22, 44, 5, mk(245,195,55));
        if (e.state == 1) { fillRect(sx + 4, sy + 20, 36, 12, mk(185,22,42)); fillRect(sx + 8, sy + 20, 6, 6, mk(255,255,255)); fillRect(sx + 30, sy + 20, 6, 6, mk(255,255,255)); }
    }
}

static void drawBossChar(int bx, int by, int bw, int bh, bool hurt) {
    Color aura = bossPhase == 3 ? mk(255,55,75) : (bossPhase == 2 ? mk(255,185,45) : mk(95,215,145));
    if (cfg.lightingQual >= 2) fillRect(bx - 6, by - 6, bw + 12, bh + 12, aura);
    if (bossType == 0) { // Spider King
        int lg = ((tick / 5) & 1) ? 8 : -8;
        for (int k = 0; k < 3; k++) { fillRect(bx - 24, by + 20 + k * 28 + lg, 32, 8, mk(36,42,52)); fillRect(bx + bw - 8, by + 20 + k * 28 - lg, 32, 8, mk(36,42,52)); }
        fillCircle(bx + bw / 2, by + 72, 44, hurt ? mk(255,255,255) : mk(168,104,56)); fillCircle(bx + bw / 2, by + 32, 30, mk(118,178,42));
        fillCircle(bx + bw / 2 - 14, by + 30, 9, mk(225,22,38)); fillCircle(bx + bw / 2 + 14, by + 30, 9, mk(225,22,38));
    } else if (bossType == 1) { // Lava Titan
        fillRect(bx, by, bw, bh, hurt ? mk(255,255,255) : mk(78,24,22)); fillRect(bx + 12, by + 12, bw - 24, bh - 24, mk(235,88,28));
        fillRect(bx + 24, by + 28, 24, 18, mk(255,235,85)); fillRect(bx + bw - 48, by + 28, 24, 18, mk(255,235,85));
    } else if (bossType == 2) { // Ice Dragon
        fillRect(bx - 26, by + 24, 34, 48, mk(115,195,245)); fillRect(bx + bw - 8, by + 24, 34, 48, mk(115,195,245));
        fillCircle(bx + bw / 2, by + bh / 2, 52, hurt ? mk(255,255,255) : mk(82,165,235));
        fillRect(bx + 32, by + 30, 18, 16, mk(255,255,255)); fillRect(bx + bw - 50, by + 30, 18, 16, mk(255,255,255));
    } else { // Cursed Knight
        fillRect(bx + 12, by, bw - 24, bh, hurt ? mk(255,255,255) : mk(42,24,68));
        fillRect(bx + 24, by + 14, bw - 48, 32, mk(115,42,155)); fillRect(bx + 32, by + 24, 18, 10, mk(255,55,95)); fillRect(bx + bw - 50, by + 24, 18, 10, mk(255,55,95));
    }
}

static void drawHUDFull(int b) {
    for (int i = 0; i < maxHp; i++) {
        int hx = 42 + i * 38, hy = 26; Color hc = i < hp ? mk(235,32,52) : mk(68,54,72);
        fillCircle(hx + 8, hy + 8, 8, hc); fillCircle(hx + 20, hy + 8, 8, hc); fillRect(hx + 4, hy + 10, 20, 12, hc);
    }
    fillCircle(60, 74, 12, mk(255,208,45)); char cb[32]; sprintf(cb, "%d  LV%d", save.coins, save.charLevel); drawText(82, 64, cb, 3, mk(255,235,110));
    if (bossActive && bossHP > 0) {
        drawLabel(T_B0 + bossType, 960, 10, 32, 3, mk(255,215,90));
        fillRect(676, 44, 568, 22, mk(28,16,38)); fillRect(680, 47, 560, 16, mk(68,24,34)); fillRect(680, 47, (bossHP * 560) / bossMaxHP, 16, mk(242,58,68));
    } else if (escapeTimer > 0) {
        char eb[24]; sprintf(eb, "ESCAPE %02dS!", escapeTimer / 60); fillRect(820, 22, 280, 46, mk(165,24,32)); drawTextC(960, 34, eb, 3, mk(255,245,115));
    } else if (timeTrialMode) {
        char tb[24]; sprintf(tb, "TIME %02d:%02d", stageTimeFrames / 60, (stageTimeFrames % 60) * 100 / 60); fillRect(820, 22, 280, 46, mk(26,20,48)); drawTextC(960, 34, tb, 3, mk(115,245,165));
    } else drawLabel(T_W0 + curWorld, 960, 18, 38, 3, mk(235,232,250));

    static const char *WPN[3] = {"L1: STOMP", "L1: BOLT", "L1: FIRE"};
    fillRect(1560, 22, 310, 44, mk(26,20,48)); drawTextC(1715, 34, WPN[curWeapon], 2, mk(95,235,255));
    if (toastTimer > 0 && toastTid >= 0) { fillRect(540, 108, 840, 56, mk(245,195,55)); fillRect(544, 112, 832, 48, mk(34,24,58)); drawLabel(toastTid, 960, 112, 48, 3, mk(255,238,125)); }
    if (npcDialogTimer > 0 && npcDialogTid >= 0) { fillRect(220, 910, 1480, 90, mk(245,195,65)); fillRect(224, 914, 1472, 82, mk(28,22,48)); drawLabel(npcDialogTid, 960, 914, 82, 2, mk(255,255,255)); }
    hudNeed[b] = false;
}

static void renderGame(int b) {
    resetClip();
    if (winTimer > 0) {
        if (needWin[b]) { fillRect(0,0,FW,FH,mk(20,90,60)); drawLabel(T_COMPLETE,960,350,110,10,mk(255,255,255)); drawLabel(T_GREAT,960,500,80,6,mk(255,220,180)); needWin[b] = false; }
        return;
    }
    int shkX = shakeTimer > 0 ? (rnd(shakeMag * 2 + 1) - shakeMag) : 0, shkY = shakeTimer > 0 ? (rnd(shakeMag * 2 + 1) - shakeMag) : 0;
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
        for (int i = 0; i < plantCount; i++) { Plant &p = plants[i]; p.drawn[b] = (p.room == roomIdx); if (p.drawn[b]) { drawPlantTuft(p.x, p.y, p.h, (int)p.angle, curWorld, p.style); p.dBend[b] = (signed char)p.angle; } }
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
            Plant &p = plants[i]; if (p.room != bufRoom[b]) continue; int bd = (int)p.angle;
            if (p.drawn[b] && p.dBend[b] != bd) { restoreRect(b, p.x - 12, p.y - p.h - 5, 24, p.h + 5, false); p.drawn[b] = false; }
            if (!p.drawn[b]) { drawPlantTuft(p.x, p.y, p.h, bd, curWorld, p.style); p.drawn[b] = true; p.dBend[b] = (signed char)bd; }
        }
        if (hudNeed[b] || timeTrialMode || escapeTimer > 0 || (tick & 7) == 0) { restoreRect(b, 0, 0, FW, 170, true); if (npcDialogTimer == 0) restoreRect(b, 210, 900, 1500, 110, true); drawHUDFull(b); }
    }

    resetClip(); int roomX = roomIdx * FW;
    // Water Surface Animation (Respects waterAnim 0..3!)
    if (cfg.waterAnim > 0) {
        int stepC = cfg.waterAnim == 1 ? 4 : (cfg.waterAnim == 2 ? 2 : 1);
        for (int c = 0; c < RCOLS; c += stepC) if (bufGrid[b][15][c] == '~') {
            int wx = c * TILE + ((tick * 2) % 30), wy = 15 * TILE + (SN[(tick / 4 + c) & 15] >> 6);
            fillRect(wx, wy, 22, 4, mk(195,248,255)); addDirty(b, wx - 2, wy - 4, 26, 12);
        }
    }
    if (cfg.ambientFx > 0) {
        int lLim = cfg.ambientFx == 1 ? 8 : (cfg.ambientFx == 2 ? 16 : MAX_LEAVES);
        for (int i = 0; i < lLim; i++) { fillRect((int)leaves[i].x, (int)leaves[i].y, leaves[i].sz, leaves[i].sz - 1, curWorld == 1 ? mk(255,135,42) : mk(115,235,145)); addDirty(b, (int)leaves[i].x - 1, (int)leaves[i].y - 1, leaves[i].sz + 2, leaves[i].sz + 2); }
        if (cfg.ambientFx >= 2) for (int i = 0; i < MAX_FIREFLIES; i++) { int fx = (int)flies[i].x, fy = (int)flies[i].y; fillRect(fx, fy, 4, 4, mk(255,248,135)); addDirty(b, fx - 2, fy - 2, 8, 8); }
    }
    for (int i = 0; i < mplatCount; i++) if (mplats[i].room == roomIdx) { int mx = (int)mplats[i].x + shkX, my = (int)mplats[i].y + shkY; fillRect(mx, my, mplats[i].w, 26, mk(96,68,42)); fillRect(mx, my, mplats[i].w, 7, mk(82,215,92)); addDirty(b, mx - 4, my - 4, mplats[i].w + 8, 34); }
    for (int i = 0; i < dplatCount; i++) if (dplats[i].room == roomIdx && dplats[i].state < 2) { int dx0 = dplats[i].x + (dplats[i].state == 1 ? ((tick & 2) ? 3 : -3) : 0); fillRect(dx0, dplats[i].y, dplats[i].w, 24, dplats[i].state == 1 ? mk(215,95,65) : mk(142,112,86)); addDirty(b, dx0 - 6, dplats[i].y - 2, dplats[i].w + 12, 28); }
    for (int i = 0; i < coinCount; i++) if (coins[i].active && coins[i].room == roomIdx) { int cx0 = coins[i].x, cy0 = coins[i].y + (SN[(tick / 5 + i) & 15] >> 6); fillRect(cx0 - 7, cy0 - 10, 14, 20, mk(255,208,42)); addDirty(b, cx0 - 10, cy0 - 14, 20, 28); }
    for (int i = 0; i < chestCount; i++) if (chests[i].room == roomIdx) { fillRect(chests[i].x, chests[i].y, 44, 34, chests[i].opened ? mk(95,62,36) : mk(168,104,42)); fillRect(chests[i].x, chests[i].y + 12, 44, 5, mk(245,195,55)); addDirty(b, chests[i].x - 2, chests[i].y - 2, 48, 38); }
    for (int i = 0; i < puCount; i++) if (powerups[i].active && powerups[i].room == roomIdx) { fillCircle(powerups[i].x + 14, powerups[i].y + 14, 13, powerups[i].kind == PU_HEART ? mk(245,42,68) : mk(75,185,255)); addDirty(b, powerups[i].x - 2, powerups[i].y - 2, 32, 32); }
    for (int i = 0; i < checkCount; i++) if (checkC[i] / RCOLS == roomIdx) { int fx = (checkC[i] % RCOLS) * TILE + 24, fy = checkR[i] * TILE - 64; fillRect(fx, fy, 6, 64, mk(220,220,235)); fillRect(fx + 6, fy + 4, 28, 20, checkAct[i] ? mk(65,235,110) : mk(235,190,55)); addDirty(b, fx - 2, fy - 2, 38, 66); }
    for (int i = 0; i < npcCount; i++) if (npcs[i].room == roomIdx) {
        int nx = npcs[i].x, ny = npcs[i].y; fillRect(nx, ny, 38, 48, mk(62,88,165)); fillRect(nx + 4, ny + 4, 30, 18, mk(242,205,168));
        if (cfg.showHints && iabs((int)px - (roomX + nx)) < 90) drawLabel(T_NPC_H, nx + 19, ny - 44, 30, 2, mk(255,235,95));
        addDirty(b, nx - 60, ny - 48, 160, 100);
    }
    for (int r = 0; r < ROWS; r++) for (int c = roomIdx * RCOLS; c < roomIdx * RCOLS + RCOLS && c < COLS; c++)
        if (level[r][c] == 'O') { int cx = c * TILE + 30 - roomX + shkX, cy = r * TILE + 30 + shkY; drawSaw(cx, cy); addDirty(b, cx - 28, cy - 28, 56, 56); }
    for (int i = 0; i < enemyCount; i++) if (enemies[i].active) { int sx = (int)enemies[i].x - roomX + shkX, sy = (int)enemies[i].y + shkY; if (sx >= -60 && sx <= FW + 60) { drawEnemyChar(enemies[i], sx, sy); addDirty(b, sx - 20, enemies[i].kind == E_SPIDER ? 60 : sy - 16, 86, enemies[i].kind == E_SPIDER ? sy : 68); } }
    if (bossActive && bossHP > 0) {
        fillRect(15 * TILE + 20, 7 * TILE - 18, 80, 18, bossSwCd == 0 ? mk(80,255,120) : mk(90,80,100)); addDirty(b, 15 * TILE + 18, 7 * TILE - 20, 84, 22);
        drawBossChar((int)bossX + shkX, (int)bossY + shkY, bossW, bossH, (bossHurt & 2) != 0); addDirty(b, (int)bossX - 36, (int)bossY - 24, bossW + 72, bossH + 48);
    }
    for (int i = 0; i < MAX_SHOTS; i++) if (shots[i].active) { int sx = (int)shots[i].x - roomX, sy = (int)shots[i].y; if (sx >= -30 && sx <= FW + 30) { fillCircle(sx, sy, 12, shots[i].kind == 2 ? mk(85,225,255) : mk(255,95,35)); addDirty(b, sx - 14, sy - 14, 28, 28); } }
    for (int i = 0; i < MAX_PBULLETS; i++) if (pbullets[i].active) { int sx = (int)pbullets[i].x - roomX, sy = (int)pbullets[i].y; fillRect(sx - 10, sy - 6, 20, 12, pbullets[i].kind == 2 ? mk(255,115,45) : mk(95,235,255)); addDirty(b, sx - 12, sy - 8, 24, 16); }
    for (int i = 0; i < MAX_PARTS; i++) { Part &p = parts[i]; if (p.life <= 0) continue; int sx = (int)p.x - roomX, sy = (int)p.y; if (sx >= -20 && sx <= FW + 20 && sy >= -20 && sy <= FH + 20) { fillRect(sx, sy, p.size, p.size, p.kind == 1 ? mk(210,18,32) : (p.kind == 2 ? mk(115,235,255) : mk(205,195,230))); addDirty(b, sx - 1, sy - 1, p.size + 2, p.size + 2); } }
    for (int i = 0; i < 8; i++) if (ghosts[i].life > 0) { int gx = (int)ghosts[i].x - roomX, gy = (int)ghosts[i].y; fillRect(gx, gy, PW, PH, mk(115,215,255)); addDirty(b, gx - 2, gy - 2, PW + 4, PH + 4); }
    for (int i = 0; i < MAX_DMGNUM; i++) if (dmgNums[i].life > 0) { int sx = (int)dmgNums[i].x - roomX, sy = (int)dmgNums[i].y; char nb[8]; sprintf(nb, "-%d", dmgNums[i].val); drawText(sx, sy, nb, 3, dmgNums[i].col); addDirty(b, sx - 4, sy - 4, 48, 30); }
    if (deadTimer == 0 && ((invulnTimer & 2) == 0)) {
        int x = (int)px - roomX + shkX, y = (int)py + shkY, dw = iclamp((int)(PW * squashX), 20, 80), dh = iclamp((int)(PH * squashY), 20, 80);
        int dx = x + (PW - dw) / 2, dy = y + (PH - dh) + ((onGround && iabs((int)vx) > 1 && ((animTick / 6) & 1)) ? -3 : 0);
        if (cfg.shadowQual >= 1 && onGround) fillRect(dx + 4, y + PH - 3, dw - 8, 6, mk(18,16,28));
        if (cfg.lightingQual >= 3 && curWorld == 4) fillCircle(dx + dw / 2, dy + dh / 2, 54, mk(98,84,48)); // Cave torch halo!
        if (shieldTimer > 0) fillCircle(dx + dw / 2, dy + dh / 2, 34, mk(85,195,255));
        drawSkinCube(dx, dy, dw, dh, facing, save.selectedSkin);
        addDirty(b, x - 58, y - 58, PW + 116, PH + 116);
    }
}

// ---------- Pro 6-Tab Settings UI (Graphics + Live Preview, Video, Audio, Language, Gameplay, Controls) ----------
static void drawSlider(int x, int y, int w, int val, int maxV, Color fillC) {
    fillRect(x, y, w, 18, mk(26,20,44));
    int fw = (iclamp(val, 0, maxV) * (w - 4)) / maxV;
    fillRect(x + 2, y + 2, fw, 14, fillC);
    fillRect(x + fw - 3, y - 4, 8, 26, mk(255,255,255));
}

static void drawProSettings() {
    fillRect(0, 0, FW, FH, mk(18, 16, 34));
    fillRect(40, 34, 1840, 82, mk(34, 24, 58)); drawLabel(T_B_SETTINGS, 960, 34, 82, 5, mk(255, 212, 72));
    // Left Sidebar (6 Categories)
    fillRect(40, 140, 340, 810, mk(26, 20, 46));
    for (int t = 0; t < 6; t++) {
        int ty = 165 + t * 115; bool sel = (t == setTab);
        if (sel) fillRect(52, ty - 4, 316, 94, mk(255,212,72));
        fillRect(56, ty, 308, 86, sel ? mk(198,36,54) : mk(42,32,66));
        drawLabel(T_TAB_GFX + t, 210, ty, 86, 3, mk(255,255,255));
    }
    // Right Content Panel
    fillRect(410, 140, 1470, 810, mk(28, 22, 48));

    if (setTab == 0) { // GRAPHICS (9 Options x 4 Levels + Live Visual Preview + Apply/Restore)
        int *vals[9] = {&cfg.grassDensity,&cfg.worldDetails,&cfg.waterAnim,&cfg.bloodParts,&cfg.grassAnim,&cfg.lightingQual,&cfg.shadowQual,&cfg.particleFx,&cfg.ambientFx};
        for (int i = 0; i < 9; i++) {
            int ry = 160 + i * 68; bool sel = (setRow == i);
            fillRect(435, ry, 920, 58, sel ? mk(198,36,54) : mk(42,32,66));
            drawLabel(T_G_GRASSD + i, 670, ry, 58, 2, mk(255,255,255));
            drawLabel(T_VLOW + iclamp(*vals[i], 0, 3), 1160, ry, 58, 2, mk(255,228,110));
        }
        // Apply & Restore buttons
        for (int b = 0; b < 2; b++) {
            int ry = 795, rx = 435 + b * 470; bool sel = (setRow == 9 + b);
            fillRect(rx, ry, 440, 62, sel ? mk(48,165,92) : mk(52,42,78));
            drawLabel(b == 0 ? T_APPLY : T_RESTORE, rx + 220, ry, 62, 2, mk(255,255,255));
        }
        // Live Graphics Preview Box (Shows real-time effect of Grass/World/Water/Light/Shadow!)
        int px0 = 1390, py0 = 180;
        fillRect(px0 - 4, py0 - 4, 468, 428, mk(245,195,65));
        fillRect(px0, py0, 460, 240, mk(46,148,225)); fillRect(px0, py0 + 240, 460, 180, mk(28,88,52));
        if (cfg.lightingQual >= 1) fillCircle(px0 + 380, py0 + 68, 32 + cfg.lightingQual * 5, mk(255,232,95));
        if (cfg.worldDetails >= 1) fillRect(px0 + 40, py0 + 140, 44, 180, mk(88,52,32));
        if (cfg.worldDetails >= 2) fillCircle(px0 + 62, py0 + 130, 52, mk(46,158,64));
        fillRect(px0, py0 + 320, 460, 100, mk(112,68,38)); fillRect(px0, py0 + 320, 460, 10, mk(64,198,58));
        if (cfg.waterAnim > 0) { fillRect(px0 + 250, py0 + 320, 180, 100, mk(32,128,175)); fillRect(px0 + 260 + ((tick * cfg.waterAnim) % 40), py0 + 324, 48, 5, mk(185,245,255)); }
        for (int g = 0; g < cfg.grassDensity * 5; g++) drawPlantTuft(px0 + 20 + g * 16, py0 + 320, 14, cfg.grassAnim > 0 ? SN[(tick/6 + g)&15]/28 : 0, 0, g % 3);
        drawTextC(px0 + 230, py0 + 448, "LIVE PREVIEW", 3, mk(255,225,110));
    } else if (setTab == 1) { // VIDEO
        int ry = 190;
        fillRect(460, ry, 1360, 78, setRow == 0 ? mk(198,36,54) : mk(42,32,66));
        drawLabel(T_V_FPS, 780, ry, 78, 3, mk(255,255,255));
        drawTextC(1480, ry + 26, cfg.fpsLimit == 60 ? "60 FPS" : "30 FPS", 4, mk(115,245,165));
        ry = 295;
        fillRect(460, ry, 1360, 78, setRow == 1 ? mk(198,36,54) : mk(42,32,66));
        drawLabel(T_V_BRIGHT, 780, ry, 78, 3, mk(255,255,255));
        drawSlider(1180, ry + 30, 420, cfg.brightness - 50, 100, mk(255,215,75));
        char bb[12]; sprintf(bb, "%d%%", cfg.brightness); drawTextC(1700, ry + 28, bb, 3, mk(255,255,255));
        // Honest hardware status rows (non-fake!)
        fillRect(460, 420, 1360, 72, mk(36,30,56)); drawLabel(T_V_RES, 1140, 420, 72, 3, mk(185,215,235));
        fillRect(460, 515, 1360, 72, mk(36,30,56)); drawLabel(T_V_VSYNC, 1140, 515, 72, 3, mk(185,215,235));
    } else if (setTab == 2) { // AUDIO (6 Independent Sliders + Mute Buttons + Restore)
        int *av[6] = {&cfg.masterVol,&cfg.musicVol,&cfg.sfxVol,&cfg.ambVol,&cfg.playerVol,&cfg.enemyVol};
        for (int i = 0; i < 6; i++) {
            int ry = 165 + i * 82; bool sel = (setRow == i);
            fillRect(460, ry, 1360, 68, sel ? mk(198,36,54) : mk(42,32,66));
            drawLabel(T_A_MASTER + i, 740, ry, 68, 3, mk(255,255,255));
            drawSlider(1080, ry + 25, 480, *av[i], 100, mk(95,225,255));
            char vb[12]; sprintf(vb, "%d%%", *av[i]); drawTextC(1690, ry + 24, vb, 3, mk(255,235,110));
        }
        int *mv[3] = {&cfg.muteAll, &cfg.muteMusic, &cfg.muteSfx};
        for (int m = 0; m < 3; m++) {
            int rx = 460 + m * 460, ry = 695; bool sel = (setRow == 6 + m);
            fillRect(rx, ry, 430, 68, sel ? mk(198,36,54) : (*mv[m] ? mk(145,42,58) : mk(42,32,66)));
            drawLabel(T_A_MUTEALL + m, rx + 215, ry, 68, 2, mk(255,255,255));
        }
        fillRect(460, 795, 1360, 64, setRow == 9 ? mk(48,165,92) : mk(52,42,78));
        drawLabel(T_RESTORE, 1140, 795, 64, 3, mk(255,255,255));
    } else if (setTab == 3) { // LANGUAGE (6 Languages)
        static const char *LNAMES[6] = {"ARABIC (RTL)", "ENGLISH", "FRANCAIS", "ESPANOL", "DEUTSCH", "TURKCE"};
        for (int i = 0; i < 6; i++) {
            int ry = 185 + i * 105; bool sel = (setRow == i), act = (cfg.lang == i);
            fillRect(520, ry, 1240, 82, sel ? mk(198,36,54) : (act ? mk(48,125,78) : mk(42,32,66)));
            drawTextC(1140, ry + 28, LNAMES[i], 4, mk(255,255,255));
        }
    } else if (setTab == 4) { // GAMEPLAY
        int *gv[7] = {&cfg.camShake,&cfg.rumble,&cfg.showDmgNum,&cfg.showDmgInd,&cfg.difficulty,&cfg.showHints,&cfg.ngPlus};
        for (int i = 0; i < 7; i++) {
            int ry = 175 + i * 86; bool sel = (setRow == i);
            fillRect(460, ry, 1360, 72, sel ? mk(198,36,54) : mk(42,32,66));
            drawLabel(T_GP_SHAKE + i, 800, ry, 72, 3, mk(255,255,255));
            if (i == 4) drawLabel(T_D_EASY + iclamp(*gv[i], 0, 2), 1480, ry, 72, 3, mk(255,228,110));
            else drawLabel(*gv[i] ? T_ON : T_OFF, 1480, ry, 72, 3, *gv[i] ? mk(115,245,145) : mk(225,135,135));
        }
        fillRect(460, 795, 1360, 64, setRow == 7 ? mk(48,165,92) : mk(52,42,78));
        drawLabel(T_RESTORE_ALL, 1140, 795, 64, 3, mk(255,255,255));
    } else { // CONTROLS (PS4 Controller Mapping + Custom Scheme)
        fillRect(460, 185, 1360, 78, setRow == 0 ? mk(198,36,54) : mk(42,32,66));
        drawTextC(1140, 212, cfg.moveMode == 0 ? "MOVEMENT: ANALOG STICK" : "MOVEMENT: D-PAD ARROWS", 3, mk(255,255,255));
        fillRect(460, 285, 1360, 78, setRow == 1 ? mk(198,36,54) : mk(42,32,66));
        drawTextC(1140, 312, cfg.btnScheme == 0 ? "LAYOUT: DEFAULT (X:JUMP  O:DASH  SQ:ATTACK/SLAM)" : "LAYOUT: ALT (X:JUMP  SQ:DASH  O:ATTACK)", 2, mk(255,235,110));
        fillRect(460, 410, 1360, 340, mk(34,28,58));
        drawTextC(1140, 445, "PS4 DUALSHOCK 4 CONTROLS", 4, mk(115,235,255));
        drawTextC(1140, 510, "X : JUMP / DOUBLE JUMP / WALL JUMP", 3, mk(245,245,255));
        drawTextC(1140, 560, "O / R1 : DASH     |     DOWN + SQUARE : GROUND SLAM", 3, mk(245,245,255));
        drawTextC(1140, 610, "L1 : SWITCH WEAPON     |     TRIANGLE : TALK TO NPC", 3, mk(245,245,255));
        drawTextC(1140, 660, "OPTIONS : PAUSE / MAP", 3, mk(245,245,255));
        fillRect(460, 795, 1360, 64, setRow == 2 ? mk(48,165,92) : mk(52,42,78));
        drawLabel(T_RESTORE, 1140, 795, 64, 3, mk(255,255,255));
    }
    fillRect(40, 975, 1840, 65, mk(34,24,58));
    drawTextC(960, 998, "L1/R1: SWITCH TAB   |   UP/DOWN: SELECT   |   LEFT/RIGHT/X: CHANGE   |   O: SAVE & BACK", 2, mk(215,215,238));
}

// ---------- Menus & Main Loop ----------
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
static void startStage(int st, bool tt) {
    curStage = st; timeTrialMode = tt; buildStage(st); roomIdx = 0; stageTimeFrames = 0; questKills = questDone = 0;
    toastTimer = npcDialogTimer = shieldTimer = speedTimer = slamShockTimer = shakeTimer = 0;
    respawn(); winTimer = deadTimer = splatCount = 0;
    for (int i = 0; i < MAX_PARTS; i++) parts[i].life = 0;
    for (int i = 0; i < MAX_PBULLETS; i++) pbullets[i].active = false;
    for (int b = 0; b < 2; b++) { for (int i = 0; i < MAX_SPLATS; i++) splats[i].drawn[b] = false; needFull[b] = true; needWin[b] = hudNeed[b] = false; dirtyCnt[b] = 0; }
    state = S_PLAY;
}

static void drawSecretMenu() {
    static const char *NAMES[5] = {"OBITO","PAIN","ITACHI","MADARA","KAKASHI"};
    fillRect(0,0,FW,FH,mk(14,12,28)); fillRect(260,64,1400,96,mk(225,48,62)); fillRect(264,68,1392,88,mk(28,20,48));
    drawTextC(960, 96, "SECRET SHINOBI CHARACTERS", 5, mk(255,212,72));
    for (int i = 0; i < 5; i++) {
        int bx = 135 + i * 335, by = 225; bool sel = (i == secretSel);
        if (sel) fillRect(bx - 6, by - 6, 302, 522, mk(255,212,72));
        fillRect(bx, by, 290, 510, sel ? mk(48,28,72) : mk(26,20,44)); fillRect(bx + 14, by + 18, 262, 340, mk(18,16,32));
        drawNarutoHD(i, bx + 85, by + 48, 6, 1); drawTextC(bx + 145, by + 410, NAMES[i], 4, sel ? mk(255,224,92) : mk(235,235,245));
    }
    fillRect(420,820,1080,64,mk(32,24,54)); drawTextC(960,842,"LEFT/RIGHT: PICK   |   X: EQUIP   |   O: BACK",2,mk(225,225,240));
}

static void drawItem(int i) {
    static const int MENU_T[8] = {T_PLAY,T_SKILLTREE,T_SHOP,T_SKINS,T_ACH,T_SETTINGS,T_CREDITS,T_EXIT};
    static const char *SKL_N[4] = {"SWIFT DASH (1 SP)","FIRE BLADE WEAPON (1 SP)","VAMPIRIC HEAL (1 SP)","START SHIELD (1 SP)"};
    static const char *SHP_N[4] = {"+1 MAX HEART (30C)","FASTER DASH (35C)","COIN MAGNET (40C)","SUPER SLAM (45C)"};
    static const char *SKN_N[5] = {"CLASSIC RED","ROYAL GOLD (40C)","SHADOW NINJA (50C)","CRYSTAL ICE (60C)","MAGMA LAVA (75C)"};
    if (state == S_MENU && i >= 0 && i < 8) {
        int y = 275 + i * 82; bool sel = (i == menuSel);
        if (sel) fillRect(606, y - 4, 708, 72, mk(255,210,85));
        fillRect(610, y, 700, 64, sel ? mk(215,30,45) : mk(48,36,68)); drawLabel(MENU_T[i], 960, y, 64, 4, mk(255,255,255));
    } else if (state == S_LEVELS) {
        if (i >= 0 && i < TOTAL_STAGES) {
            int col = i % 4, row = i / 4, x = 520 + col * 230, y = 270 + row * 135;
            bool sel = (i == levelSel), unl = (i < save.unlockedStages), bs = isBossStage(i);
            if (sel) fillRect(x - 6, y - 6, 172, 102, mk(255,245,255));
            fillRect(x, y, 160, 90, unl ? (bs ? mk(175,24,48) : mk(205,42,58)) : mk(50,40,62));
            if (unl) { char buf[16]; sprintf(buf, "W%d-%d%s", i / 2 + 1, (i & 1) + 1, bs ? " B" : ""); drawTextC(x + 80, y + 32, buf, 4, mk(255,255,255)); }
            else drawTextC(x + 80, y + 34, "LOCK", 3, mk(150,150,165));
        } else if (i == TOTAL_STAGES) { fillRect(540, 860, 840, 64, mk(35,25,55)); drawLabel(T_W0 + (levelSel / 2), 960, 860, 64, 3, mk(135,238,165)); }
    } else if (state == S_SKILLS && i >= 0 && i < 4) {
        int y = 320 + i * 115; bool sel = (i == skillSel), own = (save.skillMask & (1u << i)) != 0;
        fillRect(420, y, 1080, 88, sel ? mk(198,36,54) : (own ? mk(42,118,68) : mk(48,36,68)));
        drawTextC(960, y + 32, own ? "UNLOCKED" : SKL_N[i], 3, mk(255,255,255));
    } else if (state == S_SHOP && i >= 0 && i < 4) {
        int y = 320 + i * 115; bool sel = (i == shopSel);
        int own = (i == 0 ? save.upHp : (i == 1 ? save.upDash : (i == 2 ? save.upMagnet : save.upSlam)));
        fillRect(420, y, 1080, 88, sel ? mk(198,36,54) : (own ? mk(42,118,68) : mk(48,36,68)));
        drawTextC(960, y + 32, own ? "OWNED" : SHP_N[i], 3, mk(255,255,255));
    } else if (state == S_SKINS && i >= 0 && i < 5) {
        int y = 290 + i * 112; bool sel = (i == skinSel), own = (save.skinsOwned & (1u << i)) != 0;
        fillRect(450, y, 1020, 88, sel ? mk(198,36,54) : mk(48,36,68));
        drawSkinCube(480, y + 22, 44, 44, 1, i); drawTextC(960, y + 32, save.selectedSkin == i ? "EQUIPPED" : (own ? "OWNED" : SKN_N[i]), 3, mk(255,255,255));
    }
}

static void drawUI(int b) {
    resetClip();
    if (state == S_SECRET) { if (uiFull[b]) { drawSecretMenu(); uiFull[b] = false; } return; }
    if (state == S_SETTINGS) { if (uiFull[b]) { drawProSettings(); uiFull[b] = false; } return; }
    if (uiFull[b]) {
        fillRect(0,0,FW,300,mk(36,122,205)); fillRect(0,300,FW,340,mk(92,188,238));
        fillRect(0,640,FW,280,mk(32,102,58)); fillRect(0,920,FW,160,mk(84,52,32)); fillRect(0,920,FW,12,mk(72,208,68));
        fillCircle(1640, 130, 58, mk(255,242,155));
        for (int gx = 60; gx < FW - 60; gx += 64) drawPlantTuft(gx, 920, 15, SN[(tick/8)&15]/32, 0, (gx/64)%3);
        fillRect(50, 30, 340, 54, mk(32,24,54)); char cb[32]; sprintf(cb, "COINS:%d  SP:%d", save.coins, save.skillPoints); drawText(70, 46, cb, 3, mk(255,235,110));
        if (state == S_MENU) { drawBanner(T_TITLE, 10, 95, 135, true); drawHint(T_H_SEL); }
        else if (state == S_LEVELS) { drawBanner(T_B_LEVELS, 7, 100, 105, false); drawHint(T_H_LVL); }
        else if (state == S_SKILLS) { drawBanner(T_B_SKILLS, 7, 100, 105, false); drawHint(T_H_SEL); }
        else if (state == S_SHOP) { drawBanner(T_B_SHOP, 7, 100, 105, false); drawHint(T_H_SEL); }
        else if (state == S_SKINS) { drawBanner(T_B_SKINS, 7, 100, 105, false); drawHint(T_H_SEL); }
        else if (state == S_ACH) {
            drawBanner(T_B_ACH, 7, 100, 105, false);
            static const char *AN[6] = {"CLEAR STAGE 1","OPEN A CHEST","DEFEAT A BOSS","COLLECT 50 COINS","FIND SECRET ROOM","BEAT TIME TRIAL"};
            for (int k = 0; k < 6; k++) { bool ok = (save.achMask & (1u << k)) != 0; fillRect(420, 260 + k * 105, 1080, 82, ok ? mk(42,118,68) : mk(42,32,62)); drawTextC(960, 290 + k * 105, AN[k], 3, ok ? mk(255,245,145) : mk(165,160,185)); }
            drawHint(T_H_BACK);
        } else if (state == S_CREDITS) {
            drawBanner(T_B_CREDITS, 7, 100, 105, false); fillRect(460,280,1000,540,mk(35,25,55));
            drawTextC(960, 370, "GAME DESIGNERS", 5, mk(220,212,238)); drawTextC(960, 480, "KAREEM & CLAUDE", 7, mk(255,75,115)); drawHint(T_H_BACK);
        }
        int cnt = state == S_MENU ? 8 : (state == S_LEVELS ? TOTAL_STAGES + 1 : (state == S_SKILLS || state == S_SHOP ? 4 : (state == S_SKINS ? 5 : 0)));
        for (int i = 0; i < cnt; i++) drawItem(i);
        uiFull[b] = false; memset(pend[b], 0, 64);
    } else {
        for (int i = 0; i < 64; i++) if (pend[b][i]) { drawItem(i); pend[b][i] = false; }
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
    if (state == S_SETTINGS) {
        static const int MAXR[6] = {11, 2, 10, 6, 8, 3};
        if (in.l1New) { setTab = (setTab + 5) % 6; setRow = 0; snd(0); uiFull[0] = uiFull[1] = true; return; }
        if (in.r1New) { setTab = (setTab + 1) % 6; setRow = 0; snd(0); uiFull[0] = uiFull[1] = true; return; }
        if (nu) { setRow = (setRow + MAXR[setTab] - 1) % MAXR[setTab]; snd(0); uiFull[0] = uiFull[1] = true; }
        if (nd) { setRow = (setRow + 1) % MAXR[setTab]; snd(0); uiFull[0] = uiFull[1] = true; }
        int delta = nr ? 1 : (nl ? -1 : 0);
        if (setTab == 0) {
            int *v[9] = {&cfg.grassDensity,&cfg.worldDetails,&cfg.waterAnim,&cfg.bloodParts,&cfg.grassAnim,&cfg.lightingQual,&cfg.shadowQual,&cfg.particleFx,&cfg.ambientFx};
            if (setRow < 9 && (delta || in.crossNew)) { *v[setRow] = (*v[setRow] + (delta < 0 ? 3 : 1)) % 4; snd(0); uiFull[0] = uiFull[1] = true; }
            else if (setRow == 9 && in.crossNew) { writeSettings(); snd(1); uiFull[0] = uiFull[1] = true; }
            else if (setRow == 10 && in.crossNew) { cfg.grassDensity=2; cfg.worldDetails=3; cfg.waterAnim=2; cfg.bloodParts=2; cfg.grassAnim=2; cfg.lightingQual=2; cfg.shadowQual=2; cfg.particleFx=2; cfg.ambientFx=2; writeSettings(); snd(1); uiFull[0] = uiFull[1] = true; }
        } else if (setTab == 1) {
            if (setRow == 0 && (delta || in.crossNew)) { cfg.fpsLimit = cfg.fpsLimit == 60 ? 30 : 60; snd(0); uiFull[0] = uiFull[1] = true; }
            else if (setRow == 1 && delta) { cfg.brightness = iclamp(cfg.brightness + delta * 5, 50, 150); rebuildBrightnessLUT(); snd(0); uiFull[0] = uiFull[1] = true; }
        } else if (setTab == 2) {
            int *av[6] = {&cfg.masterVol,&cfg.musicVol,&cfg.sfxVol,&cfg.ambVol,&cfg.playerVol,&cfg.enemyVol};
            int *mv[3] = {&cfg.muteAll,&cfg.muteMusic,&cfg.muteSfx};
            if (setRow < 6 && delta) { *av[setRow] = iclamp(*av[setRow] + delta * 5, 0, 100); snd(0); uiFull[0] = uiFull[1] = true; }
            else if (setRow >= 6 && setRow < 9 && (delta || in.crossNew)) { *mv[setRow - 6] = 1 - *mv[setRow - 6]; snd(1); uiFull[0] = uiFull[1] = true; }
            else if (setRow == 9 && in.crossNew) { cfg.masterVol=85; cfg.musicVol=75; cfg.sfxVol=85; cfg.ambVol=70; cfg.playerVol=85; cfg.enemyVol=85; cfg.muteAll=cfg.muteMusic=cfg.muteSfx=0; snd(1); uiFull[0] = uiFull[1] = true; }
        } else if (setTab == 3 && in.crossNew) { cfg.lang = setRow; writeSettings(); snd(1); uiFull[0] = uiFull[1] = true; }
        else if (setTab == 4) {
            int *gv[7] = {&cfg.camShake,&cfg.rumble,&cfg.showDmgNum,&cfg.showDmgInd,&cfg.difficulty,&cfg.showHints,&cfg.ngPlus};
            if (setRow < 7 && (delta || in.crossNew)) {
                if (setRow == 4) cfg.difficulty = (cfg.difficulty + (delta < 0 ? 2 : 1)) % 3;
                else if (setRow == 6 && !save.ngPlusUnlocked) snd(3);
                else *gv[setRow] = 1 - *gv[setRow];
                snd(0); uiFull[0] = uiFull[1] = true;
            } else if (setRow == 7 && in.crossNew) { resetDefaultSettings(); writeSettings(); snd(1); uiFull[0] = uiFull[1] = true; }
        } else if (setTab == 5) {
            if (setRow == 0 && (delta || in.crossNew)) { cfg.moveMode = 1 - cfg.moveMode; snd(0); uiFull[0] = uiFull[1] = true; }
            else if (setRow == 1 && (delta || in.crossNew)) { cfg.btnScheme = 1 - cfg.btnScheme; snd(0); uiFull[0] = uiFull[1] = true; }
            else if (setRow == 2 && in.crossNew) { cfg.moveMode = 0; cfg.btnScheme = 0; snd(1); uiFull[0] = uiFull[1] = true; }
        }
        if (in.circleNew) { writeSettings(); snd(2); setState(S_MENU); }
        return;
    }
    if (state == S_MENU) {
        if (in.l2 && in.r2 && in.l1 && in.crossNew) { snd(1); setState(S_SECRET); return; }
        int o = menuSel; if (nu) menuSel = (menuSel + 7) % 8; if (nd) menuSel = (menuSel + 1) % 8;
        if (menuSel != o) { markPend(o); markPend(menuSel); snd(0); }
        if (in.crossNew) {
            if (menuSel == 0) { snd(1); setState(S_LEVELS); } else if (menuSel == 1) { snd(1); setState(S_SKILLS); }
            else if (menuSel == 2) { snd(1); setState(S_SHOP); } else if (menuSel == 3) { snd(1); setState(S_SKINS); }
            else if (menuSel == 4) { snd(1); setState(S_ACH); } else if (menuSel == 5) { snd(1); setTab = setRow = 0; setState(S_SETTINGS); }
            else if (menuSel == 6) { snd(1); setState(S_CREDITS); } else { snd(2); exit(0); }
        }
    } else if (state == S_LEVELS) {
        int o = levelSel, n = levelSel;
        if (nl && n > 0) n--; if (nr && n < TOTAL_STAGES - 1) n++; if (nu && n >= 4) n -= 4; if (nd && n + 4 < TOTAL_STAGES) n += 4;
        if (n != o) { levelSel = n; markPend(o); markPend(n); markPend(TOTAL_STAGES); snd(0); }
        if ((in.crossNew || in.squareNew) && levelSel < save.unlockedStages) { snd(1); startStage(levelSel, in.squareNew); }
        else if (in.crossNew || in.squareNew) snd(3); else if (in.circleNew) { snd(2); setState(S_MENU); }
    } else if (state == S_SKILLS) {
        int o = skillSel; if (nu) skillSel = (skillSel + 3) % 4; if (nd) skillSel = (skillSel + 1) % 4;
        if (skillSel != o) { markPend(o); markPend(skillSel); snd(0); }
        if (in.crossNew) { if (!(save.skillMask & (1u << skillSel)) && save.skillPoints > 0) { save.skillPoints--; save.skillMask |= (1u << skillSel); writeSave(); snd(1); setState(S_SKILLS); } else snd(3); }
        else if (in.circleNew) { snd(2); setState(S_MENU); }
    } else if (state == S_SHOP) {
        static const int COST[4] = {30,35,40,45};
        int o = shopSel; if (nu) shopSel = (shopSel + 3) % 4; if (nd) shopSel = (shopSel + 1) % 4;
        if (shopSel != o) { markPend(o); markPend(shopSel); snd(0); }
        if (in.crossNew) { int *p = shopSel == 0 ? &save.upHp : (shopSel == 1 ? &save.upDash : (shopSel == 2 ? &save.upMagnet : &save.upSlam)); if (*p == 0 && save.coins >= COST[shopSel]) { save.coins -= COST[shopSel]; *p = 1; writeSave(); snd(1); setState(S_SHOP); } else snd(3); }
        else if (in.circleNew) { snd(2); setState(S_MENU); }
    } else if (state == S_SKINS) {
        static const int COST[5] = {0,40,50,60,75};
        int o = skinSel; if (nu) skinSel = (skinSel + 4) % 5; if (nd) skinSel = (skinSel + 1) % 5;
        if (skinSel != o) { markPend(o); markPend(skinSel); snd(0); }
        if (in.crossNew) {
            if (save.skinsOwned & (1u << skinSel)) { save.selectedSkin = skinSel; writeSave(); snd(1); setState(S_SKINS); }
            else if (save.coins >= COST[skinSel]) { save.coins -= COST[skinSel]; save.skinsOwned |= (1u << skinSel); save.selectedSkin = skinSel; writeSave(); snd(1); setState(S_SKINS); }
            else snd(3);
        } else if (in.circleNew) { snd(2); setState(S_MENU); }
    } else if ((state == S_CREDITS || state == S_ACH) && (in.crossNew || in.circleNew)) { snd(2); setState(S_MENU); }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    scene = new Scene2D(FW, FH, 4);
    if (!scene->Init(0xC000000, 2) || !initPad()) for (;;);
    loadSettings(); loadSave(); initAudio(); setState(S_MENU); snd(1);
    for (;;) {
        int b = frameID & 1; readInput();
        if (state == S_PLAY) {
            if (in.optionsNew) setState(S_LEVELS);
            else if (winTimer > 0) {
                if (--winTimer == 0) {
                    if (curStage + 1 < TOTAL_STAGES && !timeTrialMode) startStage(curStage + 1, false);
                    else setState(S_LEVELS);
                }
            } else update();
        } else updateUI();

        if (state == S_PLAY) renderGame(b); else drawUI(b);
        scene->SubmitFlip(frameID);
        updateGameplayAudio();
        if (cfg.fpsLimit == 30) sceKernelUsleep(16600);
        scene->FrameWait(frameID);
        scene->FrameBufferSwap();
        frameID++; tick++;
    }
    return 0;
}
// ====== نهاية الجزء 2 ======
