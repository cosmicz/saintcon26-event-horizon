#include <stdio.h>
#include <time.h>
#include "eh/eh_scenes.h"
static eh_rgb565 fb[EH_FB_PIXELS]; static eh_tunnel_ws tw; static eh_terrain_ws te;
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC_RAW,&t);return t.tv_sec*1e6+t.tv_nsec/1e3;}
int main(void){ eh_camera c; eh_camera_init(&c); eh_tunnel_init(&tw,0); eh_terrain_init(&te);
 for(int seg=0;seg<8;seg++){ double s=0; int n=0; for(uint32_t t=seg*2000;t<seg*2000+2000&&t<16000;t+=33){ double a=now(); eh_terrain_render(&te,fb,t,16000,&c); s+=now()-a; n++;} printf("terrain %5u-%5u ms mean %.1f us\n",seg*2000,seg*2000+2000,s/n);}
 for(int seg=0;seg<6;seg++){ double s=0; int n=0; for(uint32_t t=seg*2000;t<seg*2000+2000;t+=33){ double a=now(); eh_tunnel_render(&tw,fb,t,12000,&c); s+=now()-a; n++;} printf("tunnel  %5u-%5u ms mean %.1f us\n",seg*2000,seg*2000+2000,s/n);}
}
