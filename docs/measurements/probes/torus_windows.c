#include <stdio.h>
#include <time.h>
#include "eh/eh_scenes.h"
static eh_rgb565 fb[EH_FB_PIXELS]; static eh_torus_ws tw;
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC_RAW,&t);return t.tv_sec*1e6+t.tv_nsec/1e3;}
int main(void){ eh_camera c; eh_camera_init(&c); eh_torus_init(&tw,0);
 for(int seg=0;seg<7;seg++){ double s=0, mx=0; int n=0; for(uint32_t t=seg*2000;t<seg*2000+2000;t+=33){ double a=now(); eh_torus_render(&tw,fb,t,14000,&c); double d=now()-a; s+=d; if(d>mx)mx=d; n++;} printf("torus %5u-%5u ms mean %.1f max %.1f us\n",seg*2000,seg*2000+2000,s/n,mx);}
}
