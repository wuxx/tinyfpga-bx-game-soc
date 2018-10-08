#include <stdint.h>
#include <stdbool.h>

#include <audio/audio.h>
#include <video/video.h>
#include <songplayer/songplayer.h>
#include <uart/uart.h>
#include <sine_table/sine_table.h>
#include <nunchuk/nunchuk.h>

#include "graphics_data.h"

// a pointer to this is a null pointer, but the compiler does not
// know that because "sram" is a linker symbol from sections.lds.
extern uint32_t sram;

#define reg_spictrl (*(volatile uint32_t*)0x02000000)
#define reg_uart_clkdiv (*(volatile uint32_t*)0x02000004)
#define reg_leds  (*(volatile uint32_t*)0x03000000)

#define MAX_SPEED 16 
#define GRAVITY 4
#define JUMP_SPEED 24
#define DEBOUNCE_TICKS 10

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
#define COINS2_TILE 31

#define X_TILE 53
#define M_TILE 56

#define W_TILE 50
#define O_TILE 60
#define R_TILE 58
#define L_TILE 51
#define D_TILE 52

const struct song_t song_pacman;

uint32_t counter_frequency = 16000000/50;  /* 50 times per second */

uint16_t score, coins, offset;
uint32_t game_start, time_left;
uint8_t buttons, jx, jy;
int16_t goomba_x[3];
bool goomba_forwards[3];

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
                               0x0C, 0x10, 0x12, 0x13, 0x18, 0x19, 
                               0x20, 0x21, 0x28, 0x29, 0x30, 0x31};

const uint8_t coin_tiles[] = {0x20, 0x21};

bool is_solid(uint8_t t) {
  for(int i=0;i<sizeof(solid_tiles);i++) {
    if (solid_tiles[i] == t) return true;
  }
  return false;
}

