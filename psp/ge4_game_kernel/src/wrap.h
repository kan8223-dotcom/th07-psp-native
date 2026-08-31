#ifndef TH07_GE4_GAME_WRAP_H
#define TH07_GE4_GAME_WRAP_H

#ifdef __cplusplus
extern "C" {
#endif

int ge4ProbeGetModel(void);
unsigned int ge4ProbeGetEdramHwSize(void);
int ge4ProbeSetEdramSize(unsigned int size);

#ifdef __cplusplus
}
#endif

#endif
