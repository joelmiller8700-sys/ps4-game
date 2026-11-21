import sys
from PIL import Image, ImageDraw, ImageFont
import arabic_reshaper
try:
    from bidi.algorithm import get_display
except Exception:
    from bidi import get_display

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

ITEMS = [
    ("TITLE", "مكعب اللحم", 90),
    ("PLAY", "ابدأ المغامرة", 44),
    ("SKILLTREE", "شجرة المهارات", 44),
    ("SHOP", "المتجر والتطويرات", 44),
    ("SKINS", "مظاهر الشخصية", 44),
    ("ACH", "الإنجازات والمهام", 44),
    ("SETTINGS", "الإعدادات", 44),
    ("CREDITS", "المصممون", 44),
    ("EXIT", "خروج", 44),
    ("BACK", "رجوع", 40),
    ("B_LEVELS", "خريطة العوالم الثمانية", 58),
    ("B_SKILLS", "شجرة المهارات والأسلحة", 58),
    ("B_SHOP", "متجر التطويرات", 58),
    ("B_SKINS", "مظاهر الشخصية", 58),
    ("B_ACH", "الإنجازات والمهام الجانبية", 54),
    ("B_SETTINGS", "إعدادات اللعبة", 58),
    ("B_CREDITS", "المصممون", 58),
    ("TAB_GFX", "الرسومات", 36),
    ("TAB_VID", "الفيديو", 36),
    ("TAB_AUD", "الصوت", 36),
    ("TAB_LANG", "اللغة", 36),
    ("TAB_GAME", "أسلوب اللعب", 36),
    ("TAB_CTRL", "التحكم", 36),
    ("G_GRASSD", "كثافة العشب", 32),
    ("G_WORLD", "تفاصيل العالم", 32),
    ("G_WATER", "حركة المياه", 32),
    ("G_BLOOD", "تطاير الدماء", 32),
    ("G_GRASSA", "حركة العشب", 32),
    ("G_LIGHT", "جودة الإضاءة", 32),
    ("G_SHADOW", "جودة الظلال", 32),
    ("G_PART", "المؤثرات الجسيمية", 32),
    ("G_AMB", "المؤثرات البيئية", 32),
    ("LVL_VLOW", "جدا ضعيف", 30),
    ("LVL_LOW", "ضعيف", 30),
    ("LVL_MED", "متوسط", 30),
    ("LVL_ULTRA", "فائق", 30),
    ("APPLY", "تطبيق وحفظ", 32),
    ("RESTORE", "استعادة الافتراضي", 32),
    ("RESTORE_ALL", "استعادة كل الإعدادات", 32),
    ("V_FPS", "معدل الإطارات (FPS)", 34),
    ("V_BRIGHT", "السطوع", 34),
    ("V_RES", "الدقة: 1080p (أصلية للتلفاز)", 32),
    ("V_VSYNC", "V-Sync: مفعّل (PS4)", 32),
    ("A_MASTER", "الصوت العام", 32),
    ("A_MUSIC", "مستوى الموسيقى", 32),
    ("A_SFX", "المؤثرات الصوتية", 32),
    ("A_AMB", "أصوات البيئة", 32),
    ("A_PLAYER", "أصوات اللاعب", 32),
    ("A_ENEMY", "أصوات الأعداء والزعماء", 32),
    ("A_MUTEALL", "كتم الكل", 30),
    ("A_MUTEMUS", "كتم الموسيقى", 30),
    ("A_MUTESFX", "كتم المؤثرات", 30),
    ("GP_SHAKE", "اهتزاز الكاميرا", 32),
    ("GP_VIBE", "اهتزاز يد التحكم", 32),
    ("GP_DMGNUM", "عرض أرقام الضرر", 32),
    ("GP_DMGIND", "مؤشرات الضرر", 32),
    ("GP_DIFF", "مستوى الصعوبة", 32),
    ("GP_HINTS", "عرض التلميحات", 32),
    ("GP_NGPLUS", "وضع New Game+", 32),
    ("D_EASY", "سهل", 30),
    ("D_NORM", "عادي", 30),
    ("D_HARD", "صعب", 30),
    ("ON", "تشغيل", 30),
    ("OFF", "إيقاف", 30),
    ("W0", "1: الغابة المسكونة", 34),
    ("W1", "2: قلب البركان", 34),
    ("W2", "3: القمم الجليدية", 34),
    ("W3", "4: القلعة المظلمة", 34),
    ("W4", "5: كهوف الكريستال", 34),
    ("W5", "6: المصنع الميكانيكي", 34),
    ("W6", "7: الجزر الطائرة", 34),
    ("W7", "8: العالم المائي", 34),
    ("B_SPIDERK", "ملك العناكب - Spider King", 32),
    ("B_LAVAT", "عملاق الحمم - Lava Titan", 32),
    ("B_ICEDRAG", "التنين الجليدي - Ice Dragon", 32),
    ("B_CURSEDK", "الفارس الملعون - Cursed Knight", 32),
    ("COMPLETE", "اكتملت المرحلة!", 80),
    ("GREAT", "أداء أسطوري!", 52),
    ("SECRET_FOUND", "اكتشفت غرفة سرية!", 36),
    ("CHEST_OPEN", "صندوق كنز! +25 عملة", 36),
    ("ACH_UNL", "تم فتح إنجاز جديد!", 34),
    ("QUEST_DONE", "اكتملت المهمة الجانبية! +40 عملة", 34),
    ("NPC_H", "مثلث: تحدث", 26),
    ("NPC0", "الحكيم: استخدم X للقفز المزدوج و O للاندفاع فوق الجسور!", 32),
    ("NPC1", "المستكشف: اضغط أسفل + مربع في الهواء لتحطيم الدروع!", 32),
    ("NPC2", "الحارس: انتبه! بعض الصناديق وحوش Mimic متنكرة!", 32),
    ("NPC3", "التاجر: بدّل سلاحك بـ L1 وافتح المهارات من شجرة المهارات!", 32),
    ("SELECT", "X: اختيار   O: رجوع", 30),
    ("BACK_HINT", "O: رجوع", 30),
    ("LEVELS_HINT", "X: لعب   مربع: تحدي الوقت   O: رجوع", 30),
]


