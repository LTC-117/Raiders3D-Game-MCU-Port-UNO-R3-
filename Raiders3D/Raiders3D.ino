////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////// OVERVIEW /////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////

/*
 * DESCRIPTION: SPI protocol demo. A port of the demo "Raiders3D" from my book
 * "Tricks of 3D Game Programming Gurus". Book included FREE with this course. We take the demo
 * which is a 3D space shooter, then port it from Windows and DirectX to run on the little
 * Arduino 328P UNO on a SPI 160x128 RGB screen.
 *
 * AUTHOR: Andre' LaMothe
 *
 * COMMENTS:
 *
 * HARDWARE SETUP:
 *
 *
 * HISTORY:
 *
 */


///////////////////////////////////////////////////////////////////////////////
////////////////////////////////// INCLUDES ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <SPI.h>

using namespace std;


///////////////////////////////////////////////////////////////////////////////
/////////////////////////// DEFINES AND CONSTANTS /////////////////////////////
///////////////////////////////////////////////////////////////////////////////


const  uint8_t BUTTON_RIGHT_PIN = A0;   // move player right
const  uint8_t BUTTON_LEFT_PIN  = A1;   // move player left
const  uint8_t BUTTON_UP_PIN    = A2;   // move player up
const  uint8_t BUTTON_DOWN_PIN  = A3;   // move player down
const  uint8_t BUTTON_FIRE_PIN  = A4;   // move player down

const  uint8_t BUTTON_FAST_PIN  = 2;    // increase speed
const  uint8_t BUTTON_SLOW_PIN  = 3;    // decrease speed

// defines for windows interface

#define WINDOW_TITLE    "T3D Graphics Console Ver 2.0"
#define WINDOW_WIDTH    128  // size of window
#define WINDOW_HEIGHT   160


///////////////////////////////////////////////////////////////////////////////


#define NUM_STARS       24  // number of stars in sim
#define NUM_TIES        2  // number of tie fighters in sim

#define TIES_MISSED_END_GAME 16  // if you miss this many tie fighters then game ends!

// 3D engine constants
#define NEAR_Z          10   // the near clipping plane
#define FAR_Z           1000 // the far clipping plane
#define VIEW_DISTANCE   64  // viewing distance from viewpoint
                             // this gives a field of view of 90 degrees
                             // when projected on a window of 640 wide
// player constants
#define CROSS_VEL       6  // speed that the cross hair moves
#define PLAYER_Z_VEL    8  // virtual z velocity that player is moving
                           // to simulate motion without moving

// tie fighter model constants
#define NUM_TIE_VERTS   10
#define NUM_TIE_EDGES   8

// explosiond
#define NUM_EXPLOSIONS  (NUM_TIES) // total number of explosions

// game states
#define GAME_RUNNING    1
#define GAME_OVER       0


// these are the GPIO pins we will use for reset and D/C pins on the LCD interface
#define TFT_RST         7 // reset signal, active LOW, or set to -1 and connect to Arduino RESET pin
#define TFT_DC          6 // data/command select signal
#define TFT_CS          5 // LCD chip select, active LOW


// For 1.44" and 1.8" TFT with ST7735 use this call:
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);


///////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// TYPES /////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


// this a 3D point
typedef struct POINT3D_TYP {
    uint16_t color;     // color of point 16-bit
    float x,y,z;        // coordinates of point in 3D
} POINT3D, *POINT3D_PTR;

// this is a 3D line, nothing more than two indices into a vertex list
typedef struct LINE3D_TYP {
    uint16_t color;     // color of line 16-bit
    int8_t v1,v2;       // indices to endpoints of line in vertex list
} LINE3D, *LINE3D_PTR;

// a tie fighter
typedef struct TIE_TYP {
    int8_t state;       // state of the tie, 0=dead, 1=alive
    float x, y, z;      // position of the tie
    float xv,yv,zv;     // velocity of the tie
} TIE, *TIE_PTR;

// a basic 3D vector used for velocity
typedef struct VEC3D_TYP {
    float x,y,z; // coords of vector
} VEC3D, *VEC3D_PTR;

