#!/usr/bin/env python3
"""Worst-case stack use of the firmware, worked out from the linked ELF.

Disassembles obd2_sniffer.elf, finds how many bytes each function takes from
the stack (push, stmdb sp!, sub sp, vpush, ...) and which functions it calls,
and follows the deepest call chain from each entry point:

- Main stack: the start-up code and main(), plus every exception handler
  nested on top, each with the frame the CPU pushes on entry. Adding up every
  handler is an upper bound: a chain of preemptions holds each handler at most
  once, whatever the priorities.
- Fault stack: the fault handlers switch to their own stack (fault_entry.S),
  so the crash-capture code is checked against that instead.

The check fails if a total does not fit its stack; if a function can call
itself, directly or through others (MISRA C:2012 rule 17.2); if an instruction
moves the stack pointer by an amount it can't work out; or if a call through a
function pointer, or a function whose address is taken, is missing from
INDIRECT_CALLS below. Each frame is also compared with GCC's own figure from
-fstack-usage, which checks this script's reading of the disassembly.

Usage:
    python3 scripts/stack_usage.py [BUILD_DIR] [--summary-md FILE]

BUILD_DIR is a firmware preset's build folder (default build/firmware).
Needs arm-none-eabi-objdump on the PATH.
"""

from __future__ import annotations

import argparse
import fnmatch
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OBJDUMP = "arm-none-eabi-objdump"

# Pushed by the CPU on exception entry, worst case: 26 words with the FPU
# registers (the firmware uses the FPU), plus 4 bytes to keep 8-byte alignment.
EXCEPTION_FRAME = 26 * 4 + 4

# Calls through function pointers, by the function making them, reviewed by
# hand: the disassembly shows that such a call happens, not where it goes.
# Targets are fnmatch patterns over function names.
INDIRECT_CALLS: dict[str, tuple[tuple[str, ...], str]] = {
    "cli_execute": (("cmd_*",), "runs the handler from the command table (console/commands.c)"),
    "console_core_feed": (("write_out",), "console output callback (console/console.c)"),
    "console_core_redraw": (("write_out",), "console output callback (console/console.c)"),
    "out": (("write_out",), "console output callback (console/console.c)"),
    "out_spaces": (("write_out",), "console output callback (console/console.c)"),
    "_printf_common": (("__ssputs_r",), "newlib: the output function vsnprintf passes in"),
    "_printf_i": (("__ssputs_r",), "newlib: the output function vsnprintf passes in"),
    "__libc_init_array": (("frame_dummy",), "start-up: the .init_array entries"),
    "register_tm_clones": ((), "start-up: a weak hook that is 0 here, so never called"),
    "deregister_tm_clones": ((), "start-up: a weak hook that is 0 here, so never called"),
}

# Functions that load the stack pointer, rather than push to it.
SETS_STACK = {
    "Reset_Handler": "loads the initial stack pointer, then runs on the main stack",
}
SWITCHES_STACK = {
    # function: (stack start symbol, stack end symbol); its callees run there
    "fault_entry": ("fault_stack", "fault_stack_end"),
}

REG_LIST_RE = re.compile(r"\{([^}]*)\}")
TARGET_RE = re.compile(r"^([0-9a-f]+) <([^>]+)>")
# A variadic function starts by saving its unnamed argument registers, e.g. {r3}.
VARARGS_RE = re.compile(r"^\{(?:r[0-2], )*r3\}$")


class StackError(Exception):
    pass


@dataclass
class Function:
    names: list[str]
    start: int
    end: int
    frame: int = 0
    calls: set[int] = field(default_factory=set)
    indirect: bool = False
    sets_sp: bool = False
    varargs: int = 0  # bytes of argument registers a variadic function saves for va_arg

    @property
    def name(self) -> str:
        return self.names[0]


def run(*args: str) -> str:
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def read_functions(elf: Path) -> dict[int, Function]:
    """Function symbols from the symbol table, keyed by start address."""
    by_start: dict[int, list[tuple[bool, str, int]]] = {}
    for line in run(OBJDUMP, "-t", str(elf)).splitlines():
        # 08004908 g     F .text<TAB>00000320 .hidden __udivmoddi4
        m = re.match(r"^([0-9a-f]{8}) (.{7}) (\S+)\t([0-9a-f]{8}) (?:\.hidden )?(\S+)$", line)
        if not m or m.group(3) != ".text":
            continue
        flags = m.group(2)
        if "F" not in flags and "g" not in flags:
            continue  # a local label inside a function, e.g. LoopCopyDataInit
        start = int(m.group(1), 16) & ~1
        weak = "w" in flags
        by_start.setdefault(start, []).append((weak, m.group(5), int(m.group(4), 16)))
    starts = sorted(by_start)
    functions = {}
    for i, start in enumerate(starts):
        symbols = sorted(by_start[start])  # strong names first, then by name
        size = max(size for _, _, size in symbols)
        end = start + size if size else (starts[i + 1] if i + 1 < len(starts) else start)
        functions[start] = Function([name for _, name, _ in symbols], start, end)
    return functions


