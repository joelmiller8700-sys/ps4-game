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
    ("T1L0", "المرحلة 1: الغابة الطبيعية - مهارة القفز", 44),
    ("T1L1", "اضغط X للقفز القصير، واضغط مطولا لقفزة عالية", 42),
    ("T1L2", "تحرك عبر الأعشاب وتجاوز الأشواك بسلام!", 42),
    ("T2L0", "المرحلة 2: الريف الذهبي - مهارة الركض السريع", 44),
    ("T2L1", "اضغط باستمرار على زر مربع للركض بأقصى سرعة", 42),
    ("T2L2", "اركض واقفز بـ X لتخطي الحفر الواسعة!", 42),
    ("T3L0", "المرحلة 3: الصحراء الرملية - القفز الجداري", 44),
    ("T3L1", "اتجه نحو الجدار للانزلاق عليه ببطء وأمان", 42),
    ("T3L2", "اضغط X أثناء لمس الجدار للقفز بين الجدران!", 42),
    ("T4L0", "المرحلة 4: القمم الثلجية - التوقيت والمناشير", 44),
    ("T4L1", "راقب المناشير الدوارة واختر لحظة القفز بدقة", 42),
    ("T4L2", "اقفز من أقصى حافة المنصة لعبور المسافات!", 42),
    ("T5L0", "المرحلة 5: وادي الحمم - الاحتراف الكامل", 44),
    ("T5L1", "ادمج الركض السريع بمربع مع القفز الجداري بـ X", 42),
    ("T5L2", "تجاوز كل العقبات للوصول إلى قلعة الطاحن!", 42),
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