// a wireframe explosion
typedef struct EXPL_TYP {
    int8_t state;       // state of explosion
    int8_t counter;     // counter for explosion
    uint16_t color;     // color of explosion

    // an explosion is a collection of edges/lines
    // based on the 3D model of the tie that is exploding
    POINT3D p1[NUM_TIE_EDGES];  // start point of edge n
    POINT3D p2[NUM_TIE_EDGES];  // end point of edge n

    VEC3D   vel[NUM_TIE_EDGES]; // velocity of shrapnel

} EXPL, *EXPL_PTR;


///////////////////////////////////////////////////////////////////////////////
/////////////////////////////// PROTOTYPES ////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


// game console
void Game_Init();
void Game_Shutdown();
void Game_Main();

// game functions
void Init_Tie(int8_t index);


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// GLOBALS /////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


char buffer[64];                         // used to print text

/*
 * the tie fighter is a collection of vertices connected by
 * lines that make up the shape. Only one tie fighter see if
 * you can replicate it?
 */

POINT3D tie_vlist[NUM_TIE_VERTS]; // vertex list for the tie fighter model
LINE3D  tie_shape[NUM_TIE_EDGES]; // edge list for the tie fighter model
TIE     ties[NUM_TIES];           // tie fighters

POINT3D stars[NUM_STARS]; // the starfield

/* Some colors, note we can't build them until we know the bit
 * depth format 5.5.5 or 5.6.5, so we wait a minute and do it in the
 * Game_Init() function
 */

uint16_t rgb_green = ST77XX_GREEN,
         rgb_white = ST77XX_WHITE,
         rgb_red   = ST77XX_RED,
         rgb_blue  = ST77XX_BLUE;

// player vars
float cross_x = 0, // cross hairs
      cross_y = 0;

int cross_x_screen  = WINDOW_WIDTH/2,   // used for cross hair
    cross_y_screen  = WINDOW_HEIGHT/2,
    target_x_screen = WINDOW_WIDTH/2,   // used for targeter
    target_y_screen = WINDOW_HEIGHT/2;

int8_t player_z_vel = 8; // virtual speed of viewpoint/ship
int8_t cannon_state = 0; // state of laser cannon
int8_t cannon_count = 0; // laster cannon counter

EXPL explosions[NUM_EXPLOSIONS]; // the explosiions

int8_t misses = 0; // tracks number of missed ships
int8_t hits   = 0; // tracks number of hits
int16_t score  = 0; // take a guess :)

int8_t game_state = GAME_RUNNING; // state of game


///////////////////////////////////////////////////////////////////////////////
//////////////////////////////// FUNCTIONS ////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


uint16_t RGB16Bit(uint8_t r,uint8_t g,uint8_t b) {
    return ((b >> 3) + ((g >> 2) << 5) + ((r >> 3) << 11));
} // end RGB16Bit


///////////////////////////////////////////////////////////////////////////////


void DrawText(char *text, uint16_t color, uint8_t x=0, uint8_t y=0) {
    tft.setCursor(x,y);
    tft.setTextColor(color);
    tft.setTextWrap(true);
    tft.print(text);
} // end DrawText


///////////////////////////////////////////////////////////////////////////////


void setup() {
    // Use this initializer if using a 1.8" TFT screen:
    tft.initR(INITR_BLACKTAB);      // Init ST7735S chip, black tab

    // depending on processor, this function allow you to set a higher speed then some limit
    // on the 328P for example, max SPI speed is 1/2 the system clock of 16Mhz, so 8Mhz
    tft.setSPISpeed(8000000);

    // clear screen
    tft.fillScreen(RGB16Bit(0,64,255)); // ST77XX_BLACK);

    DrawText("Crash Course Arduino", ST77XX_GREEN,0,0);
    DrawText("Raiders3D Running on", ST77XX_WHITE,0,24);
    DrawText("160x128 LCD", ST77XX_WHITE,0,24+8);
    delay(3000);

    // perform all game console specific initialization
    Game_Init();
} // end setup


