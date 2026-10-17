// The setup's own installer for a release folder (native.cpp). It works on a thread and reports in the event lines
// of install.py.
#ifndef PORT_SETUP_NATIVE_H
#define PORT_SETUP_NATIVE_H
#include <string>

void Native_Start(const std::string &root, const std::string &iso); // root: the folder with bt3 and bt3.dat, ends in '/'
void Native_StartEu(const std::string &root, const std::string &iso, const std::string &isoEu,
                    bool wantItalian = false); // isoEu may be empty unless wantItalian
bool Native_Poll(std::string &line);                               // the next event line, if there is one
bool Native_Running(void);
void Native_Cancel(void);
void Native_Join(void);                                            // at exit: cancel and wait
bool Native_Installed(const std::string &root);                    // the game data is unpacked already
void Native_SetGameLang(const std::string &root, int lang);        // write game_lang into bt3_settings.txt (2 = Italian text)

#endif
