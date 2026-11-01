// BT3 a11y: what the accessibility branch adds to the setup's screen reader (setup_voice.cpp), through the game's one
// (port/src/gs/a11y.c), and the disc image found in the setup's folder or the two above it.
#ifndef PORT_SETUP_VOICE_H
#define PORT_SETUP_VOICE_H
#include <string>
#include <vector>

void Voice_Start(const std::string &root);              // starts the game's screen reader (PortA11y_Begin)
bool Voice_On(void);                                      // it is on: there is someone to speak to
void Voice_Say(const std::string &text, bool interrupt);  // UTF-8; nothing while it is off
const char *Voice_Tr(const char *en);                     // the language's line for an English text, or the text
std::string Voice_Words(const std::string &en);           // the same for a setup text with parts of its own (kPatterns)
std::vector<std::string> Voice_FindIsos(const std::string &root); // the disc images of root and the two folders above, best first

#endif