///////////////////////////////////////////////////////////////////////////////


void loop() {
    // main game processing goes here
    Game_Main();
} // end loop


///////////////////////////////////////////////////////////////////////////////


void Game_Init() {
    // this function is where you do all the initialization for your game

    int16_t index; // used for looping

    // setup player control GPIO pins (buttons)
    pinMode( BUTTON_RIGHT_PIN, INPUT_PULLUP);
    pinMode( BUTTON_LEFT_PIN,  INPUT_PULLUP);
    pinMode( BUTTON_UP_PIN,    INPUT_PULLUP);
    pinMode( BUTTON_DOWN_PIN,  INPUT_PULLUP);
    pinMode( BUTTON_FIRE_PIN,  INPUT_PULLUP);

    pinMode( BUTTON_FAST_PIN,  INPUT_PULLUP);
    pinMode( BUTTON_SLOW_PIN,  INPUT_PULLUP);

    // seed random number generator
    srand(millis());

    // all your initialization code goes here...

    // create system colors
    rgb_green = RGB16Bit(0,255,0);
    rgb_white = RGB16Bit(255,255,255);
    rgb_blue  = RGB16Bit(0,0,255);
    rgb_red   = RGB16Bit(255,0,0);

    // create the starfield
    for (index=0; index < NUM_STARS; index++) {
        // randomly position stars in an elongated cylinder stretching from
        // the viewpoint 0,0,-d to the yon clipping plane 0,0,far_z
        stars[index].x = -2*WINDOW_WIDTH  + rand()%(WINDOW_WIDTH*4);
        stars[index].y = -2*WINDOW_HEIGHT + rand()%(WINDOW_HEIGHT*4);
        stars[index].z = NEAR_Z + rand()%(FAR_Z/2 - NEAR_Z);

        // set color of stars
        stars[index].color = rgb_white;
    } // end for index

    // create the tie fighter model

    // the vertex list for the tie fighter
    POINT3D temp_tie_vlist[NUM_TIE_VERTS] =
    // color, x,y,z
    {
        {rgb_white,-40,40,0 },  // p0
        {rgb_white,-40,0,0  },  // p1
        {rgb_white,-40,-40,0},  // p2
        {rgb_white,-10,0,0  },  // p3
        {rgb_white,0,20,0   },  // p4
        {rgb_white,10,0,0   },  // p5
        {rgb_white,0,-20,0  },  // p6
        {rgb_white,40,40,0  },  // p7
        {rgb_white,40,0,0   },  // p8
        {rgb_white,40,-40,0 }   // p9
    };

    // copy the model into the real global arrays
    for (index=0; index<NUM_TIE_VERTS; index++)
         tie_vlist[index] = temp_tie_vlist[index];

    // the edge list for the tie fighter
    LINE3D temp_tie_shape[NUM_TIE_EDGES] =
    // color, vertex 1, vertex 2
    {
        {rgb_green,0,2      },  // l0
        {rgb_green,1,3      },  // l1
        {rgb_green,3,4      },  // l2
        {rgb_green,4,5      },  // l3
        {rgb_green,5,6      },  // l4
        {rgb_green,6,3      },  // l5
        {rgb_green,5,8      },  // l6
        {rgb_green,7,9      }   // l7
    };

    // copy the model into the real global arrays
    for (index=0; index<NUM_TIE_EDGES; index++) {
         tie_shape[index] = temp_tie_shape[index];
    }

    // initialize the position of each tie fighter and it's velocity
    for (index=0; index<NUM_TIES; index++) {
        // initialize this tie fighter
        Init_Tie(index);
    } // end for index

} // end Game_Init


///////////////////////////////////////////////////////////////////////////////


