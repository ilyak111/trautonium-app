"""Рисует иконку приложения: resources/Trautonium.icns и resources/icon.png.

Сюжет — мануал траутониума: проволока натянута над металлической шиной,
палец прижимает её к шине (там и рождается звук), под шиной — шкала-клавиатура,
сверху — окошко с пилой и две ручки, как на панели инструмента.

Запуск из корня проекта: python3 tools/make_icon.py   (нужен Pillow)"""
import math, sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops

S = 4                       # суперсэмплинг
N = 1024 * S
PINK  = (245, 170, 195)
TEAL  = (120, 205, 215)
IVORY = (232, 222, 204)

def P(v):                   # координаты в «иконочных» пикселях 0..1024
    return v * S

def squircle_mask(size, inset, n=5.0):
    m = Image.new("L", (size, size), 0)
    c, r = size / 2, size / 2 - inset
    pts = []
    for i in range(720):
        t = 2 * math.pi * i / 720
        ct, st = math.cos(t), math.sin(t)
        pts.append((c + r * math.copysign(abs(ct) ** (2 / n), ct),
                    c + r * math.copysign(abs(st) ** (2 / n), st)))
    ImageDraw.Draw(m).polygon(pts, fill=255)
    return m

def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))

def vgrad(w, h, top, bot):
    g = Image.new("RGBA", (w, h))
    d = ImageDraw.Draw(g)
    for y in range(h):
        d.line([(0, y), (w, y)], fill=lerp(top, bot, y / max(1, h - 1)))
    return g

def rounded(img, box, r, fill):
    ImageDraw.Draw(img).rounded_rectangle([P(box[0]), P(box[1]), P(box[2]), P(box[3])], radius=P(r), fill=fill)

def main(out_icns, out_png):
    mask = squircle_mask(N, P(100))
    img = vgrad(N, N, (56, 40, 62, 255), (18, 16, 24, 255))
    glow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    dg = ImageDraw.Draw(glow)

    # ---- панель: окошко-осциллограф и две ручки ----
    rounded(img, (210, 214, 814, 404), 34, (28, 24, 34, 255))
    win = (300, 250, 724, 368)
    rounded(img, win, 18, (14, 12, 18, 255))
    # пила f и субгармоника f/2 в окошке
    for k, (col, per, amp, yc, wdt, a) in enumerate([(PINK, 70, 26, 300, 7, 255), (TEAL, 140, 22, 330, 6, 200)]):
        pts, x = [(win[0] + 22, yc + amp)], win[0] + 22
        while x + per <= win[2] - 20:
            x += per
            pts += [(x, yc - amp), (x, yc + amp)]
        pts = [(P(a_), P(b_)) for a_, b_ in pts[:-1]]
        ImageDraw.Draw(img).line(pts, fill=col + (a,), width=P(wdt), joint="curve")
        dg.line(pts, fill=col + (150,), width=P(20), joint="curve")
    # ручки
    d = ImageDraw.Draw(img)
    for cx in (252, 772):
        cy, r = 309, 30
        d.ellipse([P(cx - r - 4), P(cy - r - 4), P(cx + r + 4), P(cy + r + 4)], fill=(10, 9, 13, 255))
        knob = vgrad(P(2 * r), P(2 * r), (205, 205, 214, 255), (110, 108, 120, 255))
        km = Image.new("L", knob.size, 0)
        ImageDraw.Draw(km).ellipse([0, 0, knob.size[0] - 1, knob.size[1] - 1], fill=255)
        img.paste(knob, (P(cx - r), P(cy - r)), km)
        ang = math.radians(-130 if cx < 512 else -50)
        d.line([(P(cx), P(cy)), (P(cx + 20 * math.cos(ang)), P(cy + 20 * math.sin(ang)))], fill=PINK + (255,), width=P(6))

    # ---- шкала-клавиатура под шиной ----
    kx0, kx1, ky0, ky1 = 196, 828, 706, 786
    rounded(img, (kx0 - 6, ky0 - 6, kx1 + 6, ky1 + 6), 14, (24, 21, 28, 255))
    nwhite = 14
    ww = (kx1 - kx0) / nwhite
    for i in range(nwhite):
        x0 = kx0 + i * ww
        rounded(img, (x0 + 1.5, ky0, x0 + ww - 1.5, ky1), 4, IVORY + (255,))
    for i in range(nwhite - 1):
        if i % 7 in (2, 6):          # между E–F и B–C чёрных нет
            continue
        x = kx0 + (i + 1) * ww
        rounded(img, (x - ww * 0.3, ky0, x + ww * 0.3, ky0 + (ky1 - ky0) * 0.6), 3, (34, 30, 38, 255))

    # ---- шина ----
    rx0, rx1, ry0, ry1 = 180, 844, 640, 684
    rail = vgrad(P(rx1 - rx0), P(ry1 - ry0), (236, 236, 242, 255), (120, 118, 130, 255))
    rm = Image.new("L", rail.size, 0)
    ImageDraw.Draw(rm).rounded_rectangle([0, 0, rail.size[0] - 1, rail.size[1] - 1], radius=P(16), fill=255)
    img.paste(rail, (P(rx0), P(ry0)), rm)

    # ---- проволока: от стоек к точке, где палец прижал её к шине ----
    lx, rxp, wy = 168, 856, 505
    px, py = 598, ry0 - 3
    for x in (lx, rxp):              # стойки
        rounded(img, (x - 12, wy - 6, x + 12, ry1), 6, (150, 148, 160, 255))
        d.ellipse([P(x - 13), P(wy - 13), P(x + 13), P(wy + 13)], fill=(200, 200, 210, 255))
    wire = [(P(lx), P(wy)), (P(px), P(py)), (P(rxp), P(wy))]
    dg.line(wire, fill=PINK + (230,), width=P(24), joint="curve")
    d.line(wire, fill=PINK + (255,), width=P(8), joint="curve")

    # палец — светящаяся точка нажатия
    dg.ellipse([P(px - 95), P(py - 115), P(px + 95), P(py + 75)], fill=PINK + (170,))
    d.ellipse([P(px - 34), P(py - 72), P(px + 34), P(py - 4)], fill=(255, 236, 243, 255))

    glow = glow.filter(ImageFilter.GaussianBlur(P(22)))
    img = Image.alpha_composite(img, glow)
    # повторно поверх свечения — чёткие детали
    d = ImageDraw.Draw(img)
    d.line(wire, fill=PINK + (255,), width=P(8), joint="curve")
    d.ellipse([P(px - 34), P(py - 72), P(px + 34), P(py - 4)], fill=(255, 236, 243, 255))

    # тень и маска
    out = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    sh = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    sh.putalpha(ImageChops.multiply(mask.filter(ImageFilter.GaussianBlur(P(14))), Image.new("L", (N, N), 110)))
    out = Image.alpha_composite(out, ImageChops.offset(sh, 0, P(10)))
    img.putalpha(ImageChops.multiply(img.getchannel("A"), mask))
    out = Image.alpha_composite(out, img)

    out = out.resize((1024, 1024), Image.LANCZOS)
    out.save(out_png)
    out.save(out_icns)

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "resources/Trautonium.icns",
         sys.argv[2] if len(sys.argv) > 2 else "resources/icon.png")
