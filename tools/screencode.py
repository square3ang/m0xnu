#!/usr/bin/env python3
"""Screen <-> camera bit channel for bring-up without UART.

The phone screen (720x1280 portrait) shows a white frame and a 4x8 grid of cells.
Corner cells 0, 3, 28, 31 are references (white, black, black, black) so the decoder
can pick the orientation and the on/off threshold; the other 28 cells carry data bits,
LSB first, row-major.

  render <value> <out.png>   draw a pattern
  decode [image]             decode a camera frame (default: grab one from /dev/video0)
"""
import subprocess, sys
import cv2
import numpy as np

W, H, COLS, ROWS = 720, 1280, 4, 8
FRAME, GAP = 40, 32  # frame must stay a few camera pixels thick at ~0.25 px/screen px
# references are the 4 corners: any rotation/mirror maps corners onto corners, so the single
# white corner fixes orientation without data cells ever taking part
REF = {0: 1, 3: 0, 28: 0, 31: 0}
DATA = [i for i in range(COLS * ROWS) if i not in REF]
NBITS = len(DATA)  # 28

GX0 = GY0 = FRAME + GAP
CW, CH = (W - 2 * GX0) // COLS, (H - 2 * GY0) // ROWS


def cell_rect(i):
    x, y = GX0 + (i % COLS) * CW, GY0 + (i // COLS) * CH
    return x, y, x + CW, y + CH


def render(value):
    assert 0 <= value < 1 << NBITS
    img = np.zeros((H, W), np.uint8)
    img[:FRAME, :] = img[-FRAME:, :] = img[:, :FRAME] = img[:, -FRAME:] = 255
    bits = dict(REF)
    bits.update({c: (value >> k) & 1 for k, c in enumerate(DATA)})
    for i, b in bits.items():
        if b:
            x0, y0, x1, y1 = cell_rect(i)
            img[y0 + 6:y1 - 6, x0 + 6:x1 - 6] = 255  # small black gutter keeps cells separable
    return img


def grab():
    out = subprocess.run(["ffmpeg", "-loglevel", "error", "-f", "v4l2", "-video_size", "640x480",
                          "-i", "/dev/video0", "-vf", "select=gte(n\\,20)", "-frames:v", "1",
                          "-f", "image2pipe", "-vcodec", "png", "-"], capture_output=True, check=True).stdout
    return cv2.imdecode(np.frombuffer(out, np.uint8), cv2.IMREAD_COLOR)


def quads(gray):
    """Candidate quadrilaterals (the frame, but also the phone body, desk...), ordered so
    p0->p1 is a short edge. The caller keeps the one whose reference cells decode best."""
    _, th = cv2.threshold(cv2.GaussianBlur(gray, (5, 5), 0), 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    cnts, _ = cv2.findContours(th, cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
    for c in sorted(cnts, key=cv2.contourArea, reverse=True)[:12]:
        if cv2.contourArea(c) < 500:
            break
        q = cv2.approxPolyDP(c, 0.03 * cv2.arcLength(c, True), True)
        if len(q) == 4:
            p = q.reshape(4, 2).astype(np.float32)
            if np.linalg.norm(p[0] - p[1]) > np.linalg.norm(p[1] - p[2]):
                p = np.roll(p, -1, axis=0)
            yield p


def sample(warp):
    v = []
    for i in range(COLS * ROWS):
        x0, y0, x1, y1 = cell_rect(i)
        mx, my = (x1 - x0) // 4, (y1 - y0) // 4
        v.append(float(warp[y0 + my:y1 - my, x0 + mx:x1 - mx].mean()))
    return v


def decode(img):
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    dst = np.float32([[0, 0], [W, 0], [W, H], [0, H]])
    best = None
    for p in quads(gray):
        # contour winding is unknown (mirror) and so is 0/180 rotation: try all 4, keep best refs
        for q in (p, p[[1, 0, 3, 2]]):
            for shift in (0, 2):
                m = cv2.getPerspectiveTransform(np.roll(q, -shift, axis=0), dst)
                v = sample(cv2.warpPerspective(gray, m, (W, H)))
                white, black = v[0], max(v[3], v[28], v[31])
                if best is None or white - black > best[0]:
                    best = (white - black, v, (white + black) / 2)
    if best is None:
        raise RuntimeError("screen frame not found")
    margin, v, thr = best
    if margin < 20:
        raise RuntimeError(f"reference contrast too low ({margin:.0f})")
    return sum(1 << k for k, c in enumerate(DATA) if v[c] > thr), margin


def selftest():
    # synthetic camera view: shrink, rotate 180, add perspective + blur + noise
    val = 0x0A5C3E1
    pat = cv2.cvtColor(render(val), cv2.COLOR_GRAY2BGR)
    src = np.float32([[0, 0], [W, 0], [W, H], [0, H]])
    dst = np.float32([[470, 400], [460, 300], [170, 280], [150, 380]])  # roughly what the webcam sees
    cam = cv2.warpPerspective(pat, cv2.getPerspectiveTransform(src, dst), (640, 480))
    cam = cv2.GaussianBlur(cam, (5, 5), 0)
    cam = np.clip(cam * 0.6 + 30 + np.random.default_rng(0).normal(0, 6, cam.shape), 0, 255).astype(np.uint8)
    got, _ = decode(cam)
    assert got == val, f"selftest: {got:#x} != {val:#x}"


if __name__ == "__main__":
    selftest()
    if sys.argv[1] == "render":
        cv2.imwrite(sys.argv[3], render(int(sys.argv[2], 0)))
    elif sys.argv[1] == "decode":
        img = cv2.imread(sys.argv[2]) if len(sys.argv) > 2 else grab()
        val, margin = decode(img)
        print(f"{val:#09x} (contrast {margin:.0f})")