bool is_coin_tile(uint8_t t) {
   for(int i=0;i<sizeof(coin_tiles);i++) {
     if (coin_tiles[i] == t) return true;
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

  for(int i=0;i<8;i++) vid_write_sprite_memory(i, sprites[i]);
  for(int i=0;i<8;i++) vid_write_sprite_memory(i+8, more_sprites[i]);

  // Position Mario
  vid_set_sprite_pos(0,16,208);
  vid_set_sprite_colour(0, 5);
  vid_set_image_for_sprite(0, 0);
  vid_enable_sprite(0, 1);

  // Position Goombas
  for(int i=0;i<3;i++) {
    vid_set_sprite_pos(1+i,96 + (i << 5)  , 208);
    vid_set_sprite_colour(1+i, 6);
    vid_set_image_for_sprite(1+i, 8);
    vid_enable_sprite(1+i, 1);
  }

  // Set color for score sprite
  vid_set_sprite_colour(4, 7);
}

void move_goomba() {
  for(int i=0; i<3; i++) {
    if (goomba_forwards[i]) {
      if (++goomba_x[i] == 212) goomba_forwards[i] = false;
    } else {
      if (--goomba_x[i] == 0) goomba_forwards[i] = true;
    }
    vid_set_sprite_pos(1+i, goomba_x[i] - offset, 208);
  }
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
    if (i == 1) d = s;
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

void get_input() {
  // Get Nunchuk data
  i2c_send_reg(0x00);
  delay(100);

  jx = i2c_read();
#ifdef debug
  print("Joystick x: ");
  print_hex(jx, 2);
  print("\n");
#endif

  jy = i2c_read();
#ifdef debug
  print("Joystick y: ");
  print_hex(jy, 2);
  print("\n");
#endif

  uint8_t ax = i2c_read();
  uint8_t ay = i2c_read();
  uint8_t az = i2c_read();
  uint8_t rest = i2c_read();

#ifdef debug
  print("Buttons: ");
  print_hex(rest & 3, 2);
  print("\n");
#endif      
  buttons = rest & 3;
}

void main() {
  reg_uart_clkdiv = 138;  // 16,000,000 / 115,200
  set_irq_mask(0x00);

  setup_screen();

  songplayer_init(&song_pacman);

  // switch to dual IO mode
  reg_spictrl = (reg_spictrl & ~0x007F0000) | 0x00400000;

  // set timer interrupt to happen 1/50th sec from now
  // (the music routine runs from the timer interrupt)
  set_timer_counter(counter_frequency);

  // Initialize the Nunchuk
  i2c_send_cmd(0x40, 0x00);

  offset = 0;
  bool forwards = true;

  uint32_t time_waster = 0, tick_counter = 0;
  int16_t sprite_x = 16, sprite_y = 208;
  int8_t x_speed = 0, y_speed = 0;
  uint8_t under_tile_1 = 0, under_tile_2 = 0;

  score = 0;
  coins = 0;
  game_start = 0;

  bool jumping = false;

  for(int i=0;i<3;i++) {
    goomba_forwards[i] = false;
    goomba_x[i] = 96 + (i << 5);
  }

  while (1) {
    time_waster++;
    if ((time_waster & 0x7ff) == 0x7ff) {
      tick_counter++;
  
      // Set up the top two lines 
      blank_line(0);
      blank_line(1);
      show_text(MARIO_X + (offset >> 3), MARIO_Y, M_TILE, 5);
 
      show_score(SCORE_X + (offset >> 3), SCORE_Y, score);

      vid_set_tile(COINS_X + (offset >> 3), COINS_Y, 
                   (tick_counter & 4 ? COINS_TILE : COINS2_TILE));
      vid_set_tile(COINS_X + 1 + (offset >> 3), COINS_Y, X_TILE);
 
      show_coins(COINS_X + 2 + (offset >> 3), COINS_Y, coins);

      vid_set_tile(WORLD_X + (offset >> 3), WORLD_Y, W_TILE);
      vid_set_tile(WORLD_X + 1 + (offset >> 3), WORLD_Y, O_TILE);
      vid_set_tile(WORLD_X + 2+  (offset >> 3), WORLD_Y, R_TILE);
      vid_set_tile(WORLD_X + 3 + (offset >> 3), WORLD_Y, L_TILE);
      vid_set_tile(WORLD_X + 4 + (offset >> 3), WORLD_Y, D_TILE);
 
      vid_set_tile(WORLD_NUM_X + (offset >> 3), WORLD_NUM_Y, ONE_TILE);
      vid_set_tile(WORLD_NUM_X + 1 + (offset >> 3), WORLD_NUM_Y, HYPHEN_TILE);
      vid_set_tile(WORLD_NUM_X + 2 + (offset >> 3), WORLD_NUM_Y, ONE_TILE);

      time_left = 400 - ((tick_counter - game_start) >> 3);
      if (time_left == 0) game_start = tick_counter; 

      show_score(TIME_X + (offset >> 3), TIME_Y, time_left);
      
      if ((tick_counter & 0x3) == 0) {
        // Get nunchuk input
        get_input();

        // Set the horizontal speed 
        if (jx > 0xc0 && x_speed < MAX_SPEED) {
          x_speed++;
        } else if (jx < 0x40 && x_speed > - MAX_SPEED) {
          x_speed--;
        } else {
          x_speed = 0;
        }

        // Check for jump
        if (buttons == 2 && !jumping) {
          songplayer_trigger_effect(9);
          y_speed = JUMP_SPEED;
          jumping = true;
        }

        // Don't go above top of screen
        if (y_speed < 0 || sprite_y > y_speed) sprite_y -= y_speed;
        if (sprite_y > 208) sprite_y = 208;

// Check for solid object below
        if (y_speed < 0) {
          for(int y = ((sprite_y + y_speed) >> 3);y < ((sprite_y + 8) >> 3); y++) { 
            print("down y is ");
            print_hex(y, 4);
            print(" , sprite_y is ");
            print_hex(sprite_y, 4);
            print(" , y_speed is ");
            print_hex(y_speed, 4);
            print("\n");
            for(int x = (sprite_x >> 3); x < ((sprite_x + 24) >> 3); x++) {
              uint8_t t = tile_data[((y+2) << 6) + x];
              if (is_solid(t)) {
                print("Tile is ");
                print_hex(t, 4);
                print("\n");
                sprite_y = y << 3;
                y_speed = 0;
                jumping = false;
                if (t == 0x10) {
                  sprite_y += 16;
                  songplayer_trigger_effect(8);
                  break;
                } 
              }
            }
          }
        } else if (y_speed > 0) { // Check for object above
          for(int y = (sprite_y + y_speed)  >> 3;y > sprite_y >> 3; y--) { 
            print("up y is ");
            print_hex(y, 4);
            print(" , sprite_y is ");
            print_hex(sprite_y, 4);
            print(" , y_speed is ");
            print_hex(y_speed, 4);
            print("\n");
            for(int x = ((sprite_x + 16)  >> 3); x < ((sprite_x + 24 + x_speed) >> 3); x++) {
              uint8_t t = tile_data[(y << 6) + x];
              if (is_solid(t)) {
                print("Tile is ");
                print_hex(t, 4);
                print("\n");
                sprite_y = (y << 3) + 8;
                y_speed = 0;
                jumping = false;
                if (is_coin_tile(t)) {
                  coins++;
                  score += 200;
                  songplayer_trigger_effect(10);
                  vid_set_image_for_sprite(4,10); // 200 points
                  vid_set_sprite_pos(4, x << 3, (y-3) << 3);
                  vid_enable_sprite(4, 1);
                  for(int i=0;i<5;i++) {
                    vid_set_tile(x, y-4, COINS_TILE);
                    delay(5000);
                    vid_set_tile(x, y-4, COINS2_TILE);
                    delay(5000);
                  }
                  vid_set_tile(x, y-4, BLANK_TILE);
                  vid_enable_sprite(4, 0);
                  break;
                }
              }
            }
          }
        } 

        if (x_speed > 0) { // Check for object to right
          for(int x = (sprite_x + 16)   >> 3;x < (sprite_x + 24 + x_speed) >> 3; x++) {
            print("right x is ");
            print_hex(x, 4);
            print(" , sprite_x is ");
            print_hex(sprite_x, 4);
            print(" , x_speed is ");
            print_hex(x_speed, 4);
            print("\n");
            for(int y = (sprite_y >> 3); y < ((sprite_y + 16) >> 3); y++) {
              uint8_t t = tile_data[(y << 6) + x];
              print("Tile is ");
              print_hex(t, 4);
              print("\n");
              if (is_solid(t)) {
                x_speed = 0;
                sprite_x = (x << 3) - 16;
              }
            }
          } 
        } else if (x_speed < 0) { // Check for object to left
            for(int x = (sprite_x + x_speed)  >> 3;x >= sprite_x >> 3; x--) {
            print("left x is ");
            print_hex(x, 4);
            print(" , sprite_x is ");
            print_hex(sprite_x, 4);
            print(" , x_speed is ");
            print_hex(x_speed, 4);
            print("\n");
            for(int y = (sprite_y >> 3); y < ((sprite_y + 16) >> 3); y++) {
              uint8_t t = tile_data[(y << 6) + x];
              print("Tile is ");
              print_hex(t, 4);
              print("\n");
              if (is_solid(t)) {
                x_speed = 0;
                sprite_x = (x << 3) + 8;
              }
            }
          }
        }
    
        if (sprite_x > 160) offset = sprite_x - 160;
        else offset = 0;

        if (offset > 192) offset = 192;

        vid_set_x_ofs(offset);
        sprite_x += x_speed;

        if (sprite_x < 0) sprite_x = 0;
        else if (sprite_x > 496) sprite_x = 496;

        vid_set_image_for_sprite(0, x_speed < 0 ? 1 : 0);
        vid_set_sprite_pos(0, sprite_x - offset, sprite_y);
        
        y_speed -= GRAVITY; // Gravity

        move_goomba();
      }
    }
  }
}