void Start_Explosion(int8_t tie) {
    // this starts an explosion based on the sent tie fighter

    // first hunt and see if an explosion is free
    for (int8_t index=0; index < NUM_EXPLOSIONS; index++) {
        if (explosions[index].state==0) {
            // start this explosion up using the properties
            // if the tie figther index sent

            explosions[index].state   = 1; // enable state of explosion
            explosions[index].counter = 0; // reset counter for explosion

            // set color of explosion
            explosions[index].color = rgb_green;

            // make copy of of edge list, so we can blow it up
            for (int8_t edge=0; edge < NUM_TIE_EDGES; edge++) {
               // start point of edge
               explosions[index].p1[edge].x = ties[tie].x+tie_vlist[tie_shape[edge].v1].x;
               explosions[index].p1[edge].y = ties[tie].y+tie_vlist[tie_shape[edge].v1].y;
               explosions[index].p1[edge].z = ties[tie].z+tie_vlist[tie_shape[edge].v1].z;

               // end point of edge
               explosions[index].p2[edge].x = ties[tie].x+tie_vlist[tie_shape[edge].v2].x;
               explosions[index].p2[edge].y = ties[tie].y+tie_vlist[tie_shape[edge].v2].y;
               explosions[index].p2[edge].z = ties[tie].z+tie_vlist[tie_shape[edge].v2].z;

               // compute trajectory vector for edges
               explosions[index].vel[edge].x = ties[tie].xv - 16+rand()%32;
               explosions[index].vel[edge].y = ties[tie].yv - 16+rand()%32;
               explosions[index].vel[edge].z = -7+rand()%8;
            } // end for edge
            // done, so return
            return;
        } // end if found
    } // end for index

} // end Start_Explosion


///////////////////////////////////////////////////////////////////////////////


void Process_Explosions(void) {
    // this processes all the explosions

    // loop thro all the explosions and render them
    for (int8_t index=0; index<NUM_EXPLOSIONS; index++) {
        // test if this explosion is active?
        if (explosions[index].state == 0) {
           continue;
        }

        for (int8_t edge=0; edge<NUM_TIE_EDGES; edge++) {
            // must be exploding, update edges (shrapel)
            explosions[index].p1[edge].x += explosions[index].vel[edge].x;
            explosions[index].p1[edge].y += explosions[index].vel[edge].y;
            explosions[index].p1[edge].z += explosions[index].vel[edge].z;

            explosions[index].p2[edge].x += explosions[index].vel[edge].x;
            explosions[index].p2[edge].y += explosions[index].vel[edge].y;
            explosions[index].p2[edge].z += explosions[index].vel[edge].z;
        } // end for edge

        // test for terminatation of explosion?
        if (++explosions[index].counter > 100) {
           explosions[index].state = explosions[index].counter = 0;
        }
    } // end for index

} // end Process_Explosions


///////////////////////////////////////////////////////////////////////////////


void Draw_Explosions(void) {
    // this draws all the explosions

    // loop thro all the explosions and render them
    for (int8_t index=0; index<NUM_EXPLOSIONS; index++) {
        // test if this explosion is active?
        if (explosions[index].state==0) {
           continue;
        }

        // render this explosion
        // each explosion is made of a number of edges
        for (int8_t edge=0; edge < NUM_TIE_EDGES; edge++) {
            POINT3D p1_per, p2_per; // used to hold perspective endpoints

            // test if edge if beyond near clipping plane
            if (explosions[index].p1[edge].z < NEAR_Z &&
                explosions[index].p2[edge].z < NEAR_Z) {
               continue;
            }

            // step 1: perspective transform each end point
            p1_per.x = VIEW_DISTANCE*explosions[index].p1[edge].x/explosions[index].p1[edge].z;
            p1_per.y = VIEW_DISTANCE*explosions[index].p1[edge].y/explosions[index].p1[edge].z;
            p2_per.x = VIEW_DISTANCE*explosions[index].p2[edge].x/explosions[index].p2[edge].z;
            p2_per.y = VIEW_DISTANCE*explosions[index].p2[edge].y/explosions[index].p2[edge].z;

            // step 2: compute screen coords
            int p1_screen_x = WINDOW_WIDTH/2  + p1_per.x;
            int p1_screen_y = WINDOW_HEIGHT/2 - p1_per.y;
            int p2_screen_x = WINDOW_WIDTH/2  + p2_per.x;
            int p2_screen_y = WINDOW_HEIGHT/2 - p2_per.y;

            // step 3: draw the edge
            tft.drawLine(p1_screen_x, p1_screen_y, p2_screen_x, p2_screen_y, explosions[index].color);
        } // end for edge
    } // end for index

} // end Draw_Explosions


