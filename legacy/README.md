# Legacy: the original ROCsim (Fall 2025)

This folder preserves the original CS3307 group project **unchanged**:

- `qt-app/` is the Qt 6 / OpenGL / HIP source as delivered.
  - Team 1, physics and GPU: Jiwon Lee, Jacob Lee
  - Team 2, visualization and UI: Eric Jean, Maharshii Patel
- `README-original.md` is the original README. Several features it lists were planned but not implemented; see [`docs/audit.md`](../docs/audit.md).
- `audit/` holds the harness used in the audit. It compiles the unmodified `qt-app/particlesglwidget.cpp` headless and measures findings E1–E8.

The course proposals and deliverable reports are in [`docs/course/`](../docs/course/).

## Building the original

As delivered, the original does not build: `springmassglwidget.cpp/.h` are referenced but missing, and `mainwindow.cpp` references a `labelMemory` widget that the `.ui` file lacks. It is kept as a historical record, not as a maintained target.

## Re-running the audit harness

This needs Qt 6 (Widgets, OpenGLWidgets) and CMake:

```bash
cmake -S legacy/audit -B build/audit -DORIG=$PWD/legacy/qt-app
cmake --build build/audit
QT_QPA_PLATFORM=offscreen ./build/audit/physics_probe
```
