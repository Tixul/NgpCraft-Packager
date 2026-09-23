"""Exercise the real Win32 dialogs on a private, non-visible desktop."""
import argparse
import ctypes as C
from ctypes import wintypes as W
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import uuid
from contextlib import nullcontext

from integration import resource, run

kernel = C.WinDLL("kernel32", use_last_error=True)
class StartupInfo(C.Structure):
    _fields_ = [("cb", W.DWORD), ("reserved", W.LPWSTR), ("desktop", W.LPWSTR), ("title", W.LPWSTR),
                ("x", W.DWORD), ("y", W.DWORD), ("width", W.DWORD), ("height", W.DWORD),
                ("xchars", W.DWORD), ("ychars", W.DWORD), ("fill", W.DWORD), ("flags", W.DWORD),
                ("show", W.WORD), ("reserved2size", W.WORD), ("reserved2", C.c_void_p),
                ("stdin", W.HANDLE), ("stdout", W.HANDLE), ("stderr", W.HANDLE)]

class ProcessInfo(C.Structure):
    _fields_ = [("process", W.HANDLE), ("thread", W.HANDLE), ("pid", W.DWORD), ("tid", W.DWORD)]

kernel.CreateProcessW.argtypes = [W.LPCWSTR, W.LPWSTR, C.c_void_p, C.c_void_p, W.BOOL, W.DWORD, C.c_void_p, W.LPCWSTR, C.POINTER(StartupInfo), C.POINTER(ProcessInfo)]
kernel.CloseHandle.argtypes = [W.HANDLE]
kernel.GetExitCodeProcess.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]
kernel.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
kernel.TerminateProcess.argtypes = [W.HANDLE, W.UINT]

class NativeProcess:
    def __init__(self, desktop, args):
        startup = StartupInfo()
        startup.cb = C.sizeof(startup)
        startup.desktop = desktop
        startup.flags = 1
        startup.show = 0
        info = ProcessInfo()
        command = C.create_unicode_buffer(subprocess.list2cmdline([str(a) for a in args]))
        assert kernel.CreateProcessW(None, command, None, None, False, 0, None, None, C.byref(startup), C.byref(info)), C.get_last_error()
        kernel.CloseHandle(info.thread)
        self.handle, self.pid = info.process, info.pid

    def poll(self):
        code = W.DWORD()
        assert kernel.GetExitCodeProcess(self.handle, C.byref(code))
        return None if code.value == 259 else code.value

    def wait(self, timeout):
        assert kernel.WaitForSingleObject(self.handle, int(timeout * 1000)) == 0
        return self.poll()

    def terminate(self):
        kernel.TerminateProcess(self.handle, 1)

user = C.WinDLL("user32", use_last_error=True)
callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
user.CreateDesktopW.argtypes = [W.LPCWSTR, W.LPCWSTR, C.c_void_p, W.DWORD, W.DWORD, C.c_void_p]
user.CreateDesktopW.restype = W.HANDLE
user.CloseDesktop.argtypes = [W.HANDLE]
user.EnumDesktopWindows.argtypes = [W.HANDLE, callback, W.LPARAM]
user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
user.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
user.GetDlgItem.argtypes = [W.HWND, C.c_int]
user.GetDlgItem.restype = W.HWND
user.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user.SendMessageW.restype = C.c_ssize_t
user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
user.SetWindowTextW.argtypes = [W.HWND, W.LPCWSTR]
user.IsWindow.argtypes = [W.HWND]
user.GetWindowLongPtrW.argtypes = [W.HWND, C.c_int]
user.GetWindowLongPtrW.restype = C.c_ssize_t
user.GetMenu.argtypes = [W.HWND]
user.GetMenu.restype = W.HMENU
user.GetSubMenu.argtypes = [W.HMENU, C.c_int]
user.GetSubMenu.restype = W.HMENU
user.GetMenuState.argtypes = [W.HMENU, W.UINT, W.UINT]
user.GetMenuState.restype = W.UINT


def wait_for(fn):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        value = fn()
        if value:
            return value
        time.sleep(0.02)
    raise AssertionError("UI condition timed out")


def text(hwnd):
    buf = C.create_unicode_buffer(512)
    user.SendMessageW(hwnd, 0xD, len(buf), C.addressof(buf))
    return buf.value


def post(hwnd, message, wparam=0, lparam=0):
    assert user.PostMessageW(hwnd, message, wparam, lparam), C.get_last_error()


def set_text(hwnd, value):
    buf = C.create_unicode_buffer(value)
    assert user.SendMessageW(hwnd, 0xC, 0, C.addressof(buf))


def key(dialog, index, vk):
    field = user.GetDlgItem(dialog, 310 + index)
    user.SendMessageW(dialog, 0x28, field, 1)  # Give the field keyboard focus.
    post(field, 0x100, vk, 1)  # Goes through the dialog's keyboard dispatcher.
    post(field, 0x101, vk, 1 << 31)


