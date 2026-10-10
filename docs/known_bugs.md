# Bugs and quirks in the original code

Things a port must either reproduce exactly (when game behaviour depends on them) or fix on
purpose. Each is confirmed by C that compiles to the original bytes unless marked inferred.

## Behaviour-affecting

| Where | What | Consequence |
|---|---|---|
| `Rand_Next` (`src/sys/rand.c`) | The MT19937 refill has only its first loop and the final word; the second loop is missing. | 396 of 624 state words never change after seeding. The game's random sequence is not MT19937. A port must copy the broken refill to reproduce behaviour. |
| `BtlSeq_JudgeByHealth` | On equal health with no rule flag, outside the mode-0 time-up draw, the winner is `rand() & 1`. | A double KO can be decided by a coin flip from the C library generator. |
| `BtlMember_Init` (`battle_load.c`) | Applies items (and so looks up the default AI type) before storing the character id. | The default AI type is always looked up for character 0. |
| `Dma_PutTexStrips` | The float path adds the X offset without the `<< 4` the integer path applies. | Textured strips whose width does not divide evenly are positioned differently. |
| `Fade_IsDone` | Returns 1 when the done flag is set, which is one frame before the final colour is computed, and also for a slot that is off. | Callers proceed one frame early. |
| `Pad_Update` | A digital-only pad is reset after being read. | Controllers without analog sticks give no input at all. |
| `Pad_Reset` | Does not clear the game-level stick copies. | They keep stale values while a pad is unplugged. |

## Missing checks (crash or corruption if the limit is hit)

| Where | What |
|---|---|
| `BtlPool_Alloc` | No capacity check: an arena that overflows runs into the next one. |
| `BtlPool_CarveArenas` | Slot 5 tests slot 4's mask bit and slot 8 tests slot 7's. Harmless only because every bit is always set. |
| `File_Request` | No check for an empty free list: a 33rd pending request dereferences NULL. |
| `Snd` command queue | A 33rd command in one frame is dropped (the play returns -1); this one is handled. |
| `File_LoadSyncEx`, `Overlay_Load`, boot | Every disc operation retries forever; there is no failure path. |
| `Heap_Free` | A pointer that is not a live block calls an empty report function and then frees a NULL block. |

## Dead or broken code that nothing calls

| Where | What |
|---|---|
| `Quat_Normalize` | Reads the length from its output argument, so it is only correct in place. No callers. |
| `Save_UnlockAll` | Debug "unlock everything". No callers. |
| `Disc_Identify`, `Disc_GetDriveState` | Disc check for the three Tenkaichi games. No callers. |
| `Snd_Term` | Its loop unloads only empty slots, so it does nothing. No callers. |
| `Fade_Init` | Clears only slot 0 of three; the others rely on zeroed memory. |
| `Heap_SetDebug*`, `Dbg_*`, `Gfx_MarkPass`, `Heap_ReportBadFree` | Stripped debug hooks, compiled to empty functions. |
| ~25 list/queue/alignment utilities | Never called: a general utility library was linked in. |

## Added with the fighter-core batch

| Where | What | Consequence |
|---|---|---|
| `Mathf_WrapAngle` | The intended lower bound is overwritten, so every angle below 2 pi is pushed up by 2 pi and brought back. | Angles are quantised before `sinf`; a port must keep the sequence. A huge angle loops forever. |
| `BtlObj_UpdateVisibility` | The distance loop means to take the minimum over four box corners but passes the same corner four times. | Distance fade uses one corner. |
| `BtlObj` fighter work buffers | The code accepts a third fighter object, whose buffer would overlap the next pool. | Latent; only two fighters exist. |
| `BtlChars_UpdateFreeze` | At hit-stop level 2 the loop stops at the first exempt fighter. | Fighter 1's exemption is never considered when fighter 0 is at level 2: roster order matters. |
| `SpuHeap_Alloc` / `Free` | No failure path: exhaustion, more than 17 ranges, or an unknown address dereference NULL. | |
| `Gsc_StepTask` | An unknown command id calls a NULL handler. | |
| `BtlScript` triggers | Command 7 fills up to 50 requests with no capacity check. | |
| Replay recording | At 9000 frames the recorder silently stops storing. | A fight longer than 5 minutes of input-taking frames cannot be replayed to the end. |
| Replays and `rand()` | Nothing reseeds `rand()` for a replay. | A replayed double KO can resolve differently from the original (inferred). |
| `Timer_GetFrames` | The multiply wraps after about 71.6 s. | Used only by the movie player. |

