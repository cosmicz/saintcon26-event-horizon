#!/usr/bin/env python3
"""Source-level check that the menu and demo never address the EEPROM bus
(GPIO4/5) or I2C. It complements image_audit.py, whose literal scan cannot
see pins configured through the SDK's computed register addresses.

    python3 -I device/tools/pin_audit.py          (from the repository root)

Sources are device/menu, device/demo and src/core: .c and .h files, plus the
C-SDK sections (% c-sdk { ... %}) of .pio files. Rules:
  1. no mention of i2c (any case), EEPROM pin names, PICO_DEFAULT_I2C, or the
     raw GPIO bank handles and bases (io_bank0_hw, pads_bank0_hw, sio_hw,
     IO_BANK0_BASE, PADS_BANK0_BASE, SIO_BASE);
  2. no GPIO mask or whole-bank call (gpio_*mask*, gpio_*_all*,
     pio_sm_set_pins*, pio_sm_set_pindirs*) other than the read-only
     gpio_get_all, and no macro whose body names a pin-taking call;
  3. every pin argument of a pin-taking call (gpio_*, pio_gpio_init,
     pwm_gpio_to_*, adc_gpio_init, the sm_config_set_*pin* and
     pio_sm_set_*pin(s) setters, the pindirs call, the PIO
     claim-for-range call, and *_program_init) evaluates to a set of pins
     that excludes 4 and 5. Only this grammar evaluates; anything else
     fails (fail closed):
       EH_BADGE_*_PIN  its value in eh_badge_pins.h;
       T[i], T[i].f    every entry of a static const table T defined in the
                       same file, each entry (field f) an EH_BADGE_*_PIN;
       P               a pin parameter (name ending in "pin") of a C-SDK
                       *_program_init, bound to the union of its call sites'
                       arguments; a parameter no call site binds fails;
       E + v           E shifted by 0..N-1, where v is the variable of an
                       enclosing for (T v = 0; v < N; v++) with literal N
                       whose body never assigns v.
     A call's pin count must be a literal and widens the base pin to
     base..base+count-1;
  4. every EH_BADGE_*_PIN used anywhere is defined and is neither 4 nor 5;
  5. the board header defines no PICO_DEFAULT_I2C*.
Comments are ignored. Exit status 0 iff all rules hold. This covers the
call forms above; code reaching the pins by another route (inline assembly,
pointer casts to computed addresses) is for image_audit.py and review.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SOURCE_DIRS = ("device/menu", "device/demo", "src/core")
PINS_H = "device/board/eh_badge_pins.h"
BOARD_H = "device/board/saintcon26_badge.h"
EEPROM_PINS = {4, 5}
FORBIDDEN_WORDS = (("i2c", re.I), ("EH_BADGE_EEPROM", 0), ("PICO_DEFAULT_I2C", 0),
                   (r"\b(io_bank0|pads_bank0|iobank0|sio)_hw\b", 0), (r"\b(IO_BANK0|PADS_BANK0|SIO)_BASE\b", 0))
READ_ONLY = {"gpio_get_all", "gpio_get_all64"}
BANK_CALL = re.compile(r"^(gpio_\w*(mask|_all)\w*|pio_sm_set_pins\w*|pio_sm_set_pindirs\w*)$")
# name -> (index of the pin argument, index of a pin-count argument or None)
PIN_CALLS = {
    "pio_gpio_init": (1, None),
    "pwm_gpio_to_slice_num": (0, None),
    "pwm_gpio_to_channel": (0, None),
    "adc_gpio_init": (0, None),
    "pio_sm_set_out_pins": (2, 3),
    "pio_sm_set_set_pins": (2, 3),
    "pio_sm_set_in_pins": (2, None),
    "pio_sm_set_sideset_pins": (2, None),
    "pio_sm_set_jmp_pin": (2, None),
    "pio_sm_set_consecutive_pindirs": (2, 3),
    "sm_config_set_out_pins": (1, 2),
    "sm_config_set_set_pins": (1, 2),
    "sm_config_set_in_pins": (1, None),
    "sm_config_set_sideset_pins": (1, None),
    "sm_config_set_jmp_pin": (1, None),
    "sm_config_set_out_pin_base": (1, None),
    "sm_config_set_set_pin_base": (1, None),
    "sm_config_set_in_pin_base": (1, None),
    "sm_config_set_sideset_pin_base": (1, None),
    "pio_claim_free_sm_and_add_program_for_gpio_range": (4, 5),
}
PIN_CALL_NAME = re.compile(r"gpio_\w+|pio_gpio_init|pwm_gpio_\w+|adc_gpio_init|pio_sm_set_consecutive_pindirs|"
                           r"pio_sm_set_\w*pins?|pio_sm_set_pins\w*|pio_sm_set_pindirs\w*|sm_config_set_\w*pin\w*|"
                           r"pio_claim_free_sm_and_add_program_for_gpio_range|\w+_program_init")
LOOP = re.compile(r"\bfor\s*\(\s*(?:[A-Za-z_][\w ]*\s+)?([A-Za-z_]\w*)\s*=\s*0\s*;\s*\1\s*<\s*([^;]+?)\s*;"
                  r"\s*\1\s*\+\+\s*\)")
C_SDK = re.compile(r"^%\s*c-sdk\s*\{(.*?)^%\}", re.S | re.M)
DEFINITION = re.compile(r"\bstatic\s+inline\s+void\s+(\w+_program_init)\s*\(([^)]*)\)\s*\{")


class Unapproved(Exception):
    pass


def pin_values(text):
    return {m.group(1): int(m.group(2)) for m in re.finditer(r"#define\s+(EH_BADGE_\w+_PIN)\s+(\d+)", text)}


def strip_comments(text):
    """C source without comments (string literals are not special-cased;
    our device sources hold none that contain comment markers)."""
    return re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.S)


def c_text(path, text):
    """The C a file contributes: all of it, or a .pio file's C-SDK sections."""
    if path.endswith(".pio"):
        text = "\n".join(C_SDK.findall(text))
    return strip_comments(text)


