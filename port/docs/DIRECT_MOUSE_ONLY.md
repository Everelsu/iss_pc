# Direct mouse input: developer handoff

This build removes the unused mouse-to-right-stick emulator, rather than merely
hiding its sliders. `src/mouse_stick.h`, its test, build targets, runtime state,
settings fields, environment overrides, and Qt controls/translations are removed.
Mouse motion goes only through `runtime_mouse_aim_motion`. If the guarded native
hook is unavailable, no emulated-stick fallback is used; controller axes remain
unchanged. Do not reintroduce that fallback when merging into a newer beta.

Sensitivity, invert Y, capture/enable, mouse buttons, touchpad gestures and graffiti
aiming/shaking remain. Controller stick dead zones, rumble, gyro, mappings and
physical camera input retain their existing behavior.

Old INI files can still load. Obsolete smoothing/acceleration/anti-deadzone keys
are unknown and have no effect; the runtime settings writer drops them on save.
The launcher no longer exposes or accepts these obsolete mouse preferences.

`TlgGpuInputSettings` changed: rebuild the runtime and both rendering libraries
together. Never mix the new runtime with an old library. This is a source change
for the Linux normal-build package; it does not install files into Windows.

Regression coverage: direct-mouse routing with hook present/absent and unchanged
controller axes in `test_pad.c`; mouse buttons/touchpad and neutral emulated axes
in `test_mouse_pad.c`; old-INI compatibility and removal on save in
`test_graphics_settings.cpp`; launcher preference validation and Controls UI in
`test_qt_screens_a.py`. Full Linux CI and Linux gameplay validation are separate:
passing host tests does not establish that every Linux driver works in gameplay.
