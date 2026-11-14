# Lobbies, next protocol: matches side by side, watching any of them, more than eight

A PLAN, not built. Asked by the user on 2026-11-14: a tournament's matches should be able to run at the same time,
anyone in the lobby (in the bracket or not) should be able to watch any match that is on, and a lobby should hold
more than eight. This file is where a new session starts from; update it as steps land.

Nothing below has been tried. Where it says how today's code works, that was read in `port/src/gs/net_lobby.c`
(its heading comment and the functions named) on that day; read them again before relying on a line.

## What there is today (LOBBY_VERSION 3)

- **One lobby, one match.** The owner's game is the lobby: eight places (`LOBBY_SLOTS`), `sL.phase` 0 or 1,
  `sL.gen` the match's number, `sL.players[2]`. A tournament's bracket is at most eight (`LOBBY_TOUR`, `TOUR`).
- **Who carries a match to those who watch: the owner.** On `T_L_GO` the game in the first place hosts, the second
  joins, everyone else is a spectator *of the owner*, "whose game passes the match on to them whatever its own part
  is". So the owner is in every match, as a player or as a spectator, and a second match at the same time has
  nobody to carry it.
- **The lobby rides on the match's socket.** In a session the lobby's packets are read by net.c's look at the socket
  (`Lobby_Packet`, `Lobby_Tick`); outside one by `Lobby_Pump`. A member in a match still answers the owner (ALIVE).
- **Fixed sizes on the wire.** STATE is 620 bytes for eight places and an eight-leaf bracket (`STATE_BYTES`,
  `tour_put`); HELLO 80; the view (`PortLobbyView`) and the screens (`src/menu/port_tbo.c`: `LB_ROWS`, the bracket's
  leaf boxes) are laid out for eight.
- **Results**: each player's game says how its match's first battle ended with its ALIVE (`resGen`, `res`); the owner
  marks the winner when the two agree (`result_watch`, `t_resolve`).
- **Outside the lobby's version**: the players' pictures (types 42, 43) and squads (a tag in the name) changed no
  packet of version 3.
- **A match of two without a lobby** already lets its *host* take up to six spectators (net.c, "Spectators":
  `T_SPEC_HELLO` ... `T_SPEC_READY`). That is the piece to build on.

## What is wanted

1. **Matches side by side**: every match of a round whose two entrants are known and present can be on at once.
2. **Watch any match**: a member not playing chooses which match to watch, and can change; someone who joins the
   lobby during a tournament too.
3. **More than eight** in a lobby and in a bracket (16 first; 32 if the sizes allow).

## Shape proposed (LOBBY_VERSION 4)

- **A match is its host's.** Each match has its own number, its two players and its host (the entrant on the left of
  the bracket, as now). The host's game takes that match's spectators itself, with the code a match of two already
  has; the owner no longer carries matches, it only keeps the lobby. The owner can play, watch one, or stay on the
  lobby's page.
- **The owner keeps a table of matches** in place of `phase` / `gen` / `players`: per match its number, bracket
  match (or -1 for a versus match), the two members, the host's addresses, its state (asked, connecting, on, over),
  how many watch. `MATCHES` = half the bracket's leaves.
- **GO names one match** and a part in it (host, joiner); **WATCH** (new, member -> owner -> host's addresses back)
  is how a member asks to watch match N; the member then greets that match's host as a spectator does today.
  Leaving a match being watched brings the member back to the lobby's page, not everyone.
- **BACK ends one match** (its number), for its players and its spectators only.
- **ALIVE says which match** the member is in and as what; results stay per match number, as now.
- **STATE in parts.** One packet no longer holds the lobby: the places (N x (id, ready, name)), the rules, the
  bracket (leaves, winners), the matches' table, each as its own part with a sequence number, so that 16 or 32
  places stay under one UDP packet each (keep every part below about 1200 bytes) and a part is sent again alone when
  only it changed. The view is put together from the parts a member has.
- **Sizes**: `LOBBY_SLOTS` and `LOBBY_TOUR` become 16 (or 32), the bracket's leaves 2 / 4 / 8 / 16 (/ 32); the byes'
  tables (`kByePairs4`, `kByePairs8`) get a 16's. Places are "players" (the bracket's entrants) and the rest.
