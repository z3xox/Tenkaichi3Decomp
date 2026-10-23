# Wii Dragon Net Battle menu in the PC port

With the Wii assets extracted, the PS2 main-menu plate **Dragon Net Battle** opens the Wii mode's top-level choices
in the port's screen mode. Without those assets, it opens the PC online window over the main menu as before.
Nintendo WFC Battle opens its Wii battle menu. Custom Battle opens a page with **1 VS 1 Battle** and
**Team Battle**. Normal Battle opens a page with **10 DP Team Battle**, **15 DP Team Battle**, and
**20 DP Team Battle**. Each entry opens
the Wii opponent-search screen with its matching battle rules.
When another player is found, the VS screen shows both player names and "Opponent found!" for about three seconds
before the existing online session launcher starts the battle. Triangle cancels
the search and returns to the battle page it came from. Friend Battle is enabled and opens **Host Battle** and
**Join Battle**. Hosting offers all five battle formats, then shows a six-character room code on the Wii VS screen.
If the other player leaves during a match or the connection is lost, the restored Battle Net page shows a
dismissible memory-card style dialog with the game's native dialog font. The separate small Wii Giru sprite bobs
below it with a flashing eye; Cross or Triangle closes the message.
Joining accepts that code through the same Wii Data Center keyboard used for Create Player Name, limited to six
letters or numbers, and opens the same VS screen. When the connection succeeds, both
names remain on screen for about three seconds before battle; a failed join returns to the Friend Battle menu.
Room codes use the configured matchmaking service (`BT3_MATCH_URL`) and require that service to be reachable.
Custom Battle, Normal Battle, and their submenu entries
use their normal, enabled colors. Each submenu and opponent-search screen uses its selected battle type as the header.
Ranking Battle, View Ranking, and Battle Record remain unavailable.
By default, these entries use the existing direct lobby and battle session, with no matchmaking Worker, build
key, or internet connection. The first copy on one computer hosts on UDP port 25443; a second copy on that
computer joins over loopback. For two computers on a LAN, start the host with `BT3_LAN=host` and the other with
`BT3_LAN=join:<host IPv4 address>`, then choose the same entry on both. Allow UDP port 25443 through the host's
firewall. The direct LAN greeting includes the selected battle mode; different entries stay in search instead of
starting a match. Set `BT3_LAN=online` to use the public quick-match queues (S, T, D10, D15, D20); that service may
require an official signed release. Alternatively, test those queues with your own Worker via `BT3_MATCH_URL`; see
[the Worker setup](../../port/matchmaking/README.md).

For a local CPU opponent, start the human game with `BT3_LAN=host` and choose a Battle Net mode. In another
terminal, run `python3 port/tools/lan_bot.py`, then enter `1v1`, `team`, `dp10`, `dp15`, or `dp20` at its prompt.
Use `level 1` through `level 5` to set the game's CPU difficulty (3 by default), or enter a mode and level together,
such as `1v1 5`. Changing the level while the bot is running restarts it, so choose the level before a match.
Enter `exit` to disconnect the bot while keeping its command prompt open. Enter `quit` to close the bot script;
entering another mode restarts the bot in that mode. You can also
pass an initial mode and level, such as `python3 port/tools/lan_bot.py 1v1 --level 5`. The bot's game runs without a window or sound.
For Friend Battle room codes, enter `host 1v1`, `host team`, `host dp10`, `host dp15`, or `host dp20` in the bot
prompt. The bot prints the generated code; enter it in the game's **Friend Battle > Join Battle** screen.
Enter `join ABC123` to make the bot join a code shown by the game's **Friend Battle > Host Battle** screen.
These commands also work at startup as `python3 port/tools/lan_bot.py 1v1 --generate-code` or
`python3 port/tools/lan_bot.py --join-code ABC123`. Room codes require the configured matchmaking service.
For the direct LAN commands, it joins only the
matching LAN queue, picks a random available character on the select screen, and uses the game's CPU AI in battle.
In Team Battle, the bot randomly picks different characters to fill all five slots. In DP Team Battle, it randomly
picks characters that fit the remaining budget until the team is full or no eligible character fits.
If the bot wins, it selects **Return to Character Select** and confirms **Yes** in the result menu, so the session
can continue with another character selection.
Use `--host ADDRESS` when the human game is on another machine, `--game PATH` to select a different built executable,
or `--name NAME` to change the bot's displayed name. The bot defaults to `port/build/bt3_64`.

