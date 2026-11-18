import sys
from PIL import Image, ImageDraw, ImageFont
import arabic_reshaper
try:
    from bidi.algorithm import get_display
except Exception:
    from bidi import get_display

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

ITEMS = [
    ("TITLE", "كابوس المنزل القديم", 92),
    ("B_SETTINGS", "الإعدادات", 70),
    ("B_CREDITS", "المصممون", 70),
    ("START", "ابدأ اللعبة", 54),
    ("SETTINGS", "الإعدادات", 52),
    ("CREDITS", "المصممون", 52),
    ("EXIT", "خروج", 52),
    ("BACK", "رجوع", 52),
    ("CTRL_ANALOG", "التحكم: أنالوج", 52),
    ("CTRL_ARROWS", "التحكم: الأسهم", 52),
    ("LANG", "اللغة: العربية", 52),
    ("SELECT", "X: اختيار", 34),
    ("BACK_HINT", "O: رجوع", 34),
    ("CHANGE_TIP", "اليمين أو اليسار أو X للتغيير", 34),
    ("STORY_HINT", "X: تخطي المشهد السينمائي", 34),
    ("DESIGNERS", "مصممو اللعبة", 60),
    ("KAREEM", "كريم", 100),
    ("CLAUDE", "كلود", 100),
    ("GAME_NAME", "كابوس المنزل القديم 3D", 44),
    ("BOOKS", "الكتب:", 32),
    ("FLOOR1", "الطابق الأرضي", 30),
    ("FLOOR2", "الطابق الثاني", 30),
    ("STAIRS", "الدرج الخشبي", 30),
    ("FLASH_ON", "المصباح: مضاء (مثلث)", 26),
    ("FLASH_OFF", "المصباح: مطفأ (مثلث)", 26),
    ("NOISE_LOW", "الصوت: هادئ (مربع: ركض / O: تسلل)", 26),
    ("NOISE_HIGH", "الصوت: مرتفع! الوحش يسمع خطواتك!", 28),
    ("STALKER_NEAR", "حذار! الكيان المطارد قريب جدا!", 34),
    ("SPEED_UP", "ازدادت سرعة الوحش بعد جمع الكتب!", 38),
    ("PICK_BOOK", "اضغط X لالتقاط الكتاب", 36),
    ("DOOR_LOCKED", "الباب الرئيسي مغلق! اجمع 6 كتب للهروب", 34),
    ("DOOR_OPEN", "فتح الباب الرئيسي في الطابق الأرضي! اهرب فورا!", 36),
    ("ESCAPE_NOW", "اضغط X للهروب من الباب الرئيسي!", 38),
    ("INTRO_1", "دخلت المنزل القديم للبحث عن الكتب الستة...", 40),
    ("INTRO_2", "أغلق الباب الرئيسي خلفك! الكيان يراقبك في الظلام...", 40),
    ("INTRO_3", "الوحش يسمع صوت ركضك! اجمع 6 كتب واهرب من الباب الرئيسي", 38),
    ("OUTRO_1", "فتحت الباب الرئيسي وهربت من المنزل القديم بنجاح!", 42),
    ("OUTRO_2", "نجوت بحياتك مع الكتب الستة... عمل أسطوري!", 46),
    ("CAUGHT_1", "أمسك بك الكيان المطارد في الظلام!", 52),
    ("CAUGHT_2", "اضغط X لإعادة المحاولة أو OPTIONS للقائمة", 38),
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
