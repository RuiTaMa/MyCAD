# MyCAD

MyCAD is a standalone Windows CAD application in active development.

## V0.1 goal

The first milestone is a real `MyCAD.exe` that launches directly on Windows and does **not** require FreeCAD to be installed or opened.

Current source includes:

- Independent Qt 6 desktop window
- Open CASCADE geometry and 3D visualization
- Box and cylinder primitives
- Model tree
- Rotate / pan / zoom / Fit All
- Standard views
- STEP import/export

## Build output

GitHub Actions builds the Windows x64 package automatically.

Open:

**Actions → Build MyCAD Windows → latest successful run → Artifacts → MyCAD-Windows-x64**

The artifact contains `MyCAD.exe` and its runtime DLLs.

## Architecture

```text
MyCAD.exe
├─ MyCAD UI / commands
├─ Qt 6
└─ Open CASCADE Technology
   ├─ geometry / B-Rep
   ├─ STEP data exchange
   └─ 3D visualization
```

FreeCAD source modules can be migrated selectively in later milestones where their LGPL-licensed functionality is useful, rather than using FreeCAD as the host application.