The separate **Tenkaichi Budokai Online** plate still opens its in-game hub.
Manage Friends and Initialize Wi-Fi Data stay in the menu with a dimmed appearance when unfocused. Both can
receive focus and play the selection sound, but they do not open a screen because their online services are
unavailable in the PC reconstruction. User Settings opens a page with Create Player Name and
Initialize Wi-Fi Data, using the Wii label sheets. Create Player Name reuses the complete Data Center Password Input keyboard movie: backing, glyphs,
selected characters, arrows, delete mark and bottom buttons. Its original selection, release and confirmation
animations run unchanged. The movie is shifted upward by 30 pixels as one unit to meet the name field;
individual keys and captions are not repositioned. The password movie's root entrance is settled on opening,
then the name page moves the entire keyboard with the field during its entrance.
The Wii resource 13 frame, title, underline cursor and typed characters supply the name field above the keyboard.
The name field mirrors the Wii frame's half-width art and shows Giru behind it. Typed characters are vertically
centered inside the field.
Pan remains visible in User Settings and leaves as the keyboard opens. Giru stays beside her on the settings page,
then moves horizontally left and climbs to the upper-left of the name field, turning toward it during the
keyboard entrance. He uses his native 256×128 texture size on both pages. The keyboard backing,
glyphs, captions, and text field share the Wii nickname movie's 16-frame entrance.
Returning from User Settings preserves the home positions without replaying their entrance. Both characters keep
their continuous idle movement.

## Reusing a keyboard on a new screen

For a Wii-style text-entry screen, follow `TB_WII_NAME` (player name) or `TB_WII_FRIEND_CODE` (room code) in
[`src/menu/port_tbo.c`](../../src/menu/port_tbo.c). Both use the Data Center Password Input movie exposed by
`Port_DcNameOpen`, `Port_DcNameKey`, `Port_DcNamePress`, `Port_DcNameDraw`, and `Port_DcNameClose` in
[`src/menu/menu_z_d.c`](../../src/menu/menu_z_d.c). The helper supplies the animated keys and buttons; the caller
owns the entered text, text field, title, background, and what Decide and Quit do.

1. Add a screen mode and open the keyboard with `Port_DcNameOpen()` before entering it. If opening fails, stay on
   the current screen. Initialize `typed`, `typedLen`, `nameCursor`, `lower`, `keyRow`, and `keyCol`; the name editor
   preloads the saved player name, while Join Battle starts empty. A transition through `wiiNextMode` and
   `ColorFade_StartOut` gives the new screen the normal fade.
2. Route the mode through the main update and draw switches. `Tb_WiiNameUpdate` handles the Wii cursor, case,
   insertion, deletion, Decide, and Quit; add the new mode where those common controls are dispatched, then give
   it its own validation and completion action. `Tb_NameAdd` and `Tb_NameBack` hold the text. Set the new mode's
   length and allowed characters explicitly; the room code uses six uppercase letters or digits, while the name
   uses up to 16 characters. Keep room codes in `code`, separate from the saved player name.
3. Draw the movie with `Port_DcNameDraw(lower, keyRow, keyCol, rise)`, then draw the field and its typed characters
   as `Tb_DrawWiiName` and `Tb_DrawWiiNameText` do. `rise` moves the keyboard during its entrance; the helper
   applies the movie's fixed 30-pixel upward offset. Draw the title in `Tb_DrawText` so it follows the same
   entrance. Use the screen's own title art or a centered font title as needed.
