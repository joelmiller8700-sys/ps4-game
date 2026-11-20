import sys
from PIL import Image, ImageDraw, ImageFont
import arabic_reshaper
try:
    from bidi.algorithm import get_display
except Exception:
    from bidi import get_display

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

ITEMS = [
    ("TITLE", "مكعب اللحم", 96),
    ("PLAY", "ابدأ المغامرة", 48),
    ("SHOP", "المتجر والتطويرات", 48),
    ("SKINS", "مظاهر الشخصية", 48),
    ("ACH", "الإنجازات", 48),
    ("TIMETRIAL", "تحدي الوقت", 48),
    ("SETTINGS", "الإعدادات", 48),
    ("CREDITS", "المصممون", 48),
    ("EXIT", "خروج", 48),
    ("BACK", "رجوع", 48),
    ("B_LEVELS", "خريطة العوالم والمراحل", 62),
    ("B_SHOP", "متجر التطويرات", 66),
    ("B_SKINS", "اختر مظهرك", 66),
    ("B_ACH", "الإنجازات والجوائز", 66),
    ("B_SETTINGS", "الإعدادات", 66),
    ("B_CREDITS", "المصممون", 66),
    ("CTRL_ANALOG", "التحكم: أنالوج", 48),
    ("CTRL_ARROWS", "التحكم: الأسهم", 48),
    ("LANG", "اللغة: العربية", 48),
    ("SELECT", "X: اختيار   O: رجوع", 32),
    ("LEVELS_HINT", "X: لعب   مربع: تحدي الوقت   O: رجوع", 32),
    ("BACK_HINT", "O: رجوع", 32),
    ("CHANGE_TIP", "يمين أو يسار أو X للتغيير", 32),
    ("STORY_HINT", "X: التالي   OPTIONS: تخطي", 32),
    ("PLAYX", "اضغط X للعب", 38),
    ("LOCKED", "مغلقة", 38),
    ("DESIGNERS", "مصممو اللعبة", 58),
    ("KAREEM", "كريم", 96),
    ("CLAUDE", "كلود", 96),
    ("GAME_NAME", "مكعب اللحم - نسخة المغامرة", 42),
    ("COMPLETE", "اكتملت المرحلة!", 86),
    ("GREAT", "عمل أسطوري!", 56),
    ("W_FOREST", "العالم 1: الغابة الحية", 36),
    ("W_VOLCANO", "العالم 2: قلب البركان", 36),
    ("W_ICE", "العالم 3: القمم الجليدية", 36),
    ("W_CASTLE", "العالم 4: القلعة المظلمة", 36),
    ("W_SECRET", "المرحلة السرية: معبد الظل", 36),
    ("B_FROGUS", "الملك فروغوس - حارس الغابة", 32),
    ("B_SPIDER", "العنكبوت البركاني العملاق", 32),
    ("B_DEVOURER", "الملتهم الجليدي الهائج", 32),
    ("B_OVERLORD", "سيد القلعة المظلمة", 32),
    ("UP_HP", "قلب إضافي (+1 صحة قصوى)", 40),
    ("UP_DASH", "اندفاع أسرع (تقليل الانتظار)", 40),
    ("UP_MAGNET", "مغناطيس العملات الذهبية", 40),
    ("UP_SLAM", "ضربة أرضية خارقة (مدى أوسع)", 40),
    ("BOUGHT", "مملوك", 36),
    ("NEED_COINS", "عملات غير كافية", 36),
    ("SK_0", "المكعب الأحمر الكلاسيكي", 40),
    ("SK_1", "المكعب الذهبي الملكي", 40),
    ("SK_2", "مكعب الظل الليلي", 40),
    ("SK_3", "مكعب الجليد البلوري", 40),
    ("SK_4", "مكعب الحمم المتوهج", 40),
    ("SECRET_FOUND", "تم اكتشاف غرفة سرية!", 38),
    ("CHEST_OPEN", "صندوق كنز! +25 عملة", 38),
    ("NPC_HINT", "اضغط مثلث للتحدث", 28),
    ("NPC_0", "الحكيم: استخدم القفز المزدوج X والاندفاع O لكشف الأسرار!", 34),
    ("NPC_1", "المستكشف: اضغط أسفل + مربع في الهواء لضربة أرضية ساحقة!", 34),
    ("NPC_2", "الحارس: هناك غرف سرية وصناديق كنوز في الأعالي!", 34),
    ("NPC_3", "التاجر: اجمع العملات واشترِ تطويرات ومظاهر من المتجر!", 34),
    ("ACH_0", "البداية: أكمل المرحلة الأولى", 36),
    ("ACH_1", "صائد الكنوز: افتح صندوق كنز مخفي", 36),
    ("ACH_2", "قاهر الزعماء: اهزم أول زعيم", 36),
    ("ACH_3", "جامع الثروة: اجمع 50 عملة ذهبية", 36),
    ("ACH_4", "المستكشف: اكتشف غرفة سرية", 36),
    ("ACH_5", "البرق السريع: أنهِ مرحلة في تحدي الوقت", 36),
    ("ACH_UNLOCKED", "تم فتح إنجاز جديد!", 34),
    ("P0L0", "مكعب اللحم وفتاة الضمادة", 48),
    ("P0L1", "عاشا بسلام في الغابة القديمة الحية", 48),
    ("P1L0", "في ليلة مظلمة غزت الوحوش والزعماء العوالم الأربعة", 46),
    ("P1L1", "واختطفوا فتاة الضمادة نحو القلعة المظلمة", 46),
    ("P2L0", "انطلق مكعب اللحم بقدراته الجديدة: القفز المزدوج والاندفاع", 44),
    ("P2L1", "سأعبر الغابة والبركان والجليد لأستعيدك!", 46),
    ("P3L0", "انتصر مكعب اللحم على سيد القلعة المظلمة!", 48),
    ("P3L1", "وعاد السلام إلى جميع العوالم", 48),
    ("P3L2", "هل اكتشفت المرحلة السرية في معبد الظل؟", 46),
    ("P4L0", "النهاية الحقيقية - شكرا للعبك!", 48),
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
