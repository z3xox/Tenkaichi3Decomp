#ifndef BT3_UI_TEXT_EXTRACT_H
#define BT3_UI_TEXT_EXTRACT_H

/* Generate the English UI/dialog reference catalog from the extracted disc files.
   Work runs in a detached thread so it does not hold up the first menu. */
void UiText_ExtractOnStartup(const char *gameTextDir);

#endif