4. Include the mode in `Tb_Backdrop` according to the background it should retain. Join Battle includes
   `TB_WII_FRIEND_CODE` in the Battle Net clock and space background; routing it only to the keyboard draw path
   would leave the wrong backdrop. Also include it in the screen's fade, frame counter, and heading/prompt
   conditions where appropriate.
5. Call `Port_DcNameClose()` when leaving the keyboard mode, including cancellation and failed submission. The
   current `Tb_SetMode` does this for both Wii keyboard modes; `PortTbo_Run` also closes it on shutdown. Keep the
   input-only movie rendering so the Data Center screen's own background and password handling do not appear.

The separate PC online hub has a code keypad (`Tb_Keypad`/`Tb_PadUpdate`) and a name keypad in the same file.
Those are drawn from the port's UI kit and can also accept physical keyboard typing through `Port_TextMode` and
`Port_TextRead`. Use that path for a PC-style screen; the Wii movie path uses controller navigation.

## Adding a Dragon Net Battle submenu

Use the Custom/Normal format page (`TB_WII_CUSTOM`) or Friend Battle's Host/Join page (`TB_WII_FRIEND_MENU`)
in [`src/menu/port_tbo.c`](../../src/menu/port_tbo.c) as the starting point. Custom and Normal share a screen
mode and select different rows and titles through `wiiNormalBattle`; Friend Battle has its own mode. A new
submenu needs a mode, a cursor, labels, drawing, input, and a route back to its parent.

1. Add a `TB_WII_*` mode and enough state in `TboScreen` for its cursor and any chosen variant. Give the mode a
   title in `sTbTitle`/`Tb_SetWiiTitles` so the screen reader can announce it. Reset its cursor before opening it.
   In the parent entry's Cross branch of `Tb_WiiUpdate`, set `wiiNextMode` and call `ColorFade_StartOut`; the run
   loop calls `Tb_SetMode` after fade-out and starts the fade-in. Add Triangle navigation back to the parent in
   the same update function.
2. In `Tb_WiiUpdate`, select the new cursor and row count for the mode. Add its Cross action for each enabled
   entry. If an entry starts opponent search, choose its queue and rules, set `waitBack` to the submenu, and use
   the existing VS transition/search path. Keep unavailable entries visibly dimmed and without a Cross action.
3. Draw the submenu in `Tb_Draw`, usually by extending `Tb_DrawWii`/`Tb_DrawWiiBattle`. That renderer already
   supplies Giru, stair-step bars, focused gold/idle blue layers, pulsing label text, radar, and the animated
   header. Set the row count, cursor, labels, and title for the new mode consistently in all its drawing paths.
   If there is no extracted lettering sheet, use `Tb_WiiLabelText` for both focus states. Keep the crest draw
   after the title so it appears over the lettering.
4. Include the mode in `Tb_Backdrop` for the clock, space, and stars, and in the main run loop's Wii update,
   draw, entrance timer, and prompt conditions. `Tb_SetMode` resets `modeT` and focus animation state; add the
   mode to its `wiiBattleT` reset when it uses the Battle Net entrance. Match the input gate in `Tb_WiiUpdate`
   so rows cannot be chosen while they are entering.
5. Put editable header and entry strings in `kPortText` in
   [`port/src/gs/menu_translation.c`](../../port/src/gs/menu_translation.c) and in each applicable
   `port/translations/port/en.json` (and any other port language catalogs you provide), using stable
   `dragon-net/...` IDs. Update the catalog's count metadata too. `Tb_LoadWiiText` copies the
   resolved strings into `sTbWiiText`; update its index constants, `TB_WII_TEXT_COUNT`, and the arrays that
   reference those strings. Keep the catalog order and `PortTbo_WiiTextCopy` indices in sync. If the words or
   descriptions come from Wii disc text instead, expose their extracted IDs through the UI catalog and render
   that resolved text. Add the submenu's title, focused entry, and description to `Tb_Speak`/`Tb_BoxLine` for
   screen reading and the bottom dialog.

