# Hardware design files

Part of the [project documentation](../docs/README.md).

## In short

This folder holds the design files for the project's own circuit board, made in
[KiCad](https://www.kicad.org/). **The design is at the parts-and-decisions stage**: there are no KiCad files
here so far, only the working brief.

Two documents describe the board, and they do different jobs:

- [The circuit board](../docs/design/circuit-board.md) explains the design and why each block exists.
- [The revision A brief](rev-a/README.md) is the working sheet: requirements, pins, parts and the checklist
  to complete before ordering.

## Layout

| Folder | What's in it |
|---|---|
| [`rev-a/`](rev-a/README.md) | Revision A, the first board, for bring-up and testing |
| [`lib/`](lib/README.md) | Symbols and footprints shared between revisions |

Each board revision gets its own folder and its own KiCad project, so an earlier revision stays intact and can
still be opened. Revision B, the finished plug-in device, starts as a copy of revision A once A has been tested.

## Starting the design

The design uses KiCad 10. The project for revision A goes in `hardware/rev-a/`, named `obd2-sniffer-rev-a`.

## What goes in git

| Committed | Not committed (`.gitignore` handles these) |
|---|---|
| `*.kicad_pro`, `*.kicad_sch`, `*.kicad_pcb` | `*-backups/`, `_autosave-*`, `.history/` |
| `sym-lib-table`, `fp-lib-table` | `*.lck`, `fp-info-cache` |
| Anything in `lib/` | `*.kicad_prl` (local view settings) |
| | `production/` (Gerbers, check reports and other generated files) |

KiCad's files are plain text, so changes can be reviewed like code. After the first save, run `git status` to
confirm that only the left-hand column shows up.

## Design checks

Run these from `hardware/rev-a/` before opening a pull request. `kicad-cli` comes with KiCad; on macOS it is at
`/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli`.

```sh
kicad-cli sch erc --exit-code-violations -o production/erc.rpt obd2-sniffer-rev-a.kicad_sch
kicad-cli pcb drc --schematic-parity --exit-code-violations -o production/drc.rpt obd2-sniffer-rev-a.kicad_pcb
```

ERC checks the schematic's electrical rules, and DRC checks the board layout against the board maker's rules and
against the schematic. Both exit with an error if they find a violation. They aren't part of CI, which would have to
install KiCad on every run, so each hardware pull request says that they passed.

## Working in git

Hardware follows the same flow as the rest of the repo: a branch from `develop` (for example
`feat/rev-a-power-input`), a pull request, and a squash merge. Small pull requests, one block at a time, are the easiest
to review. The README's [project status](../README.md#project-status) and the brief's status line get updated as the
design moves along.

## Before ordering any boards

No boards get ordered until the riskiest parts of the design have been proven on the development board:

1. CAN works over real transceivers and a real wire
   ([CAN-1](../docs/design/requirements.md#can-capture)).
2. Native USB works on PA11 and PA12 ([LNK-3](../docs/design/requirements.md#link-to-the-laptop)).
3. The car to be tested uses classic CAN at its port, and has been read listen-only
   ([safety.md](../docs/design/safety.md#the-order-of-car-tests)).

Any of these could change the design. A schematic is free to redraw, and a board isn't.
