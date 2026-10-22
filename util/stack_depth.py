#!/usr/bin/env python3
"""Worst-case stack depth report for a pico firmware ELF.

The core 0 stack lives in SCRATCH_Y (4K on rp2350) and nothing checks it at
runtime: the linker only checks that PICO_STACK_SIZE fits the region, and an
overflow silently walks down into the heap. This script estimates, statically,
how deep each call chain actually goes.

Frame sizes come from each function's prologue in the disassembly (push / vpush
/ sub sp), which covers compiler output, .part/.isra clones and hand-written
assembly alike. Call edges come from direct "bl"/"blx"; a "b"/"b.w" to another
function's entry is treated as a tail call. The deepest path through that graph
is reported per function.

Indirect calls ("blx r3") are matched to the functions they can reach. The
call register is followed back through the function's branches to where it
was loaded, and then:

  * by type, from the ELF's DWARF: what the call goes through is traced to a
    parameter, a member, a register-held local, a static variable or a
    function's return value, and its type gives
      - for a virtual call, that class's slot in its own vtable and in the
        vtables of every class derived from it
      - for a function pointer member, what static objects of that struct hold
        there, plus every function the code stores there (callbacks registered
        at run time, like lwIP's), following arguments back to the callers
  * by value: a function loaded from a literal pool or a static table, or
    passed in as an argument by the direct callers
  * by shape, when there is no type: any vtable's slot for a C++ call through
    +0, else the functions whose address the same source file takes, else any
    static object with a function at that offset

Guessed edges are marked "~" with how they were matched, --explain FUNC shows
what each call site in FUNC was matched to, and --no-guess turns it all off.
The shape matches are the loose ones; the summary at the end says how many
there were. What is only known at run time (the boot ROM's function table, a
table filled in at boot) stays unmatched.

Usage:
    os/util/stack_depth.py build/pico2_w/epaper_rb.elf
    os/util/stack_depth.py build/pico2_w/epaper_rb.elf --roots main --top 5
    os/util/stack_depth.py build/pico_w/irrigation.dis --roots CalendarStatus::on_header
    os/util/stack_depth.py epaper_rb.dis --limit 4096
    os/util/stack_depth.py build/pico_w/irrigation.elf --top 10 --report build/pico_w/irrigation.stack.txt

Needs arm-none-eabi-objdump for an .elf (--objdump or $OBJDUMP to point at it).
Without a cross toolchain on the host, generate the disassembly in the build
container and pass that instead - the .elf beside it (or --elf) is still read
for data, which needs nothing installed:

    podman run --rm -v "$PWD":/workspaces/app local_build_pico_https \\
        arm-none-eabi-objdump -d /workspaces/app/build/pico2_w/epaper_rb.elf \\
        > epaper_rb.dis

Leave out -C: constructors and destructors come in several variants that
demangle to one name, and they need to stay apart. Names are demangled for
the report anyway.

Calls are followed together with the object they are made on, where that is
a static one (a singleton and its members): a callback stored in an object
by its owner then only reaches that owner, so CalendarCommands::m_client
calls back CalendarCommands and not TelegramManager. See Objects;
--no-objects turns it off.

Functions that call each other round are where recursion would be. There a
path visits each function (on each object) at most once, and the longest such
path is searched for; see Depth.

Each step of a stack shows the file and line it calls the next step from,
from the DWARF line table.

Function-local statics ("static T instance;" in a get()) are constructed by
the first call only: what runs between __cxa_guard_acquire and _release is
left out of the call graph and listed under "constructed on first use" with
its own cost. That is only right if the first call happens on a shallow stack -
at start-up - so keep it that way, or pass --count-first-use to count the
construction under every caller.

Caveats:
  * Indirect calls that cannot be matched are not followed - functions that
    make one are marked "*", and what they call shows up as separate roots.
  * A path through a cycle counts each function on it once, so real
    recursion is only the one-pass cost of it; cycles are listed at the end.
  * Interrupt frames sit on top of whatever is running.
  * alloca/VLA frames are not in the totals; they are listed from the .su files
    (build with -fstack-usage to get those).

Exits 1 if any function's worst case exceeds the stack limit.
"""

import argparse
import bisect
import collections
import contextlib
import functools
import os
import re
import struct
import subprocess
import sys

FUNC_RE = re.compile(r'^([0-9a-f]+) <(.+)>:')
INSN_RE = re.compile(r'^\s*([0-9a-f]+):\t[0-9a-f ]+\t\s*(\S+)\s*(.*)$')
TARGET_RE = re.compile(r'[0-9a-f]+ <(.+?)(\+0x[0-9a-f]+)?>\s*$')   # names can hold <> themselves
REGISTER_RE = re.compile(r'^(r\d+|sb|sl|fp|ip|lr|pc)\b')
MANGLED_RE = re.compile(r'^_Z[A-Za-z0-9_.]+$')
BRANCH_TARGET_RE = re.compile(r'([0-9a-f]+) <')
POOL_RE = re.compile(r';\s*\(([0-9a-f]+)')
MEMORY_RE = re.compile(r'\[(\w+)(?:,\s*(#-?\w+|\w+)(?:,[^\]]*)?)?\]')

# Instructions past this many into a function are no longer prologue.
PROLOGUE_WINDOW = 12

# How far back from an indirect call to look for where its register was set.
TRACE_WINDOW = 24

CXXFILT = ('arm-none-eabi-c++filt', 'c++filt')

# GCC clones whose parameters no longer match the source's.
CLONE_RE = re.compile(r'\.(isra|constprop|part)\.\d+')

REGISTER_NUMBERS = dict({f'r{n}': n for n in range(16)},
                        sb=9, sl=10, fp=11, ip=12, sp=13, lr=14, pc=15)

# What GCC brackets the construction of a function-local static with.
GUARD_ACQUIRE = '__cxa_guard_acquire'
GUARD_END = ('__cxa_guard_release', '__cxa_guard_abort')

# A call leaves these holding whatever the callee left in them.
CLOBBERED = {'r0', 'r1', 'r2', 'r3', 'ip', 'r12', 'lr'}

# Instructions that write no register (calls are handled on their own).
NO_DESTINATION = ('str', 'cmp', 'cmn', 'tst', 'teq', 'cbz', 'cbnz', 'push', 'it',
                  'nop', '.', 'bkpt', 'svc', 'dmb', 'dsb', 'isb', 'cps', 'msr',
                  'vstr', 'vpush', 'vcmp', 'stm', 'pld', 'sev', 'wf', 'udf')

# b, bx and the conditional branches - "bl" alone is a call, "blt" is not.
BRANCH_RE = re.compile(r'^b(x|eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)?$')

# Itanium vtables start with offset-to-top and the typeinfo pointer; a vptr
# points past them, at the first virtual function.
VTABLE_HEADER = 8


def canon(name):
    """Name as written in the dump.

    GCC clones (".part.0", "[clone .isra.0]") stay separate nodes: a clone that
    is called by its original shows up as its own step in the stack, which adds
    up to the same total and says more about where the bytes went.
    """
    return name.strip()


def demangle(names):
    """Best-effort c++filt over a set of names; identity if none is installed."""
    names = [n for n in names if MANGLED_RE.match(n)]
    if not names:
        return {}
    for tool in CXXFILT:
        try:
            out = subprocess.run([tool], input='\n'.join(names), check=True,
                                 capture_output=True, text=True).stdout
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue
        return dict(zip(names, out.splitlines()))
    return {}


def braced(args):
    """The register list of a push/stm, as a count."""
    start, end = args.find('{'), args.find('}')
    if start < 0 or end < 0:
        return 0
    return len([r for r in args[start + 1:end].split(',') if r.strip()])


