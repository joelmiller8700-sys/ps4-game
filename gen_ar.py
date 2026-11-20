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
    ("BOSS_HP", "طاقة الزعيم", 30),
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
    ("T1L0", "المرحلة 1: الغابة الحية - القفز المزدوج والسحق", 44),
    ("T1L1", "اضغط X مرتين للقفز المزدوج، ومربع للاندفاع السريع", 42),
    ("T1L2", "اضغط أسفل + دائرة في الهواء للسحق الأرضي وكسر الأسرار!", 42),
    ("T2L0", "المرحلة 2: الريف الذهبي - الاندفاع والمنصات", 44),
    ("T2L1", "استخدم الاندفاع لعبور الفجوات والمنصات المتحركة", 42),
    ("T2L2", "ابحث عن الجدران السرية وصناديق الكنوز المخفية!", 42),
    ("T3L0", "المرحلة 3: الصحراء الرملية - القفز الجداري", 44),
    ("T3L1", "انزلق على الجدران واضغط X للصعود للأعلى", 42),
    ("T3L2", "احذر من الخفافيش ووحوش الفطر المختبئة!", 42),
    ("T4L0", "المرحلة 4: القمم الثلجية - التوقيت والمناشير", 44),
    ("T4L1", "انتبه للمنصات المتلاشية والمناشير الدوارة", 42),
    ("T4L2", "اجمع الدروع والعملات الذهبية لتطوير قدراتك!", 42),
    ("T5L0", "المرحلة 5: معركة الزعيم - الملك الضفدع فروغوس!", 44),
    ("T5L1", "تفادَ ضربة اللسان الشوكي واقفز فوق رأسه أو اضرب المفتاح", 42),
    ("T5L2", "احذر من السلايم والقفزات الزلزالية لفتح البوابة!", 42),
    ("T10L0", "المرحلة 10: معركة الزعيم - الوحش العملاق والتمثال!", 44),
    ("T10L1", "استخدم الجدران والقفز المزدوج للارتداد فوق رأس الزعيم", 42),
    ("T10L2", "تفادَ القذائف المزدوجة وأنقذ عالم الغابة!", 42),
    ("B_SHOP", "المتجر والمظاهر", 70),
    ("B_ACH", "الإنجازات", 70),
    ("SHOP", "المتجر والمظاهر", 52),
    ("ACH", "الإنجازات", 52),
    ("COINS", "العملات", 30),
    ("TIME_LBL", "الوقت", 30),
    ("SECRETS_LBL", "الأسرار", 30),
    ("KING_FROGUS", "الملك الضفدع فروغوس", 34),
    ("CHOMP_BEAST", "الوحش الوردي العملاق", 34),
    ("FROST_LORD", "سيد الجليد المدرع", 34),
    ("MAGMA_CRUSHER", "محطم الحمم البركانية", 34),
    ("SAW_OVERLORD", "إمبراطور المناشير المظلم", 34),
    ("SHOP_HINT", "X: شراء أو تجهيز   O: رجوع", 34),
    ("BOUGHT", "مجهز", 36),
    ("EQUIP", "اضغط X للتجهيز", 36),
    ("SKIN0", "مكعب اللحم الأصلي", 42),
    ("SKIN1", "المكعب الذهبي الملكي", 42),
    ("SKIN2", "نينجا الظلام السريع", 42),
    ("SKIN3", "مكعب الصقيع الجليدي", 42),
    ("SKIN4", "مكعب الحمم المشتعل", 42),
    ("SKIN5", "مكعب السلايم الزمردي", 42),
    ("UPG0", "تطوير: درع حماية إضافي كل مرحلة", 42),
    ("UPG1", "تطوير: تبريد أسرع للاندفاع السريع", 42),
    ("UPG2", "تطوير: مغناطيس ومضاعف العملات", 42),
    ("ACH0", "أول خطوة: أكمل المرحلة الأولى", 38),
    ("ACH1", "صائد الكنوز: افتح 3 صناديق كنوز سرية", 38),
    ("ACH2", "قاهر الزعماء: اهزم الملك الضفدع فروغوس", 38),
    ("ACH3", "جامع الذهب: اجمع 100 عملة ذهبية", 38),
    ("ACH4", "العداء السريع: أنهِ مرحلة بأقل من 35 ثانية", 38),
    ("ACH5", "الأسطورة: افتح 15 مرحلة في اللعبة", 38),
    ("NPC_MSG0", "الحكيم: اضغط X بالهواء للقفز المزدوج، وأسفل+O للسحق!", 34),
    ("NPC_MSG1", "المستكشف: الجدران المتشققة تخفي خلفها غرف كنوز سرية!", 34),
    ("NPC_MSG2", "الحارس: احذر من لسان الملك الضفدع ووحوش الفطر المختبئة!", 34),
    ("SECRET_FOUND", "تم اكتشاف منطقة سرية!", 40),
    ("CHEST_OPEN", "صندوق كنز! +25 عملة ذهبية", 40),
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
