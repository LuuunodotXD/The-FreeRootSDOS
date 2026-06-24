// drivers/video/video.h
#ifndef VIDEO_H
#define VIDEO_H
typedef enum { VIDEO_TEXT, VIDEO_VGA12H, VIDEO_VGA13H, VIDEO_VESA } VideoMode;

typedef struct {
    const char *name;
    int  (*init)(void);
    void (*set_mode)(VideoMode mode);
} VideoDriver;

void video_register(VideoDriver *drv);
void video_init(void);
void video_set_mode(VideoMode mode);
#endif

