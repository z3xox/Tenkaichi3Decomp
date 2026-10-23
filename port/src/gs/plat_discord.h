#ifndef PLAT_DISCORD_H
#define PLAT_DISCORD_H

#ifdef __cplusplus
extern "C" {
#endif

void PortDiscord_Init(void);
void PortDiscord_Tick(void);
void PortDiscord_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* PLAT_DISCORD_H */