Dragon Ball Z Budokai Tenkaichi 3 with a screen reader

To install the game:
1. Put this zip and your disc image of the game (the file that ends in .iso) in the same folder.
2. Extract the zip: right-click it, "Extract All", then "Extract".
3. Go into the Tenkaichi3Decomp folder and open Tenkaichi3Decomp-setup.

The setup finds the image by itself, installs the game in a minute or two, tells you how it goes through your screen reader and opens the game at the end. If it says it found no image, copy the .iso file into the Tenkaichi3Decomp folder with the setup still open: it finds it by itself once the copy has ended.

To play later, open Tenkaichi3Decomp. Opening the game before it is installed opens the setup.

If Windows says "Windows protected your PC", choose "More info" and then "Run anyway". It says so because the program has no digital signature.

On a Mac (an experimental version, not yet tried on a Mac): it runs only on Macs with Apple's chip (M1 or later) and macOS 11 or later; on Intel Macs it does not open.
1. Extract the zip with a double click, beside your disc image.
2. macOS does not open programs that Apple did not sign, and this one is not. To let it, once: in the Finder, select the Tenkaichi3Decomp folder and press Command+Option+C, which copies its path.
3. Open Terminal (Command+Space, type Terminal and press Return). Type xattr -dr com.apple.quarantine, a space and a double quote; paste with Command+V; type another double quote and press Return.
4. Open Tenkaichi3Decomp-setup, inside the folder. A Terminal window opens with it: do not close it while you play.
On a Mac the game speaks with the system's own voice, apart from VoiceOver, in the system's language, and sends no braille. Its volume follows the Mac's volume. If macOS asks whether it may use your Personal Voice, any answer will do; until you answer, the game does not speak. With VoiceOver running, the blind player profile is turned on by itself the first time. The Mac version does not update itself: each new release is downloaded by hand.

You need your own disc image of the USA release (SLUS-21678), with the game's programs unmodified. Do not share or ask for disc images.

In the game:
- Nothing has to be pressed to know where the rival is: in a fight the game says it by itself, with a sonar ping from where the rival is (more often the nearer) and with the reader each time it changes ("Rival behind, to the left, far"). In the Accessibility tab, "Where the rival is" chooses how: only with Select, or by itself with a sound, the reader or both.
- The controller's Select (Create on a DualSense, View on an Xbox controller), in a fight: once, where the rival is; twice quickly, your gauges (health, ki and blast); three times, the rival's. When two people play, each hears their own first, on their own controller. Outside a fight it says the last message again. A GameCube controller has no Select: F2, F3, F4 and F5 do it there. With the reader on, Select no longer moves the camera in a fight.
- At first the gauges are two beeps, louder the less health is left. For the screen reader to say health, ki and blast: settings window, Accessibility tab, "Gauges: health, ki and blast". The choices "also by itself" say them too by themselves, each time a whole bar or a blast stock comes or goes, without pressing Select.
- What tells of the rival comes from where the rival is, like the sonar: the distance tick and, against the computer or online, the rival's health, ki, transformation and Ultimate Blast sounds. From behind it sounds muffled. Your own sounds stay in the middle. The rival's sounds have a buzz over them, to tell whose they are. While the rival cannot be seen (a vanish, or you are blinded), everything sounds in the middle, and so it does with the game's sound in mono. When two people play at the same computer, each one's health, ki and cues sound in the middle.
- If you would rather never press Select, the "Everything by itself" button in the Accessibility tab sets every alert to a sound and the reader, and the gauges and where the rival is told by themselves. Each one can be changed afterwards.
- On the keyboard: F2 says the last message again. F3 and F4 give player 1's and player 2's gauges. F5 says where the rival is.
- The settings window opens with F1, or with the controller's two middle buttons together (Create and Options on a DualSense, View and Menu on an Xbox controller). On a slider, the right stick lowers or raises its value.
- Its Accessibility tab has the fight's alerts: where the rival is, distance to the rival, edge of the stage, health, transformation and Ultimate Blast, gauges and important moments, with their volumes. With NVDA or JAWS the blind player profile is switched on by itself the first time; without them, distance, edge and health come switched off: switch them on there.
- In an online match the reader says what the rival writes in the chat, with their name. It is switched off in the Accessibility tab, "Read the online chat".
- In a fight, the reader tells the important moments of both players by itself: when one is down to 2 bars of health and then 1, when the ki is full and when max power starts. It also says the names of the techniques the game shows. Both are set in the Accessibility tab ("Important moments" and "Technique names").
- To learn what each sound means: in the Accessibility tab, the "Sound glossary" button opens the list; each button plays its sound and says what it means. In a fight's pause, Select does the same one sound at a time; twice quickly, the one before.
- While a fight's opening plays, the reader says who fights whom and on which stage ("Describe the fight", in the Accessibility tab).
- In the pause menu, the skill list is read one move at a time, with the buttons of your controller.

