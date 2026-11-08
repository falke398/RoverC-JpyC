"""Render PNG mockups of portrait layout proposals for the 135x240 M5StickC PLUS screen."""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

W, H, SCALE = 135, 240, 4
OUT = Path(__file__).resolve().parent.parent / "docs" / "display"

BLACK, WHITE = (0, 0, 0), (255, 255, 255)
GREEN, AMBER, RED = (0, 200, 80), (255, 176, 0), (220, 40, 40)
GREY, DARK = (120, 120, 120), (40, 40, 40)

ROLE = {"controller": "JOYC", "rover": "ROVER"}
CAP = 50

STATES = {
    "armed": dict(label="ARMED", color=GREEN, ink=BLACK, fault=None, link=True, hint=""),
    "fault": dict(label="FAULT", color=RED, ink=WHITE, fault="LINK LOST", link=False,
                  hint="Release sticks,\nthen re-arm"),
}


def font(size, bold=True, mono=False):
    names = ("consola.ttf",) if mono else (("arialbd.ttf",) if bold else ("arial.ttf",))
    for name in names + ("DejaVuSans-Bold.ttf",):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def new(bg=BLACK):
    img = Image.new("RGB", (W, H), bg)
    return img, ImageDraw.Draw(img)


def center(d, xy, text, size, fill, bold=True, max_w=W - 8):
    """Draw centered text, shrinking the font until it fits the screen width."""
    f = font(size, bold)
    while size > 8 and max(d.textlength(line, font=f) for line in text.split("\n")) > max_w:
        size -= 1
        f = font(size, bold)
    d.multiline_text(xy, text, font=f, fill=fill, anchor="mm", align="center")


def hbar(d, x, y, w, h, value, signed=False):
    d.rectangle((x, y, x + w, y + h), outline=GREY)
    if signed:
        mid = x + w // 2
        d.line((mid, y, mid, y + h), fill=GREY)
        end = mid + int(value * w / 2)
        d.rectangle((min(mid, end), y + 1, max(mid, end), y + h - 1), fill=GREEN if value >= 0 else AMBER)
    else:
        d.rectangle((x + 1, y + 1, x + int(value * w), y + h - 1), fill=GREEN)


def header(d, role, s, with_cap=False):
    d.rectangle((0, 0, W, 26), fill=s["color"])
    d.text((6, 13), ROLE[role], font=font(14), fill=s["ink"], anchor="lm")
    d.text((W - 6, 13), f"{CAP}%" if with_cap else s["label"], font=font(14), fill=s["ink"], anchor="rm")


