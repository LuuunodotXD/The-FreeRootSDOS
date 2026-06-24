// drivers/video/video.c
#include "video.h"
static VideoDriver *active = 0;
static VideoDriver *drivers[4]; static int ndrv = 0;

void video_register(VideoDriver *d) { if (ndrv<4) drivers[ndrv++]=d; }
void video_init(void) {
    for (int i=0;i<ndrv;i++) if (!drivers[i]->init||drivers[i]->init()==0)
        { active=drivers[i]; return; }
}
void video_set_mode(VideoMode m) { if (active&&active->set_mode) active->set_mode(m); }