Playing with the keyboard (player 1; keyboard and controller work together):
- Arrows: the d-pad. W, A, S and D: the left stick.
- K: Cross. L: Circle. J: Square. I: Triangle.
- U: L1. O: R1. 7 (on the number row): L2. 9: R2.
- Enter: Start. Right Shift: Select.
- F1: settings window. F2: say the last message again. F3 and F4: player 1's and player 2's gauges. Escape: asks whether to close the game.
- L3, R3 and the right stick have no key. To transform (R3) and to switch fighters (L3 and R3 together), give them keys first: F1, Controls tab, the row of R3 (or L3), Keyboard column, Enter and then the key (for example P for R3 and Y for L3).
- In a fight, with the reader on, Right Shift does nothing: F3 and F4 do it there.

Controllers:
- When a controller is plugged in, the game says its name and which player it goes to, and that controller vibrates once (player 2's twice). Unplugging one is said too. A few seconds after the game opens it says which controllers there are, or that there is none and the keyboard plays.
- The game carries the community's controller database (gamecontrollerdb.txt), which knows many generic USB controllers.
- If it says a controller "is not recognized" and was given the most common layout, try its buttons. A PlayStation-style controller with its ANALOG light off is recognized too: its d-pad moves the left stick.
- If a button does not match, or the game says a controller is not recognized, set it up by voice: F1, Controls tab, "Set up a controller" button. Press any button on the controller to set up, then each button and stick as the reader asks for it ("Press Cross: of the four buttons on the right, the one nearest to you"). A part the controller does not have: wait ten seconds and it is skipped. Escape stops. At the end the controller is set up, and it is kept in gamecontrollerdb_own.txt for the next times. If it came out wrong, set it up again; to go back to how it came, delete gamecontrollerdb_own.txt from the game's folder.
- If a controller still does not work, tell us: write a11y_log=1 in bt3_settings.txt, open the game with the controller plugged in and send us the file a11y_log.txt. It has the controller's name, its code (VID:PID) and what was set up.
- Player 1 is the controller you press: if player 1's controller has not been used, the first controller you press becomes theirs, and the game says so. It helps when Windows lists first a controller nobody holds: a virtual one of DS4Windows, HidHide, ViGEm or a handheld's own software. To switch it off, write pad_follow=0 in bt3_settings.txt.
- If your controller has an XInput mode (many have a Mode or Home button held a few seconds, or an X/D switch), in that mode it works as an Xbox controller.
- Your own SDL mappings go into gamecontrollerdb_own.txt, in the game's folder: no update replaces it, and its lines win over the database's.

If the controller does things you did not press (a button pauses, or opens the question to close the game): a program turns the controller's buttons into keys, and the game gets both. It can be Steam (A into Enter, B into Escape, by its "desktop layout"), DS4Windows, JoyToKey, reWASD, or a handheld's own software (Armoury Crate, Legion Space). The game now notices by itself: it leaves those keys out, and after three it says so and does not read the keyboard while you use the controller. It is still better to close that program while you play, or in Steam: Settings, Controller, Desktop Layout, Edit, and "Disable Steam Input".

The screen reader works with NVDA and JAWS; without them it uses Windows' own voice. It speaks through Prism (MPL-2.0; its source is at https://github.com/ethindp/prism; its licences are in licenses/prism). The language is Windows' language; for another one, write a11y_lang="en" (or the code you want) in bt3_settings.txt.