def matching(text, i):
    """Index just past the bracket closing the one at text[i]."""
    pairs = {"(": ")", "{": "}", "[": "]"}
    stack = []
    for j in range(i, len(text)):
        if text[j] in pairs:
            stack.append(pairs[text[j]])
        elif stack and text[j] == stack[-1]:
            stack.pop()
            if not stack:
                return j + 1
    raise Unapproved(f"unbalanced {text[i]} at offset {i}")


def split_args(s):
    depth, cur, out = 0, "", []
    for ch in s:
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
            continue
        depth += ch in "([{"
        depth -= ch in ")]}"
        cur += ch
    out.append(cur.strip())
    return out


def calls(text):
    """(name, args, offset) of each call of a pin-relevant name; definitions
    (a parameter list followed by a body) are skipped."""
    for m in re.finditer(r"\b(\w+)\s*\(", text):
        if not PIN_CALL_NAME.fullmatch(m.group(1)):
            continue
        end = matching(text, m.end() - 1)
        if re.match(r"\s*\{", text[end:]):
            continue
        yield m.group(1), split_args(text[m.end():end - 1]), m.start()


def loops(text):
    """(start, end, variable, bound text, body) of each for loop."""
    out = []
    for m in LOOP.finditer(text):
        body = m.end() + len(text[m.end():]) - len(text[m.end():].lstrip())
        end = matching(text, body) if text[body:body + 1] == "{" else text.index(";", body) + 1
        out.append((m.start(), end, m.group(1), m.group(2), text[body:end]))
    return out


