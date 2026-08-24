#include <stdio.h>
#include <stdint.h>
#include "game.h"
int main(void){
    static uint32_t fb[GAME_W*GAME_H];
    const char* nm[4]={"MENU","PLAY","WIN","LOSE"};
    game_init();
    game_input_press(640,360);                 /* start the game */
    for(int f=1; f<=12000; f++){               /* 200 s @60fps */
        game_tick(1.0f/60.0f, fb);
        if(f%600==0) printf("t=%5.1fs  phase=%s\n", f/60.0f, nm[game_phase()]);
        if(game_phase()!=1) { printf("ENDED at t=%.1fs phase=%s\n", f/60.0f, nm[game_phase()]); break; }
    }
    return 0;
}
