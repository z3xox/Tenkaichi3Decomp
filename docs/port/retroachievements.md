# RetroAchievements (experimental)

This branch integrates online accounts through the official rcheevos `rc_client`.
F1 > Achievements offers sign-in/sign-out, game status, unlocked counts and
unsupported counts. Unlocks produce an on-screen notification. This is an initial
**softcore-only** implementation; hardcore and leaderboard tracking are disabled.
If the game set fails to load after sign-in, **Retry loading achievements** retries
that operation using the current session, without asking for the password again.

## Scope

The memory view exposes stable objects by identity, independently of where the
port allocates them:

| Original PS2 address | Native source | Exposed data |
|---|---|---|
| `0x6B9640..0x6BD63F` | `gSaveData`, `include/sys/save.h` | Pointer-free serialized save, 0x4000 bytes |
| `0x6AF180 + offsets` | `gProgress`, menu/view headers | Scalar fields at 0x18, 0x2C, 0x3C, 0x84, 0x90, 0x620, 0x624, 0x64C, 0x674 |
| `0x31E592`, `0x31E59A` | `gLoadScreen`, `include/sys/loading.h` | Loading minigame score and type (u16) |
| `0x18706C0 + side*0x1600 + 0x9A4/0x9E4 + member*0xA4` | `gBtlChars->chars`, `include/battle/btl_char_mgr.h` | Member IDs and health only (s32), two sides, five members |
| `0x8C0880` | `gSimDay->msgLine`, `include/menu/menu_q.h` | Current simulation dialogue line (s32, native offset 0x5A0) |

All other reads return unsupported. No arbitrary host pointer or zero-filled RAM
image is exposed. Existing definitions and their submission checksums are fetched
unchanged by rc_client.