For a submenu with its own artwork, add the extracted texture IDs and dimensions in
[`include/menu/tbo_art.h`](../../include/menu/tbo_art.h) and the extractor's mapping in
[`port/tools/extract_wii_online.py`](../../port/tools/extract_wii_online.py). A submenu built from the existing
Battle Net bars, radar, and font labels needs no new texture sheet.

The settings header uses the Wii **User Settings** title from section 0, resource 4, texture 3, with the
existing header animation.
Decide saves the name to `net_name` in `bt3_settings.txt` and updates the
online name used by the port. Initialize Wi-Fi Data remains unavailable. The Wii home screen omits the port's bottom
right Enter and Back hints.

Extract the menu from a Wii disc you own:

```sh
python3 port/tools/extract_wii_online.py roms/RDSPAF.rvz
```

You can also include it in the normal install, with either a European Wii `.iso` or `.rvz` compressed image:

```sh
python3 install.py --iso /path/to/usa.iso --eu-iso roms/SLES-54945.iso --eu-wii-iso roms/RDSPAF.rvz --rebuild
```

This requires Dolphin's `dolphin-tool`; `--dolphin-tool PATH` selects an executable for either command. The
installer writes the Wii assets to `gamedata/dragon-net-battle/` after the build. An already extracted
`wzs3eu1.afs` or `wzs3us1.afs` can be given instead; pass the matching `wzs3eu2.afs` or `wzs3us2.afs`
with `--voice-afs PATH` to include the dialogue voices. Without it, an AFS input still extracts text and lip tracks.
With an ISO or RVZ input, the script extracts the second archive automatically. The script reads the archive's filename table, decodes
the DragonNe pack's BPE sections, writes native resource binaries and the GX I4/C4/C8 sheets as PNG, and makes
`gamedata/dragon-net-battle/index.html` for inspection. All output is ignored by Git. The GX palettes use split G/R and A/B
planes; the extractor reconstructs RGBA colors and the correct width and height from the texture headers.
It also walks nested resource packs, including the small disconnect-dialog Giru and eye in section 0, resource 10,
part 0, and indexes those sheets alongside the direct textures.

