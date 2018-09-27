#include <stdint.h>
#include <stdbool.h>

#include <audio/audio.h>
#include <video/video.h>
#include <songplayer/songplayer.h>
#include <uart/uart.h>
#include <sine_table/sine_table.h>
#include <nunchuk/nunchuk.h>

#include "graphics_data.h"

//#define debug 1

#define abs(x) ((x) < 0 ? -(x) : (x))

// a pointer to this is a null pointer, but the compiler does not
// know that because "sram" is a linker symbol from sections.lds.
extern uint32_t sram;

#define reg_spictrl (*(volatile uint32_t*)0x02000000)
#define reg_uart_clkdiv (*(volatile uint32_t*)0x02000004)

extern const struct song_t song_pacman;

// Board timensions
#define TILE_SIZE 8
#define BOARD_WIDTH 15
#define BOARD_HEIGHT 14

// Board properties
#define CAN_GO_LEFT 1
#define CAN_GO_RIGHT 2
#define CAN_GO_UP 4
#define CAN_GO_DOWN 8
#define FOOD 16
#define BIG_FOOD 32
#define FRUIT 64

// Tile definitions
#define BLANK_TILE 0

#define FOOD_TILE1 4
#define FOOD_TILE2 5
#define FOOD_TILE3 12
#define FOOD_TILE4 13

#define BIG_FOOD_TILE1 40
#define BIG_FOOD_TILE2 41
#define BIG_FOOD_TILE3 48
#define BIG_FOOD_TILE4 49

#define ZERO_TILE 16

#define U_TILE 7
#define P_TILE 15

#define H_TILE 56
#define I_TILE 57
#define S_TILE 58
#define C_TILE 59
#define O_TILE 60
#define R_TILE 61
#define E_TILE 62
#define A_TILE 63
#define D_TILE 32
#define Y_TILE 33

#define CHERRY_TILE 42
#define STRAWBERRY_TILE 44
#define ORANGE_TILE 26

#define PACMAN_TILE 46

// Point values
#define FOOD_POINTS 10
#define BIG_FOOD_POINTS 50

#define CHERRY_POINTS 100
#define STRAWBERRY_POINTS 300
#define ORANGE_POINTS 500
#define APPLE_POINTS 700
#define MELON_POINTS 1000
#define GALAXIAN_POINTS 2000
#define BELL_POINTS 3000
#define KEY_POINTS 5000

#define GHOST_POINTS 200

// Board positions
#define FRUIT_X 7
#define FRUIT_Y 3

// Directions
#define UP 2
#define DOWN 1
#define LEFT 3
#define RIGHT 0

// Colours
#define BLACK 0
#define RED 1
#define GREEN 2
#define YELLOW 3
#define BLUE 4
#define MAGENTA 5
#define CYAN 6
#define WHITE 7

// Sprite images
#define GHOST_IMAGE 0
#define EYES_IMAGE 1
#define SCORE_IMAGE 2

// Period lengths
#define HUNT_TICKS 30
#define STAGE_OVER_TICKS 10
#define FRUIT_TICKS 100

#define PINKY_START 20
#define INKY_START 40
#define CLYDE_START 60

// Sprite numbers
#define NUM_SPRITES 5
#define NUM_GHOSTS 4

// Point thresholds
#define LIFE_POINTS 10000

// Ghost states
#define DOCKED 0
#define ACTIVE 1
#define SCORE 2
#define EYES 3

// Screen positions
#define LIVES_X 32
#define LIVES_Y 24

#define HISCORE_X 34
#define HISCORE_Y 4

#define UP_X 32
#define UP_Y 7

#define SHOW_FRUIT_X 32
#define SHOW_FRUIT_Y 16

#define SCORE_X 34
#define SCORE_Y 8

const uint8_t pacman = 0;
const uint8_t blinky = 1;
const uint8_t pinky = 2;
const uint8_t inky = 3;
const uint8_t clyde = 4;

const uint32_t counter_frequency = 16000000/50;  /* 50 times per second */

