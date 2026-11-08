"""Render PNGs of the screens the firmware currently draws (240x135, M5GFX font 0 at text sizes 1 and 2)."""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

W, H, SCALE = 240, 135, 4
OUT = Path(__file__).resolve().parent.parent / "docs" / "display" / "current"
WHITE, BLACK = (255, 255, 255), (0, 0, 0)

CONTROLLER_MAC = "F0:24:F9:96:A2:CC"
ROVER_MAC = "F0:24:F9:97:2E:00"


def mono(size):
    for name in ("consola.ttf", "DejaVuSansMono.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def draw_lines(lines):
    """Each line is (text, text_size); glyph cell is 6x8 px times the text size, as on the device."""
    img = Image.new("RGB", (W, H), BLACK)
    d = ImageDraw.Draw(img)
    y = 0
    for text, size in lines:
        f = mono(round(10.9 * size))
        for i, ch in enumerate(text[: W // (6 * size)]):
            d.text((i * 6 * size, y), ch, font=f, fill=WHITE)
        y += 8 * size
    return img


def controller(armed, link, move, calibration="Calibration loaded"):
    return draw_lines([
        (f"JOYC  {'ARMED' if armed else 'DISARMED'}  50%", 2),
        (f"Link:{'OK' if link else 'LOST'} Fault:0", 2),
        (f"MAC {CONTROLLER_MAC}", 1),
        ("Radio: OK", 1),
        ("Joy:OK raw 97 98 103 102 btn:00", 1),
        (f"Move {move} Grip:0", 1),
        ("Motion:enabled Grip:enabled", 1),
        (calibration, 1),
    ])


def rover(armed, fault, wheels):
    return draw_lines([
        (f"ROVER {'ARMED' if armed else 'DISARMED'}", 2),
        (f"Fault:{fault} I2C:OK", 2),
        (f"MAC {ROVER_MAC}", 1),
        ("Radio: OK", 1),
        (f"Wheels {wheels}", 1),
        ("Motion:enabled Grip:enabled", 1),
        ("Peer 5A1C09E3 Ack 1842 Grip:0", 1),
    ])


def save(img, name):
    OUT.mkdir(parents=True, exist_ok=True)
    img.resize((W * SCALE, H * SCALE), Image.NEAREST).save(OUT / name)


def main():
    save(draw_lines([("JOYC starting", 2)]), "controller_startup.png")
    save(controller(False, True, "0 0 0"), "controller_disarmed.png")
    save(controller(True, True, "-25 60 40"), "controller_armed.png")
    save(controller(False, False, "0 0 0"), "controller_link_lost.png")
    save(draw_lines([("ROVER starting", 2)]), "rover_startup.png")
    save(rover(False, 0, "0 0 0 0"), "rover_disarmed.png")
    save(rover(True, 0, "30 20 30 20"), "rover_armed.png")
    save(rover(False, 1, "0 0 0 0"), "rover_link_fault.png")


if __name__ == "__main__":
    main()