The top-level Wii menu uses the extracted color art directly: eight landscape tiles, Pan, Giru, clouds, crest,
title, radar, bars, and both states of the three option labels. The cursor selects the original gold or blue
bar and its matching label. `home-motion.bin` holds keyframes decoded from the Wii FOD movie: the entrance of
the rows, Pan and Giru, Pan's idle movement, the radar pulse, and the lake sparkles. The home screen uses the
500-frame idle span at three-quarter speed. Pan and Giru use a slightly larger cosine bob with
five complete cycles in that span, easing at the top and bottom with no pause or jump at the loop seam. Giru has a different
phase. The title and crest follow the actual main-menu `fl_in` keyframes for `mc_title` and `mc_icon`:
the crest slides into place, then the title expands horizontally from its left edge. The root movie stops at
frame 59, before `fl_out` at frame 60; its icon and title children continue their original 60- and 410-frame
idle loops. The crest bobs gently and the title shifts and changes width while the menu remains open.
The radar appears after the rows finish entering, then switches directly to the selected row. Giru's artwork is
mirrored across its vertical axis with its visible bounds kept in place. The radar pulse animates independently.
These are whole-sprite movements;
the Wii source artwork does not provide articulated body frames for this home pose. A built-in motion fallback keeps them moving
if an older extraction has no `home-motion.bin`; the renderer logs which source it loaded. Selection fades through the
movie's five-frame highlight transition. The European archive has five DragonNe dialogue banks, decoded to
`home-subtitles-{en,es,de,fr,it}.bin` with 44 lines each. It also supplies five sets of headings,
option labels, and Create Player Name artwork, including its button captions, under
`languages/{en,es,de,fr,it}/`. The Nintendo WFC Battle, Custom Battle, and Normal Battle
headings, battle option labels, Fighting Point label, and VS record captions
come from each language's section 1 pack under `languages/<locale>/section-01/`.
Older extractions without those sheets fall back to the original English art.
The extractor also writes `menu-text-{en,es,de,fr,it}.bin` with all 66 Wii menu
messages, including the six battle descriptions and opponent-search status.
Every extracted Wii dialog line has a stable ID in `game-text/translations/ui/<locale>.json`:
`wii-dragon-net/dialog/0` through `/43` for spoken dialog and
`wii-dragon-net/menu-text/0` through `/65` for menu messages. Edit a row's
`text` field to override the disc wording in that language. The selected menu
language uses its Wii text when the row is unedited.
The ported screens use these IDs: home, friends, settings, and ranking dialogue
use `wii-dragon-net/dialog/<line>`; the six Nintendo WFC Battle descriptions use
`wii-dragon-net/menu-text/47` through `/52`; and the opponent-search screen uses
`wii-dragon-net/menu-text/53` and `/54`. The battle-format prompt and
player-name validation notices were written for the port; they are not Wii disc text.
The port's selected menu language chooses the subtitle bank and matching Wii artwork;
other menu languages fall back to English. Older extractions without localized artwork use the original
English sheets. The landscape, moving clouds, and river sparkles stay visible behind Wii submenus.
Section 0 resource 6 has
separate Japanese and English lip events for each. The matching ADX voices are entries 30787–30830 (Japanese) and
32159–32202 (English) in the Wii voice archive. The extractor pairs them in `home-dialog.bin` and
`voices/{ja,en}/000.adx` through `043.adx`, checking each recording against its own lip track.
The game's Sound Options voice choice selects the matching recording and lip track independently of subtitle language.
Pan's base pose has open eyes; its original closed-eye overlay blinks briefly. After the
first cursor movement she uses the Wii pointing pose with its own eye and mouth sprites. The Wii lip track gates the
mouth animation for either pose: the body texture supplies the widest mouth, the first overlay is half open, and
the second overlay is closed. The recorded lip track chooses speaking or a pause; the two speaking shapes alternate
while it is open. Giru's dim eye is used while idle and his bright eye follows his own lip track.
The home scene speaks Pan's introduction and Giru's reply. Moving the cursor speaks the original description of the
selected option. The other lower Wii menu pages use their original spoken descriptions too. Pan's mouth and Giru's eye
variants come from the same Wii art pack and change on their recorded lip events. The existing game message window
displays the extracted line. A screenshot is useful for comparison but is not loaded by the port. The Nintendo WFC
Battle page uses section 1's clock background, six blue and gold bars, label sheets, title, and Giru pose. It opens
after a fade; Giru enters from the left at his native 256×128 Wii texture size, and the title and crest play their header
animation after the fade. Pan does not appear or speak on this page. The battle entries sit below the header and
show neutral descriptions without character dialogue. The other lower Wii menu pages currently use the port's
drawn UI over the decoded landscape.
Focusing Ranking Battle or View Ranking shows the Wii Fighting Point panel on the left with `0000` as a placeholder.
The Custom Battle and Normal Battle pages use their extracted Wii titles at the same position and size.
Both format pages use the battle page's clock, Giru, bars, cursor, and fade.
The Nintendo WFC Battle, both format pages, and opponent-search page draw the
extracted nebula and both halves of the shimmering star field inside the clock's
circular window. Only the small central stars are shown; the large glowing rim
stars are hidden.
Each format page's first option starts at the same vertical position as the first option on the preceding battle page.
The opponent-search screen uses section 1 resource 3's blue and red VS panel halves, panel highlights,
1P/2P badges, VS emblem, and battle-record labels and digits. Both panels show Battles, Wins, and Losses
with `0` placeholders. Names sit above the records in the game's native message-window font.
The opponent-search screen keeps the selected format's title and the same clock background.
Giru enters fully on screen at the lower left after the fade.
The landscape cloud sheet begins at the Wii movie's (0, −10) position and moves left at a constant speed in a horizontal loop.
The saved player name is centered in the blue panel; the opponent's name appears in the red panel when found.
On the battle and format pages, each lower option starts 10.5 screen units farther right, following the original Wii menu's
stair-step layout; the radar cursor follows the selected row's horizontal position and shares its text pulse.
The five option labels have no matching Wii lettering sheet. They use the
Evolution Z style menu-label renderer with the selected Menu font; when it is
Original, they use the first available Menu font in `fonts.tsv`. The label
fill is 19 screen pixels high, with orange focused and purple idle states.
# Editable Dragon Net Battle text