## Added with the fighter-core batch

| Where | What | Consequence |
|---|---|---|
| `BtlChange_Update` / time stop | The fight is frozen for as long as a transformation, fusion or switch model takes to load. | Duration depends on disc speed; a deterministic port must fix it. |
| `BtlColl_Update`, `BtlHit_ApplyHit` | Fighter 0 is tested and applied first; its hit writes fighter 1's reaction and health before fighter 1's hit is applied. | Player 1 / player 2 asymmetry. |
| `BtlAiPad_Set` | The stick is averaged against a per-frame accumulator that starts at zero. | The CPU sends half the stick deflection it asks for. |
| Script commands 13 / 14, option `-B` | Calls the ki adder instead of the blast adder. | Scripts cannot add blast gauge. |
| Script command 1602, option `-h` | The channel value is stored and then overwritten with 0. | Voice always plays on channel 0. |
| Fighter flags | Flags written before the first stage of a fight are never promoted; `ClearFlag` does not refresh the previous plain bit. | Edge tests can misreport in those cases. |
| `prev` action (fighter +0x950) | Never written anywhere in the executable. | Reads 0, so every branch on the previous action is constant: ki blasts never alternate hands, clash C always plays its first strike animation, dash start animations never vary. |
| `BtlMove_CalcApproachPoint` | Takes `sqrt(len^2 - dy^2)`, which can be negative (inferred hazard). | The PS2 returns a number where a PC returns NaN. |
| `BtlMove_CalcVerticalSpeed`, `BtlMove_CalcJumpSpeed` | Loops end on a sign change (inferred hazard). | Would not end on a NaN. |
| `BtlCharSnd_PlayCommonFar` | near = far = 100000, so the attenuation divides by zero. | |
| Fighter sound requests | A fifth request in one frame is dropped silently. | |
| `BtlColl_TryGuard` | Allows a guard kind 6 that skips the from-behind test, but the classifier never returns 6. | Dead branch. |

## Found by the differential behaviour test (2026-10-05)

- `EftChain_BlendKeys` (src/battle/eft_s.c, chain effect key blending): the original computes
  `endAlpha = row20[k0] + (row9[k1] - row9[k0]) * t`, i.e. it interpolates the end alpha with
  the DELTA OF ROW 9 (the shrink rate) instead of row 20's own delta: a copy-paste slip in
  the original source. Visual only. Our C attempt had "corrected" it silently; it now
  reproduces the original (marked `sic` in the source).
- Uninitialised stack reads in the original that reach outputs (a PC build inherits them
  unless the locals are initialised): `EftGeyser_StartSmoke` (8 bytes of the 156-byte block
  passed to `EftSmoke_Create`, from byte +76), `HudGauge_UpdateAura` (u16 locals reaching
  memory stores). Visual only; a port should zero these and accept that the PS2's result
  depended on stack junk.

## Port only: a tournament match crashed for some players (fixed in 0.1.17)

Reported on Windows with 0.1.16: starting a World Tournament match crashed in `BattleSetup_SetSide`, "reading
address 0xFFFFFFFFFFFFFFFF", called from `Bracket_SetupBattle`. It did not happen on the user's own machine.

