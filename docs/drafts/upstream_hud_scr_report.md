# SUPERSEDED: upstream fixed this in 0.1.32 (PR #52, `ae803c6`); not to be published

# DRAFT: report for z3xox/Tenkaichi3Decomp (not published; for review)

This is a draft of an issue report for upstream. Nothing has been sent; publishing it is the owner's decision. The
evidence was gathered on 2026-10-12 (commands below; any C preprocessor reproduces it).

---

**Title:** `HUD_SCR` is defined only under `#ifdef PORT` but used outside it: the PS2 build of four HUD files no longer
expands it

**Affected revision:** `fff151e` (0.1.31); introduced in `5d4e9bc` (2026-10-09, "Online play: the joining player's own
side of the HUD is on the left …"), with more uses added in `38909b0`, `1ca2857`, `640d202` and `fe21675`. Not checked
against `c5d2917` (0.1.32).

**What happens.** `src/battle/hud_a_b.c`, `hud_b.c`, `hud_c_b.c` and `hud_e_b.c` each define

```c
#ifdef PORT
...
extern int Port_HudSwap(void);
#define HUD_SCR(side) ((side) ^ Port_HudSwap())
#endif
```

but the uses (`HUD_SCR(gHudCombo->side) == 0`, `tex += HUD_SCR(i)`, …) are outside any `PORT` block: 19 uses after
preprocessing (hud_a_b 1, hud_b 4, hud_c_b 6, hud_e_b 8). Without `PORT` (the PS2 / matching build in `configure.py`)
the macro is not defined, so `HUD_SCR(x)` stays a call to a function that does not exist. The README's rule "changes
for PC are inside `#ifdef PORT`" is broken there, and only there: all 274 game sources preprocessed without `PORT`,
these four are the only ones where a PC-only name (`Port_*`, `gPort*`, `PORT_*`, `HUD_SCR`) remains.

**Smallest reproduction** (from the repository root; any C preprocessor):

```sh
clang -E -P -w --target=mipsel-unknown-elf -Iinclude -Isrc src/battle/hud_c_b.c | grep -n HUD_SCR
```

prints its six uses, unexpanded. With `-DPORT` it prints nothing.

**Expected consequence (not run: needs Sony's ee-gcc 2.96 and the disc).** ee-gcc 2.96 (C89) accepts the implicit
declaration, so these functions compile to calls to an undefined `HUD_SCR`: the link fails, and the functions no longer
match the original.

**Proposed fix** (four lines, one per file): give the macro its PS2 meaning, the expression it replaced:

```c
#ifdef PORT
...
#define HUD_SCR(side) ((side) ^ Port_HudSwap())
#else
#define HUD_SCR(side) side
#endif
```

Unparenthesised on purpose: with it, each file preprocessed without `PORT` is token-for-token identical to the same
file before `5d4e9bc` (checked for all four), so the PS2 build gets exactly the original expressions. Every use passes
a plain expression (`i`, `side`, `gHudCombo->side`, …). At `fff151e` with the fix, no `HUD_SCR` or `Port_HudSwap`
remains in the four files without `PORT`. With `PORT` nothing changes.

**Verification done:**
- `5d4e9bc^` vs `5d4e9bc` + fix, each preprocessed without `PORT`: identical token streams (hud_a_b, hud_b, hud_c_b,
  hud_e_b);
- `fff151e` + fix, without `PORT`: 0 occurrences of `HUD_SCR` / `Port_HudSwap`;
- all 274 game sources preprocess without `PORT` (0 failures); only these four leak a PC-only name.

**Not verified:** the PS2 matching build itself (no ee-gcc 2.96 or disc in this environment).

---

Review notes (not part of the report):
- The browser port defines `PORT`, so neither the defect nor the fix changes the browser build.
- Script used: a throwaway shell script (git archive of each revision, the fix applied by a regex, clang -E -P,
  tokens compared with cmp); its steps are as described above and can be repeated by hand.
