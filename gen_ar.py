import sys
from PIL import Image, ImageDraw, ImageFont
import arabic_reshaper
try:
    from bidi.algorithm import get_display
except Exception:
    from bidi import get_display

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

try:
    LAYOUT_BASIC = ImageFont.Layout.BASIC
except Exception:
    LAYOUT_BASIC = ImageFont.LAYOUT_BASIC

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
    ("CTRL_ANALOG", "التحكم: أنلوج", 52),
    ("CTRL_ARROWS", "التحكم: أسهم", 52),
    ("LANG", "اللغة: العربية", 52),
    ("SELECT", "X: اختيار", 34),
    ("LEVELS_HINT", "X: لعب   O: رجوع", 34),
    ("BACK_HINT", "O: رجوع", 34),
    ("CHANGE_TIP", "يمين أو يسار أو X للتغيير", 34),
    ("STORY_HINT", "X: التالي   OPTIONS: تخطي", 34),
    ("PLAYX", "اضغط X للعب", 40),
    ("LOCKED", "مقفولة", 40),
    ("DESIGNERS", "مصممو اللعبة", 60),
    ("KAREEM", "كريم", 100),
    ("CLAUDE", "كلود", 100),
    ("GAME_NAME", "مكعب اللحم", 44),
    ("DEATHS", "الموتات", 30),
    ("COMPLETE", "اكتملت المرحلة", 90),
    ("GREAT", "عمل رائع", 60),
    ("P0L0", "مكعب اللحم وبنت الضمادة", 50),
    ("P0L1", "عاشا في الغابة القديمة", 50),
    ("P1L0", "في ليلة جاء الطحان", 50),
    ("P1L1", "وأخذت مناشيره بنت الضمادة بعيداً", 50),
    ("P2L0", "استيقظ مكعب اللحم غاضباً", 50),
    ("P2L1", "سأعيدك مهما كان!", 50),
    ("P3L0", "آثار المناشير تتجه شرقاً", 50),
    ("P3L1", "نحو الوادي الصخري", 50),
    ("P4L0", "ينتهي الوادي عند وادٍ أخضر", 50),
    ("P4L1", "وتقف أطلال حجرية بين الأعشاب", 50),
    ("P5L0", "تغوص الآثار تحت التل", 50),
    ("P5L1", "داخل كهف بلوري مظلم", 50),
    ("P6L0", "ينفتح الكهف على المصنع", 50),
    ("P6L1", "والجدران مبللة بالدماء", 50),
    ("P7L0", "يرتفع البرج أمامه", 50),
    ("P7L1", "وبنت الضمادة بالداخل بلا شك", 50),
    ("P8L0", "وجد في القمة ورقة فقط", 50),
    ("P8L1", "إنها في مكان آخر", 50),
    ("P8L2", "يتبع...", 50),
    ("TIP_RUN0", "اضغط زر المربع للركض", 40),
    ("TIP_RUN1", "اركض ثم اقفز لتعبر الفجوات الكبيرة", 40),
    ("TIP_RUN2", "واصل الركض. الفجوات تصبح أعلى", 40),
    ("TIP_GRASS", "العشب يتحرك عندما تمشي عليه", 40),
    ("TIP_WALL", "ادفع نحو الجدار ثم اقفز لتتسلق", 40),
    ("TIP_WALL2", "واصل التسلق. أنت قادر على ذلك", 40),
    ("TIP_SAW", "لا تقفز تحت المناشير. فقط امشِ", 40),
    ("TIP_HOP", "المس زر X لقفزة قصيرة", 40),
    ("TIP_MIX", "استخدم كل ما تعلمته", 40),
    ("TIP_FINAL", "البرج. لا مزيد من النصائح", 40),
]


def visual(text):
    shaped = arabic_reshaper.reshape(text)
    try:
        return get_display(shaped, base_dir="R")
    except TypeError:
        return get_display(shaped)


def render(text, size):
    shaped = visual(text)
    font = ImageFont.truetype(FONT, size, layout_engine=LAYOUT_BASIC)
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