Cause (verified in the release program's code): three menu headers (`menu_i.h`, `menu_l.h`, `menu_q.h`) declared the
function's eighth argument, a pointer to the usable-character bits, as `s32`. The callers pass 0 for it. On the PS2
both are 32 bits and nothing is wrong. In a 64-bit build the eighth argument travels on the stack: the caller,
believing it an `s32`, wrote 32 bits of zero (`movl $0, 0x38(%rsp)`) and the function read 64, the upper half
being whatever the stack held. When that was not zero the function took it for a pointer and read through it.
Whether it crashed depended on what had run before, which is why it was not seen everywhere. The same wrong
declaration was used by the callers in `menu_h_d.c` (training) and `menu_q_b.c` (Sim Dragon).

Fix: the three declarations say `void *`; the caller now writes 64 bits (`movq`). `port/tools/proto_check.py`
lists every function declared with a pointer in one place and a 32-bit integer in another (78 of them): this was
the only one in an argument passed on the stack on both systems (the fifth and later on Windows, the seventh and
later on Linux). The others are in registers, where a 32-bit write clears the upper half, or are return values.
The reporting player, for whom it crashed with every character every time, confirmed that a build with the fix
starts the match (2026-10-09). Released in 0.1.17.


## The replay list stopped the game (found 2026-10-10, reported by a player)

Opening the replay list (to save a replay after a fight, or in the Data Center) ended the game with an arithmetic
fault in `ReplayMenu_Draw`. The list works out its slot number's digits with `pow(10.0, i)`, the game's only call of
a `double` maths function. The game code is built with software floating point (doubles as 64-bit patterns in the
integer registers) and every `float` function goes through a bridge (port/include/port_libm.h); `pow` had none and
went to the host's, which reads its arguments from the float unit. What came back was whatever the integer register
held, often 0, and the list divided by it. In every release up to 0.1.19, on Linux and Windows.
Fixed: `Port_pow` (port/src/plat_libm.c). The other calls of the host's `pow` are the port's own code (the settings
window, the sound), which is built for the float unit. Checked: the program's code calls the bridge at all four
places; the replay check. Seen fixed by the user (2026-10-10).

## Two reports of 2026-10-10 (both in every release up to 0.1.20)

**The picture was stretched after the shape was changed during a fight.** A view's projection is worked out when
the view is set up (`View_InitLayout`: the start of a fight or of a scene), and the port widened it there. Changed
in the settings window during a fight, the picture was shown at the new shape with the old projection until the
next fight. `View_UpdateMatrices` now holds every view to the shape of now (src/battle/btl_cam.c). Checked without
a window: a run that changes from 4:3 to 16:9 partway (`BT3_ASPECT_AT=<frame>:16:9`) gives, after the change, the
same pictures as a run at 16:9 throughout.

**The cross-fade between the shots of a fight's intro was missing.** The game reads the frame being shown back
from the GS (`ScrXfade_Capture`, `sceGsExecStoreImage`), uploads it again as a texture and draws it over the new
shot with a falling alpha, for a second. The port answered the read-back with nothing (docs/port/README.md listed
the cross-fade among the effects still dropped). It is answered now (`Gs_StoreImage`, port/src/gs/gs_core.c): from
GS memory in the software renderer, and in the GPU back ends by reading the frame buffer's picture from the card
(`GsBackend.targetRead`), reduced to 512 x 448; black with the back end on a thread of its own (`BT3_GS_THREAD`).
`ScrXfade_StoreHalf` is the decompilation's matched version now (the port's copy was an earlier draft; it was
compiled all the same). Checked without a window, in the software renderer: with the cross-fade asked for in a
fight (`BT3_XFADE_AT=<frame>`) the frame taken shows over the following ones and fades out.
With that alone the GPU back ends still showed no cross-fade (seen by the user): the game uploads the frame it read
over the DEPTH page (0x1C00) and draws it from there, and gs_draw.c dropped every draw that samples the depth page
(the passes that read it as depth). A 24-bit picture drawn from there as a sprite is let through now.
Checked: the OpenGL back end without a window (`SDL_VIDEODRIVER=offscreen BT3_GS=gpu`: the Vulkan one cannot
start there, OpenGL takes over, and `BT3_SHOT` writes its pictures) shows the frame taken fading out over the next
ones, the right way up and in its colours. NOT seen: the Vulkan back end's read-back, and an intro itself.
