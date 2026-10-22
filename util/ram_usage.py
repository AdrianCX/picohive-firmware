#!/usr/bin/env python3
"""Static RAM report for a pico firmware ELF: what is in .data and .bss, the
largest objects first, and how much is left over for the heap.

Every static object - singletons included - is RAM the heap never gets, used
or not. The heap is what the linker leaves between the end of .bss and the
stack, which is what getTotalHeap() reports on the board.

Usage:
    os/util/ram_usage.py build/pico_w/test.elf
    os/util/ram_usage.py build/pico_w/test.elf --top 30
    os/util/ram_usage.py build/pico_w/test.elf --report build/pico_w/test.ram.txt

Needs arm-none-eabi-nm (--nm or $NM). Without a cross toolchain on the host,
run it in the build container:

    podman run --rm -v "$PWD":/workspaces/app -w /workspaces/app local_build_pico_https \\
        os/util/ram_usage.py build/pico_w/robotarm.elf
"""
import argparse
import os
import subprocess
import sys


def symbols(nm, elf):
    """(address, size, type, name, source file) of every symbol, names demangled."""
    out = subprocess.run([nm, "-S", "-l", "-C", elf], capture_output=True, text=True, check=True).stdout
    found = []

    for line in out.splitlines():
        line, _, where = line.partition("\t")
        parts = line.split(None, 3)
        source = where.rsplit(":", 1)[0]

        if len(parts) == 4:
            found.append((int(parts[0], 16), int(parts[1], 16), parts[2], parts[3], source))
        elif len(parts) == 3:
            found.append((int(parts[0], 16), 0, parts[1], parts[2], source))

    return found


def owner(source):
    """What a symbol belongs to: an os library, the app's own src/, a part of the SDK."""
    if "/os/libraries/" in source:
        return "os " + source.split("/os/libraries/")[1].split("/")[0]
    if "/pico-sdk/lib/" in source:
        return "sdk " + source.split("/pico-sdk/lib/")[1].split("/")[0]
    if "/pico-sdk/src/" in source:
        return "sdk " + source.split("/pico-sdk/src/")[1].split("/")[1]
    if "/src/" in source:
        return "app " + source.split("/src/")[-1].split("/")[0]
    return "toolchain / unknown"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("elf")
    parser.add_argument("--top", type=int, default=15, help="largest objects printed (default 15)")
    parser.add_argument("--report", help="every object, largest first, written here")
    parser.add_argument("--nm", default=os.environ.get("NM", "arm-none-eabi-nm"))
    args = parser.parse_args()

    try:
        found = symbols(args.nm, args.elf)
    except FileNotFoundError:
        sys.exit(f"{args.nm} not found: pass --nm, or run it in the build container (see --help)")

    by_name = {name: address for address, _, _, name, _ in found}
    ram = [(size, kind, name, owner(source)) for _, size, kind, name, source in found if (kind in "bBdD") and (size > 0)]
    ram.sort(reverse=True)

    groups = {}
    for size, _, _, group in ram:
        groups[group] = groups.get(group, 0) + size

    data = sum(size for size, kind, _, _ in ram if kind in "dD")
    bss = sum(size for size, kind, _, _ in ram if kind in "bB")

    heap = None
    if ("__end__" in by_name) and ("__HeapLimit" in by_name):
        heap = by_name["__HeapLimit"] - by_name["__end__"]

    name = os.path.basename(args.elf)
    print(f"{name}: static RAM {(data + bss) / 1024:.1f}K (.data {data / 1024:.1f}K, .bss {bss / 1024:.1f}K)"
          + (f", heap {heap / 1024:.1f}K" if heap is not None else ""))

    print("  by where it comes from:")
    for group, size in sorted(groups.items(), key=lambda item: -item[1])[:args.top]:
        print(f"  {size:7d}  {group}")

    print("  largest objects:")
    for size, kind, symbol, _ in ram[:args.top]:
        print(f"  {size:7d}  {'data' if kind in 'dD' else 'bss '}  {symbol}")

    if args.report:
        with open(args.report, "w") as report:
            report.write(f"{name}: .data {data} bytes, .bss {bss} bytes"
                         + (f", heap {heap} bytes" if heap is not None else "") + "\n\n")

            for group, size in sorted(groups.items(), key=lambda item: -item[1]):
                report.write(f"{size:7d}  {group}\n")

            report.write("\n")

            for size, kind, symbol, group in ram:
                report.write(f"{size:7d}  {'data' if kind in 'dD' else 'bss '}  {symbol:60s}  {group}\n")

        print(f"Every static object, largest first: {args.report}")


if __name__ == "__main__":
    main()
