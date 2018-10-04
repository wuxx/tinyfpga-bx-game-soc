#include <stdint.h>
#include <stdbool.h>

#include <audio/audio.h>
#include <video/video.h>
#include <songplayer/songplayer.h>
#include <uart/uart.h>
#include <sine_table/sine_table.h>
#include "graphics_data.h"

// a pointer to this is a null pointer, but the compiler does not
// know that because "sram" is a linker symbol from sections.lds.
extern uint32_t sram;

#define reg_spictrl (*(volatile uint32_t*)0x02000000)
#define reg_uart_clkdiv (*(volatile uint32_t*)0x02000004)
#define reg_leds  (*(volatile uint32_t*)0x03000000)

#define BLANK_TILE 0
#define ZERO_TILE 40
#define ONE_TILE 41

#define HYPHEN_TILE 61

#define SCORE_X 4
#define SCORE_Y 1

#define MARIO_X 3
#define MARIO_Y 0

#define COINS_X 12
#define COINS_Y 1

#define WORLD_X 19
#define WORLD_Y 0

#define WORLD_NUM_X 20
#define WORLD_NUM_Y 1

#define TIME_X 32
#define TIME_Y 1

#define COINS_TILE 35

#define X_TILE 53
#define M_TILE 56

#define W_TILE 50
#define O_TILE 60
#define R_TILE 58
#define L_TILE 51
#define D_TILE 52

const struct song_t song_petergun;

uint32_t counter_frequency = 16000000/50;  /* 50 times per second */

uint32_t set_irq_mask(uint32_t mask); asm (
    ".global set_irq_mask\n"
    "set_irq_mask:\n"
    ".word 0x0605650b\n"
    "ret\n"
);

uint32_t set_timer_counter(uint32_t val); asm (
    ".global set_timer_counter\n"
    "set_timer_counter:\n"
    ".word 0x0a05650b\n"
    "ret\n"
);


const uint8_t solid_tiles[] = {0x01, 0x02, 0x03, 0x04, 0x0A, 0x0B, 
                               0x10, 0x12, 0x18, 0x19, 
                               0x20, 0x21, 0x28, 0x29, 0x30, 0x31};

uint16_t score, coins;
uint32_t game_start, time_left;

bool is_solid(uint8_t t) {
  for(int i=0;i<sizeof(solid_tiles);i++) {
    if (solid_tiles[i] == t) return true;
  }
  return false;
}

void setup_screen() {
  vid_init();

  vid_set_x_ofs(0);
  vid_set_y_ofs(0);
  int tex,x,y;

  for (tex = 0; tex < 64; tex++) {
    for (x = 0; x < 8; x++) {
      for (y = 0 ; y < 8; y++) {
        int texrow = tex >> 3;   // 0-7, row in texture map
        int texcol = tex & 0x07; // 0-7, column in texture map
        int pixx = (texcol<<3)+x;
        int pixy = (texrow<<3)+y;
        uint32_t pixel = texture_data[(pixy<<6)+pixx];
        vid_set_texture_pixel(tex, x, y, pixel);
      }
    }
  }

  for (x = 0; x < 64; x++) {
    for (y = 0; y < 64; y++) {
      vid_set_tile(x,y,tile_data[(y<<6)+x]);
    }
  }

  vid_write_sprite_memory(0, sprites[0]);
  vid_set_sprite_pos(0,16,208);
  vid_set_sprite_colour(0, 5);
  vid_set_image_for_sprite(0, 0);
  vid_enable_sprite(0, 1);
}

void irq_handler(uint32_t irqs, uint32_t* regs)
{
  /* timer IRQ */
  if ((irqs & 1) != 0) {
    // retrigger timer
    set_timer_counter(counter_frequency);

    songplayer_tick();
  }

}

const int divisor[] = {10000,1000,100,10};

// Display score, hi-score or another numnber
void show_score(int x, int y, int score) {
  int s = score;
  for(int i=0; i<5; i++) {
    int d = 0;
    if (i == 4) d = s;
    else {
      int div = divisor[i];
      while (s >= div) {
        s -= div;
        d++;
      } 
    }
    vid_set_tile(x+i, y, ZERO_TILE + d);
  }
}


void show_coins(int x, int y, int coins) {
  int s = coins;
  for(int i=0; i<2; i++) {
    int d = 0;
    if (i == 4) d = s;
    else {
      int div = divisor[i+3];
      while (s >= div) {
        s -= div;
        d++;
      } 
    }
    vid_set_tile(x+i, y, ZERO_TILE + d);
  }
}

void show_text(int x, int y, uint8_t tile, int len) {
  for(int i=0;i<len;i++) vid_set_tile(x+i, y, tile+i);
}

// Delay in some units or other
void delay(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) asm volatile ("");
}

void blank_line(int l) {
  for(int i=0;i<64;i++) vid_set_tile(i, l, BLANK_TILE);
}

