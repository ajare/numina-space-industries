# Numina Space Industries

Station Systems and generic serialization as a standalone library, plus the
Editor application shell that will present them.

## Language

**Editor**:
The standalone GUI application in this repository (`editor/`) that hosts
dockable panels over a shared frame (menubar, toolbar, statusbar). Not to be
confused with the Promethium editor, which lives in another repository and is
not this term.
_Avoid_: Promethium editor, station editor

**Panel**:
A dockable content window inside the Editor's dockspace; the "Editor" panel
is the first. Panels never own chrome (menubar, toolbar, statusbar) — the
host frame does.
