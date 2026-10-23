#!/usr/bin/env python3
"""Run a headless CPU opponent over direct LAN or a Friend Battle room code."""

import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import threading


ROOT = Path(__file__).resolve().parents[2]
MODES = {
    "1v1": "S",
    "team": "T",
    "dp10": "D10",
    "dp15": "D15",
    "dp20": "D20",
}
ROOM_CODE = re.compile(r"[A-Z0-9]{6}\Z")
LOBBY_CODE = re.compile(r"bt3: lobby: state -?\d+, code '([A-Z0-9]{6})'")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", choices=MODES, help="optional mode to start immediately")
    startup = parser.add_mutually_exclusive_group()
    startup.add_argument("--join-code", metavar="CODE", help="join a Friend Battle room code immediately")
    startup.add_argument("--generate-code", action="store_true", help="host a Friend Battle room and print its code")
    parser.add_argument("--host", default="127.0.0.1", help="address of the human player's game")
    parser.add_argument("--name", default="SparkingBot", help="name shown to the other player")
    parser.add_argument("--level", type=int, choices=range(1, 6), default=3,
                        metavar="1-5", help="CPU difficulty, 1 easiest and 5 hardest (default: 3)")
    parser.add_argument("--game", type=Path, default=ROOT / "port/build/bt3_64", help="built game executable")
    args = parser.parse_args()
    game = args.game.resolve()
    if not game.is_file():
        parser.error(f"game executable not found: {game}")
    if ":" in args.host or not args.host:
        parser.error("--host must be an IPv4 address or hostname without a port")
    if args.join_code and args.mode:
        parser.error("--join-code does not take a battle mode; the host supplies it")
    if args.join_code and not ROOM_CODE.fullmatch(args.join_code.upper()):
        parser.error("--join-code must contain six letters or numbers")
    if args.generate_code and not args.mode:
        parser.error("--generate-code requires a mode such as 1v1 or team")

    process: subprocess.Popen[str] | None = None
    selected_level = args.level
    current_request: tuple[str, str] | None = None

    def read_game_log(child: subprocess.Popen[str]) -> None:
        assert child.stderr is not None
        for line in child.stderr:
            match = LOBBY_CODE.search(line)
            if match:
                print(f"\nRoom code: {match.group(1)} — enter it under Friend Battle > Join Battle.", flush=True)
            print(line, file=sys.stderr, end="", flush=True)

    def stop_game() -> None:
        nonlocal process, current_request
        current_request = None
        if process is None:
            return
        if process.poll() is None:
            try:
                if os.name == "posix":
                    os.killpg(process.pid, signal.SIGTERM)
                else:
                    process.terminate()
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    if os.name == "posix":
                        os.killpg(process.pid, signal.SIGKILL)
                    else:
                        process.kill()
                except ProcessLookupError:
                    pass
                process.wait()
        process = None

    def start_game(request: tuple[str, str]) -> None:
        nonlocal process, current_request
        stop_game()
        env = os.environ.copy()
        for name in ("BT3_REPLAY", "BT3_DEMO", "BT3_NET_HOST", "BT3_NET_JOIN", "BT3_LOBBY"):
            env.pop(name, None)
        env["BT3_NET_BOT"] = "1"
        env["BT3_NET_BOT_LEVEL"] = str(selected_level - 1)
        env["BT3_NET_NAME"] = args.name
        kind, value = request
        if kind == "lan":
            env["BT3_LOBBY"] = f"lan:join:{args.host}:{MODES[value]}"
        elif kind == "host":
            env["BT3_LOBBY"] = f"host:{MODES[value]}"
        else:
            env["BT3_LOBBY"] = f"join:{value}"
        env["BT3_GS"] = "none"
        env["BT3_NOSOUND"] = "1"
        process = subprocess.Popen([str(game)], cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE, text=True, bufsize=1,
                                   start_new_session=os.name == "posix")
        threading.Thread(target=read_game_log, args=(process,), daemon=True).start()
        current_request = request
        if kind == "host":
            print(f"Hosting {value} at CPU level {selected_level}; waiting for a room code...", flush=True)
        elif kind == "join":
            print(f"Joining room {value} at CPU level {selected_level}.", flush=True)
        else:
            print(f"Headless bot running in {value} mode at CPU level {selected_level}, joining {args.host}.", flush=True)

    help_text = "Commands: 1v1, team, dp10, dp15, dp20 (LAN); host MODE (generate code); join CODE; level 1-5; exit (disconnect); quit"
    print(help_text, flush=True)
    try:
        if args.join_code:
            start_game(("join", args.join_code.upper()))
        elif args.generate_code:
            start_game(("host", args.mode))
        elif args.mode is not None:
            start_game(("lan", args.mode))
        while True:
            try:
                command = input("bot> ").strip().lower()
            except EOFError:
                break
            if command == "exit":
                stop_game()
                print("Disconnected. Enter a mode, host MODE, or join CODE to connect again.", flush=True)
                continue
            if command == "quit":
                break
            words = command.split()
            if not words:
                continue
            if words[0] == "level" and len(words) == 2 and words[1] in ("1", "2", "3", "4", "5"):
                selected_level = int(words[1])
                if current_request is not None:
                    start_game(current_request)
                else:
                    print(f"CPU level set to {selected_level}")
            elif words[0] in MODES and len(words) in (1, 2) and (len(words) == 1 or words[1] in ("1", "2", "3", "4", "5")):
                if len(words) == 2:
                    selected_level = int(words[1])
                start_game(("lan", words[0]))
            elif words[0] == "host" and len(words) == 2 and words[1] in MODES:
                start_game(("host", words[1]))
            elif words[0] == "join" and len(words) == 2 and ROOM_CODE.fullmatch(words[1].upper()):
                start_game(("join", words[1].upper()))
            elif command == "help":
                print(help_text)
            elif command:
                print(f"Unknown command: {command}. Type help for available commands.")
    except KeyboardInterrupt:
        print()
    finally:
        stop_game()


if __name__ == "__main__":
    main()