void main() {
   reg_uart_clkdiv = 138;  // 16,000,000 / 115,200
   set_irq_mask(0x00);

   setup_screen();

   songplayer_init(&song_petergun);

   // switch to dual IO mode
   reg_spictrl = (reg_spictrl & ~0x007F0000) | 0x00400000;

   // set timer interrupt to happen 1/50th sec from now
   // (the music routine runs from the timer interrupt)
   set_timer_counter(counter_frequency);

   uint16_t offset = 0;
   bool forwards = true;

   uint32_t time_waster = 0i, tick_counter = 0;
   uint16_t sprite_x = 16, sprite_y = 208;
   int8_t x_speed = 1, y_speed = 0;
   uint8_t under_tile_1 = 0, under_tile_2 = 0;

   score = 0;
   coins = 0;
   game_start = 0;

   while (1) {

     time_waster++;
     if ((time_waster & 0x7ff) == 0x7ff) {
       tick_counter++;
   
       vid_set_tile(forwards ? MARIO_X + (offset >> 3) -1 : MARIO_X + 6, 
                    MARIO_Y, BLANK_TILE); 
       show_text(MARIO_X + (offset >> 3), MARIO_Y, M_TILE, 5);
 
       vid_set_tile(forwards ? SCORE_X + (offset >> 3) -1 : SCORE_X + 6, 
                    SCORE_Y, BLANK_TILE); 
       show_score(SCORE_X + (offset >> 3), SCORE_Y, score);

       vid_set_tile(forwards ? COINS_X + (offset >> 3) -1 : COINS_X + 4, 
                    COINS_Y, BLANK_TILE);
       vid_set_tile(COINS_X + (offset >> 3), COINS_Y, COINS_TILE);
       vid_set_tile(COINS_X + 1 + (offset >> 3), COINS_Y, X_TILE);
 
       show_coins(COINS_X + 2 + (offset >> 3), COINS_Y, coins);

       vid_set_tile(forwards ? WORLD_X + (offset >> 3) -1 : WORLD_X + 6, 
                    WORLD_Y, BLANK_TILE); 
       vid_set_tile(WORLD_X + (offset >> 3), WORLD_Y, W_TILE);
       vid_set_tile(WORLD_X + 1 + (offset >> 3), WORLD_Y, O_TILE);
       vid_set_tile(WORLD_X + 2+  (offset >> 3), WORLD_Y, R_TILE);
       vid_set_tile(WORLD_X + 3 + (offset >> 3), WORLD_Y, L_TILE);
       vid_set_tile(WORLD_X + 4 + (offset >> 3), WORLD_Y, D_TILE);
 
       vid_set_tile(forwards ? WORLD_NUM_X + (offset >> 3) -1 : WORLD_NUM_X + 4, 
                    WORLD_NUM_Y, BLANK_TILE); 
       vid_set_tile(WORLD_NUM_X + (offset >> 3), WORLD_NUM_Y, ONE_TILE);
       vid_set_tile(WORLD_NUM_X + 1 + (offset >> 3), WORLD_NUM_Y, HYPHEN_TILE);
       vid_set_tile(WORLD_NUM_X + 2 + (offset >> 3), WORLD_NUM_Y, ONE_TILE);

       vid_set_tile(forwards ? TIME_X + (offset >> 3) - 1 : TIME_X + 5, 
                    TIME_Y, BLANK_TILE); 

       time_left = 400 - ((tick_counter - game_start) >> 3);
       if (time_left == 0) game_start = tick_counter; 

       show_score(TIME_X + (offset >> 3), TIME_Y, time_left);

       if ((tick_counter & 0x3) == 0) {
         if (y_speed < 0 || sprite_y > y_speed) sprite_y -= y_speed;

         uint8_t y_tile = (sprite_y >> 3) + 2;
         uint8_t x_tile = sprite_x >> 3;

         under_tile_1 = tile_data[(y_tile << 6) + x_tile];
         under_tile_2 = tile_data[(y_tile << 6) + x_tile + 2];
         
         if (under_tile_1 != 0 || under_tile_2 != 0) {
           print(" Under tiles ");
           print_hex(under_tile_1, 4);
           print(" , ");
           print_hex(under_tile_2, 4);
           print("\n");
           //if (under_tile_1 != 0) vid_set_tile(x_tile, y_tile, 9);
           //if (under_tile_2 != 0) vid_set_tile(x_tile+2, y_tile, 9);
         }

         int8_t old_y_speed = y_speed;

         if (y_speed < 0 && (is_solid(under_tile_1) || 
                             is_solid(under_tile_2))) {
           y_speed = 16;
           sprite_y = (sprite_y >> 3) << 3;
         }

         if (old_y_speed <= -8) {
           under_tile_1 = tile_data[((y_tile - 1) << 6) + x_tile];
           under_tile_2 = tile_data[((y_tile - 1) << 6) + x_tile + 2];

           print_hex(under_tile_1, 4);
           print(" , ");
           print_hex(under_tile_2, 4);
           print("\n");

           if (is_solid(under_tile_1) ||
               is_solid(under_tile_2)) {
             print("Thru solid\n");
             y_speed = 16;
             sprite_y = ((sprite_y - 8) >> 3) << 3;
           }
         }           

         y_speed -= 1; // Gravity
         
         vid_set_sprite_pos(0, sprite_x - offset, sprite_y);

         if (sprite_x > 160) offset = sprite_x - 160;
         if (offset > 192) offset = 192;
         if (sprite_x >= 512) {
           sprite_x = 0;
           offset = 0;
           blank_line(0);
           blank_line(1);
         }
         vid_set_x_ofs(offset);
         sprite_x += x_speed;
       }
    }
  }
}
