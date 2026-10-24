# rcheevos

Source: https://github.com/RetroAchievements/rcheevos

Stable master revision: `1433173220a7eaede6a9ed7a18e94117be1821e0` (12.5.0).
Vendored `include/` and `src/` with a local native-port extension in:
- `include/rc_client.h`: `RC_CLIENT_READ_MEMORY_UNAVAILABLE` return value.
- `include/rc_runtime_types.h`: per-reference temporary availability state (not serialized).
- `src/rc_client.c`: skip unavailable reads; propagate availability through derived
  references; reset/suspend dependent achievements; seed current/prior values and
  skip the first restored frame. Zero-byte invalid reads retain upstream behavior.

This extension is for this softcore integration, with leaderboards disabled. It
changes neither server definitions nor their MD5/submission checksums. Allocation
and free hooks in the game adapter force a missing frame even on address reuse.
Regression tests exercise absence, destruction/recreation, legitimate health and
dialogue transitions, hit-count reset, derived references and permanent invalid reads.

Only the runtime, client, API and MD5
sources are compiled, with `RC_DISABLE_LUA` and without optional external clients,
RAIntegration or the general-purpose disc hash subsystem.

License: MIT (`LICENSE`). The MD5 implementation retains its own permissive notice
at the top of `src/rhash/md5.h` and `md5.c`.