def section_words(elf: Path, sections: list[str]) -> dict[str, list[int]]:
    """Little-endian 32-bit words of each named section."""
    args = [OBJDUMP, "-s"] + [f"-j{s}" for s in sections] + [str(elf)]
    words: dict[str, list[int]] = {}
    current = None
    for line in run(*args).splitlines():
        m = re.match(r"^Contents of section (\S+):", line)
        if m:
            current = words.setdefault(m.group(1), [])
            continue
        m = re.match(r"^ [0-9a-f]+ ((?:[0-9a-f]{2,8} ?){1,4})", line)
        if current is not None and m:
            for group in m.group(1).split():
                if len(group) == 8:
                    current.append(int.from_bytes(bytes.fromhex(group), "little"))
    return words


def registers(operands: str) -> int:
    """Bytes for a register list such as {r4, r5, lr} or {d8-d15}."""
    m = REG_LIST_RE.search(operands)
    if not m:
        raise StackError(f"no register list in '{operands}'")
    total = 0
    for item in m.group(1).split(","):
        item = item.strip()
        width = 8 if item.startswith("d") else 4
        if "-" in item:
            lo, hi = (int(re.sub(r"\D", "", r)) for r in item.split("-"))
            total += (hi - lo + 1) * width
        else:
            total += width
    return total


def writes_sp(mnemonic: str, operands: str) -> bool:
    if mnemonic.split(".")[0] in ("push", "pop", "vpush", "vpop"):
        return True  # sp is implicit
    first = operands.split(",")[0].strip()
    writeback = "]!" in operands or "], #" in operands
    return first in ("sp", "sp!") or ("[sp" in operands and writeback)


def stack_effect(mnemonic: str, operands: str, frame_pointers: set[str]) -> int | None:
    """Bytes taken from the stack; 0 for a recognised release; None if unknown."""
    base = mnemonic.split(".")[0]
    m = re.match(r"^sp, (\w+)$", operands)
    if base == "mov" and m and m.group(1) in frame_pointers:
        return 0  # back to the function's own frame pointer: never below it
    if base in ("push", "vpush") or (base in ("stmdb", "stmfd", "vstmdb") and "sp!" in operands):
        return registers(operands)
    if base in ("pop", "vpop") or (
        base in ("ldmia", "ldm", "ldmfd", "vldmia") and "sp!" in operands
    ):
        return 0
    m = re.match(r"^\w+, \[sp, #-(\d+)\]!$", operands) or re.match(
        r"^\w+, \w+, \[sp, #-(\d+)\]!$", operands
    )
    if base in ("str", "strd") and m:
        return int(m.group(1))
    if base.startswith("ldr") and re.search(r"\[sp\], #\d+$", operands):
        return 0  # post-indexed pop
    if base in ("sub", "subw"):
        m = re.match(r"^sp, (?:sp, )?#(\d+)$", operands)
        return int(m.group(1)) if m else None
    if base in ("add", "addw") and re.match(r"^sp, (?:sp, )?#\d+$", operands):
        return 0
    return None