def table_entries(text, name, field):
    """The entry expressions of static const table `name` (its `field`)."""
    if field:
        m = re.search(r"\bstatic\s+const\s+struct\s*\{([^{}]*)\}\s*" + re.escape(name) + r"\s*\[\s*\]\s*=\s*\{", text)
        if not m:
            raise Unapproved(f"no static const struct table {name}[] in this file")
        fields = [re.search(r"(\w+)\s*(\[[^]]*\])?\s*$", d).group(1) for d in m.group(1).split(";") if d.strip()]
        if field not in fields:
            raise Unapproved(f"table {name}[] has no field {field}")
        rows = split_args(text[m.end():matching(text, m.end() - 1) - 1])
        entries = []
        for row in filter(None, rows):
            if not row.startswith("{"):
                raise Unapproved(f"table {name}[] row {row!r} is not a braced initializer")
            entries.append(split_args(row[1:-1])[fields.index(field)])
        return entries
    m = re.search(r"\bstatic\s+const\s+[\w\s]+?\b" + re.escape(name) + r"\s*\[\s*\]\s*=\s*\{", text)
    if not m:
        raise Unapproved(f"no static const table {name}[] in this file")
    return [e for e in split_args(text[m.end():matching(text, m.end() - 1) - 1]) if e]


class Context:
    def __init__(self, text, pins, env, loop_spans, offset):
        self.text, self.pins, self.env, self.offset = text, pins, env, offset
        self.loops = [lp for lp in loop_spans if lp[0] <= offset < lp[1]]

    def loop_bound(self, var):
        inner = [lp for lp in self.loops if lp[2] == var]
        if not inner:
            raise Unapproved(f"'+ {var}' outside a for loop over {var}")
        bound, body = inner[-1][3:]
        if not re.fullmatch(r"\d+", bound):
            raise Unapproved(f"loop over {var} has non-literal bound {bound!r}")
        if re.search(r"(\+\+|--)\s*" + var + r"\b|\b" + var + r"\s*([-+*/%&|^]|<<|>>)?=(?!=)|\b" + var + r"\s*(\+\+|--)", body):
            raise Unapproved(f"loop body assigns its variable {var}")
        return int(bound)

    def span(self, expr):
        e = expr.strip()
        while e.startswith("(") and matching(e, 0) == len(e):
            e = e[1:-1].strip()
        m = re.fullmatch(r"(.+)\+\s*([A-Za-z_]\w*)", e)
        if m:
            n = self.loop_bound(m.group(2))
            return {p + k for p in self.span(m.group(1)) for k in range(n)}
        if re.fullmatch(r"EH_BADGE_\w+_PIN", e):
            if e not in self.pins:
                raise Unapproved(f"{e} not defined in {PINS_H}")
            return {self.pins[e]}
        if e in self.env:
            return self.env[e]
        m = re.fullmatch(r"(\w+)\s*\[\s*\w+\s*\](?:\s*\.\s*(\w+))?", e)
        if m:
            return set().union(*(self.span_entry(x, m.group(1)) for x in table_entries(self.text, *m.groups())))
        raise Unapproved(f"pin expression {e!r} is outside the approved grammar")

    def span_entry(self, entry, table):
        if not re.fullmatch(r"EH_BADGE_\w+_PIN", entry):
            raise Unapproved(f"table {table}[] entry {entry!r} is not an EH_BADGE_*_PIN")
        return self.span(entry)


def call_span(name, args, ctx, definitions):
    """Pins a call addresses; for a *_program_init, {param: pins} instead."""
    if name.endswith("_program_init"):
        if name not in definitions:
            raise Unapproved(f"{name} has no C-SDK definition in the audited sources")
        params = definitions[name]
        if len(args) != len(params):
            raise Unapproved(f"{name} called with {len(args)} arguments, defined with {len(params)}")
        return {p: ctx.span(a) for p, a in zip(params, args) if p.endswith("pin")}
    idx, count_idx = PIN_CALLS.get(name, (0, None))
    if idx >= len(args):
        raise Unapproved(f"{name} has no argument {idx}")
    pins = ctx.span(args[idx])
    if count_idx is not None:
        count = args[count_idx] if count_idx < len(args) else ""
        if not re.fullmatch(r"\d+", count):
            raise Unapproved(f"{name} pin count {count!r} is not a literal")
        pins = {p + k for p in pins for k in range(int(count))}
    return {"": pins}