- **Spectators per match**: a host takes six today. With 16 in a lobby and one match left on (the final), fourteen
  may want it: either raise the host's limit (it sends each spectator the inputs; measure the cost first), or let a
  spectator pass the match on to others (a chain: more delay, more to go wrong). Decide after measuring; first
  version: raise the limit, and say "this match's stands are full" past it.

## The screens

- **Lobby page**: sixteen places do not fit as rows of today's height: two columns, or a list that scrolls. The
  match list ("on now": A vs B, watch) is new.
- **Bracket page**: sixteen leaves, eight a side; "Start Next Match" becomes "Start the round's matches" (all that
  can start) beside starting one; a match that is on can be chosen and watched from here.
- **In a match being watched**: a way to another match without going through the lobby (the spectator's menu,
  `port/src/spec_menu.c`).

## Steps, each one playable by itself

1. **Read and write down** how a spectator of a match of two connects and is fed (net.c: `spec_packet`, the host's
   side), and how a lobby's member enters and leaves a session (`NetIo_Go`, `Lobby_SessionEnded`, state.c's return
   to the moment before). The plan above assumes a lobby's match can use the first as it is: confirm or correct.
2. **Version 4 with one match still**: the matches' table with one entry, GO / BACK / ALIVE by match number,
   spectators fed by the match's host instead of the owner. No new feature; the tests of today must pass
   (`BT3_LOBBY_DO`: a versus match, a four-entrant tournament to its champion, a rejoin).
3. **Two matches at once** in a four-entrant tournament (both semifinals). Results, walkovers, a held place (90 s)
   and "the two games disagree" per match.
4. **Watching**: WATCH, the match list, changing match, a newcomer during a tournament.
5. **STATE in parts, sixteen places**, the screens for sixteen. (Before 3 if the sizes get in the way.)
6. **More spectators per match** (measure, then raise or chain).

## Tests there are to lean on

`BT3_LOBBY=lobby:<port>` / `lobbyjoin:<address>:<port>` and `BT3_LOBBY_DO` (wait, rule, tour, next, waittour, ...)
drive a lobby of several copies on one machine without a button pressed; `BT3_TBO=1` with `BT3_SHOT_VBLANK` takes a
picture of the owner's screen; `BT3_TBO_PAGE` opens a page. A match played to its end needs the players fed buttons
(`BT3_PAD_TABLE`, the random-press table of docs/port/README.md "Tournaments": how its tests were run): find out how
before step 2, a tournament has not been played to its end on this Mac yet.

## What can go wrong

- **Every copy must be the same release**: version 4 does not talk to version 3 (as 3 did not to 2). The fork's
  friends all update together.
- **The owner leaving** closes the lobby and every match, as now. With matches side by side that is more people
  thrown out at once; a lobby that outlives its owner is a larger change and is not in this plan.
- **Routers**: a match's host must be reachable by its opponent and by its spectators. Today the owner is the one
  everybody already reaches; a host that only the relay reaches will carry its spectators through the relay too.
  The fork's matchmaking has no relay (port/matchmaking/supabase): by room code, players behind strict routers may
  not be able to host. Say so on the screen rather than fail silently.
- **Load on a host**: playing with rollback and feeding spectators at once. Measure with the meter (F1, Video).
- **Not yet played here**: a whole tournament, two machines, Mac against Windows or Linux. Do those with version 3
  first; a fault found after the rewrite is harder to place.

## Open questions for the user

- Sixteen, or thirty-two?
- Side by side always, or the owner's choice per round ("one at a time" stays for a final everyone watches)?
- Is a versus lobby (no bracket) to have several matches too, as tables people sit at?