The public reference patch in [BT3-Recomp](https://github.com/z3xox/BT3-Recomp/blob/main/docs/retroachievements/bt3-raw-patch.json)
(blob `961b9d1ab48119943407c1dc7ee95f985d37b917`) contains 153 game achievements plus
a zero-point unknown-emulator warning. All 153 game achievements use the verified
fields above: 104 stable-object achievements, 42 using fighter objects and seven
using simulation dialogue. The live set can change; unknown future reads remain
unsupported and the UI reports the runtime's actual unsupported count.

Temporary objects require a small, documented native-port extension to the pinned
rcheevos client. A known field whose object is absent returns
`RC_CLIENT_READ_MEMORY_UNAVAILABLE`, without writing destination bytes. Unlike an
invalid address (zero bytes read), it does not permanently disable achievements.
The client skips updating that memory reference and resets/suspends every dependent
achievement, including dependencies through derived references. Stable-object
achievements continue. When the object returns, the first actual observation seeds
both current/prior values; one warm-up frame is skipped before normal evaluation.
No fabricated zero, stale observation or counter from an earlier object can trigger
an award. Allocation/free hooks also handle reuse of the same address between ticks.
This extension is scoped to this softcore integration; leaderboard tracking is off.
Its changes are listed in `port/third_party/rcheevos/UPSTREAM.md`.

## Identification, networking and lifecycle

The game hash is computed from the user's extracted `disc/SLUS_216.78` with the
PS2 algorithm in rcheevos `src/rhash/hash_disc.c`: MD5 of the BOOT2 executable
basename followed by its contents, capped at 64 MiB. The port only accepts the USA
release. Neither the resulting hash nor a game ID is hardcoded.

A worker sends HTTPS requests using WinHTTP on Windows or the system curl library
on Unix, with TLS verification and a product/version User-Agent. No subprocess is
used for credentials. Completion callbacks, rc_client state and memory reads run
on the game thread, once per vertical blank; render/UI code queues commands and
copies status under a mutex. Passwords and tokens are not persisted or logged.
Server retry and session behavior use rc_client rather than custom award requests.

Processing waits for the port's original-data verification. It is suspended for
netplay, replay/scripted input, diagnostic runs, nonstandard speed, clash assistance
and Unlock Everything. Unlock Everything is refused while RA is enabled,
including a pending login. Before the debug function can mutate the save, a
`.retroachievements-tainted` marker must be written in the save root (`saves`, or
`BT3_SAVES`). If that write fails, the debug operation is canceled. The marker
blocks achievement processing for that save folder across process restarts and
account changes; a separate clean save folder is required for achievements.
The marker is checked again when the save root changes (including netplay's
temporary save-folder switch).
This records uses of the option in this build, not the provenance of older saves.
No network request is made until the player explicitly signs in. Signing out
unloads the game and clears account status; responses for an aborted account are
drained before another login begins.

The pinned library revision and licenses are in
`port/third_party/rcheevos/UPSTREAM.md`. It is built with the host ABI and excluded
from rollback snapshots. Release packaging includes the library/MD5 notices.

## Verification

```sh
clang -std=c99 -Wall -Wextra -Werror port/src/ra_memory.c port/tests/ra_memory.c -o ra_memory_test
./ra_memory_test
python port/tests/run_ra_client.py
python port/tests/run_ra_game.py
# Optional: pass the public reference patch (no network/account used by the test)
python port/tests/run_ra_client.py /path/to/bt3-raw-patch.json
```

The client test compiles the real runtime with a fake transport and no native
network implementation. It checks login, unsupported reads, a local fixture
unlock, retry after connection failure, softcore flags, cancellation, account
isolation, game-load failure recovery without a second login, temporary-object
destruction/recreation, delta safety, hit-count resets, derived references and shutdown.
It never contacts RetroAchievements or awards a real
account. SDL3 and a C compiler are required; `SDL3_CFLAGS` and `SDL3_LIBS` can select
an installation. `BT3_TEST_TMPDIR` selects a writable test output directory.

The memory test checks relocation, partial pointer reads, non-pointer integers,
null pointers, adjacent regions, holes, malformed descriptors, overflow and
preserving the destination on failure.

The game-adapter test runs production mapping and identification code against
synthetic native objects. It checks actual Save/Progress/loading/fighter/dialogue
reads, absent and recreated objects, and rejects unverified neighboring fields, compares the executable hash with
Python's independent hashlib result, rejects invalid/empty executables, and
checks data-verification/netplay/speed/clash/rollback/Unlock Everything gates,
refusal during an active account session, marker-write failure, the taint marker
in a new process and rechecking it after restoring another save folder.
It requires an x86-64 Linux or Windows host, but no disc, SDL or account.

`.github/workflows/retroachievements.yml` runs all offline checks and builds both
complete release archives in the project's official Docker toolchain.
It downloads the pinned public reference blob, verifies its Git object hash, then
tests all 153 game definitions through the production adapter/client with a fake
transport. This checks zero unsupported references with temporary objects absent,
safe fighter destruction/recreation under active tournament guards, and a valid
health transition unlocking the actual tournament definition. The synthetic
unknown-emulator warning is excluded. No account or game data is used.

A separate copy of an installed Windows build was observed for 30 seconds without
a window. Its relocated Progress/Save object offsets agree with these source
layouts. The modified executable at commit `4c3a892` was subsequently built for
Windows and Linux by the complete release workflow (run `40053794686`), which
also passed all offline tests. Its Windows binary ran headlessly for 30 seconds
with isolated saves and a copy of the player's original data: original-data
verification passed, 1,784 vertical blanks elapsed, all 12 expected memory regions
were registered, and the executable hash was
`18df2548cc72b98287170b19f73c502b`. No account login or real award was attempted.
The player subsequently signed in through the settings UI and confirmed that a
legitimate achievement unlocked in the game and appeared on the RetroAchievements
site. That live unlock used the initial 104-achievement build.

The expanded implementation at `9c6b5c2` passed all offline checks, including the
153-definition production reference test, and complete Windows/Linux release
builds in [run 40100268348](https://github.com/JulioHeroZ/Tenkaichi3Decomp/actions/runs/40100268348).
Its Windows executable ran for 30 seconds in both normal and diagnostic-battle
modes with copied original data and isolated saves. Normal mode passed data
verification, registered the stable regions and produced the expected executable
hash. In diagnostic battle mode, 107 samples observed two fighter objects with
member IDs 0/37; opponent health changed from 40000 to 31920 at the mapped offsets,
in agreement with the game's own trace. Diagnostic runs correctly remain
ineligible for achievements. The expanded live-account set-loading check is pending;
not every battle/dialogue challenge has received individual live gameplay acceptance.

References: [rc_client integration](https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration),
[PS2 game page](https://retroachievements.org/game/20590), issues
[#93](https://github.com/z3xox/Tenkaichi3Decomp/issues/93) and
[#111](https://github.com/z3xox/Tenkaichi3Decomp/issues/111).