def a_banner(role, s):
    img, d = new()
    d.rectangle((0, 0, W, 100), fill=s["color"])
    center(d, (W // 2, 16), ROLE[role], 14, s["ink"])
    center(d, (W // 2, 62), s["label"], 36, s["ink"])
    center(d, (W // 2, 140), f"{CAP}%", 56, WHITE)
    center(d, (W // 2, 184), "Link OK" if s["link"] else "Link LOST", 22, GREEN if s["link"] else RED)
    center(d, (W // 2, 222), s["fault"] or "no fault", 16, RED if s["fault"] else GREY, bold=bool(s["fault"]))
    return img


def b_dashboard(role, s):
    img, d = new()
    header(d, role, s)
    if role == "controller":
        rows = [("SPEED", CAP / 60, False), ("FWD", 0.6, True), ("TURN", -0.25, True), ("CRAB", 0.4, True)]
        dots = ["Link OK", "Radio OK", "Joy OK"]
    else:
        rows = [("SPEED", CAP / 60, False), ("FL", 0.6, True), ("FR", 0.4, True),
                ("RL", 0.6, True), ("RR", 0.4, True)]
        dots = ["Link OK", "Radio OK", "I2C OK"]
    y = 38
    for label, value, signed in rows:
        d.text((6, y + 5), label, font=font(11), fill=WHITE, anchor="lm")
        hbar(d, 44, y, 84, 10, value, signed)
        y += 24
    y += 6
    for name in dots:
        d.ellipse((10, y - 4, 18, y + 4), fill=GREEN)
        d.text((26, y), name, font=font(13), fill=WHITE, anchor="lm")
        y += 20
    return img


def c_page1(role, s):
    img, d = new()
    center(d, (W // 2, 40), s["label"], 34, s["color"])
    center(d, (W // 2, 110), f"{CAP}%", 60, WHITE)
    center(d, (W // 2, 170), "Link OK", 22, GREEN)
    d.ellipse((W // 2 - 14, 224, W // 2 - 8, 230), fill=WHITE)
    d.ellipse((W // 2 + 8, 224, W // 2 + 14, 230), outline=GREY)
    return img


def c_page2(role, s):
    img, d = new()
    d.rectangle((0, 0, W, 18), fill=DARK)
    d.text((6, 9), f"{ROLE[role]} DIAG", font=font(12), fill=WHITE, anchor="lm")
    d.ellipse((W - 26, 6, W - 20, 12), outline=GREY)
    d.ellipse((W - 14, 6, W - 8, 12), fill=WHITE)
    if role == "controller":
        lines = [("MAC", GREY), ("F0:24:F9:96:A2:CC", WHITE), ("", WHITE), ("Radio OK", GREEN),
                 ("Joy OK", GREEN), ("", WHITE), ("raw", GREY), ("97 98 103 102", WHITE),
                 ("btn 00", WHITE), ("", WHITE), ("Move 40 60 -25", WHITE), ("Grip 0", WHITE),
                 ("Cal: saved", WHITE), ("Fault 0", WHITE)]
    else:
        lines = [("MAC", GREY), ("F0:24:F9:97:2E:00", WHITE), ("", WHITE), ("Radio OK", GREEN),
                 ("I2C OK", GREEN), ("", WHITE), ("Peer", GREY), ("5A1C09E3", WHITE),
                 ("Ack 1842", WHITE), ("", WHITE), ("Wheels", GREY), ("30 20 30 20", WHITE),
                 ("Grip 0", WHITE), ("Fault 0", WHITE)]
    for i, (text, color) in enumerate(lines):
        d.text((6, 30 + i * 15), text, font=font(12, mono=True), fill=color, anchor="lm")
    return img


def stick_box(d, x, y, size, px, py, label):
    cx, cy = x + size // 2, y + size // 2
    d.rectangle((x, y, x + size, y + size), outline=GREY)
    d.line((x, cy, x + size, cy), fill=DARK)
    d.line((cx, y, cx, y + size), fill=DARK)
    dx, dy = cx + int(px * size / 2), cy - int(py * size / 2)
    d.line((cx, cy, dx, dy), fill=GREY)
    d.ellipse((dx - 6, dy - 6, dx + 6, dy + 6), fill=GREEN)
    center(d, (cx, y - 9), label, 12, WHITE)


def d_visual(role, s):
    img, d = new()
    header(d, role, s, with_cap=True)
    if role == "controller":
        stick_box(d, 29, 46, 76, -0.25, 0.6, "TURN / FWD")
        stick_box(d, 29, 152, 76, 0.4, 0.0, "CRAB")
    else:
        center(d, (W // 2, 36), "FRONT", 11, GREY, bold=False)
        d.rectangle((52, 46, 83, 196), outline=GREY, fill=DARK)
        for (x, y), value, left in [((28, 46), 0.6, True), ((87, 46), 0.4, False),
                                    ((28, 136), 0.6, True), ((87, 136), 0.4, False)]:
            d.rectangle((x, y, x + 20, y + 60), outline=WHITE)
            mid, length = y + 30, int(value * 30)
            d.rectangle((x + 1, mid - length, x + 19, mid), fill=GREEN)
            d.text((x - 4 if left else x + 24, mid), str(int(value * CAP)), font=font(14),
                   fill=WHITE, anchor="rm" if left else "lm")
        center(d, (W // 2, 220), f"CAP {CAP}%", 14, WHITE)
    return img


def e_hud(role, s):
    if s["fault"]:
        img, d = new(RED)
        center(d, (W // 2, 80), s["fault"].replace(" ", "\n"), 44, WHITE)
        center(d, (W // 2, 172), s["hint"], 18, WHITE, bold=False)
        return img
    img, d = new()
    center(d, (W // 2, 36), s["label"], 34, s["color"])
    center(d, (W // 2, 80), f"SPEED {CAP}%", 14, WHITE)
    for k in range(6):
        top = 96 + (5 - k) * 22
        d.rectangle((17, top, 118, top + 18), fill=GREEN if k < CAP // 10 else DARK)
    return img


def save(img, name):
    OUT.mkdir(parents=True, exist_ok=True)
    img.resize((W * SCALE, H * SCALE), Image.NEAREST).save(OUT / name)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("*.png"):
        old.unlink()
    for role in ROLE:
        for key in ("armed", "fault"):
            save(a_banner(role, STATES[key]), f"{role}_a_banner_{key}.png")
            save(e_hud(role, STATES[key]), f"{role}_e_hud_{key}.png")
        armed = STATES["armed"]
        save(b_dashboard(role, armed), f"{role}_b_dashboard.png")
        save(c_page1(role, armed), f"{role}_c_two_page_1_driving.png")
        save(c_page2(role, armed), f"{role}_c_two_page_2_diagnostics.png")
        save(d_visual(role, armed), f"{role}_d_visual.png")


if __name__ == "__main__":
    main()