def audit_sources(sources, pins, board_text):
    """sources: {path: text}. Returns a list of failure strings."""
    failures = []
    texts = {path: c_text(path, text) for path, text in sources.items()}
    definitions, bodies = {}, []
    for path, text in texts.items():
        for m in DEFINITION.finditer(text):
            params = [re.search(r"(\w+)\s*$", p).group(1) for p in split_args(m.group(2))]
            definitions[m.group(1)] = params
            bodies.append((path, m.group(1), m.end() - 1, matching(text, m.end() - 1)))
    bindings = {name: {p: set() for p in params if p.endswith("pin")} for name, params in definitions.items()}

    def audit_calls(path, text, env, keep, inside_definition=False):
        loop_spans = loops(text)
        for name, args, offset in calls(text):
            if not keep(offset):
                continue
            if name in READ_ONLY:
                continue
            if BANK_CALL.match(name):
                failures.append(f"{path}: {name} is a mask or whole-bank call")
                continue
            if name.endswith("_program_init") and inside_definition:
                failures.append(f"{path}: {name} called inside a C-SDK definition")
                continue
            try:
                spans = call_span(name, args, Context(text, pins, env, loop_spans, offset), definitions)
            except Unapproved as e:
                failures.append(f"{path}: {name}({', '.join(args)}): {e}")
                continue
            for param, reached in spans.items():
                if param:
                    bindings[name][param] |= reached
                if reached & EEPROM_PINS:
                    failures.append(f"{path}: {name}({', '.join(args)}) reaches EEPROM pin(s) "
                                    f"{sorted(reached & EEPROM_PINS)}")

    for path, text in texts.items():
        for word, flags in FORBIDDEN_WORDS:
            if re.search(word, text, flags):
                failures.append(f"{path}: mentions {word}")
        for m in re.finditer(r"#\s*define\s+\w+(\([^)]*\))?[ \t]+([^\n]*)", text):
            if re.search(r"\b(" + PIN_CALL_NAME.pattern + r")\s*\(", m.group(2)):
                failures.append(f"{path}: macro hides a pin-taking call: {m.group(0).strip()}")
        spans = [(lo, hi) for p, _, lo, hi in bodies if p == path]
        audit_calls(path, text, {}, lambda o, spans=spans: not any(lo <= o < hi for lo, hi in spans))
    for path, name, lo, hi in bodies:
        unbound = [p for p, s in bindings[name].items() if not s]
        if unbound:
            failures.append(f"{path}: {name} pin parameter(s) {unbound} bound by no call site")
            continue
        audit_calls(path, texts[path], bindings[name], lambda o, lo=lo, hi=hi: lo <= o < hi, inside_definition=True)
    for name in sorted({u for t in texts.values() for u in re.findall(r"EH_BADGE_\w+_PIN", t)}):
        if name not in pins:
            failures.append(f"{name} not defined in {PINS_H}")
        elif pins[name] in EEPROM_PINS:
            failures.append(f"{name} is EEPROM pin {pins[name]}")
    if re.search(r"#define\s+PICO_DEFAULT_I2C", board_text):
        failures.append(f"{BOARD_H} defines PICO_DEFAULT_I2C*")
    return failures


def main():
    sources = {str(p.relative_to(ROOT)): p.read_text()
               for d in SOURCE_DIRS for p in sorted((ROOT / d).glob("*")) if p.suffix in (".c", ".h", ".pio")}
    pins = pin_values((ROOT / PINS_H).read_text())
    failures = audit_sources(sources, pins, (ROOT / BOARD_H).read_text())
    print(f"pin_audit: {len(sources)} files, pins used: "
          + ", ".join(f"{n[9:-4]}={pins.get(n)}" for n in sorted(
              {u for t in sources.values() for u in re.findall(r'EH_BADGE_\w+_PIN', strip_comments(t))})))
    for f in failures:
        print(f"  FAIL {f}")
    print(f"pin_audit: {'PASS' if not failures else 'FAIL'}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