class Desktop:
    def __init__(self):
        self.name = "NgpcOptionsTest-" + uuid.uuid4().hex
        self.handle = user.CreateDesktopW(self.name, None, None, 0, 0x1FF, None)
        assert self.handle, C.get_last_error()
        self.processes = []

    def launch(self, *args):
        proc = NativeProcess(self.name, args)
        self.processes.append(proc)
        return proc

    def window(self, proc, cls):
        found = []

        @callback
        def enum(hwnd, _):
            pid = W.DWORD()
            user.GetWindowThreadProcessId(hwnd, C.byref(pid))
            name = C.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, len(name))
            if pid.value == proc.pid and name.value == cls:
                found.append(hwnd)
            return True

        user.EnumDesktopWindows(self.handle, enum, 0)
        return found[0] if found else None

    def dialog(self, proc, parent, command):
        post(parent, 0x111, command)
        return wait_for(lambda: self.window(proc, "#32770"))

    def close(self):
        for proc in self.processes:
            if proc.poll() is None:
                proc.terminate()
            proc.wait(timeout=5)
            kernel.CloseHandle(proc.handle)
        user.CloseDesktop(self.handle)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("packager", type=Path)
    parser.add_argument("rom", type=Path)
    args = parser.parse_args()
    packager, rom = args.packager.resolve(), args.rom.resolve()
    desktop = Desktop()
    temp = tempfile.TemporaryDirectory(prefix="ngpc-options-test-")
    try:
        proc = desktop.launch(packager)
        parent = wait_for(lambda: desktop.window(proc, "NgpCraftPackager"))
        dialog = desktop.dialog(proc, parent, 14)
        assert text(dialog) == "Controls and settings"
        assert text(user.GetDlgItem(dialog, 310)) == "Up"
        assert text(user.GetDlgItem(dialog, 316)) == "Enter"
        assert text(user.GetDlgItem(dialog, 1)) == "Apply"
        assert text(user.GetDlgItem(dialog, 2)) == "Cancel"
        enter_name = text(user.GetDlgItem(dialog, 316))
        key(dialog, 0, ord("W"))
        wait_for(lambda: text(user.GetDlgItem(dialog, 310)) == "W")
        key(dialog, 0, 0x70)  # F1 reserved.
        wait_for(lambda: "Reserved key" in text(user.GetDlgItem(dialog, 325)))
        assert text(user.GetDlgItem(dialog, 310)) == "W"
        key(dialog, 1, ord("W"))
        wait_for(lambda: "already assigned" in text(user.GetDlgItem(dialog, 325)))
        assert text(user.GetDlgItem(dialog, 311)) != "W"
        key(dialog, 6, 32)  # Free Enter, then bind it through the real dialog loop.
        wait_for(lambda: "Key changed" in text(user.GetDlgItem(dialog, 325)))
        key(dialog, 1, 13)
        time.sleep(0.1)
        assert user.IsWindow(dialog), "Enter must rebind instead of closing the dialog"
        assert text(user.GetDlgItem(dialog, 311)) == enter_name
        set_text(user.GetDlgItem(dialog, 322), "101")
        post(dialog, 0x111, 1)
        wait_for(lambda: "between 0 and 100" in text(user.GetDlgItem(dialog, 325)))
        set_text(user.GetDlgItem(dialog, 322), "65")
        post(dialog, 0x111, 1)
        wait_for(lambda: not user.IsWindow(dialog))
        dialog = desktop.dialog(proc, parent, 14)
        assert text(user.GetDlgItem(dialog, 310)) == "W"
        assert text(user.GetDlgItem(dialog, 322)) == "65"
        post(dialog, 0x111, 324)  # Defaults, then cancel: must preserve accepted options.
        wait_for(lambda: text(user.GetDlgItem(dialog, 310)) != "W")
        post(dialog, 0x111, 2)
        wait_for(lambda: not user.IsWindow(dialog))
        dialog = desktop.dialog(proc, parent, 14)
        assert text(user.GetDlgItem(dialog, 310)) == "W"
        post(dialog, 0x111, 2)
        wait_for(lambda: not user.IsWindow(dialog))
        post(parent, 0x10)
        assert proc.wait(timeout=5) == 0
        print("PASS: actual packager dialog, key capture, reserved/duplicate keys, volume validation, Cancel")

        with nullcontext(temp.name) as folder:
            root = Path(folder)
            game = root / "game.exe"
            run([packager, "--pack", rom, game, "--keys", "87,83,65,68,74,75,32", "--volume", "65", "--scale", "3"], root)
            embedded = struct.unpack("<12I", resource(game, 105))
            assert embedded == (1, 87, 83, 65, 68, 74, 75, 32, 3, 0, 65, 1)
            for invalid in ["87,87,65,68,74,75,32", "112,83,65,68,74,75,32", "87,83", "87,83,65,68,74,75,32,33"]:
                run([packager, "--pack", rom, root / "invalid.exe", "--keys", invalid], root, success=False)
            print("PASS: embedded custom controls; invalid command-line bindings rejected")
            report = root / "session.txt"
            proc = desktop.launch(game, "--ui-test", report)
            parent = wait_for(lambda: desktop.window(proc, "NgpCraftPlayer"))
            # Pause first: opening/closing the dialog must not resume the game.
            post(parent, 0x111, 401)
            menu = user.GetSubMenu(user.GetMenu(parent), 0)
            wait_for(lambda: user.GetMenuState(menu, 401, 0) & 8)
            post(parent, 0x100, 0x1B, 1)  # Escape now opens options, never exits.
            dialog = wait_for(lambda: desktop.window(proc, "#32770"))
            assert text(user.GetDlgItem(dialog, 310)) == "W"
            assert text(user.GetDlgItem(dialog, 322)) == "65"
            key(dialog, 0, ord("P"))
            wait_for(lambda: text(user.GetDlgItem(dialog, 310)) == "P")
            key(dialog, 1, ord("M"))
            wait_for(lambda: text(user.GetDlgItem(dialog, 311)) == "M")
            set_text(user.GetDlgItem(dialog, 322), "37")
            user.SendMessageW(user.GetDlgItem(dialog, 320), 0x14E, 0, 0)  # 2x.
            user.SendMessageW(user.GetDlgItem(dialog, 323), 0xF1, 0, 0)  # Fit.
            post(dialog, 0x111, 1)
            wait_for(lambda: not user.IsWindow(dialog))
            assert user.GetMenuState(menu, 401, 0) & 8
            prefs = wait_for(lambda: next(iter((root / "smoke-saves").glob("*/preferences.dat")), None))
            stored = struct.unpack("<12I", prefs.read_bytes())
            assert stored == (1, 80, 77, 65, 68, 74, 75, 32, 2, 0, 37, 0)
            # Cancellation must leave both running options and persisted bytes untouched.
            dialog = desktop.dialog(proc, parent, 400)
            key(dialog, 0, ord("Q"))
            wait_for(lambda: text(user.GetDlgItem(dialog, 310)) == "Q")
            post(dialog, 0x111, 2)
            wait_for(lambda: not user.IsWindow(dialog))
            assert struct.unpack("<12I", prefs.read_bytes()) == stored
            # Fullscreen and sound shortcuts persist without changing key bindings.
            post(parent, 0x111, 403)
            wait_for(lambda: user.GetWindowLongPtrW(parent, -16) & 0x80000000)
            time.sleep(0.1)  # Let the shortcut's atomic file replacement finish before opening it.
            post(parent, 0x111, 402)
            time.sleep(0.1)
            wait_for(lambda: struct.unpack("<12I", prefs.read_bytes())[10] == 0)
            post(parent, 0x111, 402)
            time.sleep(0.1)
            wait_for(lambda: struct.unpack("<12I", prefs.read_bytes())[10] == 37)
            post(parent, 0x10)
            assert proc.wait(timeout=5) == 0
            print("PASS: in-game menu, pause preservation, remap P/M, options saved, Cancel, fullscreen and mute")
            # Fresh process proves settings come from disk, not global memory.
            proc = desktop.launch(game, "--ui-test", report)
            parent = wait_for(lambda: desktop.window(proc, "NgpCraftPlayer"))
            dialog = desktop.dialog(proc, parent, 400)
            assert text(user.GetDlgItem(dialog, 310)) == "P"
            assert text(user.GetDlgItem(dialog, 311)) == "M"
            assert text(user.GetDlgItem(dialog, 322)) == "37"
            assert user.SendMessageW(user.GetDlgItem(dialog, 321), 0xF0, 0, 0) == 1
            post(dialog, 0x111, 324)
            wait_for(lambda: text(user.GetDlgItem(dialog, 310)) == "W")
            assert text(user.GetDlgItem(dialog, 322)) == "65"  # Packaged defaults, not generic defaults.
            post(dialog, 0x111, 1)
            wait_for(lambda: not user.IsWindow(dialog))
            assert struct.unpack("<12I", prefs.read_bytes()) == embedded
            post(parent, 0x10)
            assert proc.wait(timeout=5) == 0
            # Corrupt files fall back gracefully; game and SRAM still work.
            prefs.write_bytes(b"damaged preferences")
            run([game, "--smoke", report], root)
            values = dict(line.split("=", 1) for line in report.read_text().splitlines())
            assert values["key0"] == "87" and values["volume"] == "65"
            assert values["video"] == "180"
            print("PASS: preferences reloaded, packaged defaults restored, corrupt preferences recovered")
    finally:
        desktop.close()
        temp.cleanup()
    print("All options UI checks passed.")


if __name__ == "__main__":
    main()