def load_font(size):
    try:
        return ImageFont.truetype(FONT, size, layout_engine=ImageFont.Layout.BASIC)
    except Exception:
        return ImageFont.truetype(FONT, size)


def render(text, size):
    try:
        shaped = get_display(arabic_reshaper.reshape(text), base_dir="R")
    except Exception:
        shaped = get_display(arabic_reshaper.reshape(text))
    font = load_font(size)
    probe = ImageDraw.Draw(Image.new("L", (8, 8), 0))
    l, t, r, b = probe.textbbox((0, 0), shaped, font=font)
    w, h = r - l + 4, b - t + 4
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((2 - l, 2 - t), shaped, font=font, fill=255)
    px = img.load()
    rb = (w + 7) // 8
    data = []
    for y in range(h):
        for xb in range(rb):
            v = 0
            for bit in range(8):
                x = xb * 8 + bit
                if x < w and px[x, y] > 110:
                    v |= 0x80 >> bit
            data.append(v)
    return w, h, data


def main():
    out_path = sys.argv[1]
    lines = [
        "#pragma once",
        "struct ArImg { short w; short h; const unsigned char *bits; };",
    ]
    dims = []
    for i, (key, text, size) in enumerate(ITEMS):
        w, h, data = render(text, size)
        dims.append((w, h))
        lines.append("#define AR_%s %d" % (key, i))
        lines.append("static const unsigned char AR_BITS_%d[] = {%s};"
                     % (i, ",".join(str(b) for b in data)))
    lines.append("#define AR_COUNT %d" % len(ITEMS))
    lines.append("static const ArImg AR_IMGS[AR_COUNT] = {")
    for i, (w, h) in enumerate(dims):
        lines.append("  { %d, %d, AR_BITS_%d }," % (w, h, i))
    lines.append("};")
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("generated", len(ITEMS), "arabic strings")


main()
