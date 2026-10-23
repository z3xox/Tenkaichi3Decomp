/* Tenkaichi Budokai Online: its pictures, drawn by the renderer at their own resolution. Each is tbo/ui/<name>.png
   next to the program; the game asks for one by its number (src/menu/port_tbo.c), the renderer reads and keeps it
   (port/src/gs/gs_draw.c, tbo_texture). Plain C, no other headers: both sides include it. The PC hub pictures are
   made by port/build/ml/tbo_assets.py; Wii pictures are decoded by port/tools/extract_wii_online.py. */
#ifndef TBO_ART_H
#define TBO_ART_H

#define TBO_ART_LIST(X) \
    X(TBO_BG, "bg")                         /* the tournament grounds, 16:9 */ \
    X(TBO_CREST, "crest")                   /* the tournament's crest */ \
    X(TBO_HOSTS_MIC_WAVE, "hosts_mic_wave") /* Goten and Trunks, one picture per pose */ \
    X(TBO_HOSTS_BINOCULARS, "hosts_binoculars") \
    X(TBO_HOSTS_ARMS_CROSSED, "hosts_arms_crossed") \
    X(TBO_HOSTS_PUZZLED, "hosts_puzzled") \
    X(TBO_HOSTS_CHEER, "hosts_cheer") \
    X(TBO_HOSTS_THUMBS_UP, "hosts_thumbs_up") \
    X(TBO_KIT_SEL, "kit_sel")               /* the interface pieces: the chosen bar, */ \
    X(TBO_KIT_BAR, "kit_bar")               /* a bar, */ \
    X(TBO_KIT_DIALOG, "kit_dialog")         /* the hosts' box, */ \
    X(TBO_KIT_WINDOW, "kit_window")         /* a window, */ \
    X(TBO_KIT_GOLD, "kit_gold")             /* the gold capsule, */ \
    X(TBO_KIT_STEEL, "kit_steel")           /* the steel capsule */ \
    X(TBO_BTN_CROSS, "btn_cross")           /* the pad's buttons, in the order of the prompts' kinds */ \
    X(TBO_BTN_SQUARE, "btn_square") \
    X(TBO_BTN_TRIANGLE, "btn_triangle") \
    X(TBO_BTN_LEFTRIGHT, "btn_leftright") \
    X(TBO_BTN_CIRCLE, "btn_circle") \
    X(TBO_BTN_START, "btn_start") \
    X(TBO_WII_BG_0, "wii_bg_0") \
    X(TBO_WII_BG_1, "wii_bg_1") \
    X(TBO_WII_BG_2, "wii_bg_2") \
    X(TBO_WII_BG_3, "wii_bg_3") \
    X(TBO_WII_BG_4, "wii_bg_4") \
    X(TBO_WII_BG_5, "wii_bg_5") \
    X(TBO_WII_BG_6, "wii_bg_6") \
    X(TBO_WII_BG_7, "wii_bg_7") \
    X(TBO_WII_PAN, "wii_pan") \
    X(TBO_WII_PAN_EYES_OPEN, "wii_pan_eyes_open") \
    X(TBO_WII_PAN_EYES_CLOSED, "wii_pan_eyes_closed") \
    X(TBO_WII_PAN_MOUTH_OPEN, "wii_pan_mouth_open") \
    X(TBO_WII_PAN_MOUTH_CLOSED, "wii_pan_mouth_closed") \
    X(TBO_WII_PAN_POINT, "wii_pan_point") \
    X(TBO_WII_PAN_POINT_EYES_CLOSED, "wii_pan_point_eyes_closed") \
    X(TBO_WII_PAN_POINT_MOUTH_OPEN, "wii_pan_point_mouth_open") \
    X(TBO_WII_PAN_POINT_MOUTH_CLOSED, "wii_pan_point_mouth_closed") \
    X(TBO_WII_GIRU, "wii_giru") \
    X(TBO_WII_GIRU_EYE_BRIGHT, "wii_giru_eye_bright") \
    X(TBO_WII_GIRU_EYE_DIM, "wii_giru_eye_dim") \
    X(TBO_WII_CLOUDS, "wii_clouds") \
    X(TBO_WII_SPARKLE_0, "wii_sparkle_0") \
    X(TBO_WII_SPARKLE_1, "wii_sparkle_1") \
    X(TBO_WII_RADAR, "wii_radar") \
    X(TBO_WII_BAR_BLUE, "wii_bar_blue") \
    X(TBO_WII_BAR_GOLD, "wii_bar_gold") \
    X(TBO_WII_CREST, "wii_crest") \
    X(TBO_WII_TITLE, "wii_title") \
    X(TBO_WII_LABEL_SELECTED, "wii_label_selected") \
    X(TBO_WII_LABEL_IDLE, "wii_label_idle") \
    X(TBO_WII_SETTINGS_LABEL_SELECTED, "wii_settings_label_selected") \
    X(TBO_WII_SETTINGS_LABEL_IDLE, "wii_settings_label_idle") \
    X(TBO_WII_NAME_TITLE, "wii_name_title") \
    X(TBO_WII_NAME_BUTTON_LABELS, "wii_name_button_labels") \
    X(TBO_WII_NAME_KEYBOARD, "wii_name_keyboard") \
    X(TBO_WII_NAME_GLYPHS, "wii_name_glyphs") \
    X(TBO_WII_NAME_KEY_HIGHLIGHT, "wii_name_key_highlight") \
    X(TBO_WII_NAME_KEY_WIDE_HIGHLIGHT, "wii_name_key_wide_highlight") \
    X(TBO_WII_NAME_DELETE_X, "wii_name_delete_x") \
    X(TBO_WII_NAME_CURSOR, "wii_name_cursor") \
    X(TBO_WII_NAME_INPUT_FRAME, "wii_name_input_frame") \
    X(TBO_WII_SETTINGS_TITLE, "wii_settings_title") \
    X(TBO_WII_BATTLE_TITLE, "wii_battle_title") \
    X(TBO_WII_BATTLE_BAR_BLUE, "wii_battle_bar_blue") \
    X(TBO_WII_BATTLE_BAR_GOLD, "wii_battle_bar_gold") \
    X(TBO_WII_BATTLE_LABEL_IDLE, "wii_battle_label_idle") \
    X(TBO_WII_BATTLE_LABEL_SELECTED, "wii_battle_label_selected") \
    X(TBO_WII_BATTLE_BG_0, "wii_battle_bg_0") \
    X(TBO_WII_BATTLE_BG_1, "wii_battle_bg_1") \
    X(TBO_WII_BATTLE_BG_2, "wii_battle_bg_2") \
    X(TBO_WII_BATTLE_BG_3, "wii_battle_bg_3") \
    X(TBO_WII_BATTLE_BG_4, "wii_battle_bg_4") \
    X(TBO_WII_BATTLE_BG_5, "wii_battle_bg_5") \
    X(TBO_WII_BATTLE_BG_6, "wii_battle_bg_6") \
    X(TBO_WII_BATTLE_BG_7, "wii_battle_bg_7") \
    X(TBO_WII_BATTLE_NEBULA, "wii_battle_nebula") \
    X(TBO_WII_BATTLE_STARS_0, "wii_battle_stars_0") \
    X(TBO_WII_BATTLE_STARS_1, "wii_battle_stars_1") \
    X(TBO_WII_BATTLE_GIRU, "wii_battle_giru") \
    X(TBO_WII_BATTLE_GIRU_EYE_BRIGHT, "wii_battle_giru_eye_bright") \
    X(TBO_WII_BATTLE_GIRU_EYE_DIM, "wii_battle_giru_eye_dim") \
    X(TBO_WII_BATTLE_POINT_PANEL, "wii_battle_point_panel") \
    X(TBO_WII_BATTLE_POINT_LABEL, "wii_battle_point_label") \
    X(TBO_WII_BATTLE_POINT_DIGITS, "wii_battle_point_digits") \
    X(TBO_WII_CUSTOM_TITLE, "wii_custom_title") \
    X(TBO_WII_NORMAL_TITLE, "wii_normal_title") \
    X(TBO_WII_VS_PANELS, "wii_vs_panels") \
    X(TBO_WII_VS_MARK, "wii_vs_mark") \
    X(TBO_WII_VS_LINES, "wii_vs_lines") \
    X(TBO_WII_VS_PLAYERS, "wii_vs_players") \
    X(TBO_WII_VS_DIGITS, "wii_vs_digits") \
    X(TBO_WII_VS_WINS_DIGITS, "wii_vs_wins_digits") \
    X(TBO_WII_VS_RECORD_LABELS, "wii_vs_record_labels") \
    X(TBO_WII_RESULT_WIN, "wii_result_win") \
    X(TBO_WII_RESULT_BADGE, "wii_result_badge") \
    X(TBO_WII_RESULT_LABEL_IDLE, "wii_result_label_idle") \
    X(TBO_WII_RESULT_LABEL_SELECTED, "wii_result_label_selected") \
    X(TBO_WII_RESULT_BAR_BLUE, "wii_result_bar_blue") \
    X(TBO_WII_RESULT_BAR_GOLD, "wii_result_bar_gold") \
    X(TBO_WII_DISCONNECT_GIRU, "wii_disconnect_giru") \
    X(TBO_WII_DISCONNECT_GIRU_EYE, "wii_disconnect_giru_eye")