// Working data
uint8_t board[BOARD_HEIGHT][BOARD_WIDTH];
uint8_t pacman_images[8];
uint8_t ghost_images[8];
uint8_t sprite_x[NUM_SPRITES];
uint8_t sprite_y[NUM_SPRITES];
uint8_t old_sprite_x[NUM_SPRITES], old_sprite_y[NUM_SPRITES];
uint8_t old2_sprite_x[NUM_SPRITES], old2_sprite_y[NUM_SPRITES];
bool ghost_active[NUM_GHOSTS];
uint8_t old_sprite_x[NUM_SPRITES], old_sprite_y[NUM_SPRITES];
uint8_t old2_sprite_x[NUM_SPRITES], old2_sprite_y[NUM_SPRITES];
bool ghost_eyes[NUM_GHOSTS];
bool ghost_active[NUM_GHOSTS];
uint8_t pacman_image, ghost_image;
uint16_t score, hi_score, old_score, food_items, ghost_points;
uint16_t ghost_speed_counter, ghost_speed;
uint8_t stage, direction, hunting, num_fruit, num_lives, kills;
uint32_t tick_counter, game_start, hunt_start, stage_over_start, skip_ticks;
bool play, chomp, game_over, new_stage;

// Set the IRQ mask
uint32_t set_irq_mask(uint32_t mask); asm (
    ".global set_irq_mask\n"
    "set_irq_mask:\n"
    ".word 0x0605650b\n"
    "ret\n"
);

// Set the timer counter
uint32_t set_timer_counter(uint32_t val); asm (
    ".global set_timer_counter\n"
    "set_timer_counter:\n"
    ".word 0x0a05650b\n"
    "ret\n"
);

// Interrupt handling used for playing audio
void irq_handler(uint32_t irqs, uint32_t* regs)
{
  /* timer IRQ */
  if ((irqs & 1) != 0) {
    // retrigger timer
    set_timer_counter(counter_frequency);

    // Play song
    songplayer_tick();
  }
}

// Delay a few clock cycles - used by Nunchuk code
void delay(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) asm volatile ("");
}

// Set all tiles on board section of screen to blank
void clear_screen() {
 for (int x = 0; x < 32; x++) {
    for (int y = 0; y < 32; y++) {
      vid_set_tile(x,y,BLANK_TILE);
    }
  } 
}

// Set up the board grid with cell properties
void setup_board() {
  food_items = 0;

  for(int y = 0; y < 14; y++) {
    for(int x = 0;  x < 15; x++) {
      uint8_t n = 0;
      uint8_t t = tile_data[((y*2 + 1) << 5) + x*2 + 1];

      // Only process board tiles
      if (t != BLANK_TILE && t != FOOD_TILE1 && t != BIG_FOOD_TILE1) continue;

      // Check for food
      if (t == FOOD_TILE1) {
        n |= FOOD;
        food_items++;
      } else if (t == BIG_FOOD_TILE1) {
        n |= BIG_FOOD;
        food_items++;
      } 

      // Set valid board directions
      if (y > 0) {
        uint8_t above = tile_data[(((y-1)*2 + 2) << 5) + x*2 + 1];
        if (above == BLANK_TILE || above == FOOD_TILE3 || 
            above == BIG_FOOD_TILE3) n |= CAN_GO_UP;
      }

      if (y < BOARD_HEIGHT - 1) {
        uint8_t below = tile_data[(((y+1)*2 + 1) << 5) + x*2 + 1];
        if (below == BLANK_TILE || below == FOOD_TILE1 || 
            below == BIG_FOOD_TILE1) n |= CAN_GO_DOWN;
      }

      if (x > 0) {
        uint8_t left = tile_data[((y*2 + 1) << 5) + (x-1)*2 + 2];
        if (left == BLANK_TILE || left == FOOD_TILE2 || 
            left == BIG_FOOD_TILE2) n |= CAN_GO_LEFT;
      }

      if (x < BOARD_WIDTH - 1) {
        uint8_t right = tile_data[((y*2 + 1) << 5) + (x+1)*2 + 1];
        if (right == BLANK_TILE || right == FOOD_TILE1 || 
            right == BIG_FOOD_TILE1) n |= CAN_GO_RIGHT;
      }

      board[y][x] = n;
    }
  }      
}

// Display just the board, not its contents
void show_board() {
  for (int x = 0; x < 32; x++) {
    for (int y = 0; y < 32; y++) {
     uint8_t t = tile_data[(y<<5)+x];
     if (t < 16 && t != 4 && t != 5 && t != 12 && t != 13) vid_set_tile(x,y,t);
    }
  }
}

#ifdef debug
// Diagnostic print of board
void print_board() {
  print("Board:\n");
  for(int y = 0; y < 14; y++) {
    for(int x = 0; x < 15; x++) {
      print_hex(board[y][x],2);
      print(" ");
    }
    print("\n");
  }
}  
#endif