The Dragon Net Battle screens read their entries and headers from
`gamedata/game-text/translations/port/en.json`. Edit the `text` value of an ID
beginning with `dragon-net/`, leaving its `original` and ID unchanged. This
includes the home, battle, custom battle, friends, settings, and ranking
headers and entries. Battle help and opponent-search descriptions use Wii disc
text and can be edited in the UI catalog. The
Create Player Name keyboard title always uses its Wii artwork.
Fighting Point always uses its Wii artwork. Restart the game to load changes.

With **Menu font: Original**, unchanged labels with Wii artwork use it;
edited labels and the Custom Battle entries use font text. With another Menu font,
art-backed labels and headers keep their Wii artwork unless the selected menu
language supplies translated port text for that entry. English menu text uses
the selected font in English. The Wii rows keep their screen
positions and the headers keep their entrance animation. Longer wording is
fitted to its available width. The screen reader uses the edited entries and
headers as well, while the keyboard title remains artwork.
Disabled font labels use the same dim tint as their Wii bars.
Font labels use Evolution Z's 19-pixel plate-caption letter height in both
states. Their focused layer replaces the idle layer during the four-frame
selection change, then continues a looping vertical stretch while focused.
The extracted texture labels on the home, battle, and settings pages use the
same focused-layer fade and vertical stretch. Their size now uses Evolution Z's
measured 19-pixel letter fill: the Wii home, battle, and settings sheets measure
17, 19, and 21 pixels, so they draw at 19/17, 19/19, and 19/21 of source size.
Both texture layers keep the original left edge and vertical center.
The Custom Battle text and fallback submenu entries use the same 19-pixel
letter height.
Evolution Z's heading has a 21-pixel letter height. Dragon Net Battle's Wii
title artwork and selected Menu font headings already match it; edited-title
fallback text now uses that measured height through the same title animation.
Text follows each bar's entrance motion. The first focused entry begins the
effect after the page's rows finish entering.

## Online battle results

A completed online battle fades out and tears down its battle scene before opening
results in the Dragon Net menu. The results reuse the searching-opponent screen's
clock/space background, animated mode heading (its entrance starts after fade-in,
matching the other Dragon Net pages; its source movie is sampled explicitly because
online boot skips the main menu), two player plates, VS mark, native
name rendering, and record labels. Results advance and attach the message window’s
text clip before drawing names, even though the message box itself is hidden. The session's battle and win/loss totals update
between rematches. Record columns share render dimensions. All three use identical glyphs and UV bounds from the record digit sheet. Wins uses
a grayscale base recolored with the pale-coral to salmon fill sampled from the Wins
label in texture 042, avoiding a cyan cast. Digit
tint channels stay at or below GS neutral (0x80) to avoid amplifying brightness. The extracted Wii resource 3 supplies the WIN badge (texture 23),
starburst/request badge (25), button lettering (3/4), and selection bars (17/18). Localized Wii
button sheets follow the same language lookup as the other menu artwork.

The in-battle online pause menu omits Return to Character Select (entry 0xD) and
Return to Main Menu (entry 0xF);
character selection is reached through mutual post-battle rematch requests.