#define TBO_ART_ENUM(id, name) id,
enum { TBO_ART_LIST(TBO_ART_ENUM) TBO_ART_COUNT };
#undef TBO_ART_ENUM

/* The texture format that marks a draw as one of these pictures (a reserved PSM value the GS has no format for and
   the game never uses); the picture's number is in the palette address (CBP), the size given is 1024 x 1024 so
   that UVs 0..1024 cover the whole picture whatever its own size. */
#define TBO_PSM 0x05

/* Tracks in gamedata/dragon-net-battle/home-motion.bin, decoded from the Wii FOD timeline. */
enum {
    TBO_WII_MOTION_BARS_X,
    TBO_WII_MOTION_PAN_ENTRY_X,
    TBO_WII_MOTION_PAN_BOB_Y,
    TBO_WII_MOTION_RADAR_SCALE,
    TBO_WII_MOTION_GIRU_X,
    TBO_WII_MOTION_GIRU_Y,
    TBO_WII_MOTION_SPARKLE_0,
    TBO_WII_MOTION_SPARKLE_1
};

/* The lines of letters (port/src/gs/tbo_text.c) are drawn the same way: numbers TBO_TEXT_BASE and on, one per line
   the screens write, the picture made from a TrueType font of the font list. */
#define TBO_TEXT_BASE 0x100
#define TBO_TEXT_SLOTS 128

/* Native artwork for the first-run language selector, decoded from the player's EU disc. */
#define TBO_LANG_BG 0x200
#define TBO_LANG_ARROW_LEFT 0x201
#define TBO_LANG_ARROW_RIGHT 0x202

#endif