// Reset sprites to their original positions, and reset other state data
void reset_positions() {
  sprite_x[pacman] = 7;
  sprite_y[pacman] = 11;
  direction = RIGHT;

  sprite_x[blinky] = 7;
  sprite_y[blinky] = 7;

  sprite_x[pinky] = 6;
  sprite_y[pinky] = 10;

  sprite_x[inky] = 7;
  sprite_y[inky] = 10;

  sprite_x[clyde] = 8;
  sprite_y[clyde] = 10;

  num_fruit = 3;
  num_lives = 3;
  
  hunting = 0;
  kills = 0;
  chomp = true;
  new_stage = false;
}

// Add fruit to the board
void add_fruit(uint8_t x, uint8_t y, uint8_t fruit_tile) {
  board[y][x] |= FRUIT;
  vid_set_tile(2*x + 1,2*y + 1, fruit_tile);
  vid_set_tile(2*x + 2,2*y + 1, fruit_tile+1);
  vid_set_tile(2*x + 1,2*y + 2, fruit_tile+8);
  vid_set_tile(2*x + 2,2*y + 2, fruit_tile+9);
}

// Set ghosts to their initial colours
void set_ghost_colours() {
  vid_set_sprite_colour(inky, CYAN);
  vid_set_sprite_colour(pinky, MAGENTA);
  vid_set_sprite_colour(blinky, RED);
  vid_set_sprite_colour(clyde, GREEN);
}

void set_board_colour(uint8_t color) {
  // Set up the 64 8x8 textures
  for (int tex = 0; tex < 64; tex++) {
    for (int x = 0; x < 8; x++) {
      for (int y = 0 ; y < 8; y++) {
        int texrow = tex >> 3;   // 0-7, row in texture map
        int texcol = tex & 0x07; // 0-7, column in texture map
        int pixx = (texcol<<3)+x;
        int pixy = (texrow<<3)+y;
        uint32_t pixel = texture_data[(pixy<<6)+pixx];
        if (pixel != 0 && tex < 16 && tex != 0 && tex != 4 && 
            tex != 5 && tex != 12 && tex != 13) 
          pixel = color;
        vid_set_texture_pixel(tex, x, y, pixel);
      }
    }
  }
}

// Set up all the graphics data for board portion of screen
void setup_screen() {
  // Initialse the video and set offset to (0,0)
  vid_init();
  vid_set_x_ofs(0);
  vid_set_y_ofs(0);

  // Set up the 64 8x8 textures
  for (int tex = 0; tex < 64; tex++) {
    for (int x = 0; x < 8; x++) {
      for (int y = 0 ; y < 8; y++) {
        int texrow = tex >> 3;   // 0-7, row in texture map
        int texcol = tex & 0x07; // 0-7, column in texture map
        int pixx = (texcol<<3)+x;
        int pixy = (texrow<<3)+y;
        uint32_t pixel = texture_data[(pixy<<6)+pixx];
        vid_set_texture_pixel(tex, x, y, pixel);
      }
    }
  }

  // Set up the 32x32 tiles
  for (int x = 0; x < 32; x++) {
    for (int y = 0; y < 32; y++) {
      vid_set_tile(x,y,tile_data[(y<<5)+x]);
    }
  }

  // Reset the sprite positions
  reset_positions();

  // Set up the Pacman sprite images as images 0-7 and set the image to the first one
  for(int i=0;i<8;i++) pacman_images[i] = i;
  for(int i=0; i<8; i++) vid_write_sprite_memory(pacman_images[i], pacman_sprites[i]);
  pacman_image = pacman_images[0];
  vid_set_image_for_sprite(pacman, pacman_image);

  // Set up the ghost sprite images as image 8-15 and set the current ghost image to the first one
  for(int i=0;i<8;i++) ghost_images[i] = 8+i;
  for(int i=0; i<8; i++) vid_write_sprite_memory(ghost_images[i], ghost_sprites[i]);
  ghost_image = ghost_images[GHOST_IMAGE];
  for(int i=0;i<NUM_GHOSTS;i++) vid_set_image_for_sprite(i+1, ghost_image);

  // Set the sprite colours, to their defaults
  set_ghost_colours();
  vid_set_sprite_colour(pacman, YELLOW);
 
  // Position the sprites to their home positions
  for(int i=0;i<NUM_SPRITES;i++)
    vid_set_sprite_pos(i, 8 + (sprite_x[i] << 4), 8 + (sprite_y[i] << 4));

  // Enable all the sprites
  for(int i=0;i<NUM_SPRITES;i++) vid_enable_sprite(i, 1);
 
  // Disable ghost eyes and set ghosts inactive 
  for(int i=0;i<NUM_GHOSTS;i++) ghost_eyes[i] = false;
  for(int i=0;i<NUM_GHOSTS;i++) ghost_active[i] = false;
}