Use left/right and Cross/A to choose Rematch Request or End Battle. Each request
shows its 1P/2P badge above its button. Both players must request a rematch. Once both agree, choices lock for three seconds
(180 menu frames), then the menu fades back to the stock single/team character
selection via battle-result reason 0x10. Players choose characters and a stage again;
the online session, lobby rules, and session records are retained.
Disabled Rematch uses the Dragon Net menu’s opaque RGB 0x58 tint on both its bar
and lettering, rather than fading their opacity.
An End Battle choice cancels requests, disables rematch, and keeps the results open
until both players acknowledge End Battle. Their badges appear above that button.
This sends a normal End Battle goodbye reason, so neither player sees the
“other player left the match” dialog. Unexpected disconnects keep their existing messages.
WIN and confirmed player badges pulse together with their starburst backings in a
90-frame loop (1.5 seconds), scaling by ±6% around fixed centers. The shared
starburst retains the extracted Wii texture’s original transparency and yellow outline.
The online bot waits five seconds (300 menu frames), then always requests a rematch after losing; otherwise it chooses once with an
80% chance of Rematch Request and 20% End Battle.
If the opponent ends, it acknowledges End Battle after its initial wait.

The sequence change is in `src/battle/btl_seq.c`; `src/battle/battle.c` runs the menu
after battle teardown, then returns to the ordinary menu flow for character selection. The screen and synchronized decision
state are in `src/menu/port_tbo.c`, with no results overlay or input handler in the
battle HUD. Existing synchronized controller input carries the choices. Offline
results retain the stock flow.

Validation: port compile/link and decision tests pass, including the 300-frame bot
wait, request cancellation, and mutual rematch. A debugger smoke test initializes
and tears down a battle, then runs twenty result-menu frames. An offscreen OpenGL
render was inspected for the Wii backdrop, title, plates, records, WIN artwork, and
button sheets. Full two-client rematch/disconnect testing remains outstanding.

Result button lettering follows Menu font using the same idle/selected rasterizer
and disabled tint as Dragon Net entries. Original retains the Wii lettering sheets;
translated or edited labels without Wii artwork use the font fallback. The labels
are editable as `dragon-net/results/rematch` and `dragon-net/results/end` in the
port translation catalog. Default text supports English, Spanish, German, French,
Italian, and Portuguese. Long labels fit within the button.

An incomplete room code shows “A Room Code has 6 letters and numbers.” in the same
dismissible dialog as connection loss. This applies to both room-code entry screens;
the keypad waits until Cross/A or Triangle closes the dialog, preserving the code.

A room code not found by matchmaking uses the same dismissible dialog, retaining
the entered code in the message “Room with code ABC123 not found.”

These message dialogs play the memory-card window’s native opening animation and
opening sound, then accept dismissal once the window is fully open.

Giru and his eye glow slide up together from below the viewport during dialog
opening, easing into the idle bob over 24 menu frames. Closing slides him down.

When the online bot is the stage chooser, it selects the native Random stage cell for both single
and team/DP battles. The game chooses an unlocked stage using synchronized randomness.
When a human is the stage chooser, stage selection stays under that human’s control.

Results use the Dragon Net cursor sound when changing buttons and confirmation
sound when the local player submits a new rematch request or End Battle decision.
Repeated confirmations of an existing request do not replay the sound.

Stage and music selection belongs to the host in the first match, then the previous
match’s winner. A draw retains the previous chooser. Character selection stays
independent. A winning bot also uses Random stage when it is the next chooser.

Stage and music chooser badges display 1P or 2P to match the current chooser.

Chooser markers use the extracted Wii 1P/2P badge drawn in the native marker’s
transformed shape bounds. The marker timeline continues to position and animate it;
its fixed 1P artwork is replaced rather than cropped as a presumed two-player atlas.

The bot selects the native Random music entry alongside Random stage whenever it
controls the stage/music choice, in both single and team/DP battles.

Stage/music chooser badges render at 75% of their native bounds, centered on the
same position for both 1P and 2P.