///////////////////////////////////////////////////////////////////////////////


void Move_Starfield(void) {
    // move the stars

    int8_t index; // looping var

    // the stars are technically stationary,but we are going
    // to move them to simulate motion of the viewpoint
    for (index=0; index<NUM_STARS; index++) {
        // move the next star
        stars[index].z-=player_z_vel;

        // test for past near clipping plane
        if (stars[index].z <= NEAR_Z) {
            stars[index].z = FAR_Z;
        }
    } // end for index

} // end Move_Starfield


///////////////////////////////////////////////////////////////////////////////


void Draw_Starfield(void) {
    // draw the stars in 3D using perspective transform

    int8_t index; // looping var

    for (index=0; index<NUM_STARS; index++) {
        // draw the next star
        // step 1: perspective transform
        float x_per = VIEW_DISTANCE*stars[index].x/stars[index].z;
        float y_per = VIEW_DISTANCE*stars[index].y/stars[index].z;

        // step 2: compute screen coords
        int x_screen = WINDOW_WIDTH/2  + x_per;
        int y_screen = WINDOW_HEIGHT/2 - y_per;

        // clip to screen coords
        if (x_screen>=WINDOW_WIDTH || x_screen < 0 ||
            y_screen >= WINDOW_HEIGHT || y_screen < 0) {
           // continue to next star
           continue;
        } // end if
        else {
           // else render to screen
           tft.drawPixel( x_screen, y_screen, stars[index].color );
        } // end else
    } // end for index

}  // Draw_Starfield


///////////////////////////////////////////////////////////////////////////////


void Init_Tie(int8_t index) {
    // this function starts a tie fighter up at the far end
    // of the universe and sends it our way!

    // position each tie in the viewing volume
    ties[index].x = -WINDOW_WIDTH  + rand()%(2*WINDOW_WIDTH);
    ties[index].y = -WINDOW_HEIGHT + rand()%(2*WINDOW_HEIGHT);
    ties[index].z =  2*FAR_Z;

    // initialize velocity of tie fighter
    ties[index].xv = -4+rand()%8;
    ties[index].yv = -4+rand()%8;
    ties[index].zv = -8-rand()%64;

    // turn the tie fighter on
    ties[index].state = 1;
} // end Init_Tie


///////////////////////////////////////////////////////////////////////////////


void Process_Ties(void) {
    // process the tie fighters and do AI (what there is of it!)
    int8_t index; // looping var

    // move each tie fighter toward the viewpoint
    for (index=0; index<NUM_TIES; index++) {
        // is this one dead?
        if (ties[index].state==0) {
           continue;
        }

        // move the next tie
        ties[index].z+=ties[index].zv;
        ties[index].x+=ties[index].xv;
        ties[index].y+=ties[index].yv;

        // test for past near clipping plane
        if (ties[index].z <= NEAR_Z) {
           // reset this tie
           Init_Tie(index);

           // another got away
           misses++;
        } // reset tie
    } // end for index

}  // Process_Ties


///////////////////////////////////////////////////////////////////////////////