// Display available fruit
void show_fruit() {
  for(int i=0;i<4;i++) {
    int tile = CHERRY_TILE;
    
    if (i == 1) tile = STRAWBERRY_TILE;
    else if (i == 2) tile = ORANGE_TILE;

    vid_set_tile(SHOW_FRUIT_X + i*2, SHOW_FRUIT_Y, (i >= num_fruit ? BLANK_TILE : tile));
    vid_set_tile(SHOW_FRUIT_X + 1 + i*2, SHOW_FRUIT_Y, (i >= num_fruit ? BLANK_TILE : tile + 1));
    vid_set_tile(SHOW_FRUIT_X + i*2, SHOW_FRUIT_Y + 1, (i >= num_fruit ? BLANK_TILE : tile + 8));
    vid_set_tile(SHOW_FRUIT_X + 1 + i*2, SHOW_FRUIT_Y + 1, (i >= num_fruit ? BLANK_TILE : tile + 9));
  }
}

// Display available lives
void show_lives() {
  for(int i=0;i<4;i++) {
    vid_set_tile(LIVES_X + i*2, LIVES_Y, (i < num_lives ? PACMAN_TILE : BLANK_TILE));
    vid_set_tile(LIVES_X + 1 + i*2, LIVES_Y, (i < num_lives ? PACMAN_TILE+1 : BLANK_TILE));
    vid_set_tile(LIVES_X + i*2, LIVES_Y + 1, (i < num_lives ? PACMAN_TILE+8 : BLANK_TILE));
    vid_set_tile(LIVES_X + 1 + i*2, LIVES_Y + 1, (i < num_lives ? PACMAN_TILE+9 : BLANK_TILE));
  }
}

const int divisor[] = {10000,1000,100,10};

// Display score, hi-score or another numnber
void show_score(int x, int y, int score) {
  int s = score;
  bool blank = true;
  for(int i=0; i<5; i++) {
    int d = 0;
    if (i == 4) d = s;
    else {
      int div = divisor[i];
      while (s >= div) {
        s -= div;
        d++;
      } 
      if (d !=0) blank = false;
    }
    int tile = blank && i != 4 ? BLANK_TILE : ZERO_TILE + d;
    vid_set_tile(x+i, y, tile);
  }
}

// Show ready message
void show_ready() {
  vid_set_tile(13, 8, R_TILE);
  vid_set_tile(14, 8, E_TILE);
  vid_set_tile(15, 8, A_TILE);
  vid_set_tile(16, 8, D_TILE);
  vid_set_tile(17, 8, Y_TILE);
}

// Chase a sprite or go to a target
void chase(uint8_t target_x, uint8_t target_y, uint8_t* x, uint8_t* y, 
           uint8_t avoid_x, uint8_t avoid_y) {
  uint8_t n = board[*y][*x];
#ifdef debug
  print("Chasing ");
  print_hex(target_x,4);
  print(" ");
  print_hex(target_y,4);
  print("\n");
#endif
  if (target_x < *x && (n & CAN_GO_LEFT) && (*x)-1 != avoid_x) (*x)--;
  else if (target_x > *x && (n & CAN_GO_RIGHT) && (*x)+1 != avoid_x) (*x)++;
  else if (target_y < *y && (n & CAN_GO_UP) && (*y)-1 != avoid_y) (*y)--;
  else if (target_y > *y && (n & CAN_GO_DOWN) && (*y)+1 != avoid_y) (*y)++;
  else if (n & CAN_GO_LEFT) (*x)--;
  else if (n & CAN_GO_RIGHT) (*x)++;
  else if (n & CAN_GO_DOWN) (*y)--;
  else if (n & CAN_GO_UP) (*y)++;
}

