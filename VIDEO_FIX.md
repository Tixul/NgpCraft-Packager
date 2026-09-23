# Intermittent black flash — version 0.2.2

The player's previous `WM_PAINT` handler cleared the visible client area to black with `FillRect`, then drew the emulated frame with `StretchDIBits`. These were separate operations on the window DC. An intermediate clear could become visible even when the emulated frame itself was not black.

The player now composes the entire client image, including black margins, in a reusable offscreen GDI bitmap. It presents the completed result with one `BitBlt`. The bitmap is recreated only when the client size changes and its GDI resources are released when replaced or destroyed. Minimized/zero-size windows are skipped. No emulated frames are filtered or dropped by this fix.

## Diagnosis

During the original presentation investigation, the bundled core was exercised with an Overrev ROM over two 3,600-frame scenarios (menus and a race). Black frames occurred at startup/transitions, while the final 1,200 race frames had none. These measurements predate the 0.4 core update.

This supports the presentation defect as the likely source of the reported intermittent flash. It does not prove that every Overrev ROM version and scene is free of core-side issues, or reproduce the user's exact display/compositor timing.

## Regression test

`ctest --test-dir build --output-on-failure` runs `video_surface_test` against real GDI memory surfaces. It verifies that composing 240 alternating frames at multiple sizes never changes the target before presentation, that the presented image has the correct colors and margins, that image orientation is correct, and that genuine black frames remain visible. It also checks zero-size handling and GDI handle lifetime across repeated resizes.

Existing export and settings UI tests are also run against the updated packager. Games exported before this update must be re-exported to use the corrected player. For save-format changes in version 0.4, see the README.