def disassemble(elf: Path, functions: dict[int, Function]) -> list[int]:
    """Fill in frames and calls; return the words in the code's literal pools."""
    starts = sorted(functions)
    literal_words: list[int] = []
    current: Function | None = None
    frame_pointers: set[str] = set()  # registers holding sp (or above) in this function
    jump_table: Function | None = None
    for line in run(OBJDUMP, "-d", "--no-show-raw-insn", str(elf)).splitlines():
        m = re.match(r"^ +([0-9a-f]+):\t(\S+)\t?([^@]*)", line)
        if not m:
            continue
        addr, mnemonic, operands = int(m.group(1), 16), m.group(2), m.group(3).strip()
        if current is None or not current.start <= addr < current.end:
            current = next(
                (functions[s] for s in starts if functions[s].start <= addr < functions[s].end),
                None,
            )
            frame_pointers = set()
        if mnemonic == ".word":
            word = int(operands, 16)
            if jump_table is not None:
                if jump_table.start <= (word & ~1) < jump_table.end:
                    continue  # an entry of a jump table inside the function
                jump_table = None
            literal_words.append(word)
            continue
        jump_table = None
        if current is None:
            continue
        fn = current
        if addr == fn.start and mnemonic == "push" and VARARGS_RE.match(operands):
            fn.varargs = registers(operands)

        m = re.match(r"^(\w+), sp(?:, #\d+)?$", operands)
        if mnemonic.split(".")[0] in ("add", "mov") and m and m.group(1) != "sp":
            frame_pointers.add(m.group(1))

        if writes_sp(mnemonic, operands):
            effect = stack_effect(mnemonic, operands, frame_pointers)
            if effect is None:
                if fn.name not in SETS_STACK and fn.name not in SWITCHES_STACK:
                    raise StackError(f"{fn.name}: can't size '{mnemonic} {operands}'")
                fn.sets_sp = True
            else:
                fn.frame += effect  # every decrement counts, so this is an upper bound

        base = mnemonic.split(".")[0]
        target = TARGET_RE.match(operands.split(", ")[-1])
        if base in ("bl", "blx") and target:
            fn.calls.add(callee(functions, int(target.group(1), 16), fn, line))
        elif (
            (base.startswith("b") or base in ("cbz", "cbnz"))
            and target
            and "+" not in target.group(2)
        ):
            if not fn.start <= int(target.group(1), 16) < fn.end:  # a tail call
                fn.calls.add(callee(functions, int(target.group(1), 16), fn, line))
        elif (base in ("blx", "bx") and operands != "lr") or (
            base == "mov" and operands.startswith("pc,")
        ):
            fn.indirect = True
        elif base.startswith("ldr") and operands.startswith("pc,"):
            if re.match(r"^pc, \[\w+, \w+, lsl #2\]$", operands):
                jump_table = fn  # the .word entries that follow are checked above
            else:
                fn.indirect = True
    return literal_words


def callee(functions: dict[int, Function], address: int, caller: Function, line: str) -> int:
    if address not in functions:
        raise StackError(f"{caller.name}: branch into the middle of a function: {line.strip()}")
    return address


def resolve_indirect(functions: dict[int, Function], address_taken: set[int]) -> None:
    """Add the reviewed targets of each indirect call; check nothing is unaccounted for."""
    targets_of: set[int] = set()
    for fn in functions.values():
        entry = next((INDIRECT_CALLS[n] for n in fn.names if n in INDIRECT_CALLS), None)
        if fn.indirect and entry is None:
            raise StackError(
                f"{fn.name} calls through a function pointer: add it to INDIRECT_CALLS"
            )
        if entry is None:
            continue
        for pattern in entry[0]:
            matches = {
                f.start
                for f in functions.values()
                if any(fnmatch.fnmatchcase(n, pattern) for n in f.names)
            }
            if not matches:
                raise StackError(f"INDIRECT_CALLS[{fn.name}]: no function matches '{pattern}'")
            fn.calls |= matches
            targets_of |= matches
    for start in sorted(address_taken - targets_of):
        name = functions[start].name
        raise StackError(
            f"the address of {name} is taken, but no entry in INDIRECT_CALLS lists it as a target"
        )


def check_against_gcc(build: Path, functions: dict[int, Function]) -> int:
    """Compare frames with GCC's -fstack-usage figures; return how many were compared."""
    gcc: dict[str, set[int]] = {}
    for su in build.rglob("*.su"):
        if "CompilerId" in su.parts[-2]:
            continue
        for line in su.read_text().splitlines():
            where, size, kind = line.split("\t")
            if kind != "static":
                raise StackError(f"{where}: stack use is {kind}, not fixed")
            gcc.setdefault(where.rsplit(":", 1)[-1], set()).add(int(size))
    if not gcc:
        raise StackError(f"no .su files under {build}: build with -fstack-usage")
    compared = 0
    for fn in functions.values():
        sizes = gcc.get(fn.name)
        if sizes is None or len(sizes) != 1 or len(fn.names) != 1:
            continue  # library and assembly code, or a name used twice
        (expected,) = sizes
        # GCC's figure leaves out the argument registers a variadic function saves.
        expected += fn.varargs
        if fn.frame != expected:
            raise StackError(
                f"{fn.name}: {fn.frame} bytes from the disassembly, GCC says {expected}"
            )
        compared += 1
    return compared


@dataclass
class Chain:
    depth: int
    path: list[str]