// Evade a sprite or go away from a target
void evade(uint8_t target_x, uint8_t target_y, uint8_t* x, uint8_t* y, 
           uint8_t avoid_x, uint8_t avoid_y) {
  uint8_t n = board[*y][*x];
#ifdef debug
  print("Evading ");
  print_hex(target_x,4);
  print(" ");
  print_hex(target_y,4);
  print("\n");
#endif
  if (target_x < *x && (n & CAN_GO_LEFT)) (*x)--;
  else if (target_x < *x && (n & CAN_GO_RIGHT)) (*x)++;
  else if (target_y > *y && (n & CAN_GO_UP)) (*y)--;
  else if (target_y < *y && (n & CAN_GO_DOWN)) (*y)++;
  else if (n & CAN_GO_LEFT) (*x)--;
  else if (n & CAN_GO_RIGHT) (*x)++;
  else if (n & CAN_GO_DOWN) (*y)--;
  else if (n & CAN_GO_UP) (*y)++;
}

// Blnky behaviour
void move_blinky() {
  if (!ghost_active[blinky-1]) return;

  // Aim at Pacman
  uint8_t target_x = sprite_x[pacman], target_y = sprite_y[pacman];

  if (ghost_eyes[blinky-1]) {
    target_x = 7;
    target_y = 7;

    if (sprite_x[blinky] == 7 && sprite_y[blinky] == 7) {
      sprite_y[blinky] = 9;
      ghost_eyes[blinky] = false;
      return;
    }
  }

  if (hunting == 0 || ghost_eyes[blinky-1]) {
    chase(target_x, target_y, &sprite_x[blinky], &sprite_y[blinky], 
          old2_sprite_x[blinky], old2_sprite_y[blinky]);
  } else {
    evade(target_x, target_y, &sprite_x[blinky], &sprite_y[blinky], 
          old2_sprite_x[blinky], old2_sprite_y[blinky]);
  }
}

// Pinky behaviour
void move_pinky() {
  if (!ghost_active[pinky-1]) return;

  // Aim ahead of Pacman
  uint8_t target_x = sprite_x[pacman], target_y = sprite_y[pacman];
  switch (direction) {
    case UP: target_y--; break;
    case DOWN: target_y++; break;
    case LEFT: target_x--; break;
    case RIGHT: target_x++; break;
  }

  if (ghost_eyes[pinky-1]) {
    target_x = 7;
    target_y == 7;
    if (sprite_x[pinky] == 7 && sprite_y[pinky] == 7) {
      sprite_x[pinky] = 6;
      sprite_y[pinky] = 10;
      ghost_eyes[pinky] - false;
      return;
    }
  }

  if (hunting == 0 || ghost_eyes[pinky-1]) {
    chase(target_x, target_y, &sprite_x[pinky], &sprite_y[pinky], 
          old2_sprite_x[pinky], old2_sprite_y[pinky]);
  } else {
    evade(target_x, target_y, &sprite_x[pinky], &sprite_y[pinky], 
          old2_sprite_x[pinky], old2_sprite_y[pinky]);
  }
}

// Inky behaviour
void move_inky() {
  if (!ghost_active[inky-1]) return;

  uint8_t target_x = sprite_x[pacman], target_y = sprite_y[pacman];

  // Alternate between aiming at Pacman and evading him
  if (ghost_eyes[inky-1]) {
    target_x = 7;
    target_y = 7;

    if (sprite_x[inky] == 7 && sprite_y[inky] == 7) {
      sprite_x[inky] = 7;
      sprite_y[inky] = 10;
      return;
    }
  }

  if ((hunting == 0 && tick_counter & 0x40) || ghost_eyes[inky-1]) {
    chase(target_x, target_y, &sprite_x[inky], &sprite_y[inky], 
          old2_sprite_x[inky], old2_sprite_y[inky]);
  } else {
    evade(target_x, target_y, &sprite_x[inky], &sprite_y[inky], 
          old2_sprite_x[inky], old2_sprite_y[inky]);
  }
}

// Clyde behaviour
void move_clyde() {
  if (!ghost_active[clyde-1]) return;

  // Alternate between aiming at Pacman and the bottom left corner
  uint8_t target_x = sprite_x[pacman], target_y = sprite_y[pacman];
  if (abs(sprite_x[clyde] - sprite_x[pacman]) < 3 ||
      abs(sprite_y[clyde] - sprite_y[pacman]) < 3) {
    target_x = 0;
    target_y = 13;
  }

  if (ghost_eyes[clyde-1]) {
    target_x = 7;
    target_y = 7;

    if (sprite_x[clyde] == 7 && sprite_y[clyde] == 7) {
      sprite_y[clyde] = 8;
      sprite_y[clyde] = 10;
      return;
    }
  }

  if (hunting == 0 || ghost_eyes[clyde-1]) {
    chase(target_x, target_y, &sprite_x[clyde], &sprite_y[clyde], 
          old2_sprite_x[clyde], old2_sprite_y[clyde]);
  } else {
    evade(target_x, target_y, &sprite_x[clyde], &sprite_y[clyde], 
          old2_sprite_x[clyde], old2_sprite_y[clyde]);
  }
}

