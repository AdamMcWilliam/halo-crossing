"""Drive and capture the running prototype window (Windows only).

Used for scripted smoke tests: focus the game, hold keys for real frame
durations (the game polls keyboard state once per frame, so instant
press/release pairs get lost), move the mouse, and save screenshots.

  python tools/devctl.py shot out.png
  python tools/devctl.py peek out.png           (no focus change, no input)
  python tools/devctl.py keys "enter:150 wait:2000 space:120" --shot out.png
  python tools/devctl.py mouse 200 0            (relative mouse move)
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import sys
import time

user32 = ctypes.WinDLL("user32", use_last_error=True)

WINDOW_TITLE_PREFIXES = ("Animal Crossing", "Halo Crossing")

VK = {
    "enter": 0x0D, "space": 0x20, "shift": 0x10, "lshift": 0xA0, "esc": 0x1B, "tab": 0x09,
    "up": 0x26, "down": 0x28, "left": 0x25, "right": 0x27, "ctrl": 0x11, "backspace": 0x08,
}
for i in range(1, 13):
    VK[f"f{i}"] = 0x6F + i
for c in "abcdefghijklmnopqrstuvwxyz0123456789":
    VK[c] = ord(c.upper())

KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_SCANCODE = 0x0008
MOUSEEVENTF_MOVE = 0x0001
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
MOUSEEVENTF_RIGHTDOWN = 0x0008
MOUSEEVENTF_RIGHTUP = 0x0010


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD), ("time", wt.DWORD),
                ("dwExtraInfo", ctypes.c_size_t)]


class _INPUTUNION(ctypes.Union):
    _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT), ("pad", ctypes.c_byte * 32)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wt.DWORD), ("u", _INPUTUNION)]


def _send(inp):
    n = user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))
    if n != 1:
        raise OSError(ctypes.get_last_error())


def key(vk, down):
    # SDL reads scancodes, so send both the VK and its scancode.
    scan = user32.MapVirtualKeyW(vk, 0)
    flags = KEYEVENTF_SCANCODE | (KEYEVENTF_KEYUP if not down else 0)
    if vk in (0x25, 0x26, 0x27, 0x28):
        flags |= 0x0001  # extended key for arrows
    inp = INPUT(type=1)
    inp.u.ki = KEYBDINPUT(vk, scan, flags, 0, 0)
    _send(inp)


def mouse_move(dx, dy):
    inp = INPUT(type=0)
    inp.u.mi = MOUSEINPUT(dx, dy, 0, MOUSEEVENTF_MOVE, 0, 0)
    _send(inp)


def mouse_button(which, down):
    flag = {("l", True): MOUSEEVENTF_LEFTDOWN, ("l", False): MOUSEEVENTF_LEFTUP,
            ("r", True): MOUSEEVENTF_RIGHTDOWN, ("r", False): MOUSEEVENTF_RIGHTUP}[(which, down)]
    inp = INPUT(type=0)
    inp.u.mi = MOUSEINPUT(0, 0, 0, flag, 0, 0)
    _send(inp)


def find_window(pid=None):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        if pid is not None:
            owner = wt.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value != pid:
                return True
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, buf, 256)
        if buf.value.startswith(WINDOW_TITLE_PREFIXES):
            found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def focus(hwnd):
    user32.ShowWindow(hwnd, 9)  # SW_RESTORE
    # Alt tap lets SetForegroundWindow succeed from a background process.
    key(0x12, True)
    key(0x12, False)
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.15)


def screenshot(hwnd, path):
    from PIL import ImageGrab
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    pt = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    bbox = (pt.x, pt.y, pt.x + rect.right, pt.y + rect.bottom)
    ImageGrab.grab(bbox=bbox, all_screens=True).save(path)
    print(f"saved {path} {bbox}")


def peek(hwnd, path):
    """Capture the client area without focusing (works while other apps own the desktop)."""
    from PIL import Image
    gdi32 = ctypes.WinDLL("gdi32")
    rect = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    w, h = rect.right, rect.bottom
    wdc = user32.GetDC(hwnd)
    mdc = gdi32.CreateCompatibleDC(wdc)
    bmp = gdi32.CreateCompatibleBitmap(wdc, w, h)
    gdi32.SelectObject(mdc, bmp)
    PW_CLIENTONLY, PW_RENDERFULLCONTENT = 1, 2
    ok = user32.PrintWindow(hwnd, mdc, PW_CLIENTONLY | PW_RENDERFULLCONTENT)
    buf = ctypes.create_string_buffer(w * h * 4)
    # BITMAPINFOHEADER for a top-down 32-bit DIB
    bmi = (ctypes.c_uint32 * 10)(40, w, (-h) & 0xFFFFFFFF, 1 | (32 << 16), 0, 0, 0, 0, 0, 0)
    gdi32.GetDIBits(mdc, bmp, 0, h, buf, bmi, 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(hwnd, wdc)
    Image.frombuffer("RGB", (w, h), buf, "raw", "BGRX", 0, 1).save(path)
    print(f"saved {path} ({w}x{h}, PrintWindow ok={ok})")


def ensure_foreground(hwnd):
    """Input goes to whatever window is in front; never type into anything else."""
    if hwnd is None or user32.GetForegroundWindow() == hwnd:
        return
    focus(hwnd)
    if user32.GetForegroundWindow() != hwnd:
        raise SystemExit("game window lost focus; aborting so input is not sent elsewhere")


def run_script(script, hwnd=None):
    """Space-separated steps: key:ms (hold), wait:ms, lmb:ms, rmb:ms, lmbdown, lmbup,
    mouse:dx,dy, combo a+b:ms, shot:path (mid-script screenshot)."""
    for step in script.split():
        name, _, arg = step.partition(":")
        if name != "wait":
            ensure_foreground(hwnd)
        if name == "wait":
            time.sleep(int(arg) / 1000.0)
        elif name == "shot":
            screenshot(hwnd, arg)
        elif name in ("lmbdown", "lmbup"):
            mouse_button("l", name == "lmbdown")
            time.sleep(0.03)
        elif name == "mouse":
            dx, dy = (int(v) for v in arg.split(","))
            mouse_move(dx, dy)
            time.sleep(0.05)
        elif name in ("lmb", "rmb"):
            which = name[0]
            mouse_button(which, True)
            time.sleep(int(arg or 100) / 1000.0)
            mouse_button(which, False)
        else:
            keys = [VK[k] for k in name.split("+")]
            for k in keys:
                key(k, True)
            time.sleep(int(arg or 100) / 1000.0)
            for k in reversed(keys):
                key(k, False)
            time.sleep(0.05)


def load_script_file(path):
    """One script per line; '# comment', and 'repeat N <steps>' expands N times."""
    steps = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            if line.startswith("repeat "):
                _, n, rest = line.split(None, 2)
                steps.extend([rest] * int(n))
            else:
                steps.append(line)
    return " ".join(steps)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pid", type=int, help="target this process's window when several are open")
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("shot")
    s.add_argument("path")
    p = sub.add_parser("peek")
    p.add_argument("path")
    k = sub.add_parser("keys")
    k.add_argument("script")
    k.add_argument("--shot")
    r = sub.add_parser("run")
    r.add_argument("file")
    r.add_argument("--shot")
    m = sub.add_parser("mouse")
    m.add_argument("dx", type=int)
    m.add_argument("dy", type=int)
    args = ap.parse_args()

    hwnd = find_window(args.pid)
    if not hwnd:
        print("game window not found", file=sys.stderr)
        return 1
    if args.cmd == "shot":
        focus(hwnd)
        screenshot(hwnd, args.path)
    elif args.cmd == "peek":
        peek(hwnd, args.path)
    elif args.cmd in ("keys", "run"):
        focus(hwnd)
        run_script(args.script if args.cmd == "keys" else load_script_file(args.file), hwnd)
        if args.shot:
            screenshot(hwnd, args.shot)
    elif args.cmd == "mouse":
        focus(hwnd)
        mouse_move(args.dx, args.dy)
    return 0


if __name__ == "__main__":
    sys.exit(main())