def deepest(functions: dict[int, Function], start: int, stops: set[int]) -> Chain:
    """Deepest call chain from `start`; calls into `stops` cost nothing here."""
    memo: dict[int, Chain] = {}

    def walk(address: int, path: list[int]) -> Chain:
        if address in path:
            cycle = [functions[a].name for a in path[path.index(address) :]] + [
                functions[address].name
            ]
            raise StackError(f"recursion: {' > '.join(cycle)}")
        if address in memo:
            return memo[address]
        fn = functions[address]
        best = Chain(0, [])
        if address not in stops:
            for call in sorted(fn.calls):
                chain = walk(call, [*path, address])
                if chain.depth > best.depth or not best.path:
                    best = chain
        result = Chain(fn.frame + best.depth, [fn.name, *best.path])
        memo[address] = result
        return result

    return walk(start, [])


def symbol_address(functions_dump: str, name: str) -> int:
    m = re.search(rf"^([0-9a-f]{{8}}) .* {re.escape(name)}$", functions_dump, re.M)
    if not m:
        raise StackError(f"symbol {name} not found")
    return int(m.group(1), 16)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("build", nargs="?", type=Path, default=ROOT / "build" / "firmware")
    parser.add_argument("--summary-md", type=Path)
    args = parser.parse_args()
    elf = args.build / "firmware" / "obd2_sniffer.elf"

    try:
        symbols = run(OBJDUMP, "-t", str(elf))
        functions = read_functions(elf)
        by_name = {n: f for f in functions.values() for n in f.names}
        literal_words = disassemble(elf, functions)
        data = section_words(
            elf, [".isr_vector", ".rodata", ".data", ".init_array", ".preinit_array"]
        )
        vectors = data.pop(".isr_vector")
        address_taken = {
            w & ~1
            for w in literal_words + [w for words in data.values() for w in words]
            if w & 1 and (w & ~1) in functions
        }
        resolve_indirect(functions, address_taken)
        compared = check_against_gcc(args.build, functions)

        for name, why in SETS_STACK.items():
            if name in by_name and not by_name[name].sets_sp:
                raise StackError(f"SETS_STACK[{name}] ({why}) no longer loads the stack pointer")
        switches = {by_name[n].start for n in SWITCHES_STACK if n in by_name}

        reset = vectors[1] & ~1
        handlers = sorted({w & ~1 for w in vectors[2:] if w} - {reset})
        thread = deepest(functions, reset, switches)
        nested = [(functions[h].name, deepest(functions, h, switches)) for h in handlers]
        main_total = thread.depth + sum(EXCEPTION_FRAME + chain.depth for _, chain in nested)
        main_size = symbol_address(symbols, "_Min_Stack_Size")

        fault_rows = []
        for name, (lo, hi) in SWITCHES_STACK.items():
            fn = by_name[name]
            size = symbol_address(symbols, hi) - symbol_address(symbols, lo)
            chains = [deepest(functions, c, set()) for c in fn.calls]
            worst = max(chains, key=lambda c: c.depth)
            fault_rows.append((lo, size, worst))
    except StackError as err:
        print(f"stack check: {err}", file=sys.stderr)
        return 1

    ok = main_total <= main_size and all(c.depth <= size for _, size, c in fault_rows)
    lines = [
        f"### Stack use, worst case ({args.build.name}): {'PASSED' if ok else 'FAILED'}",
        "",
        f"Main stack: **{main_total:,} of {main_size:,} bytes** reserved "
        f"({100 * main_total / main_size:.0f}%).",
        "",
        "| Entry point | Bytes | Deepest call chain |",
        "|---|---:|---|",
        f"| Start-up and main loop | {thread.depth:,} | {' > '.join(thread.path)} |",
    ]
    for name, chain in sorted(nested, key=lambda item: -item[1].depth):
        lines.append(
            f"| {name} (+{EXCEPTION_FRAME} exception frame) | {chain.depth:,} | "
            f"{' > '.join(chain.path)} |"
        )
    for stack, size, chain in fault_rows:
        lines += [
            "",
            f"Fault stack (`{stack}`): **{chain.depth:,} of {size:,} bytes**: "
            f"{' > '.join(chain.path)}.",
        ]
    lines += [
        "",
        f"{len(functions)} functions; {compared} frames match GCC's -fstack-usage figures. "
        f"Each nested exception adds {EXCEPTION_FRAME} bytes, and every handler is counted once.",
    ]
    report = "\n".join(lines) + "\n"
    print(report)
    if args.summary_md:
        with args.summary_md.open("a") as fh:
            fh.write(report)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