void Draw_Ties(void) {
    // draw the tie fighters in 3D wireframe with perspective

    int8_t index; // looping var

    // used to compute the bounding box of tie fighter
    // for collision detection
    int bmin_x, bmin_y, bmax_x, bmax_y;

    // draw each tie fighter
    for (index=0; index < NUM_TIES; index++) {
        // draw the next tie fighter

        // is this one dead?
        if (ties[index].state==0) {
           continue;
        }

        // reset the bounding box to impossible values
        bmin_x =  4000;
        bmax_x = -4000;
        bmin_y =  4000;
        bmax_y = -4000;

        // based on z-distance shade tie fighter
        // normalize the distance from 0 to max_z then
        // scale it to 255, so the closer the brighter TODO

        uint16_t tieIntensity = (uint8_t)(255.0-255.0f*(ties[index].z/(8.0f*FAR_Z)));

        if ( tieIntensity > 255 ) tieIntensity = 255;
        else if ( tieIntensity < 0 ) tieIntensity = 0;

        uint16_t rgb_tie_color = RGB16Bit(0,tieIntensity,0);

        // each tie fighter is made of a number of edges
        for (int8_t edge=0; edge < NUM_TIE_EDGES; edge++) {
            POINT3D p1_per, p2_per; // used to hold perspective endpoints
            // step 1: perspective transform each end point
            // note the translation of each point to the position of the tie fighter
            // that is the model is relative to the position of each tie fighter -- IMPORTANT
            p1_per.x =
                    VIEW_DISTANCE*(ties[index].x+tie_vlist[tie_shape[edge].v1].x)/
                    (tie_vlist[tie_shape[edge].v1].z+ties[index].z);

            p1_per.y = VIEW_DISTANCE*(ties[index].y+tie_vlist[tie_shape[edge].v1].y)/
                       (tie_vlist[tie_shape[edge].v1].z+ties[index].z);

            p2_per.x = VIEW_DISTANCE*(ties[index].x+tie_vlist[tie_shape[edge].v2].x)/
                       (tie_vlist[tie_shape[edge].v2].z+ties[index].z);

            p2_per.y = VIEW_DISTANCE*(ties[index].y+tie_vlist[tie_shape[edge].v2].y)/
                       (tie_vlist[tie_shape[edge].v2].z+ties[index].z);

            // step 2: compute screen coords
            int p1_screen_x = WINDOW_WIDTH/2  + p1_per.x;
            int p1_screen_y = WINDOW_HEIGHT/2 - p1_per.y;
            int p2_screen_x = WINDOW_WIDTH/2  + p2_per.x;
            int p2_screen_y = WINDOW_HEIGHT/2 - p2_per.y;

            // step 3: draw the edge
            tft.drawLine(p1_screen_x, p1_screen_y, p2_screen_x, p2_screen_y, rgb_tie_color);

            // update bounding box with next edge
            int min_x = min(p1_screen_x, p2_screen_x);
            int max_x = max(p1_screen_x, p2_screen_x);

            int min_y = min(p1_screen_y, p2_screen_y);
            int max_y = max(p1_screen_y, p2_screen_y);

            bmin_x = min(bmin_x, min_x);
            bmin_y = min(bmin_y, min_y);

            bmax_x = max(bmax_x, max_x);
            bmax_y = max(bmax_y, max_y);
        } // end for edge

        // test if this guy has been hit by lasers???
        if (cannon_state==1) {
            // simple test of screen coords of bounding box contain laser target
            if (target_x_screen > bmin_x && target_x_screen < bmax_x &&
                target_y_screen > bmin_y && target_y_screen < bmax_y) {
                // this tie is dead meat!
                Start_Explosion(index);

                // increase score
                score+=ties[index].z;

                // add one more hit
                hits++;

                // finally reset this tie figher
                Init_Tie(index);
            } // end if

        } // end if

    } // end for index

} // end Draw_Ties


///////////////////////////////////////////////////////////////////////////////


