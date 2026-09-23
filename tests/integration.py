"""Windows end-to-end tests. Uses only the Python standard library; no ROM shipped."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time

kernel = C.WinDLL("kernel32", use_last_error=True)
kernel.LoadLibraryExW.argtypes = [W.LPCWSTR, W.HANDLE, W.DWORD]
kernel.LoadLibraryExW.restype = W.HMODULE
kernel.FindResourceW.argtypes = [W.HMODULE, C.c_void_p, C.c_void_p]
kernel.FindResourceW.restype = W.HANDLE
kernel.LoadResource.argtypes = [W.HMODULE, W.HANDLE]
kernel.LoadResource.restype = W.HANDLE
kernel.LockResource.argtypes = [W.HANDLE]
kernel.LockResource.restype = C.c_void_p
kernel.SizeofResource.argtypes = [W.HMODULE, W.HANDLE]
kernel.SizeofResource.restype = W.DWORD
kernel.FreeLibrary.argtypes = [W.HMODULE]


def resource(path, ident, kind=10):
    module = kernel.LoadLibraryExW(str(path), None, 2 | 32)
    assert module, C.get_last_error()
    try:
        res = kernel.FindResourceW(module, ident, kind)
        assert res, (ident, kind, C.get_last_error())
        ptr = kernel.LockResource(kernel.LoadResource(module, res))
        return C.string_at(ptr, kernel.SizeofResource(module, res))
    finally:
        kernel.FreeLibrary(module)


def run(args, cwd, success=True):
    result = subprocess.run([str(x) for x in args], cwd=cwd, capture_output=True, timeout=60)
    assert (result.returncode == 0) == success, (result.returncode, result.stderr.decode(errors="replace"))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("packager", type=Path)
    parser.add_argument("rom", type=Path)
    opt = parser.parse_args()
    packager, original = opt.packager.resolve(), opt.rom.resolve()
    source_hash = hashlib.sha256(original.read_bytes()).digest()
    with tempfile.TemporaryDirectory(prefix="ngpc-packager-test-") as temp:
        root = Path(temp)
        folder = root / "Chemin avec espaces et accents é"
        folder.mkdir()
        rom = folder / "démo.ngc"
        shutil.copyfile(original, rom)
        standalone = root / "standalone"
        standalone.mkdir()
        game = standalone / "Mon jeu.exe"
        icon = folder / "test.ico"
        # Valid 16x16 32-bit DIB icon with an opaque blue square.
        dib = struct.pack("<IiiHHIIiiII", 40, 16, 32, 1, 32, 0, 1024, 0, 0, 0, 0)
        dib += bytes([220, 100, 30, 255]) * 256 + bytes(64)
        icon.write_bytes(struct.pack("<HHH", 0, 1, 1) + struct.pack("<BBBBHHII", 16, 16, 0, 0, 1, 32, len(dib), 22) + dib)
        command = [packager, "--pack", rom, game, "--title", "Démo française", "--scale", "3", "--icon", icon, "--fullscreen"]
        run(command, root)
        assert resource(game, 101) == rom.read_bytes()
        cfg = resource(game, 102)
        assert struct.unpack_from("<4I", cfg) == (1, 3, 1, 0)
        assert cfg[16:].decode("utf-16-le").rstrip("\0") == "Démo française"
        assert resource(game, 1, 14)[:6] == struct.pack("<HHH", 0, 1, 1)
        assert resource(game, 1, 3) == dib
        assert b"MIT License" in resource(game, 103)
        assert b"retro_load_game" not in game.read_bytes()
        print("PASS: ROM, Unicode settings, fullscreen, icon, licences embedded")
        before = hashlib.sha256(game.read_bytes()).digest()
        run(command, root, success=False)
        assert hashlib.sha256(game.read_bytes()).digest() == before
        bad = folder / "bad.ico"
        bad.write_bytes(b"not an icon")
        run([packager, "--pack", rom, game, "--force", "--icon", bad], root, success=False)
        assert hashlib.sha256(game.read_bytes()).digest() == before
        assert not list(standalone.glob("*.tmp"))
        run(command + ["--force"], root)
        print("PASS: overwrite protection, failed export rollback, explicit replacement")
        for name, data in [("empty", b""), ("tiny", bytes(63)), ("large", bytes(4 * 1024 * 1024 + 1))]:
            invalid = folder / (name + ".ngc")
            invalid.write_bytes(data)
            output = root / (name + ".exe")
            run([packager, "--pack", invalid, output], root, success=False)
            assert not output.exists()
        run([packager, "--pack", rom, root / "badscale.exe", "--scale", "0"], root, success=False)
        run([packager, "--pack", rom, root / "missing" / "game.exe"], root, success=False)
        print("PASS: invalid ROM size, options and output folder rejected")
        # Only the game EXE is in this working directory. No core DLL or external BIOS.
        for iteration in range(2):
            report = standalone / f"report-{iteration}.txt"
            run([game, "--smoke", report], standalone)
            values = dict(line.split("=", 1) for line in report.read_text().splitlines())
            assert int(values["frames"]) == 180
            assert int(values["video"]) == 180
            assert int(values["audio"]) > 100000
            assert int(values["input"]) >= 180
            assert int(values["save_bytes"]) > 0
        saves = list((standalone / "smoke-saves").glob("*/save.ngpsav"))
        assert len(saves) == 1
        assert saves[0].parent.name == hashlib.sha256(rom.read_bytes()).hexdigest()
        valid_save = saves[0].read_bytes()
        assert valid_save[:8] == b"NGPS\x01\x00\x00\x00"
        damaged_save = valid_save[:-1] + bytes([valid_save[-1] ^ 1])
        saves[0].write_bytes(damaged_save)
        report = standalone / "invalid-save.txt"
        run([game, "--smoke", report], standalone, success=False)
        assert "Invalid game save" in report.read_text()
        assert saves[0].read_bytes() == damaged_save, "invalid save was overwritten"
        saves[0].write_bytes(valid_save)
        print("PASS: standalone execution twice, native video/audio/input, persistent flash save")
        print("PASS: new save format and corrupt-file preservation")
        # A hidden GUI launch verifies control construction without interrupting the desktop.
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        proc = subprocess.Popen([str(packager)], startupinfo=startup)
        user = C.WinDLL("user32", use_last_error=True)
        user.FindWindowW.argtypes = [W.LPCWSTR, W.LPCWSTR]
        user.FindWindowW.restype = W.HWND
        user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
        window = None
        try:
            for _ in range(50):
                window = user.FindWindowW("NgpCraftPackager", None)
                if window:
                    break
                time.sleep(0.1)
            assert window and proc.poll() is None
            user.PostMessageW(window, 0x10, 0, 0)
            assert proc.wait(timeout=5) == 0
        finally:
            if proc.poll() is None:
                proc.terminate()
                proc.wait()
        assert hashlib.sha256(original.read_bytes()).digest() == source_hash
        print("PASS: packager GUI opens/closes; original ROM untouched")
    print("All integration checks passed.")


if __name__ == "__main__":
    main()
