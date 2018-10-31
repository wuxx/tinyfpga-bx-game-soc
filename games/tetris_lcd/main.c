#include <stdint.h>
#include <stdbool.h>

#include <audio/audio.h>
#include <video/video.h>
#include <songplayer/songplayer.h>
#include <uart/uart.h>
#include <button/button.h>

#include "graphics_data.h"

// a pointer to this is a null pointer, but the compiler does not
// know that because "sram" is a linker symbol from sections.lds.
extern uint32_t sram;

#define reg_spictrl (*(volatile uint32_t*)0x02000000)
#define reg_uart_clkdiv (*(volatile uint32_t*)0x02000004)

#define BLANK_TILE 0
#define ZERO_TILE 40
#define ONE_TILE 41

#define SCORE_X 32
#define SCORE_Y 5

#define LEVEL_X 32
#define LEVEL_Y 9

#define LINES_X 32
#define LINES_Y 13

#define NEXT_PIECE_X 30
#define NEXT_PIECE_Y 20

#define START_BOARD 9
#define END_BOARD 28

#define BOARD_WIDTH 10
#define BOARD_HEIGHT 20

const struct song_t song_pacman;

uint32_t counter_frequency = 16000000/50;  /* 50 times per second */

// Working data
uint16_t score, level, lines;
uint8_t piece,piece_y, orientation, speed;
int8_t piece_x;
uint8_t next_piece;
uint8_t old_x, old_y, old_piece, old_orientation, old_buttons;
uint8_t buttons;
uint8_t board[BOARD_HEIGHT][BOARD_WIDTH];
bool fallen;

// Bits show occupied tiles for a piece in the different orientations
const uint16_t rotations[7][4] = {
  {0x0F00, 0x2222, 0x00f0, 0x4444},
  {0x8e00, 0x6440, 0x0e20, 0x44c0},
  {0x2e00, 0x4460, 0x0e80, 0xc440},
  {0xcc00, 0xcc00, 0xcc00, 0xcc00},
  {0x6c00, 0x4620, 0x06c0, 0x8c40},
  {0x4e00, 0x4640, 0x0e40, 0x4c40},
  {0xc600, 0x2640, 0x0c60, 0x4c80}
};

const uint8_t offsets[7][4] = {
  {0x01, 0x20, 0x02, 0x10},
  {0x00, 0x10, 0x01, 0x00},
  {0x00, 0x10, 0x01, 0x00},
  {0x00, 0x00, 0x00, 0x00},
  {0x00, 0x10, 0x01, 0x00},
  {0x00, 0x10, 0x01, 0x00},
  {0x00, 0x10, 0x01, 0x00}
};

// Assembler routine to set the irq mask
uint32_t set_irq_mask(uint32_t mask); asm (
    ".global set_irq_mask\n"
    "set_irq_mask:\n"
    ".word 0x0605650b\n"
    "ret\n"
);

// Assembler routine to set the timer counter
uint32_t set_timer_counter(uint32_t val); asm (
    ".global set_timer_counter\n"
    "set_timer_counter:\n"
    ".word 0x0a05650b\n"
    "ret\n"
);


// Set up the screen
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

  for (x = 0; x < 40; x++) {
    for (y = 0; y < 30; y++) {
      vid_set_tile(x,y,tile_data[y*40+x]);
    }
  }
}

const uint8_t colors[] = {6, 4, 7, 3, 2, 5, 1};

// Show the board
void show_board() {
  for(int y=0; y<BOARD_HEIGHT; y++) 
    for(int x=0; x<BOARD_WIDTH; x++) {
      uint8_t p = board[y][x];
      vid_set_tile(x + 1, y + 9, (p == 0 ? 0 : colors[p-1]));
    }
}

// Clear the whole board
void clear_board() {
  for(int y=0; y<BOARD_HEIGHT; y++) 
    for(int x=0; x<BOARD_WIDTH; x++)
      board[y][x] = 0;
}

// Get the width of a piece
uint8_t piece_width(int p, int o) {
  switch(p) {
    case 0:
      return (o & 1 ? 1 : 4);
    case 1:
    case 2:
    case 4:
    case 5:
    case 6:
      return (o & 1 ? 2 : 3);
    case 3: return 2;
    default: return 4;
  } 
}

// Get the height of a piece
uint8_t piece_height(int p, int o) {
  switch(p) {
    case 0:
      return (o & 1 ? 4 : 1);
    case 1:
    case 2:
    case 4:
    case 5:
    case 6:
      return (o & 1 ? 3 : 2);
    case 3: return 2;
    default: return 4;
  } 
}

// Helper for show piece and blank piece
void show_piece_c(int x0, int y0, uint8_t p, uint8_t o, uint8_t c) {
  uint16_t bit = rotations[p][o];
  
  for(int y=0;y<4;y++) 
   for(int x=0;x<4;x++) {
     if (bit & 0x8000) vid_set_tile(x0+x, y0+y, c);
     bit <<= 1;
   }
}

// Show the piece on the board
void show_piece(int x, int y, uint8_t p, int o) {
  show_piece_c(x, y, p, o, colors[p]);
}

// Blank old piece
void blank_piece(int x, int y, uint8_t p, int o) {
  show_piece_c(x, y, p, o, 0);
}