void Game_Main() {
    // this is the workhorse of your game it will be called
    // continuously in real-time this is like main() in C
    // all the calls for your game go here!

    int8_t  index;       // looping var

    // start the timing clock TODO
    uint16_t startTime = millis();

    // clear the drawing surface
    tft.fillScreen(ST77XX_BLACK);

    // game logic here...

    if (game_state==GAME_RUNNING) {
        // move players crosshair
        if (!digitalRead( BUTTON_RIGHT_PIN )) {
            // move cross hair to right
            cross_x+=CROSS_VEL;

            // test for wraparound
            if (cross_x > WINDOW_WIDTH/2)
            cross_x = -WINDOW_WIDTH/2;
        } // end if
        else
        if (!digitalRead( BUTTON_LEFT_PIN )) {
            // move cross hair to left
            cross_x-=CROSS_VEL;

            // test for wraparound
            if (cross_x < -WINDOW_WIDTH/2)
            cross_x = WINDOW_WIDTH/2;
        } // end if

        if (!digitalRead( BUTTON_DOWN_PIN )) {
            // move cross hair up
            cross_y-=CROSS_VEL;

            // test for wraparound
            if (cross_y < -WINDOW_HEIGHT/2)
            cross_y = WINDOW_HEIGHT/2;
        } // end if
        else
        if (!digitalRead( BUTTON_UP_PIN )) {
            // move cross hair down
            cross_y+=CROSS_VEL;

            // test for wraparound
            if (cross_y > WINDOW_HEIGHT/2)
            cross_y = -WINDOW_HEIGHT/2;
        } // end elif

        // speed of ship controls
        if (!digitalRead( BUTTON_FAST_PIN )) {
            player_z_vel++;
        }
        else
        if (!digitalRead( BUTTON_SLOW_PIN )) {
            player_z_vel--;
        }

        // test if player is firing laser cannon
        if (!digitalRead( BUTTON_FIRE_PIN ) && cannon_state==0) {
            // fire the cannon
            cannon_state = 1;
            cannon_count = 0;

            // save last position of targeter
            target_x_screen = cross_x_screen;
            target_y_screen = cross_y_screen;
        } // end if

    } // end if game running

    // process cannon, simple FSM ready->firing->cool

    // firing phase
    if (cannon_state == 1) {
        if (++cannon_count > 10) {
            cannon_state = 2;
        }
    }

    // cool down phase
    if (cannon_state == 2) {
        if (++cannon_count > 15) {
            cannon_state = 0;
        }
    }

    // move the starfield
    Move_Starfield();

    // move and perform ai for ties
    Process_Ties();

    // Process the explosions
    Process_Explosions();

    // draw the starfield
    Draw_Starfield();

    // draw the tie fighters
    Draw_Ties();

    // draw the explosions
    Draw_Explosions();

    // draw the crosshairs

    // first compute screen coords of crosshair
    // note inversion of y-axis
    cross_x_screen = WINDOW_WIDTH/2  + cross_x;
    cross_y_screen = WINDOW_HEIGHT/2 - cross_y;

    // draw the crosshair in screen coords
    tft.drawLine(cross_x_screen-8,cross_y_screen, cross_x_screen+8,cross_y_screen,rgb_red);
    tft.drawLine(cross_x_screen,cross_y_screen-8,cross_x_screen,cross_y_screen+8,rgb_red);
    tft.drawLine(cross_x_screen-8,cross_y_screen-2,cross_x_screen-8,cross_y_screen+2,rgb_red);
    tft.drawLine(cross_x_screen+8,cross_y_screen-2,cross_x_screen+8,cross_y_screen+2,rgb_red);

    // draw the laser beams
    if (cannon_state == 1) {
        uint16_t beamColor = RGB16Bit(32,32,128+rand()%128);

        if ((rand()%2 == 1)) {
            // right beam
            tft.drawLine(WINDOW_WIDTH-1, WINDOW_HEIGHT-1,-4+rand()%8+target_x_screen,-4+rand()%8+target_y_screen,beamColor);
        } // end if
        else {
            // left beam
            tft.drawLine(0, WINDOW_HEIGHT-1,-4+rand()%8+target_x_screen,-4+rand()%8+target_y_screen,beamColor);
        } // end else

    } // end if

    // draw the informtion, comment out to reduce FLICKER
#if 0
    sprintf(buffer, "S:%d K:%d Miss:%d", score, hits, misses);
    DrawText(buffer, RGB16Bit(0,255,0));
#endif

    if (game_state==GAME_OVER) {
        DrawText("G A M E  O V E R", RGB16Bit(255,255,255), 20, 128 );
    } // end if

    // sync to 30ish fps
    while( ( millis() - startTime ) < 15 );

    // check for game state switch
    if (misses > TIES_MISSED_END_GAME) {
       game_state = GAME_OVER;
    }
} // end Game_Main


