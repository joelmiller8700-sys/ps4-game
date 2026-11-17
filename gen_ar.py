import sys
from PIL import Image, ImageDraw, ImageFont
import arabic_reshaper
try:
    from bidi.algorithm import get_display
except Exception:
    from bidi import get_display

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

ITEMS = [
    ("TITLE", "مكعب اللحم", 100),
    ("B_LEVELS", "المراحل", 70),
    ("B_SETTINGS", "الإعدادات", 70),
    ("B_CREDITS", "المصممون", 70),
    ("LEVELS", "المراحل", 52),
    ("SETTINGS", "الإعدادات", 52),
    ("CREDITS", "المصممون", 52),
    ("EXIT", "خروج", 52),
    ("BACK", "رجوع", 52),
    ("CTRL_ANALOG", "التحكم: أنالوج", 52),
    ("CTRL_ARROWS", "التحكم: الأسهم", 52),
    ("LANG", "اللغة: العربية", 52),
    ("SELECT", "X: اختيار", 34),
    ("LEVELS_HINT", "X: لعب   O: رجوع", 34),
    ("BACK_HINT", "O: رجوع", 34),
    ("CHANGE_TIP", "اليمين أو اليسار أو X للتغيير", 34),
    ("STORY_HINT", "X: التالي   OPTIONS: تخطي", 34),
    ("PLAYX", "اضغط X للعب", 40),
    ("LOCKED", "مغلقة", 40),
    ("DESIGNERS", "مصممو اللعبة", 60),
    ("KAREEM", "كريم", 100),
    ("CLAUDE", "كلود", 100),
    ("GAME_NAME", "مكعب اللحم", 44),
    ("DEATHS", "مرات الموت", 30),
    ("COMPLETE", "اكتملت المرحلة", 90),
    ("GREAT", "عمل رائع!", 60),
    ("P0L0", "مكعب اللحم وفتاة الضمادة", 50),
    ("P0L1", "عاشا بسلام في الغابة القديمة", 50),
    ("P1L0", "وفي إحدى الليالي جاء الطاحن", 50),
    ("P1L1", "واختطفت مناشيره فتاة الضمادة بعيدا", 50),
    ("P2L0", "استيقظ مكعب اللحم غاضبا", 50),
    ("P2L1", "سأستعيدك مهما كلف الأمر!", 50),
    ("P3L0", "وصل مكعب اللحم إلى البرج", 50),
    ("P3L1", "لكنه لم يجدها هناك", 50),
    ("P3L2", "وجد رسالة فقط: اتبع المناشير", 50),
    ("P4L0", "يتبع...", 50),
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
