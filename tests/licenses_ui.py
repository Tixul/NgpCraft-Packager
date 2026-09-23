"""Credits editor, packaged metadata and real in-game license dialogs."""
import argparse
import ctypes as C
from pathlib import Path
import struct
import tempfile

from integration import resource, run
from options_ui import Desktop, user, post, set_text, wait_for, text


def full_text(hwnd):
    size = user.SendMessageW(hwnd, 0xE, 0, 0)
    buf = C.create_unicode_buffer(size + 1)
    user.SendMessageW(hwnd, 0xD, size + 1, C.addressof(buf))
    return buf.value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("packager", type=Path)
    parser.add_argument("rom", type=Path)
    args = parser.parse_args()
    packager, rom = args.packager.resolve(), args.rom.resolve()
    desktop = Desktop()
    temp = tempfile.TemporaryDirectory(prefix="ngpc-license-test-")
    try:
        proc = desktop.launch(packager)
        parent = wait_for(lambda: desktop.window(proc, "NgpCraftPackager"))
        dialog = desktop.dialog(proc, parent, 15)
        assert text(dialog) == "Game credits and license"
        values = ["Étoile Studio", "Copyright 2026 Étoile Studio", "My custom game license", "First line\r\nSecond line"]
        for ident, value in zip(range(501, 505), values):
            set_text(user.GetDlgItem(dialog, ident), value)
        post(dialog, 0x111, 1)
        wait_for(lambda: not user.IsWindow(dialog))
        dialog = desktop.dialog(proc, parent, 15)
        for ident, value in zip(range(501, 505), values):
            assert full_text(user.GetDlgItem(dialog, ident)) == value
        set_text(user.GetDlgItem(dialog, 501), "Discard this edit")
        post(dialog, 0x111, 2)
        wait_for(lambda: not user.IsWindow(dialog))
        dialog = desktop.dialog(proc, parent, 15)
        assert text(user.GetDlgItem(dialog, 501)) == values[0]
        post(dialog, 0x111, 2)
        wait_for(lambda: not user.IsWindow(dialog))
        post(parent, 0x10)
        assert proc.wait(timeout=5) == 0
        print("PASS: credits editor, Unicode, multiline text, Apply and Cancel")

        root = Path(temp.name)
        license_file = root / "license.txt"
        terms = "Custom game terms — café\n" + "Permission statement.\n" * 1000 + "END OF GAME LICENSE"
        license_file.write_text(terms, encoding="utf-8-sig", newline="\n")
        game = root / "game.exe"
        run([packager, "--pack", rom, game, "--title", "My Game", "--game-author", values[0], "--game-copyright", values[1],
             "--game-license-name", values[2], "--game-license", license_file], root)
        data = resource(game, 106)
        version, *sizes = struct.unpack_from("<5I", data)
        assert version == 1
        offset = 20
        fields = []
        for size in sizes:
            fields.append(data[offset:offset + size].decode("utf-8"))
            offset += size
        assert fields == values[:3] + [terms], [(repr(a[:80]), repr(b[:80]), len(a), len(b)) for a, b in zip(fields, values[:3] + [terms]) if a != b]
        assert offset == len(data)
        assert b"Copyright (c) 2026 tixul" in resource(game, 103)
        before = game.read_bytes()
        license_file.write_bytes(b"\xff\xfeinvalid")
        run([packager, "--pack", rom, game, "--force", "--game-license", license_file], root, success=False)
        assert game.read_bytes() == before
        print("PASS: game license embedded verbatim, runtime notices preserved, invalid import leaves output intact")

        proc = desktop.launch(game, "--ui-test", root / "report.txt")
        parent = wait_for(lambda: desktop.window(proc, "NgpCraftPlayer"))
        post(parent, 0x100, 0x71, 1)  # F2.
        dialog = wait_for(lambda: desktop.window(proc, "#32770"))
        wait_for(lambda: text(dialog) == "Credits and licenses")
        assert user.SendMessageW(user.GetDlgItem(dialog, 521), 0x146, 0, 0) == 2
        body = full_text(user.GetDlgItem(dialog, 523))
        assert values[0] in body and values[1] in body and values[2] in body
        assert body.endswith("END OF GAME LICENSE") and "MIT License" not in body
        assert terms.replace("\n", "\r\n") in body
        for index, expected in [(1, "Copyright (c) 2026 tixul")]:
            user.SendMessageW(user.GetDlgItem(dialog, 521), 0x14E, index, 0)
            post(dialog, 0x111, 521 | (1 << 16))
            wait_for(lambda: expected in full_text(user.GetDlgItem(dialog, 523)))
            notice = full_text(user.GetDlgItem(dialog, 523))
            assert "Permission is hereby granted" in notice and "THE SOFTWARE IS PROVIDED" in notice
            assert values[0] not in notice and "#ifndef" not in notice
        post(dialog, 0x111, 1)
        wait_for(lambda: not user.IsWindow(dialog))
        post(parent, 0x10)
        assert proc.wait(timeout=5) == 0
        print("PASS: F2 viewer, long text not truncated, separate scopes and complete MIT notices")

        default_game = root / "default.exe"
        run([packager, "--pack", rom, default_game], root)
        proc = desktop.launch(default_game, "--ui-test", root / "report.txt")
        parent = wait_for(lambda: desktop.window(proc, "NgpCraftPlayer"))
        dialog = desktop.dialog(proc, parent, 405)
        wait_for(lambda: "No game license terms" in full_text(user.GetDlgItem(dialog, 523)))
        assert "MIT" not in full_text(user.GetDlgItem(dialog, 523))
        post(dialog, 0x111, 1)
        wait_for(lambda: not user.IsWindow(dialog))
        post(parent, 0x10)
        assert proc.wait(timeout=5) == 0
        print("PASS: no game license is inferred when fields are empty")
    finally:
        desktop.close()
        temp.cleanup()
    print("All license checks passed.")


if __name__ == "__main__":
    main()