// End the hunt
void end_hunt() {
  hunting = 0;
  kills = 0;
  set_ghost_colours();

  for(int i=0;i<NUM_GHOSTS;i++) {
    vid_enable_sprite(i+1, 1);         
    vid_set_image_for_sprite(i+1, ghost_images[0]);
    ghost_eyes[i] = false; 
  } 

  // Let blinky out again 
  if (sprite_x[blinky] == 7 && sprite_y[blinky] == 8) sprite_y[blinky] = 7;
}

// Main entry point
void main() {
  reg_uart_clkdiv = 138;  // 16,000,000 / 115,200
  set_irq_mask(0x00);

  // Set up the screen
  setup_screen();
    
  // Set up the board
  setup_board();

#ifdef debug
  print_board();
#endif

  // Play music
  songplayer_init(&song_pacman);

  // switch to dual IO mode
  reg_spictrl = (reg_spictrl & ~0x007F0000) | 0x00400000;

  // set timer interrupt to happen 1/50th sec from now
  // (the music routine runs from the timer interrupt)
  set_timer_counter(counter_frequency);

  // Initialize the Nunchuk
  i2c_send_cmd(0x40, 0x00);

  play = false;

  // Default high score
  hi_score = 10000;

  // Display score and HI-SCORE
  vid_set_tile(32,2, H_TILE);
  vid_set_tile(33,2, I_TILE);
  vid_set_tile(35,2, S_TILE);
  vid_set_tile(36,2, C_TILE);
  vid_set_tile(37,2, O_TILE);
  vid_set_tile(38,2, R_TILE);
  vid_set_tile(39,2, E_TILE);

  // Display the ready message
  show_ready();

  uint32_t time_waster = 0;

  // Main loop
  while (1) {
    time_waster = time_waster + 1;
    if ((time_waster & 0xfff) == 0xfff) {
      // Update tick counter
      tick_counter++;

      // Get Nunchuk data
      i2c_send_reg(0x00);
      delay(100);

      uint8_t jx = i2c_read();
#ifdef debug
      print("Joystick x: ");
      print_hex(jx, 2);
      print("\n");
#endif

      uint8_t jy = i2c_read();
#ifdef debug
      print("Joystick y: ");
      print_hex(jy, 2);
      print("\n:1");
#endif

      uint8_t ax = i2c_read();
#ifdef debug
      print("Accel  x: ");
      print_hex(ax, 2);
      print("\n");
#endif

      uint8_t ay = i2c_read();
#ifdef debug
      print("Accel  y: ");
      print_hex(ay, 2);
      print("\n");
#endif

      uint8_t az = i2c_read();
#ifdef debug
      print("Accel  z: ");
      print_hex(az, 2);
      print("\n");
#endif

      uint8_t rest = i2c_read();
#ifdef debug
      print("Rest: ");
      print_hex(rest,2);
      print("\n");
      print("Buttons: ");
      print_hex(rest & 3, 2);
      print("\n");
#endif      
      uint8_t buttons = rest & 3;

      // Check buttons for start or restart
      if (buttons < 2) { 
        setup_screen();
        setup_board();
        game_start = tick_counter;
        play = (buttons == 0);
        if (!play) stage = 0;
        ghost_speed = (stage < 16 ? 16 - stage : 0);
        ghost_speed_counter = 0;
        if (stage == 0) score = 0;
        show_score(SCORE_X, SCORE_Y + 4, stage);
  
        // Start Blinky immediately
        ghost_active[blinky-1] = true;
      }

      // Save score
      old_score = score;

      if ((tick_counter - game_start) == FRUIT_TICKS) 
        add_fruit(FRUIT_X, FRUIT_Y, (stage == 0 ? CHERRY_TILE : STRAWBERRY_TILE));

      // Save last Pacman position and one before last
      old2_sprite_x[pacman] = old_sprite_x[pacman];
      old2_sprite_y[pacman] = old_sprite_y[pacman];
      old_sprite_x[pacman] = sprite_x[pacman];
      old_sprite_y[pacman] = sprite_y[pacman];

      /* Update Pacman location. If playing, pacman is moved by joystick, otherwise moves himself.
         Direction of moves is determined and chomp alternates as pacman moves. */
      int n = board[sprite_y[pacman]][sprite_x[pacman]];

      if (play) { // Playing a game
         if (sprite_x[pacman] < 30 && jx > 0xc0 && (n & CAN_GO_RIGHT)) {
           sprite_x[pacman]++; 
           direction=RIGHT;
         } else if (sprite_x[pacman] > 0 && jx < 0x40 && (n & CAN_GO_LEFT) ) {
           sprite_x[pacman]--; 
           direction=LEFT;
         } else if (sprite_y[pacman] < 28 && jy < 0x40 && (n & CAN_GO_DOWN)) {
           sprite_y[pacman]++; 
           direction=DOWN;
         } else if (sprite_y[pacman] > 0 && jy > 0xc0 && (n & CAN_GO_UP)) {
           sprite_y[pacman]--; 
           direction=UP;
         }
       } else { // Game animation
        if ((n & CAN_GO_UP) && (sprite_y[pacman]-1 != old2_sprite_y[pacman])) {
          sprite_y[pacman]--; 
          direction=UP;
        } else if ((n & CAN_GO_RIGHT) && (sprite_x[pacman]+1 != old2_sprite_x[pacman])) {
          sprite_x[pacman]++; 
          direction=RIGHT;
        } else if ((n & CAN_GO_DOWN) && (sprite_y[pacman]+1 != old2_sprite_y[pacman])) {
          sprite_y[pacman]++; 
          direction=DOWN;
        } else if ((n & CAN_GO_LEFT) && (sprite_x[pacman]-1 == old2_sprite_x[pacman])) {
          sprite_x[pacman]--; 
          direction=LEFT;
        }
      }
      
      if (sprite_x[pacman] != old_sprite_x[pacman] || 
          sprite_y[pacman] != old_sprite_y[pacman]) chomp = !chomp;

      // Set Pacman sprite position
      vid_set_sprite_pos(pacman, TILE_SIZE + (sprite_x[pacman] << 4), 
                                 TILE_SIZE + (sprite_y[pacman] << 4));

      // Is it time to let Pinky out?
      if (tick_counter == (game_start + PINKY_START)) {
        ghost_active[pinky-1] = true;
        sprite_x[pinky] = 7;
        sprite_y[pinky] = 7;
      }

      // What about Inky?
      if (tick_counter == (game_start + INKY_START)) {
        ghost_active[inky-1] = true;
        sprite_x[inky] = 7;
        sprite_y[inky] = 7;
      }

      // What about Clyde?
      if (tick_counter == (game_start + CLYDE_START)) {
        ghost_active[clyde-1] = true;
        sprite_x[inky] = 7;
        sprite_y[inky] = 7;
      }

      // Show fruit
      show_fruit();

      // Show lives
      show_lives();

      // Move ghosts
      for(int i=0; i<NUM_GHOSTS;i++) {
        if (ghost_eyes[i] || (ghost_speed_counter == ghost_speed)) {
          // Save last ghost position and one before last
          old2_sprite_x[i+1] = old_sprite_x[i+1];
          old2_sprite_y[i+1] = old_sprite_y[i+1];
          old_sprite_x[i+1] = sprite_x[i+1];
          old_sprite_y[+1] = sprite_y[i+1];
          if (i+1 == blinky) move_blinky();
          else if (i+1 == pinky) move_pinky();
          else if (i+1 == inky) move_inky();
          else if (i+1 == clyde) move_clyde();
        }
      }

      if (ghost_speed_counter++ == ghost_speed) ghost_speed_counter = 0;

      // Check for death
      for(int i= 0;i<NUM_GHOSTS;i++) {
        if ((sprite_x[pacman] == sprite_x[i+1] && 
             sprite_y[pacman] == sprite_y[i+1]) && !ghost_eyes[i]) {
          if (hunting > 0) {
            score += ghost_points;
            vid_set_image_for_sprite(i+1, ghost_images[SCORE_IMAGE+kills++]);
            vid_set_sprite_colour(i+1, WHITE);
            ghost_eyes[i] = true;
            ghost_points <= 1;
          } else if (num_lives == 1) {
            clear_screen();
            num_lives = 0;
          
            game_over = true;
            play = false;
            stage = 0;

            // Line them up
            sprite_x[pacman] = 3; sprite_y[pacman] = 3;
            sprite_x[blinky] = 5; sprite_y[blinky] = 3;
            sprite_x[pinky] = 7; sprite_y[pinky] = 3;
            sprite_x[inky] = 9; sprite_y[inky] = 3;
            sprite_x[clyde] = 11; sprite_y[clyde] = 3;
          } else if (num_lives > 0) num_lives--;
        }
      }

      // Set the approriate Pacman image
      vid_set_image_for_sprite(pacman, pacman_images[chomp ?  pacman_images[1 + direction] : 0]);

      // Set ghost sprite positions and make them jump (other than blinky)
      for(int i=0;i<NUM_GHOSTS;i++)  
        vid_set_sprite_pos(i+1, TILE_SIZE + (sprite_x[i+1] << 4), 
                                TILE_SIZE + ((sprite_y[i+1] - 
                                ((!ghost_active[i] && i+1 != blinky) & 
                                tick_counter & 1)) << 4));

      // Eat your food
      n = board[sprite_y[pacman]][sprite_x[pacman]];
      if (n & FOOD || n & BIG_FOOD || n & FRUIT) {
         food_items--;

         vid_set_tile(sprite_x[pacman]*2 + 1, sprite_y[pacman]*2 + 1, BLANK_TILE);
         vid_set_tile(sprite_x[pacman]*2 + 2, sprite_y[pacman]*2 + 1, BLANK_TILE);
         vid_set_tile(sprite_x[pacman]*2 + 1, sprite_y[pacman]*2 + 2, BLANK_TILE);
         vid_set_tile(sprite_x[pacman]*2 + 2, sprite_y[pacman]*2 + 2, BLANK_TILE);

         score += (n & BIG_FOOD ? BIG_FOOD_POINTS : 
                  ( n & FRUIT ? (stage == 0 ? CHERRY_POINTS : STRAWBERRY_POINTS) : FOOD_POINTS));
         board[sprite_y[pacman]][sprite_x[pacman]] &= ~(FOOD | BIG_FOOD | FRUIT);
         
         if (n & BIG_FOOD && !hunting) {
           hunting = 1;
           kills = 0;
           hunt_start = tick_counter;
           ghost_points = GHOST_POINTS;
           for(int i=0;i<NUM_GHOSTS;i++) vid_set_sprite_colour(i+1, BLUE);
         }
      }
 
      // Check for end of hunting
      if (hunting == 1 && (tick_counter - hunt_start) > HUNT_TICKS) { // End of blue phase
        for(int i=0;i<NUM_GHOSTS;i++) vid_set_sprite_colour(i+1, WHITE);
        hunting = 2;
        hunt_start = tick_counter;
      } else if (hunting == 2 && (tick_counter - hunt_start) > HUNT_TICKS) { // End of white phase
        end_hunt();
        if (sprite_x[blinky] == 7 && sprite_y[blinky] == 8) sprite_y[blinky] = 7;
      }

      // Flash ghosts when hunting
      if (hunting == 2) for(int i=0;i<NUM_GHOSTS;i++) vid_enable_sprite(i+1, tick_counter & 1); 

      // Extra live after 10000 points
      if (score >= LIFE_POINTS && old_score < LIFE_POINTS) num_lives++;

      // Show the score   
      show_score(SCORE_X, SCORE_Y, score);

      // Show hi-score
      if (score > hi_score) hi_score = score;
      show_score(HISCORE_X, HISCORE_Y, hi_score);

      // Show number of food items
      show_score(SCORE_X, SCORE_Y + 2, food_items);

      // Check for stage won
      if (play && food_items == 0) {
        end_hunt();
        clear_screen();
        stage_over_start = tick_counter;
        new_stage = true;
        stage++;
        play = false;
      }

      // Flash board for new stage
      if (new_stage) {
        show_board();
        if ((tick_counter - stage_over_start) < STAGE_OVER_TICKS) { 
          if (tick_counter & 1) set_board_colour(WHITE);
          else set_board_colour(BLUE);
        } else {
          new_stage = false;
          set_board_colour(BLUE);
          clear_screen();
        }
      }
      
      // Flash 1UP
      if ((tick_counter & 1) == 1) {
        vid_set_tile(UP_X, UP_Y, ZERO_TILE + 1);
        vid_set_tile(UP_X + 1, UP_Y, U_TILE);
        vid_set_tile(UP_X + 2, UP_Y, P_TILE);
      } else {
        for(int i=0;i<3;i++) vid_set_tile(32+i, 7, BLANK_TILE);
      }
    }
  }
}