class Elf:
    """Just enough of a 32 bit little endian ELF: symbols and initialised data."""

    def __init__(self, path):
        with open(path, 'rb') as handle:
            self.data = handle.read()
        if self.data[:4] != b'\x7fELF' or self.data[4] != 1 or self.data[5] != 1:
            raise ValueError(f'{path} is not a 32 bit little endian ELF')

        shoff, = struct.unpack_from('<I', self.data, 0x20)
        shentsize, shnum = struct.unpack_from('<HH', self.data, 0x2e)
        sections = [struct.unpack_from('<IIIIIIIIII', self.data, shoff + i * shentsize)
                    for i in range(shnum)]

        shstrndx, = struct.unpack_from('<H', self.data, 0x32)
        names = sections[shstrndx][4]
        self.sections = {}   # name -> (file offset, size)
        for section in sections:
            end = self.data.index(b'\0', names + section[0])
            self.sections[self.data[names + section[0]:end].decode(errors='replace')] = (section[4], section[5])

        # (start, end, file offset) of everything with contents in the image.
        self.ranges = [(s[3], s[3] + s[5], s[4]) for s in sections
                       if (s[2] & 2) and s[1] != 8 and s[5] > 0]   # SHF_ALLOC, not NOBITS
        # Everything in memory, .bss included, and what of it can be written.
        self.memory = [(s[3], s[3] + s[5]) for s in sections if (s[2] & 2) and s[5] > 0 and s[3] > 0]
        self.writable = [(s[3], s[3] + s[5]) for s in sections if (s[2] & 3) == 3 and s[5] > 0 and s[3] > 0]

        self.symbols = []   # (name, address, size, type)
        for section in sections:
            if section[1] != 2:   # SHT_SYMTAB
                continue
            strtab = sections[section[6]][4]
            for i in range(section[5] // 16):
                name, value, size, info, _, _ = struct.unpack_from(
                    '<IIIBBH', self.data, section[4] + i * 16)
                end = self.data.index(b'\0', strtab + name)
                self.symbols.append((self.data[strtab + name:end].decode(errors='replace'),
                                     value, size, info & 0xf))

    def word(self, address):
        for start, end, offset in self.ranges:
            if start <= address and address + 4 <= end:
                return struct.unpack_from('<I', self.data, offset + address - start)[0]
        return None

    def symbol(self, name):
        for symbol, value, _, _ in self.symbols:
            if symbol == name:
                return value
        return None


def bare(value):
    """A traced value without the register it passed through."""
    while value[0] == 'at':
        value = value[3]
    return value


COMMENT_RE = re.compile(r';|\s@')

@functools.lru_cache(maxsize=None)
def operands(args):
    """The operands of an instruction, without the trailing comment."""
    return COMMENT_RE.split(args, 1)[0].strip()


@functools.lru_cache(maxsize=None)
def is_call(op):
    return op.split('.', 1)[0] in ('bl', 'blx')


@functools.lru_cache(maxsize=None)
def destinations(op, args):
    """Registers an instruction writes."""
    base = op.split('.', 1)[0]
    text = operands(args)
    if BRANCH_RE.match(base) or is_call(op):
        return frozenset()
    if base.startswith(('pop', 'ldm', 'stm')):
        start, end = text.find('{'), text.find('}')
        found = {r.strip() for r in text[start + 1:end].split(',')} if start >= 0 and not base.startswith('stm') else set()
        if '!' in text.split(',')[0]:
            found.add(text.split(',')[0].strip().rstrip('!'))   # the base, written back
        return frozenset(found)
    if base.startswith(NO_DESTINATION) or not text:
        return frozenset()
    parts = [p.strip().rstrip('!') for p in text.split(',')]
    if base.startswith(('ldrd', 'umull', 'smull', 'umlal', 'smlal')):
        return frozenset(parts[:2])
    return frozenset((parts[0],))


class Image:
    def __init__(self, disassembly):
        self.frames = {}                          # function -> own frame bytes
        self.starts = {}                          # address -> function
        self.starts_of = {}                       # function -> address
        self.code = collections.defaultdict(list) # function -> [(address, op, args)]
        self.graph = collections.defaultdict(set)  # caller -> callees
        self.callers = collections.defaultdict(set)
        self.sites = []                           # (function, index of the call, register)
        self.indirect = set()                     # functions with a call not followed
        self.guessed = {}                         # (caller, callee) -> how it was matched
        self.first_use = collections.defaultdict(set)  # function -> what it constructs once
        self.once_sites = set()                   # (function, index) of indirect calls in there
        self.guarding = False                     # between __cxa_guard_acquire and _release
        self._parse(disassembly)

    def _parse(self, disassembly):
        current, seen = None, 0
        self.pool = {}          # address -> literal pool word
        self.loaded = {}        # register -> pool address it was loaded from, in this prologue
        self.adjusts = []       # (function, pool address, sign): sp moved by a pool word
        for line in disassembly.splitlines():
            match = FUNC_RE.match(line)
            if match:
                current = canon(match.group(2))
                self.frames.setdefault(current, 0)
                self.starts[int(match.group(1), 16)] = current
                self.starts_of.setdefault(current, int(match.group(1), 16))
                seen = 0
                self.guarding = False
                self.loaded = {}
                continue
            match = INSN_RE.match(line)
            if not match or current is None:
                continue
            address, op, args = int(match.group(1), 16), match.group(2), match.group(3)
            if op == '.word':
                self.pool[address] = int(args.split()[0], 16)
            seen += 1
            if seen <= PROLOGUE_WINDOW:
                self._prologue(current, op, args)
            self.code[current].append((address, op, args))
            self._edges(current, op, args)

        # Frames too big for an immediate (thumb1 stops at 508 bytes) are
        # "ldr rX, =-size; add sp, rX": the size is in the literal pool.
        for func, address, sign in self.adjusts:
            value = self.pool.get(address)
            if value is not None:
                if value & 0x80000000:
                    value -= 1 << 32
                self.frames[func] += sign * value

    def _prologue(self, func, op, args):
        if op.startswith('push') or (op.startswith('stmdb') and 'sp!' in args):
            self.frames[func] += 4 * braced(args)
        elif op.startswith('vpush'):
            self.frames[func] += 8 * braced(args)
        elif op.startswith('ldr') and '[pc' in args:
            pool = POOL_RE.search(args)
            if pool:
                self.loaded[operands(args).split(',')[0].strip()] = int(pool.group(1), 16)
        elif op.startswith(('sub', 'add')) and args.startswith('sp,'):
            parts = [p.strip() for p in operands(args).split(',')]
            sign = 1 if op.startswith('sub') else -1
            immediate = re.search(r'#(\d+)', args)
            if immediate and sign == 1:
                self.frames[func] += int(immediate.group(1))
            elif not immediate and parts[-1] in self.loaded:
                self.adjusts.append((func, self.loaded[parts[-1]], sign))

    def _edges(self, func, op, args):
        target = TARGET_RE.search(args)
        register = REGISTER_RE.match(args)
        if is_call(op):                            # bl / blx: a real call
            if target:
                callee = canon(target.group(1))
                if callee == GUARD_ACQUIRE:
                    self.guarding = True
                elif callee in GUARD_END:
                    self.guarding = False
                elif self.guarding:
                    # A function-local static being constructed: that runs
                    # once, on the first call, not on every one.
                    self.first_use[func].add(callee)
                else:
                    self._edge(func, callee)
            elif register:
                if self.guarding:
                    self.once_sites.add((func, len(self.code[func]) - 1))
                self.indirect.add(func)
                self.sites.append((func, len(self.code[func]) - 1, register.group(1)))
        elif op.startswith('bx') and register and register.group(1) != 'lr':
            # A tail call through a register - unless the register was just
            # popped, which is how thumb1 returns past a pushed lr.
            recent = self.code[func][-4:-1]
            if not any(o.startswith('pop') and register.group(1) in a for _, o, a in recent):
                self.indirect.add(func)
                self.sites.append((func, len(self.code[func]) - 1, register.group(1)))
        elif op.startswith('b'):
            # A branch straight to another function's entry is a tail call;
            # anything with a +0x offset is just intra-function control flow.
            if target and not target.group(2):
                self._edge(func, canon(target.group(1)))

    def _edge(self, caller, callee):
        if callee != caller:
            self.graph[caller].add(callee)
            self.callers[callee].add(caller)


def uleb(data, offset):
    result, shift = 0, 0
    while True:
        byte = data[offset]
        offset += 1
        result |= (byte & 0x7f) << shift
        shift += 7
        if not byte & 0x80:
            return result, offset


def sleb(data, offset):
    result, shift = 0, 0
    while True:
        byte = data[offset]
        offset += 1
        result |= (byte & 0x7f) << shift
        shift += 7
        if not byte & 0x80:
            if byte & 0x40:
                result -= 1 << shift
            return result, offset


# DWARF tags and attributes this reads; everything else is skipped over.
TAG_ARRAY, TAG_CLASS, TAG_PARAMETER, TAG_MEMBER, TAG_POINTER, TAG_REFERENCE = 0x01, 0x02, 0x05, 0x0d, 0x0f, 0x10
TAG_STRUCT, TAG_SUBROUTINE_TYPE, TAG_TYPEDEF, TAG_UNION, TAG_INHERITANCE = 0x13, 0x15, 0x16, 0x17, 0x1c
TAG_SUBRANGE, TAG_CONST, TAG_SUBPROGRAM, TAG_VARIABLE, TAG_VOLATILE = 0x21, 0x26, 0x2e, 0x34, 0x35
TAG_RESTRICT, TAG_NAMESPACE, TAG_RVALUE_REFERENCE = 0x37, 0x39, 0x42
TAG_COMPILE_UNIT, TAG_LEXICAL_BLOCK, TAG_INLINED = 0x11, 0x0b, 0x1d
AT_LOCATION, AT_NAME, AT_BYTE_SIZE, AT_LOW_PC, AT_UPPER_BOUND = 0x02, 0x03, 0x0b, 0x11, 0x2f
AT_ABSTRACT_ORIGIN, AT_COUNT, AT_MEMBER_LOCATION, AT_DECLARATION = 0x31, 0x37, 0x38, 0x3c
AT_SPECIFICATION, AT_TYPE = 0x47, 0x49

CLASS_TAGS = (TAG_CLASS, TAG_STRUCT, TAG_UNION)
SKIPPED_TYPES = (TAG_TYPEDEF, TAG_CONST, TAG_VOLATILE, TAG_RESTRICT)
POINTER_TAGS = (TAG_POINTER, TAG_REFERENCE, TAG_RVALUE_REFERENCE)
WANTED = {AT_LOCATION, AT_NAME, AT_BYTE_SIZE, AT_LOW_PC, AT_UPPER_BOUND, AT_ABSTRACT_ORIGIN,
          AT_COUNT, AT_MEMBER_LOCATION, AT_DECLARATION, AT_SPECIFICATION, AT_TYPE}

# Static arrays are only looked into this far.
MAX_POSITIONS = 512


class Dwarf:
    """The types in .debug_info that say what an indirect call is made on.

    Kept to what that needs: classes with their members and bases, pointers
    and arrays, the parameters and register-held locals of each function, and
    the static variables with their addresses. Classes are known by qualified
    name, since every compilation unit has its own copy of each.
    """

    def __init__(self, elf):
        self.tag = {}        # die -> tag
        self.attrs = {}      # die -> {attribute: value}, wanted ones only
        self.parent = {}     # die -> parent die
        self.children = collections.defaultdict(list)
        self.classes = {}    # qualified name -> die with the definition
        self.params = {}     # function address -> [parameter die]
        self.functions = {}  # function address -> its die
        self.units = {}      # function address -> compilation unit it is in
        self.registers = {}  # function address -> [(start, end, register, type die)]
        self.list_base = {}  # die -> base address its location list is relative to
        self.anonymous = {}  # die of an unnamed struct -> the typedef that names it
        self.variables = {}  # address -> type die
        self.unit_of = {}    # function die -> its compilation unit
        self.positions_cache = {}
        if '.debug_info' not in elf.sections or '.debug_abbrev' not in elf.sections:
            return
        strings = elf.sections.get('.debug_str', (0, 0))[0]
        self.data = elf.data
        self.loc = elf.sections.get('.debug_loc')
        self._parse(elf.data, elf.sections['.debug_info'], elf.sections['.debug_abbrev'][0], strings)
        self._index()

    def _abbrevs(self, data, offset):
        table = {}
        while True:
            code, offset = uleb(data, offset)
            if code == 0:
                return table
            tag, offset = uleb(data, offset)
            children = data[offset]
            offset += 1
            specs = []
            while True:
                attribute, offset = uleb(data, offset)
                form, offset = uleb(data, offset)
                if attribute == 0 and form == 0:
                    break
                specs.append((attribute, form))
            table[code] = (tag, children, specs)

    def _parse(self, data, info, abbrev_base, strings):
        offset, end = info
        end += offset
        base = offset
        tables = {}
        while offset < end:
            unit = offset
            length, version, abbrev = struct.unpack_from('<IHI', data, offset)
            address_size = data[offset + 10]
            unit_end = offset + 4 + length
            offset += 11
            if version not in (2, 3, 4) or address_size != 4:
                offset = unit_end
                continue
            if abbrev not in tables:
                tables[abbrev] = self._abbrevs(data, abbrev_base + abbrev)
            table = tables[abbrev]
            parents = [None]
            unit_base = 0
            while offset < unit_end:
                die = offset - base
                code, offset = uleb(data, offset)
                if code == 0:
                    parents.pop()
                    continue
                tag, has_children, specs = table[code]
                found = {}
                for attribute, form in specs:
                    value, offset = self._value(data, offset, form, unit - base, strings, version)
                    if attribute in WANTED:
                        found[attribute] = value
                if tag == TAG_COMPILE_UNIT:
                    unit_base = found.get(AT_LOW_PC, 0) or 0
                elif tag == TAG_SUBPROGRAM:
                    self.unit_of[die] = unit
                if isinstance(found.get(AT_LOCATION), int):
                    self.list_base[die] = unit_base
                self.tag[die] = tag
                self.attrs[die] = found
                self.parent[die] = parents[-1]
                if parents[-1] is not None:
                    self.children[parents[-1]].append(die)
                if has_children:
                    parents.append(die)
            offset = unit_end

    @staticmethod
    def _value(data, offset, form, unit, strings, version):
        if form in (0x01, 0x06, 0x13, 0x17):           # addr, data4, ref4, sec_offset
            value = struct.unpack_from('<I', data, offset)[0]
            return (value + unit if form == 0x13 else value), offset + 4
        if form == 0x10:                               # ref_addr
            return struct.unpack_from('<I', data, offset)[0], offset + 4
        if form in (0x0b, 0x11, 0x0c):                 # data1, ref1, flag
            return data[offset] + (unit if form == 0x11 else 0), offset + 1
        if form in (0x05, 0x12):                       # data2, ref2
            return struct.unpack_from('<H', data, offset)[0] + (unit if form == 0x12 else 0), offset + 2
        if form in (0x07, 0x14, 0x20):                 # data8, ref8, ref_sig8
            return None, offset + 8
        if form == 0x0e:                               # strp
            start = strings + struct.unpack_from('<I', data, offset)[0]
            return data[start:data.index(b'\0', start)].decode(errors='replace'), offset + 4
        if form == 0x08:                               # string
            stop = data.index(b'\0', offset)
            return data[offset:stop].decode(errors='replace'), stop + 1
        if form == 0x0f:                               # udata
            return uleb(data, offset)
        if form == 0x15:                               # ref_udata
            value, offset = uleb(data, offset)
            return value + unit, offset
        if form == 0x0d:                               # sdata
            return sleb(data, offset)
        if form == 0x19:                               # flag_present
            return True, offset
        if form in (0x18, 0x09):                       # exprloc, block
            size, offset = uleb(data, offset)
            return bytes(data[offset:offset + size]), offset + size
        if form == 0x0a:                               # block1
            return bytes(data[offset + 1:offset + 1 + data[offset]]), offset + 1 + data[offset]
        if form == 0x03:                               # block2
            size = struct.unpack_from('<H', data, offset)[0]
            return bytes(data[offset + 2:offset + 2 + size]), offset + 2 + size
        if form == 0x04:                               # block4
            size = struct.unpack_from('<I', data, offset)[0]
            return bytes(data[offset + 4:offset + 4 + size]), offset + 4 + size
        if form in (0x1f20, 0x1f21):                   # GNU_ref_alt, GNU_strp_alt
            return None, offset + 4
        if form == 0x16:                               # indirect
            form, offset = uleb(data, offset)
            return Dwarf._value(data, offset, form, unit, strings, version)
        raise ValueError(f'DWARF form {form:#x} not handled')

    def _index(self):
        for die, tag in self.tag.items():
            if tag == TAG_TYPEDEF and AT_NAME in self.attrs[die]:
                target = self.attrs[die].get(AT_TYPE)
                if self.tag.get(target) in CLASS_TAGS and AT_NAME not in self.attrs[target]:
                    self.anonymous.setdefault(target, self.attrs[die][AT_NAME])
        for die, tag in self.tag.items():
            attrs = self.attrs[die]
            if tag in CLASS_TAGS and not attrs.get(AT_DECLARATION) and (AT_NAME in attrs or die in self.anonymous):
                self.classes.setdefault(self.qualified(die), die)
            elif tag == TAG_SUBPROGRAM and AT_LOW_PC in attrs:
                address = attrs[AT_LOW_PC] & ~1
                params = [c for c in self.children[die] if self.tag[c] == TAG_PARAMETER]
                # Constructors and destructors have several entries at one
                # address; the one that lists the parameters is the one wanted.
                if address not in self.functions or len(params) > len(self.params[address]):
                    self.params[address] = params
                    self.functions[address] = die
                    self.units[address] = self.unit_of.get(die)
                self.registers.setdefault(address, []).extend(self._locals(die))
            elif tag == TAG_VARIABLE:
                location = attrs.get(AT_LOCATION)
                if isinstance(location, bytes) and len(location) == 5 and location[0] == 0x03:
                    address = struct.unpack_from('<I', location, 1)[0]
                    self.variables[address] = self.attr(die, AT_TYPE)

    def _locals(self, function):
        """Where each parameter and local of a function lives, when that is a
        register: [(start, end, register number, type die)]."""
        found = []
        pending = list(self.children.get(function, ()))
        while pending:
            die = pending.pop()
            tag = self.tag[die]
            if tag in (TAG_LEXICAL_BLOCK, TAG_INLINED):
                pending.extend(self.children.get(die, ()))
                continue
            if tag not in (TAG_VARIABLE, TAG_PARAMETER):
                continue
            location = self.attrs[die].get(AT_LOCATION)
            kind = self.attr(die, AT_TYPE)
            if isinstance(location, bytes):
                if location and 0x50 <= location[0] <= 0x6f:   # DW_OP_reg0..31
                    found.append((0, 1 << 32, location[0] - 0x50, kind))
            elif isinstance(location, int) and self.loc:
                for start, end, expression in self._location_list(location, self.list_base.get(die, 0)):
                    if expression and 0x50 <= expression[0] <= 0x6f:
                        found.append((start, end, expression[0] - 0x50, kind))
        return found

    def _location_list(self, offset, base):
        data, (start, size) = self.data, self.loc
        offset += start
        while offset + 8 <= start + size:
            begin, end = struct.unpack_from('<II', data, offset)
            offset += 8
            if begin == 0 and end == 0:
                return
            if begin == 0xffffffff:
                base = end
                continue
            length, = struct.unpack_from('<H', data, offset)
            yield begin + base, end + base, bytes(data[offset + 2:offset + 2 + length])
            offset += 2 + length

    def in_register(self, function, address, register):
        """Types of the variables in register at address, in function."""
        return [kind for start, end, number, kind in self.registers.get(function, ())
                if number == register and start <= address < end]

    def attr(self, die, attribute):
        """An attribute, from the die or the declaration it completes."""
        seen = 0
        while die is not None and seen < 8:
            attrs = self.attrs.get(die, {})
            if attribute in attrs:
                return attrs[attribute]
            die = attrs.get(AT_SPECIFICATION, attrs.get(AT_ABSTRACT_ORIGIN))
            seen += 1
        return None

    def qualified(self, die):
        parts = []
        while die is not None:
            tag = self.tag.get(die)
            if tag in CLASS_TAGS or tag == TAG_NAMESPACE:
                parts.append(self.attrs[die].get(AT_NAME) or self.anonymous.get(die, '(anonymous namespace)'))
            die = self.parent.get(die)
        return '::'.join(reversed(parts))

    def strip(self, die):
        while die is not None and self.tag.get(die) in SKIPPED_TYPES:
            die = self.attrs[die].get(AT_TYPE)
        return die

    def class_of(self, die):
        """Qualified name of the class die is, or None."""
        die = self.strip(die)
        if die is not None and self.tag.get(die) in CLASS_TAGS and (AT_NAME in self.attrs[die] or die in self.anonymous):
            return self.qualified(die)
        return None

    def definition(self, die):
        """The die with the members, for a class seen through any copy of it."""
        die = self.strip(die)
        name = self.class_of(die)
        return self.classes.get(name, die) if name else die

    def target(self, die):
        """The type a pointer or reference type points at."""
        die = self.strip(die)
        if die is not None and self.tag.get(die) in POINTER_TAGS:
            return self.attrs[die].get(AT_TYPE)
        return None

    def is_function_pointer(self, die):
        die = self.strip(die)
        if die is None or self.tag.get(die) != TAG_POINTER:
            return False
        target = self.strip(self.attrs[die].get(AT_TYPE))
        return target is not None and self.tag.get(target) == TAG_SUBROUTINE_TYPE

    def size(self, die):
        die = self.strip(die)
        if die is None:
            return None
        if self.tag.get(die) == TAG_ARRAY:
            count, element = self.count(die), self.size(self.attrs[die].get(AT_TYPE))
            return count * element if count and element else None
        return self.attrs.get(self.definition(die), {}).get(AT_BYTE_SIZE)

    def count(self, die):
        """Elements in an array type, None if DWARF does not say."""
        for child in self.children.get(die, ()):
            if self.tag[child] == TAG_SUBRANGE:
                attrs = self.attrs[child]
                if isinstance(attrs.get(AT_COUNT), int):
                    return attrs[AT_COUNT]
                if isinstance(attrs.get(AT_UPPER_BOUND), int):
                    return attrs[AT_UPPER_BOUND] + 1
        return None

    def bases(self, name):
        """[(base class, offset)] of a class."""
        die = self.classes.get(name)
        found = []
        for child in self.children.get(die, ()):
            if self.tag[child] == TAG_INHERITANCE:
                base = self.class_of(self.attrs[child].get(AT_TYPE))
                where = self.attrs[child].get(AT_MEMBER_LOCATION, 0)
                if base and isinstance(where, int):
                    found.append((base, where))
        return found

    def _members(self, die):
        """[(offset, type die)] of the members and bases of a class."""
        found = []
        definition = self.definition(die)
        union = self.tag.get(definition) == TAG_UNION
        for child in self.children.get(definition, ()):
            if self.tag[child] not in (TAG_MEMBER, TAG_INHERITANCE):
                continue
            # A member with no location is a static one - not in the object -
            # except in a union, where they all start at 0.
            where = self.attrs[child].get(AT_MEMBER_LOCATION, 0 if union else None)
            if isinstance(where, int):
                found.append((where, self.attrs[child].get(AT_TYPE)))
        return found

    def owner(self, address):
        """The class a function at address is a member of, as a die."""
        die = self.functions.get(address)
        seen = 0
        while die is not None and seen < 4:
            parent = self.parent.get(die)
            if self.tag.get(parent) in CLASS_TAGS:
                return parent
            die = self.attrs[die].get(AT_SPECIFICATION, self.attrs[die].get(AT_ABSTRACT_ORIGIN))
            seen += 1
        return None

    def place(self, die, offset):
        """What the word at +offset of an object of type die is, going down
        through members, bases and array elements: (the innermost class
        holding it, its offset in that class, its type - None unless
        something starts exactly there)."""
        container, inside = None, offset
        die = self.strip(die)
        for _ in range(16):
            if die is None:
                return container, inside, None
            tag = self.tag.get(die)
            if tag == TAG_ARRAY:
                element = self.attrs[die].get(AT_TYPE)
                size = self.size(element)
                if not size:
                    return container, inside, None
                offset %= size
                die = self.strip(element)
                continue
            if tag not in CLASS_TAGS:
                return container, inside, (die if offset == 0 else None)
            container, inside = self.class_of(die), offset
            best, best_where = None, -1
            for where, kind in self._members(die):
                if best_where < where <= offset:
                    best, best_where = kind, where
            if best is None:
                return container, inside, None
            die, offset = self.strip(best), offset - best_where
        return container, inside, None

    def embedded(self, die, offset):
        """The class of an object that starts at +offset of an object of
        type die (the object itself at 0), as a die."""
        die = self.strip(die)
        for _ in range(16):
            if die is None:
                return None
            if offset == 0 and self.class_of(die):
                return die
            tag = self.tag.get(die)
            if tag == TAG_ARRAY:
                element = self.attrs[die].get(AT_TYPE)
                size = self.size(element)
                if not size:
                    return None
                offset %= size
                die = self.strip(element)
                continue
            if tag not in CLASS_TAGS:
                return None
            best, best_where = None, -1
            for where, kind in self._members(die):
                if best_where < where <= offset:
                    best, best_where = kind, where
            if best is None:
                return None
            die, offset = self.strip(best), offset - best_where
        return None

    def positions(self, die, name, depth=0):
        """Offsets in an object of type die where a class name object starts."""
        die = self.strip(die)
        key = (die, name)
        if key in self.positions_cache:
            return self.positions_cache[key]
        found = []
        if die is not None and depth < 8:
            tag = self.tag.get(die)
            if self.class_of(die) == name:
                found = [0]
            elif tag in CLASS_TAGS:
                for where, kind in self._members(die):
                    found += [where + p for p in self.positions(kind, name, depth + 1)]
            elif tag == TAG_ARRAY:
                element = self.attrs[die].get(AT_TYPE)
                size, count = self.size(element), self.count(die)
                inner = self.positions(element, name, depth + 1)
                if size and count and inner:
                    for i in range(min(count, MAX_POSITIONS)):
                        found += [i * size + p for p in inner]
        self.positions_cache[key] = found[:MAX_POSITIONS]
        return self.positions_cache[key]


class Guesser:
    """Matches indirect calls to the functions they can reach.

    First by type, from DWARF: what the call is made on is traced back to a
    parameter, a member, a register-held local or a static variable, so a
    virtual call only reaches that class and the ones derived from it, and a
    function pointer member only what static objects hold there and what the
    code stores there. Without a type, by shape: any vtable slot, the
    functions whose address the same source file takes, or any static object
    with a function at that offset.
    """

    def __init__(self, image, elf, pretty):
        self.image = image
        self.elf = elf
        self.dwarf = Dwarf(elf)

        self.objects = []   # (start, end) of static objects, for untyped matches
        vtable_symbols = {}
        for name, address, size, kind in elf.symbols:
            if size == 0:
                continue
            if name.startswith('_ZTV'):
                vtable_symbols[name] = (address, address + size)
            elif kind == 1 and not name.startswith('_ZTI'):   # STT_OBJECT, not typeinfo
                self.objects.append((address, address + size))

        # Class name -> (start, end) of its vtable group.
        names = demangle(vtable_symbols)
        self.vtables = {}
        for symbol, extent in vtable_symbols.items():
            name = names.get(symbol, symbol)
            if name.startswith('vtable for '):
                self.vtables[name[len('vtable for '):]] = extent

        self.derived = collections.defaultdict(set)   # base -> classes deriving from it
        for name in self.vtables:
            self._derive(name, name, set())

        self.pure = {f for f in image.frames if pretty(f) == '__cxa_pure_virtual'}
        self.address = {func: address for address, func in image.starts.items()}
        self.stores = None    # (class, offset) -> functions stored there
        self.taken = None     # compilation unit -> functions whose address it takes
        self.typing = set()   # functions whose argument types are being worked out
        self.argument_types = {}   # (function, argument number) -> type the callers pass
        self.preds = {}       # function -> predecessors of each instruction

    def _derive(self, top, name, seen):
        if name in seen:
            return
        seen.add(name)
        self.derived[name].add(top)
        for base, _ in self.dwarf.bases(name):
            self._derive(top, base, seen)

    def offset_of(self, derived, base, depth=0):
        """Where base sits inside derived, None if it does not."""
        if derived == base:
            return 0
        if depth > 8:
            return None
        for parent, where in self.dwarf.bases(derived):
            inner = self.offset_of(parent, base, depth + 1)
            if inner is not None:
                return where + inner
        return None

    def function_at(self, value):
        # Function pointers carry the thumb bit; data that happens to equal a
        # function's address without it is not one.
        if value is None or not (value & 1):
            return None
        callee = self.image.starts.get(value & ~1)
        return None if callee in self.pure else callee

    def is_address(self, value):
        """A constant that points into memory the image lays out, rather than a number."""
        return value is not None and any(start <= value < end for start, end in self.elf.memory)

    # -- where a register's value came from ---------------------------------

    def back(self, func, index):
        """The instructions that can run before code[func][index], nearest
        first: walking the function's branches backwards, not its listing."""
        preds = self.predecessors(func)
        seen = set()
        queue = collections.deque(preds[index])
        while queue:
            i = queue.popleft()
            if i in seen:
                continue
            seen.add(i)
            yield i
            queue.extend(preds[i])

    def predecessors(self, func):
        """For each instruction of func, the ones that can run just before it."""
        if func in self.preds:
            return self.preds[func]
        code = self.image.code[func]
        where = {address: i for i, (address, _, _) in enumerate(code)}
        preds = [[] for _ in code]
        for i, (_, op, args) in enumerate(code):
            base = op.split('.', 1)[0]
            target = BRANCH_TARGET_RE.search(args)
            if (BRANCH_RE.match(base) and base != 'bx' or base in ('cbz', 'cbnz')) and target:
                j = where.get(int(target.group(1), 16))
                if j is not None:
                    preds[j].append(i)
            # Nothing falls through an unconditional branch, a return or a literal pool.
            ends = (base in ('b', 'bx') or base.startswith('.')
                    or (base == 'pop' and 'pc' in args) or 'pc' in destinations(op, args))
            if not ends and i + 1 < len(code):
                preds[i + 1].append(i)
        # A block nothing branches to is reached through a jump table (a
        # switch, on thumb1 through __gnu_thumb1_case_*): take it as following
        # the code above it, past the table.
        for i in range(1, len(code)):
            if not preds[i] and not code[i][1].startswith('.'):
                j = i - 1
                while j > 0 and code[j][1].startswith('.'):
                    j -= 1
                preds[i].append(j)
        self.preds[func] = preds
        return preds

    def trace(self, func, index, register, depth=0, skip=frozenset()):
        """Where register got its value before code[func][index], as a tuple:

          ('const', value)          from a literal pool
          ('num', value)            built from immediates
          ('arg', n)                argument n, as the function was entered
          ('returned', function)    what a direct call returned
          ('load', base, offset)    a word loaded from base + offset
          ('offset', base, offset)  base + offset
          ('element', table)        an entry of a static table
          ('frame', offset)         sp + offset
          ('sum', a, b)             a + b, one of them a pointer
          ('at', register, address, value)   value, as held in register there
          ('unknown', why)
        """
        code = self.image.code[func]
        if depth > 6:
            return ('unknown', 'too deep')
        for i in self.back(func, index):
            address, op, args = code[i]
            if i in skip:
                continue   # a loop's own increment: what it started from is wanted
            if is_call(op):
                if register in CLOBBERED:
                    target = TARGET_RE.search(args)
                    if register == 'r0' and target and not target.group(2):
                        return ('returned', canon(target.group(1)))
                    return ('unknown', 'returned by a call')
                continue
            if register not in destinations(op, args):
                continue
            base = op.split('.', 1)[0]
            text = operands(args)
            parts = [p.strip() for p in text.split(',')]
            if base.startswith(('stm', 'ldm')) and parts[0].rstrip('!') == register:
                count = len(text[text.find('{') + 1:text.find('}')].split(','))
                return ('offset', self.trace(func, i, register, depth + 1), 4 * count)
            if base == 'ldr':
                return self._load(func, i, text, args, depth)
            if base in ('mov', 'movs') and len(parts) == 2 and parts[1] == 'sp':
                return ('frame', 0)
            if base in ('mov', 'movs') and len(parts) == 2 and REGISTER_RE.match(parts[1]):
                return ('at', parts[1], address, self.trace(func, i, parts[1], depth + 1))
            if base in ('mov', 'movs', 'movw') and len(parts) == 2 and parts[1].startswith('#'):
                return ('num', int(parts[1][1:], 0))
            if base in ('lsls', 'lsl') and parts[-1].startswith('#'):
                inner = bare(self.trace(func, i, parts[1] if len(parts) == 3 else parts[0], depth + 1))
                if inner[0] == 'num':
                    return ('num', inner[1] << int(parts[-1][1:], 0))
                return ('unknown', op)
            if base in ('adds', 'subs') and len(parts) == 3 and parts[2] == '#0':
                return self.trace(func, i, parts[1], depth + 1)   # a move that sets flags
            if base in ('add', 'adds', 'sub', 'subs') and parts[-1].startswith('#'):
                source = parts[1] if len(parts) == 3 else parts[0]
                amount = int(parts[-1][1:], 0) * (-1 if base.startswith('sub') else 1)
                if source == 'sp':
                    return ('frame', amount)
                if source == 'pc':
                    return ('unknown', 'an address in the code')
                inner = self.trace(func, i, source, depth + 1, skip | {i} if source == parts[0] else skip)
                shape = bare(inner)
                if shape[0] in ('num', 'frame'):
                    return (shape[0], shape[1] + amount)
                return ('offset', inner, amount)
            if base in ('add', 'adds', 'sub', 'subs') and all(REGISTER_RE.match(p) for p in parts):
                # Pointer plus or minus a count: still points into the same
                # object, whichever of the two the pointer was.
                sources = parts[1:] if len(parts) == 3 else parts
                if 'sp' in sources or 'pc' in sources:
                    return ('unknown', 'an address on the stack')
                return ('sum',) + tuple(('at', r, address, self.trace(func, i, r, depth + 1)) for r in sources)
            return ('unknown', op)
        if register in ('r0', 'r1', 'r2', 'r3'):
            return ('arg', int(register[1]))
        return ('unknown', 'set before the function')

    def _load(self, func, i, text, args, depth):
        """The value an ldr at code[func][i] loads."""
        address = self.image.code[func][i][0]
        memory = MEMORY_RE.search(text)
        if not memory:
            return ('unknown', 'ldr')
        register, offset = memory.group(1), memory.group(2)
        if register == 'pc':
            pool = POOL_RE.search(args)
            return ('const', self.elf.word(int(pool.group(1), 16))) if pool else ('unknown', 'ldr')
        if register == 'sp':
            return self.stack(func, i, int(offset[1:], 0) if offset else 0, depth)
        inner = ('at', register, address, self.trace(func, i, register, depth + 1))
        if offset is None or offset.startswith('#'):
            offset = int(offset[1:], 0) if offset else 0
        else:
            index = bare(self.trace(func, i, offset, depth + 1))
            if index[0] == 'const' and self.is_address(index[1]):
                return ('element', index[1])         # table[i], the table in the other register
            if bare(inner)[0] == 'const' and self.is_address(bare(inner)[1]):
                return ('element', bare(inner)[1])
            if index[0] in ('num', 'const') and index[1] is not None:
                offset = index[1]                     # an offset too big for the instruction
            else:
                return ('indexed', inner)
        shape = bare(inner)
        if shape[0] == 'frame':
            return self.stack(func, i, shape[1] + offset, depth)
        if shape[0] == 'offset':
            # base + a, then a load at +b: the field at a + b, split in two
            # because the instruction cannot reach it.
            return ('load', shape[1], shape[2] + offset)
        return ('load', inner, offset)

    def stack(self, func, index, offset, depth):
        """What a load from sp + offset reads: what was spilled there, or an
        argument passed on the stack."""
        code = self.image.code[func]
        for i in self.back(func, index):
            _, op, args = code[i]
            if not op.startswith('str') or op.startswith(('strb', 'strh', 'strd')):
                continue
            text = operands(args)
            memory = MEMORY_RE.search(text)
            if not memory:
                continue
            slot = memory.group(2)
            if slot is not None and not slot.startswith('#'):
                continue
            slot = int(slot[1:], 0) if slot else 0
            if memory.group(1) == 'sp':
                where = slot
            else:
                base = bare(self.trace(func, i, memory.group(1), depth + 1))
                if base[0] != 'frame':
                    continue
                where = base[1] + slot
            if where == offset:
                return self.trace(func, i, text.split(',')[0].strip(), depth + 1)
        # Not spilled here: past the frame is where the caller put arguments
        # five and on. Only right while sp has not moved since the prologue.
        frame = self.image.frames.get(func, 0)
        if offset >= frame and (offset - frame) % 4 == 0:
            return ('arg', 4 + (offset - frame) // 4)
        return ('unknown', 'a local')

    # -- what type it is ------------------------------------------------------

    def located(self, func, value):
        """(type of the object value points into, offset into it)."""
        shape = bare(value)
        if shape[0] == 'offset':
            kind, offset = self.located(func, shape[1])
            return kind, offset + shape[2]
        if shape[0] == 'sum':
            kind, offset = None, 0
            for part in shape[1:]:
                number = bare(part)
                if number[0] == 'num' or (number[0] == 'const' and not self.is_address(number[1])):
                    offset += number[1] or 0
                elif kind is None:
                    kind = self.type_of(func, part)
            return kind, offset
        return self.type_of(func, value), 0

    def type_of(self, func, value):
        """The DWARF type of what value points at, or None."""
        kind = value[0]
        if kind == 'at':
            found = self.type_of(func, value[3])
            if self.dwarf.class_of(found) is None:
                # Untyped where it came from (a void * context, say): what
                # DWARF says is in that register at that point.
                number = REGISTER_NUMBERS.get(value[1])
                for held in self.dwarf.in_register(self.address.get(func), value[2], number):
                    target = self.dwarf.target(held)
                    if self.dwarf.class_of(target):
                        return target
            return found
        if kind == 'arg':
            params = self.dwarf.params.get(self.address.get(func), [])
            if value[1] < len(params) and not CLONE_RE.search(func):
                declared = self.dwarf.attr(params[value[1]], AT_TYPE)
                pointee = self.dwarf.target(declared)
                if pointee is None and declared is not None and self.dwarf.tag.get(self.dwarf.strip(declared)) == TAG_POINTER:
                    # A void * context of a class's own static function - a C
                    # callback - is most likely an object of that class.
                    return self.dwarf.owner(self.address.get(func))
                return pointee
            # A clone's arguments are not the ones DWARF lists: what the callers pass.
            return self.argument_type(func, value[1])
        if kind == 'const':
            return self.dwarf.variables.get(value[1]) if value[1] is not None else None
        if kind == 'element':
            array = self.dwarf.strip(self.dwarf.variables.get(value[1]))
            if array is not None and self.dwarf.tag.get(array) == TAG_ARRAY:
                return self.dwarf.target(self.dwarf.attrs[array].get(AT_TYPE))
            return None
        if kind == 'returned':
            die = self.dwarf.functions.get(self.address.get(value[1]))
            return self.dwarf.target(self.dwarf.attr(die, AT_TYPE)) if die is not None else None
        if kind == 'load':
            outer, offset = self.located(func, value[1])
            if outer is None:
                return None
            return self.dwarf.target(self.dwarf.place(outer, offset + value[2])[2])
        if kind in ('offset', 'sum'):
            outer, offset = self.located(func, value)
            return self.dwarf.embedded(outer, offset) if outer is not None else None
        return None

    # How many callers up argument_type() follows. Unbounded, a clone called
    # through layers of other clones takes minutes for nothing more precise.
    MAX_ARGUMENT_DEPTH = 3

    def argument_type(self, func, number):
        """The type the direct callers pass as argument number, if they agree."""
        if number > 3 or func in self.typing or len(self.typing) >= self.MAX_ARGUMENT_DEPTH:
            return None
        if (func, number) in self.argument_types:
            return self.argument_types[(func, number)]
        self.typing.add(func)
        found = {}
        try:
            for caller in self.image.callers.get(func, ()):
                for i, (address, op, args) in enumerate(self.image.code[caller]):
                    target = TARGET_RE.search(args)
                    if not (op.split('.', 1)[0] in ('bl', 'b') and target
                            and target.group(1) == func and not target.group(2)):
                        continue
                    register = f'r{number}'
                    kind = self.type_of(caller, ('at', register, address, self.trace(caller, i, register)))
                    found[self.dwarf.class_of(kind)] = kind
        finally:
            self.typing.discard(func)
        names = set(found) - {None}
        result = found[names.pop()] if len(names) == 1 else None
        self.argument_types[(func, number)] = result
        return result

    # -- what can be there ----------------------------------------------------

    def passed(self, func, number, depth=0):
        """Functions the direct callers of func pass as argument number."""
        found = set()
        if number > 3 or depth > 3:
            return found
        register = f'r{number}'
        for caller in self.image.callers.get(func, ()):
            for i, (_, op, args) in enumerate(self.image.code[caller]):
                target = TARGET_RE.search(args)
                if not (op.split('.', 1)[0] in ('bl', 'b') and target and target.group(1) == func
                        and not target.group(2)):
                    continue
                value = bare(self.trace(caller, i, register))
                if value[0] == 'const':
                    callee = self.function_at(value[1])
                    if callee:
                        found.add(callee)
                elif value[0] == 'arg':
                    found |= self.passed(caller, value[1], depth + 1)
        return found

    def stored(self, name, offset):
        """Functions the code stores at +offset of a class name object: the
        callbacks that are registered at run time, like lwIP's."""
        if self.stores is None:
            self._index_stores()
        return self.stores.get((name, offset), set())

    def _index_stores(self):
        """Every store of a function's address (or of an argument that the
        callers fill with one), by the class and offset it lands at."""
        self.stores = collections.defaultdict(set)
        for func, code in self.image.code.items():
            for i, (address, op, args) in enumerate(code):
                if op.split('.', 1)[0] != 'str':
                    continue
                text = operands(args)
                memory = MEMORY_RE.search(text)
                if not memory or memory.group(1) in ('sp', 'pc'):
                    continue
                where = memory.group(2)
                if where is not None and not where.startswith('#'):
                    continue
                value = bare(self.trace(func, i, text.split(',')[0].strip()))
                if value[0] == 'const':
                    callees = {self.function_at(value[1])} - {None}
                elif value[0] == 'arg':
                    callees = self.passed(func, value[1])
                else:
                    continue
                if not callees:
                    continue
                base = ('at', memory.group(1), address, self.trace(func, i, memory.group(1)))
                outer, extra = self.located(func, base)
                if outer is None:
                    continue
                container, inside, _ = self.dwarf.place(outer, extra + (int(where[1:], 0) if where else 0))
                if container:
                    self.stores[(container, inside)] |= callees

    def statics(self, name, offset):
        """Function pointers at +offset of every class name object held in a
        static variable, alone, in an array or inside another object."""
        found = set()
        for address, kind in self.dwarf.variables.items():
            for position in self.dwarf.positions(kind, name):
                callee = self.function_at(self.elf.word(address + position + offset))
                if callee:
                    found.add(callee)
        return found

    def virtual(self, name, slot):
        """What slot of name's vtable can reach: name and everything derived."""
        found = set()
        for derived in self.derived.get(name, ()):
            start, end = self.vtables[derived]
            where = self.offset_of(derived, name)
            if where is None:
                continue
            # The vtable for the name part of derived: offset-to-top says
            # how far that part sits from the start of the object.
            for header in range(start, end - 8, 4):
                top = self.elf.word(header)
                if top is None or ((-top) & 0xffffffff) != (where & 0xffffffff):
                    continue
                if self.elf.word(header + 4) != self.elf.word(start + 4):
                    continue
                if header + VTABLE_HEADER + slot + 4 <= end:
                    callee = self.function_at(self.elf.word(header + VTABLE_HEADER + slot))
                    if callee:
                        found.add(callee)
                break
        return found

    def taken_in(self, func):
        """Functions whose address the source file of func loads somewhere."""
        if self.taken is None:
            self.taken = collections.defaultdict(set)
            for other, code in self.image.code.items():
                unit = self.dwarf.units.get(self.address.get(other))
                if unit is None:
                    continue
                for _, op, args in code:
                    if op.split('.', 1)[0] == 'ldr' and '[pc' in args:
                        pool = POOL_RE.search(args)
                        callee = self.function_at(self.elf.word(int(pool.group(1), 16))) if pool else None
                        if callee:
                            self.taken[unit].add(callee)
        unit = self.dwarf.units.get(self.address.get(func))
        return self.taken.get(unit, set()) if unit is not None else set()

    def any_slot(self, offset):
        """Untyped: that slot of every vtable."""
        found = set()
        for start, end in self.vtables.values():
            if start + VTABLE_HEADER + offset + 4 <= end:
                callee = self.function_at(self.elf.word(start + VTABLE_HEADER + offset))
                if callee:
                    found.add(callee)
        return found

    def any_field(self, offset):
        """Untyped: functions at +offset of any static object."""
        found = set()
        for start, end in self.objects:
            if start + offset + 4 <= end:
                callee = self.function_at(self.elf.word(start + offset))
                if callee:
                    found.add(callee)
        return found

    def table(self, address):
        """Every function pointer in the object at address."""
        for start, end in self.objects:
            if start <= address < end:
                found = set()
                for word in range(start, end - 3, 4):
                    callee = self.function_at(self.elf.word(word))
                    if callee:
                        found.add(callee)
                return found
        return set()

    # -- putting it together --------------------------------------------------

    def match(self, func, index, register, pretty):
        """(how, callees) for one call site; callees empty if not matched."""
        value = bare(self.trace(func, index, register))
        kind = value[0]
        if kind == 'const':
            callee = self.function_at(value[1])
            return ('constant', {callee} if callee else set())
        if kind == 'element' and value[1] is not None:
            return ('table', self.table(value[1]))
        if kind == 'arg':
            return (f'argument {value[1] + 1} from the callers', self.passed(func, value[1]))
        if kind != 'load':
            return (f'not matched: {value[1] if kind == "unknown" else kind}', set())

        inner, offset = value[1], value[2]
        shape = bare(inner)
        if shape[0] == 'const' and self.is_address(shape[1]):
            if self.elf.word(shape[1] + offset) is None:
                return ('not matched: a table filled at run time', set())
            callee = self.function_at(self.elf.word(shape[1] + offset))
            return (f'table +{offset}', {callee} if callee else set())

        # Typed: a virtual call on a class, or a function pointer member.
        if shape[0] == 'load' and shape[2] == 0:
            outer, extra = self.located(func, shape[1])
            name = self.dwarf.class_of(self.dwarf.embedded(outer, extra)) if outer is not None else None
            if name in self.derived:
                return (f'virtual {name} +{offset}', self.virtual(name, offset))
        outer, extra = self.located(func, inner)
        if outer is not None:
            container, inside, member = self.dwarf.place(outer, extra + offset)
            if container and self.dwarf.is_function_pointer(member):
                return (f'member {container} +{inside}',
                        self.statics(container, inside) | self.stored(container, inside))

        # Untyped. C has no vtables; C++ calls through +0 are most likely one.
        is_cpp = pretty(func) != func or '::' in func or '(' in func
        if is_cpp and shape[0] == 'load' and shape[2] == 0:
            found = self.any_slot(offset)
            if found:
                return (f'untyped vtable slot +{offset}', found)
        found = self.taken_in(func)
        if found:
            return (f'untyped field +{offset}, taken in this file', found)
        return (f'untyped field +{offset}', self.any_field(offset))

    def apply(self, pretty):
        """Adds the edges it can match; returns (func, address, how, callees) per site."""
        results = []
        unresolved = set()
        for func, index, register in self.image.sites:
            how, callees = self.match(func, index, register, pretty)
            results.append((func, self.image.code[func][index][0], how, callees))
            if not callees:
                unresolved.add(func)
                continue
            if (func, index) in self.image.once_sites:
                self.image.first_use[func] |= callees - {func}
                continue
            for callee in callees:
                if callee == func:
                    continue
                if callee not in self.image.graph[func]:
                    self.image.guessed[(func, callee)] = f'{how}, 1 of {len(callees)}'
                self.image._edge(func, callee)
        self.image.indirect = unresolved
        return results


class Objects:
    """The call graph with the object each call is made on.

    A node is (function, object): the function, and the address of the static
    object its `this` (r0) points at, or None when that is not known. Objects
    start at the static instances - a singleton's get() returns one - and
    members are offsets from them, so CalendarCommands::m_client and
    TelegramManager::m_client are two different HTTPClient objects.

    What the code stores while running on a known object is remembered by
    address: constructors writing their vptrs, setHttpCallback(this),
    set_callback(this). An indirect call made on a known object then reads
    what was stored there, so a callback reaches its own owner and not every
    class implementing the interface. Without that, the Guesser's match for
    the call site is used.

    It only sees stores made by code it followed with the object known; one
    made through an unknown pointer is missed. --no-objects turns it off.
    """

    MAX_VALUES = 32

    def __init__(self, image, guesser, elf, results, enabled=True):
        self.image = image
        self.guesser = guesser
        self.elf = elf
        self.enabled = enabled and guesser is not None
        self.count_first_use = False
        self.fallback = {(func, address): (how, callees) for func, address, how, callees in results}
        self.memory = collections.defaultdict(set)   # address -> values stored there
        self.traces = {}
        self.returns_cache = {}
        self.object_cache = {}
        self.edges = collections.defaultdict(set)    # node -> nodes it calls
        self.first = collections.defaultdict(set)    # node -> nodes it constructs once
        self.labels = {}                             # (node, node) -> how a call was matched
        self.where = {}                              # (node, node) -> address of the call
        self.nodes = set()
        self.calls = {}                              # function -> its call sites
        self.stores = {}                             # function -> its word stores

        self.writable = elf.writable if elf else []

        # Static objects of class or struct type, for telling an object apart
        # from a buffer or a string.
        self.statics = []
        if self.enabled:
            sizes = {address: end - address for address, end in guesser.objects}
            for address, kind in guesser.dwarf.variables.items():
                size = sizes.get(address) or guesser.dwarf.size(kind)
                if size and kind is not None:
                    self.statics.append((address, address + size, kind))
            self.statics.sort()

    # -- what the code does -------------------------------------------------

    def call_sites(self, func):
        """[(index, kind, detail)]: 'call' / 'first' with the callee, or
        'indirect' with the register (and whether it runs once)."""
        if func in self.calls:
            return self.calls[func]
        sites = []
        indirect = {i: r for f, i, r in self.image.sites if f == func}
        guarding = False
        for i, (_, op, args) in enumerate(self.image.code[func]):
            target = TARGET_RE.search(args)
            if i in indirect:
                sites.append((i, 'once' if guarding else 'indirect', indirect[i]))
            elif is_call(op) and target:
                callee = canon(target.group(1))
                if callee == GUARD_ACQUIRE:
                    guarding = True
                elif callee in GUARD_END:
                    guarding = False
                else:
                    sites.append((i, 'first' if guarding else 'call', callee))
            elif op.startswith('b') and not op.startswith('bx') and target and not target.group(2):
                callee = canon(target.group(1))
                if callee != func:
                    sites.append((i, 'call', callee))   # a tail call
        self.calls[func] = sites
        return sites

    def word_stores(self, func):
        """[(index, value register, base register, offset)] of the word
        stores in func; the offset is a number or a register holding one."""
        if func in self.stores:
            return self.stores[func]
        found = []
        for i, (_, op, args) in enumerate(self.image.code[func]):
            base = op.split('.', 1)[0]
            text = operands(args)
            if base == 'str':
                memory = MEMORY_RE.search(text)
                if not memory or memory.group(1) in ('sp', 'pc'):
                    continue
                where = memory.group(2)
                if where is None or where.startswith('#'):
                    found.append((i, text.split(',')[0].strip(), memory.group(1), int(where[1:], 0) if where else 0))
                else:
                    found.append((i, text.split(',')[0].strip(), memory.group(1), where))
            elif base in ('stmia', 'stm', 'stmea'):
                register = text.split(',')[0].strip().rstrip('!')
                start, end = text.find('{'), text.find('}')
                if register in ('sp', 'pc') or start < 0:
                    continue
                for n, value in enumerate(r.strip() for r in text[start + 1:end].split(',')):
                    found.append((i, value, register, 4 * n))
        self.stores[func] = found
        return found

    def trace(self, func, index, register):
        key = (func, index, register)
        if key not in self.traces:
            self.traces[key] = self.guesser.trace(func, index, register)
        return self.traces[key]

    # -- what values are ----------------------------------------------------

    def value(self, func, value, this, depth=0):
        """The set of words value can be, running with this as the object,
        or None if that is not known."""
        kind = value[0]
        if depth > 12:
            return None
        if kind == 'at':
            return self.value(func, value[3], this, depth + 1)
        if kind in ('const', 'num'):
            return {value[1]} if value[1] is not None else None
        if kind == 'arg':
            return {this} if value[1] == 0 and this is not None else None
        if kind == 'offset':
            base = self.value(func, value[1], this, depth + 1)
            return {(b + value[2]) & 0xffffffff for b in base} if base is not None else None
        if kind == 'sum':
            parts = [self.value(func, p, this, depth + 1) for p in value[1:]]
            if any(p is None for p in parts) or len(parts) != 2:
                return None
            found = {(a + b) & 0xffffffff for a in parts[0] for b in parts[1]}
            return found if len(found) <= self.MAX_VALUES else None
        if kind == 'load':
            base = self.value(func, value[1], this, depth + 1)
            if base is None:
                return None
            found = set()
            for address in base:
                if address < 0x100:
                    continue   # through NULL (a member not set yet): never actually followed
                held = self.read((address + value[2]) & 0xffffffff)
                if held is None:
                    return None
                found |= held
            return found if len(found) <= self.MAX_VALUES else None
        if kind == 'returned':
            return self.returns(value[1])
        return None

    def read(self, address):
        """What a word in static memory can hold: its initial value and
        everything stored there. None outside static memory."""
        if not any(start <= address < end for start, end in self.elf.memory):
            return None
        initial = self.elf.word(address)
        if not self.is_writable(address):
            return {initial} if initial is not None else None
        return {initial if initial is not None else 0} | self.memory.get(address, set())

    def is_writable(self, address):
        return any(start <= address < end for start, end in self.writable)

    def returns(self, func):
        """What func can return in r0, from its return instructions."""
        if func in self.returns_cache:
            return self.returns_cache[func]
        self.returns_cache[func] = None   # while working it out
        code = self.image.code.get(func, ())
        found = set()
        for i, (_, op, args) in enumerate(code):
            if (op.startswith('pop') and 'pc' in args) or (op.startswith('bx') and 'lr' in args):
                held = self.value(func, self.trace(func, i, 'r0'), None)
                if held is None:
                    found = None
                    break
                found |= held
        self.returns_cache[func] = found if found else None
        return self.returns_cache[func]

    def is_object(self, address):
        """address is a class or struct object in static memory."""
        if address in self.object_cache:
            return self.object_cache[address]
        found = False
        if self.is_writable(address):
            for start, end, kind in self.statics:
                if start <= address < end:
                    found = self.guesser.dwarf.embedded(kind, address - start) is not None
                    break
                if start > address:
                    break
        self.object_cache[address] = found
        return found

    def this_for(self, func, index, this):
        """The object a call at code[func][index] is made on, if known."""
        if not self.enabled:
            return None
        held = self.value(func, self.trace(func, index, 'r0'), this)
        if held and len(held) == 1:
            address = next(iter(held))
            if self.is_object(address):
                return address
        return None

    # -- following it -------------------------------------------------------

    def build(self, roots):
        """Follows calls from roots until neither new nodes nor new stores turn up."""
        self.nodes = set(roots)
        changed = True
        while changed:
            changed = False
            for node in sorted(self.nodes, key=lambda n: (n[0], n[1] or 0)):
                if self.visit(node):
                    changed = True

    def visit(self, node):
        func, this = node
        changed = False
        if self.enabled:
            for i, value_register, base_register, offset in self.word_stores(func):
                bases = self.value(func, self.trace(func, i, base_register), this)
                if not bases:
                    continue
                if isinstance(offset, str):
                    offsets = self.value(func, self.trace(func, i, offset), this)
                    if not offsets or len(offsets) != 1:
                        continue
                    offset = next(iter(offsets))
                values = self.value(func, self.trace(func, i, value_register), this)
                if not values:
                    continue
                for base in bases:
                    address = (base + offset) & 0xffffffff
                    if not self.is_writable(address):
                        continue
                    before = len(self.memory[address])
                    self.memory[address] |= values
                    if len(self.memory[address]) > self.MAX_VALUES:
                        self.memory[address] = set(list(self.memory[address])[:self.MAX_VALUES])
                    changed |= len(self.memory[address]) != before

        for index, kind, detail in self.call_sites(func):
            callee_this = self.this_for(func, index, this)
            if kind in ('call', 'first'):
                targets, how = {detail}, None
            else:
                targets, how = self.indirect(func, index, detail, this)
            for callee in targets:
                if callee == func or callee not in self.image.frames:
                    continue
                target = (callee, callee_this)
                into = self.first if kind in ('first', 'once') and not self.count_first_use else self.edges
                if target not in into[node]:
                    into[node].add(target)
                    changed = True
                if how:
                    self.labels[(node, target)] = f'{how}, 1 of {len(targets)}'
                self.where.setdefault((node, target), self.image.code[func][index][0])
                if target not in self.nodes:
                    self.nodes.add(target)
                    changed = True
        return changed

    def indirect(self, func, index, register, this):
        """(callees, how) for an indirect call made running on this."""
        how, fallback = self.fallback.get((func, self.image.code[func][index][0]), (None, set()))
        if self.enabled:
            held = self.value(func, self.trace(func, index, register), this)
            if held:
                callees = {self.guesser.function_at(v) for v in held} - {None}
                if callees and callees != set(fallback):
                    return callees, 'on this object'
        return set(fallback), how


class Lines:
    """Address -> (file, line), from .debug_line (DWARF 2 to 4)."""

    def __init__(self, elf):
        self.rows = []   # (address, file, line, ends a sequence), sorted
        self.addresses = []
        if elf is None or '.debug_line' not in elf.sections:
            return
        data = elf.data
        offset, size = elf.sections['.debug_line']
        end = offset + size
        while offset + 4 <= end:
            length, version = struct.unpack_from('<IH', data, offset)
            unit_end = offset + 4 + length
            if version not in (2, 3, 4):
                offset = unit_end
                continue
            self._unit(data, offset, unit_end, version)
            offset = unit_end
        self.rows.sort(key=lambda row: row[0])
        self.addresses = [row[0] for row in self.rows]

    def _unit(self, data, offset, unit_end, version):
        header_length, = struct.unpack_from('<I', data, offset + 6)
        program = offset + 10 + header_length
        at = offset + 10
        minimum = data[at]
        at += 2 if version >= 4 else 1                 # max_ops_per_instruction from 4 on
        at += 1                                        # default_is_stmt
        line_base = struct.unpack_from('<b', data, at)[0]
        line_range, opcode_base = data[at + 1], data[at + 2]
        lengths = data[at + 3:at + 3 + opcode_base - 1]
        at += 3 + opcode_base - 1
        directories = ['']
        while data[at]:
            stop = data.index(b'\0', at)
            directories.append(data[at:stop].decode(errors='replace'))
            at = stop + 1
        at += 1
        files = ['?']
        while data[at]:
            stop = data.index(b'\0', at)
            name = data[at:stop].decode(errors='replace')
            directory, at = uleb(data, stop + 1)
            _, at = uleb(data, at)
            _, at = uleb(data, at)
            files.append(os.path.join(directories[directory] if directory < len(directories) else '', name))

        address, file, line = 0, 1, 1
        at = program
        while at < unit_end:
            op = data[at]
            at += 1
            if op >= opcode_base:
                adjusted = op - opcode_base
                address += (adjusted // line_range) * minimum
                line += line_base + adjusted % line_range
                self.rows.append((address, files[file] if file < len(files) else '?', line, False))
            elif op == 0:
                length, at = uleb(data, at)
                sub, body = data[at], at + 1
                if sub == 1:                               # end_sequence
                    self.rows.append((address, '?', 0, True))
                    address, file, line = 0, 1, 1
                elif sub == 2:                             # set_address
                    address = struct.unpack_from('<I', data, body)[0]
                elif sub == 3:                             # define_file
                    stop = data.index(b'\0', body)
                    files.append(data[body:stop].decode(errors='replace'))
                at += length
            elif op == 1:                                  # copy
                self.rows.append((address, files[file] if file < len(files) else '?', line, False))
            elif op == 2:                                  # advance_pc
                step, at = uleb(data, at)
                address += step * minimum
            elif op == 3:                                  # advance_line
                step, at = sleb(data, at)
                line += step
            elif op == 4:                                  # set_file
                file, at = uleb(data, at)
            elif op == 8:                                  # const_add_pc
                address += ((255 - opcode_base) // line_range) * minimum
            elif op == 9:                                  # fixed_advance_pc
                address += struct.unpack_from('<H', data, at)[0]
                at += 2
            else:                                          # the rest only carry ulebs
                for _ in range(lengths[op - 1] if op - 1 < len(lengths) else 0):
                    _, at = uleb(data, at)

    def at(self, address):
        """'file:line' for address, '' if the line table has nothing."""
        i = bisect.bisect_right(self.addresses, address) - 1
        if i < 0 or self.rows[i][3]:
            return ''
        _, file, line = self.rows[i][:3]
        return f'{os.path.basename(file)}:{line}'


class Depth:
    """Longest path through the call graph. Nodes are (function, object)
    pairs, see Objects.

    Outside cycles it is exact. Functions that call each other round (a
    strongly connected component of the graph) are where recursion would be:
    there a path may visit each node once, and the longest such path is
    searched for, most promising calls first. That search is exponential, so
    it stops after BUDGET steps per starting point and keeps the longest path
    found by then; those starting points are listed as approximate.
    """

    BUDGET = 4000

    def __init__(self, edges, frames, exclude=()):
        self.edges = edges
        self.frames = frames
        self.exclude = set(exclude)
        self.memo = {}
        self.exits = {}
        self.estimates = {}      # node -> a quick, cut-at-the-cycle depth, to order the search
        self.component = {}      # node -> its cycle (a frozenset), for nodes in one
        self.approximate = set()
        self._components()

    def callees(self, node):
        return sorted((n for n in self.edges.get(node, ()) if n[0] not in self.exclude),
                      key=lambda n: (n[0], n[1] or 0))

    def _components(self):
        """Tarjan's strongly connected components, without recursion."""
        nodes = set(self.edges)
        for targets in self.edges.values():
            nodes |= targets
        index, low, on_stack, stack, counter = {}, {}, set(), [], 0
        for root in sorted(nodes, key=lambda n: (n[0], n[1] or 0)):
            if root in index:
                continue
            work = [(root, iter(self.callees(root)))]
            index[root] = low[root] = counter
            counter += 1
            stack.append(root)
            on_stack.add(root)
            while work:
                node, children = work[-1]
                advanced = False
                for child in children:
                    if child not in index:
                        index[child] = low[child] = counter
                        counter += 1
                        stack.append(child)
                        on_stack.add(child)
                        work.append((child, iter(self.callees(child))))
                        advanced = True
                        break
                    if child in on_stack:
                        low[node] = min(low[node], index[child])
                if advanced:
                    continue
                work.pop()
                if work:
                    low[work[-1][0]] = min(low[work[-1][0]], low[node])
                if low[node] == index[node]:
                    members = []
                    while True:
                        member = stack.pop()
                        on_stack.discard(member)
                        members.append(member)
                        if member == node:
                            break
                    if len(members) > 1:
                        cycle = frozenset(members)
                        for member in members:
                            self.component[member] = cycle

    def of(self, node):
        """(bytes, [(node, frame)]) of the deepest path starting at node."""
        if node in self.memo:
            return self.memo[node]
        if node in self.component:
            result = self._search(node, self.component[node])
        else:
            deepest, tail = 0, []
            for callee in self.callees(node):
                total, path = self.of(callee)
                if total > deepest:
                    deepest, tail = total, path
            frame = self.frames.get(node[0], 0)
            result = (frame + deepest, [(node, frame)] + tail)
        self.memo[node] = result
        return result

    def _exit(self, node, cycle):
        """The deepest way out of the cycle from node, not counting node."""
        if node not in self.exits:
            best = (0, [])
            for callee in self.callees(node):
                if callee not in cycle:
                    found = self.of(callee)
                    if found[0] > best[0]:
                        best = found
            self.exits[node] = best
        return self.exits[node]

    def _estimate(self, node, cycle, active):
        """How deep node looks, cutting the cycle wherever it closes - the
        old, order-dependent measure, only used to try likely calls first."""
        if node in self.estimates:
            return self.estimates[node]
        active.add(node)
        deepest = self._exit(node, cycle)[0]
        for callee in self.callees(node):
            if callee in cycle and callee not in active:
                deepest = max(deepest, self._estimate(callee, cycle, active))
        active.discard(node)
        self.estimates[node] = self.frames.get(node[0], 0) + deepest
        return self.estimates[node]

    def _search(self, start, cycle):
        best = [0, []]
        steps = [0]

        def promise(node):
            return self._estimate(node, cycle, set())

        def walk(node, visited, path, cost):
            steps[0] += 1
            frame = self.frames.get(node[0], 0)
            cost += frame
            path = path + [(node, frame)]
            out = self._exit(node, cycle)
            if cost + out[0] > best[0]:
                best[0], best[1] = cost + out[0], path + out[1]
            if steps[0] > self.BUDGET:
                self.approximate.add(start)
                return
            inside = [c for c in self.callees(node) if c in cycle and c not in visited]
            for callee in sorted(inside, key=lambda c: -promise(c)):
                walk(callee, visited | {callee}, path, cost)
                if steps[0] > self.BUDGET:
                    return

        walk(start, {start}, [], 0)
        return best[0], best[1]


def dynamic_frames(su_dir):
    """alloca/VLA frames, which -fstack-usage reports but cannot size."""
    found = set()
    for root, _, files in os.walk(su_dir):
        for name in files:
            if not name.endswith('.su'):
                continue
            with open(os.path.join(root, name), errors='replace') as handle:
                for line in handle:
                    fields = line.rstrip('\n').split('\t')
                    if len(fields) >= 3 and 'dynamic' in fields[2]:
                        found.add((int(fields[1]), fields[0]))
    return sorted(found, reverse=True)


def run_objdump(objdump, elf, *flags):
    try:
        return subprocess.run([objdump, *flags, elf], check=True,
                              capture_output=True, text=True).stdout
    except FileNotFoundError:
        sys.exit(f'{objdump} not found; pass --objdump, set $OBJDUMP, or hand '
                 f'this script a pre-generated disassembly (see --help)')
    except subprocess.CalledProcessError as error:
        sys.exit(f'{objdump} failed: {error.stderr.strip()}')


def stack_limit_from_elf(elf):
    """PICO_STACK_SIZE as linked, i.e. __StackTop - __StackBottom."""
    top, bottom = elf.symbol('__StackTop'), elf.symbol('__StackBottom')
    if top is not None and bottom is not None:
        return top - bottom
    return None


def print_stack(total, path, limit, image, objects, lines, pretty):
    verdict = 'OVER' if total > limit else 'under'
    print(f'\n{pretty(path[0][0][0])}: {total} bytes ({verdict} {limit})')

    # Where each step is: the line of its call into the next one, and for the
    # last, where the function starts.
    places = []
    for i, (node, _) in enumerate(path):
        if i + 1 < len(path):
            address = objects.where.get((node, path[i + 1][0]))
        else:
            address = image.starts_of.get(node[0])
        places.append(lines.at(address) if address is not None else '')
    width = max([len(p) for p in places] + [len('file:line')])

    print(f'  {"frame":>7} {"cum":>7}  {"file:line":<{width}}')
    running, crossed, previous = 0, False, None
    for (node, frame), place in zip(path, places):
        running += frame
        guess = objects.labels.get((previous, node))
        previous = node
        if frame == 0 and not guess:
            continue
        note = ''
        if not crossed and running > limit:
            note, crossed = '   <-- over the limit here', True
        mark = '*' if node[0] in image.indirect else ' '
        mark += '~' if guess else ' '
        how = f'   ({guess})' if guess else ''
        print(f'  {frame:7d} {running:7d}  {place:<{width}} {mark} {pretty(node[0])}{how}{note}')


def find_functions(wanted, image, pretty):
    """Functions named wanted: as in the dump, demangled, or without arguments."""
    if wanted in image.frames:
        return [wanted]
    exact = [f for f in image.frames if pretty(f) == wanted]
    if exact:
        return exact
    return sorted(f for f in image.frames if pretty(f).split('(', 1)[0] == wanted)


def main():
    # The searches recurse once per step of a call chain.
    sys.setrecursionlimit(max(sys.getrecursionlimit(), 20000))

    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('image', help='firmware .elf, or a saved objdump -d dump (without -C)')
    parser.add_argument('--elf', help='the ELF to read data from for a .dis (default: '
                                      'the .elf beside it)')
    parser.add_argument('--objdump', default=os.environ.get('OBJDUMP', 'arm-none-eabi-objdump'),
                        help='cross objdump to use for an .elf (default: %(default)s)')
    parser.add_argument('--su-dir', help='directory holding the -fstack-usage .su files '
                                         '(default: CMakeFiles/<image>.dir beside the ELF)')
    parser.add_argument('--limit', type=int,
                        help='stack budget in bytes (default: __StackTop - __StackBottom '
                             'from the ELF, else 4096)')
    parser.add_argument('--top', type=int, default=3, metavar='N',
                        help='how many of the deepest call stacks to print (default: %(default)s)')
    parser.add_argument('--roots', nargs='+', metavar='FUNC',
                        help='print these functions instead of the deepest ones; '
                             'a name without its arguments matches every overload')
    parser.add_argument('--exclude', nargs='+', default=[], metavar='FUNC',
                        help='ignore calls to these functions (e.g. panic, __wrap_printf) '
                             'to see the depth without a known-unreachable path')
    parser.add_argument('--frames', type=int, default=10, metavar='N',
                        help='how many of the largest single frames to list (default: %(default)s)')
    parser.add_argument('--no-guess', action='store_true',
                        help='do not match indirect calls, only follow direct ones')
    parser.add_argument('--no-objects', action='store_true',
                        help='do not follow which object each call is made on (see Objects)')
    parser.add_argument('--count-first-use', action='store_true',
                        help='count the construction of function-local statics under every caller, '
                             'not only once (see "constructed on first use")')
    parser.add_argument('--explain', nargs='+', default=[], metavar='FUNC',
                        help='list what each indirect call in these functions was matched to')
    parser.add_argument('--report', metavar='FILE',
                        help='write the report to FILE and print only its outcome, '
                             'with where FILE is (the build does this)')
    args = parser.parse_args()

    if not args.report:
        return report(args)[0]

    with open(args.report, 'w') as handle, contextlib.redirect_stdout(handle):
        status, outcome = report(args)

    # The largest single frames are shown here too; the report has the rest.
    with open(args.report) as handle:
        written = handle.read().split('\n')
    largest = []
    if '=== largest single frames ===' in written:
        start = written.index('=== largest single frames ===')
        while start < len(written) and written[start].strip():
            largest.append(written[start])
            start += 1

    headline, verdict = outcome
    print(headline)
    if largest:
        print()
        print('\n'.join(largest))
        print()
    print(verdict)
    shown = os.path.relpath(args.report)
    if not shown.startswith(('.', os.sep)):
        shown = os.path.join('.', shown)
    print(f'The {args.top} deepest call stacks, with where each call is made from: {shown}')
    return status


def report(args):
    """Prints the report; returns (exit status, the lines that sum it up)."""

    stem, extension = os.path.splitext(args.image)
    elf_path = args.elf or (args.image if extension == '.elf' else stem + '.elf')
    elf = None
    if os.path.isfile(elf_path):
        try:
            elf = Elf(elf_path)
        except ValueError as error:
            print(f'warning: {error}', file=sys.stderr)

    if extension == '.elf':
        disassembly = run_objdump(args.objdump, args.image, '-d')
        default_su = os.path.join(os.path.dirname(args.image) or '.',
                                  'CMakeFiles', os.path.basename(stem) + '.dir')
    else:
        with open(args.image, errors='replace') as handle:
            disassembly = handle.read()
        default_su = None

    limit = args.limit or (stack_limit_from_elf(elf) if elf else None) or 4096

    image = Image(disassembly)
    if not image.frames:
        sys.exit(f'no functions found in {args.image} - is it a disassembly?')

    names = demangle(image.frames)
    def pretty(func):
        return names.get(func, func)

    guesser, sites = None, []
    if elf and not args.no_guess:
        guesser = Guesser(image, elf, pretty)
        sites = guesser.apply(pretty)

    exclude = [f for wanted in args.exclude for f in find_functions(wanted, image, pretty)]

    # The entry points: what nothing calls - the reset handler, interrupts,
    # and whatever is only reached through a call that was not matched.
    constructed = {c for callees in image.first_use.values() for c in callees}
    entries = sorted(f for f in image.frames if not image.callers.get(f) and f not in constructed)
    asked = [f for wanted in args.roots or () for f in find_functions(wanted, image, pretty)]
    biggest = [f for f, _ in sorted(image.frames.items(), key=lambda kv: -kv[1])[:args.frames]]

    lines = Lines(elf)
    objects = Objects(image, guesser, elf, sites, enabled=not args.no_objects)
    objects.count_first_use = args.count_first_use
    objects.build([(f, None) for f in entries + asked + biggest])

    depth = Depth(objects.edges, image.frames, exclude)
    def worst_of(func):
        found = [depth.of(n)[0] for n in objects.nodes if n[0] == func]
        return max(found) if found else 0

    ranked = sorted(((depth.of((f, None))[0], f) for f in entries), reverse=True)
    worst = ranked[0][0] if ranked else 0

    headline = (f'{args.image}: {len(image.frames)} functions, stack limit {limit} bytes, '
                f'worst case {worst} bytes')
    print(headline)
    if args.exclude:
        print(f'ignoring calls to: {", ".join(args.exclude)}')

    for wanted in args.explain:
        for func in find_functions(wanted, image, pretty) or [wanted]:
            print(f'\n=== indirect calls in {pretty(func)} ===')
            found = [s for s in sites if s[0] == func]
            if not found:
                print('  none' if func in image.frames else '  not in the image')
            for _, address, how, callees in found:
                print(f'  {address:08x}  {how}: {len(callees)} function(s)')
                for callee in sorted(callees, key=pretty):
                    print(f'      {pretty(callee)}')

    print('\n=== deepest call stacks ===')
    if args.roots:
        for wanted in args.roots:
            matches = find_functions(wanted, image, pretty)
            if not matches:
                print(f'\n{wanted}: not in the image')
            for root in matches:
                total, path = depth.of((root, None))
                print_stack(total, path, limit, image, objects, lines, pretty)
    else:
        # Walk down the ranking, skipping anything already shown as a step of a
        # printed stack, so the N stacks are N distinct paths.
        shown, printed = set(), 0
        for total, func in ranked:
            if printed >= args.top:
                break
            if func in shown:
                continue
            path = depth.of((func, None))[1]
            print_stack(total, path, limit, image, objects, lines, pretty)
            shown.update(node[0] for node, _ in path)
            printed += 1

    if objects.first:
        print('\n=== constructed on first use (not counted under their callers) ===')
        print('  Function-local statics: the first call constructs them, later ones do not.')
        print('  Only right if that first call is on a shallow stack - at start-up, say;')
        print('  --count-first-use counts them under every caller instead.')
        rows = {}
        for node, constructs in objects.first.items():
            deepest = max(constructs, key=lambda n: depth.of(n)[0])
            total = image.frames.get(node[0], 0) + depth.of(deepest)[0]
            if total > rows.get(node[0], (0,))[0]:
                rows[node[0]] = (total, deepest[0])
        for func, (total, deepest) in sorted(rows.items(), key=lambda kv: -kv[1][0]):
            print(f'  {total:6d}  {pretty(func)} -> {pretty(deepest)}')

    print('\n=== largest single frames ===')
    biggest = sorted(image.frames.items(), key=lambda kv: -kv[1])[:args.frames]
    places = {func: lines.at(image.starts_of[func]) if func in image.starts_of else '' for func, _ in biggest}
    width = max([len(p) for p in places.values()] + [0])
    for func, frame in biggest:
        print(f'  frame {frame:6d}   worst case {worst_of(func):6d}   {places[func]:<{width}}   {pretty(func)}')

    su_dir = args.su_dir or default_su
    if su_dir and os.path.isdir(su_dir):
        dynamic = dynamic_frames(su_dir)
        if dynamic:
            print('\n=== alloca/VLA frames (size not included above) ===')
            for size, location in dynamic[:10]:
                print(f'  {size:6d}  {location}')

    cycles = set(depth.component.values())
    if cycles:
        print('\n=== cycles: a path visits each function in one at most once ===')
        for cycle in sorted(cycles, key=len, reverse=True):
            functions = sorted({pretty(node[0]) for node in cycle})
            shown = ', '.join(functions[:4]) + (f', ... ({len(functions)} functions)' if len(functions) > 4 else '')
            print(f'  {shown}')
        if depth.approximate:
            print(f'  the search inside the cycles stopped early from {len(depth.approximate)} starting '
                  f'point(s), so those depths are the longest found, not proven longest')

    print('\n=== indirect calls ===')
    if sites:
        def kind(how):
            if 'taken in this file' in how:
                return 'untyped from the same file'
            return ' '.join(how.split()[:2 if how.startswith('untyped') else 1])
        kinds = collections.Counter(kind(how) for _, _, how, callees in sites if callees)
        matched = sum(kinds.values())
        print(f'  {len(sites)} call sites, {matched} matched ('
              + ', '.join(f'{count} {kind}' for kind, count in kinds.most_common()) + ')')
        print(f'  {len(sites) - matched} not matched, in {len(image.indirect)} functions '
              f'(marked "*"); guessed edges are marked "~"')
    elif args.no_guess or not elf:
        reason = '--no-guess' if args.no_guess else f'no ELF at {elf_path} to read data from'
        print(f'  not followed ({reason}); {len(image.indirect)} functions make one (marked "*")')

    # The three deepest distinct paths, picked as the stacks above are: each
    # shares no function with the ones before it.
    deepest, seen = [], set()
    for total, func in ranked:
        if func in seen:
            continue
        deepest.append((total, func))
        seen.update(node[0] for node, _ in depth.of((func, None))[1])
        if len(deepest) == 3:
            break
    used = ''.join(f'\n  - {name}: {pretty(func)} uses {total} of {limit} bytes'
                   for name, (total, func) in zip(('deepest', 'second deepest', 'third deepest'), deepest))

    if worst > limit:
        verdict = f'FAIL: worst case {worst} bytes exceeds the {limit} byte stack{used}'
    else:
        verdict = f'OK: {limit - worst} bytes of headroom on the deepest known path{used}'
    print(f'\n{verdict}')
    return (1 if worst > limit else 0), [headline, verdict]


if __name__ == '__main__':
    sys.exit(main())