// Place the piece on the board
void place_piece(int x0, int y0, uint8_t p, uint8_t o) {
  uint16_t bit = rotations[p][o];
  
  for(int y=0;y<4;y++) 
   for(int x=0;x<4;x++) {
     if ((bit & 0x8000) &&
         y0 + y < BOARD_HEIGHT &&
         x0 + x >= 0 &&
         x0 + x < BOARD_WIDTH)
       board[y0 + y][x0 + x] = p + 1;
     bit <<= 1;
   }
}

// Check if current piece touches one on the board
bool touched() {
  uint16_t bit = rotations[piece][orientation];

  for(int y=0;y<4;y++) 
    for(int x=0;x<4;x++) {
      if ((bit & 0x8000) && 
           piece_y - 8 + y < BOARD_HEIGHT &&
           piece_x - 1 + x < BOARD_WIDTH &&
           piece_x -1 + x >= 0 &&
           (board[piece_y - 8 + y][piece_x - 1 + x] > 0)) 
        return true;
      bit <<= 1;
    }
  return false;
}

// Interrupt handler plays background audio
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

// Remove completed line
void drop_line(int n) {
   for(int y=n;y>0;y--) 
     for(int x=0;x<BOARD_WIDTH;x++) 
       board[y][x] = board[y-1][x];
}

// Check for completed lines
void check_board() {
  for (int y= BOARD_HEIGHT-1;y>0;y--) {
    int count = 0;

    for(int x=0;x<BOARD_WIDTH;x++) 
      if (board[y][x] > 0) count++;
    
    if (count == BOARD_WIDTH) {
      drop_line(y);
      lines++;
      y++;
    }
  }
}

// Delay in some units or other
void delay(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) asm volatile ("");
}

void get_input() {
  buttons = reg_buttons;
}

void start_game() {
  score = 0;
  lines = 0;
  level = 1;
  piece_x = 4;
  piece_y = 9;
  speed = 7;
  orientation = 0;
  fallen = false;
  piece = 0;
  next_piece = 1;

  old_x = piece_x;
  old_y = piece_y;
  old_piece = piece;
  old_orientation = orientation;

}

void main() {
  reg_uart_clkdiv = 138;  // 16,000,000 / 115,200
  set_irq_mask(0x00);

  setup_screen();
  clear_board();

  songplayer_init(&song_pacman);

  // switch to dual IO mode
  reg_spictrl = (reg_spictrl & ~0x007F0000) | 0x00400000;

  // set timer interrupt to happen 1/50th sec from now
  // (the music routine runs from the timer interrupt)
  set_timer_counter(counter_frequency);

  uint32_t time_waster = 0, tick_counter = 0;

  start_game();

  uint8_t rand;

  while (1) {
    time_waster++;
    
    if ((time_waster & 0x7ff) == 0x7ff) {
      tick_counter++;

      // Blank the old piece
      blank_piece(old_x, old_y, old_piece, old_orientation);
      blank_piece(NEXT_PIECE_X, NEXT_PIECE_Y, piece, 0);
 
      // Switch to new piece if old one has fallen
      if (fallen) {
        piece = next_piece;
        orientation = 0;
        piece_x = 4;
        piece_y = 9;
        next_piece = rand;
        fallen = false;

        // If immediately touches, then game over
        if (touched()) {
          songplayer_trigger_effect(9);
          clear_board();
          start_game();
          continue;
        }
      }

      // Show scores, etc.
      show_score(SCORE_X, SCORE_Y, score);
      show_score(LEVEL_X, LEVEL_Y, level);
      show_score(LINES_X, LINES_Y, lines);

      // Show the board
      show_board();  

      // Show the current piece on the board
      show_piece(piece_x, piece_y, piece, orientation);

      // Show next piece
      show_piece(NEXT_PIECE_X, NEXT_PIECE_Y, next_piece, 0);

      // Save current values
      old_x = piece_x;
      old_y = piece_y;
      old_piece = piece;
      old_orientation = orientation;
      old_buttons = buttons;

      // Check buttons
      get_input();

      // Move the piece
      if ((buttons & BUTTON_LEFT) && 
          piece_x + (offsets[piece][orientation] >> 4) > 1) {
        piece_x--;
      }

      if ((buttons & BUTTON_RIGHT) && 
          (piece_width(piece, orientation) + 
          (offsets[piece][orientation] >> 4) +
          piece_x < BOARD_WIDTH + 1)) 
        piece_x++;

      // Rotate the piece
      if ((buttons & BUTTON_UP) && !(old_buttons & BUTTON_UP)) {
        orientation = (orientation + 1) & 3;
        uint8_t new_width = piece_width(piece, orientation);
        if (piece_x + new_width > BOARD_WIDTH) {
          piece_x = BOARD_WIDTH - new_width;
        } else if (piece_x + (offsets[piece][orientation] >> 4) < 1) {
          piece_x = 1;
        }
        songplayer_trigger_effect(9); 
      }

      // Move the piece down
      if ((buttons & BUTTON_DOWN) || ((tick_counter & speed) == 0)) { 
        // Check for touched an existing piece or reached bottom of board
        if (!touched() && 
            ((piece_y + (offsets[piece][orientation] & 3) + 
              piece_height(piece, orientation)) < 29)) {
          piece_y++;
        } else {
          // Play sound effect and place piece on board
          songplayer_trigger_effect(10);
          place_piece(piece_x - 1, 
                      piece_y - 9, 
                      piece, orientation);
          fallen = true;
          check_board();
        }
      }
    } else rand = (++rand >= 7 ? 0 : rand);
  }
}
