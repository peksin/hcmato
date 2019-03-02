/*
=================================================================================
Author: Pekka Sinkkonen 1.3.2019

This is someday supposed to be a hardcore snake game. I started out trying to
learn 16 bit DOS graphics programming with "Alex Russell's Dos Game Programming
in C for Beginners". The idea was to learn how to draw graphics without the aid 
of external libraries such as SDL, OpenGL and the like.

http://www3.telus.net/alexander_russell/course/introduction.htm

I went along with the tutorial where applicable and made modifications as 
necessary. I then branched out to make a game of my own out of it.

I've left a ton of comments as notes for my future self in case I need to come
back to this project later on.

Compiled inside DOSBox with Borland Turbo C++ 3.0

TODO:
Snek behaviour
Divide subroutines to modules
Sound system
Check for latency

NEXT LEVEL TODO:
Moving collectibles?
Multiplayer?
Explosion graphics?
High score -table?
Dynamic colours?
Port to SDL or something similar?
=================================================================================
*/

#include <stdio.h>
#include <conio.h>
#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <dos.h>
#include <time.h>

#define INPUT_STATUS_0 0x3da

// BIOS data area pointer to incrementing unsigned long integer
// We'll use this as a timer for animations etc
#define TICKS (*(volatile unsigned long far *)(0x0040006CL))

#define BYTE unsigned char

// codes for the keys used to control the player pixel
enum
{
	ESC = 27,
	ARROW_UP = 256 + 72,
	ARROW_DOWN = 256 + 80,
	ARROW_LEFT = 256 + 75,
	ARROW_RIGHT = 256 + 77
};

// struct for pixel location and movement speed
struct pixel
{
	int x, y, dx, dy;
};

// snek snek[0] and snek declarations
struct pixel *snek;


/*
===========================================================================
Global variables
===========================================================================
*/

unsigned char far *screen;		// pointer to the VGA video memory
unsigned char far *off_screen;	// pointer to the off screen buffer
int screen_width, screen_height;
unsigned int screen_size;

int old_mode;					// old video mode before we change it


/*
===========================================================================
Global player-related pixel declarations
===========================================================================
*/



/*
=================================================================================
Get TICK
This returns a number that increases by one 18 times a second so it is useful
to us as a timer to bind animation speed to something else other than CPU clock 
speed *memories of Bethesda tying their physics engine to FPS*
=================================================================================
*/

unsigned long get_tick(void)
{
	return (TICKS);
}

/*
=================================================================================
Enter mode 13h
This is 320x200, 256 colours. Video memory is laid out in a simple rectangular 
grid where 0,0 is at the top left, x increases to the right, and y increases 
down. Each pixel is one byte, and its colour is determined by the colour palette
it indexes. Mode 13h has a colour palette of 256 colours, each of which is 
defined by 6 bits of red, blue, and green. Pixel coordinates are always presented 
as a pair of numbers indicating the X and Y position of the pixel. For example, 
the black pixel above is at location (3,1) Please note that the top left corner 
is (0,0) NOT (1,1). The bottom right pixel is (319, 199). This linear arrangement
if video memory is common to most modern video systems. While it is natural to 
think of video memory as a grid like this, it is actually just a long array of
memory.

Some useful BIOS interrupts:

0x10      the BIOS video interrupt. 
0x0C      BIOS func to plot a pixel. 
0x00      BIOS func to set the video mode. 
0x13      use to set 256-color mode. 
0x03      use to set 80x25 text mode. 
=================================================================================
*/

void enter_mode13h(void)
{
	union REGS in, out;

	// get old video mode
	in.h.ah = 0xf;
	int86(0x10, &in, &out);
	old_mode = out.h.al;

	// enter mode 13h
	in.h.ah = 0;
	in.h.al = 0x13;
	int86(0x10, &in, &out);
}

/*
==============================================================================
Leave mode 13h
=============================================================================
*/

void leave_mode13h(void)
{
	union REGS in, out;

	// change to the video mode we were in before we switched to mode 13h
	in.h.ah = 0;
	in.h.al = old_mode;
	int86(0x10, &in, &out);
}

/*
================================================================================
Initialize mode13h and create the off screen buffer

We wait for the vertical re-trace by checking the value of the INPUT_STATUS port
on the VGA card. This returns a number of flags about the VGA's current state.
Bit 3 tells if it is in a vertical blank (2^3 = 8). We first wait until it is
NOT blanking, to make sure we get a full vertical blank time for our copy. 
Then we wait for a vertical blank.
===============================================================================
*/

int init_video_mode(void)
{
	off_screen = farmalloc(64000u);

	if (off_screen)
	{
		screen = MK_FP (0xa000, 0);
		screen_width = 320;
		screen_height = 200;
		screen_size = 64000u;
		enter_mode13h();
		_fmemset(off_screen, 0, screen_size);
		return 0;
	}
	else
	{
		// no memory -> return error code
		leave_mode13h();
		printf("Out of memory!\n");
		return 1;
	}	
}

// copy the off screen buffer to video memory
void update_buffer(void)
{
	// wait for vertical re-trace
	while ( inportb(INPUT_STATUS_0) & 8 )
		;
	while ( !(inportb(INPUT_STATUS_0) & 8) )
		;
	
	// copy everything to video memory
	_fmemcpy(screen, off_screen, screen_size);
}

/*
================================================================================
Draw a pixel

Now that we can update the whole screen lets draw a single pixel. To draw a 
pixel we need to calculate where in the off screen buffer to change the
value of one byte to the index of the colour we want. If we want to draw on 
the first line it is easy, it is just x pixels from the start. To draw on a 
line other than the first line we have to move down to the yth line. How do 
we do that? Each line is 320 pixels long. For each y we must move 320 pixels.
Therefore the calculation of a pixel's position is:

off_screen = pointer to the off screen buffer

Offset = y * screen_width + x; (screen width being 320 in this case)

y * screen_width + x	this gives us the offset from the start of the buffer to
the position of the pixel at (x,y)
*(off_screen + offset) sets the value at offset to colour.

Then update_buffer() would have to be called to make the pixel actually appear 
on the CRT screen.
To get the value of a pixel we just return the value in off_screen at the offset
for x, and y. 

===============================================================================
*/

void draw_pixel(int x, int y, int colour)
{
	*(off_screen + y * screen_width + x) = colour;
}

/*
=============================================================================
Draw a horizontal line

Horizontal lines are only slightly more complicated than pixels. The offset 
to the start of the line is calculated then the line is drawn by setting 
adjacent pixels on the same line to the colour. 

p is set to point to the start of the horizontal line then the memset()
function is used to fill in the line with colour. This code does not check 
to see if the line goes off the right edge of the screen. If the line did 
extend past the right edge it would continue on the next line starting at 
the left edge. Drawing a very long line at (319, 199) can crash the computer 
as the line will be 'drawn' into memory past the end of the of screen memory. 
=============================================================================
*/

void horz_line(int x, int y, int length, int colour)
{
	unsigned char far *p;

	p = off_screen + y * screen_width + x;	// make p point to the start of the
											// line
	_fmemset(p, colour, length);			// fill in the line
}

/*
===============================================================================
Get key
"Press any key to continue" etc...
===============================================================================
*/

int get_key(void)
{
	int key;

	key = getch();
	if (key == ESC)
	{
		farfree(off_screen);
		leave_mode13h();
		exit(1);
	}

	return key;
}

/*
===============================================================================
Get code
Get a keycode for keypress
===============================================================================
*/

int get_code (void)
{
  int ch = getch();

  if ( ch == 0 || ch == 224 )
    {
		ch = 256 + getch();
	}

  return ch;
}

/*
===================================================================================
Check if snek needs to be wrapped to the other side of the screen
===================================================================================
*/

void check_wrap(void)
{
	// make the player pixel wrap to the other side if needed
	// left border
	if ( snek[0].x < 1 )
		{
			snek[0].x = 319;
		}					 
	
	// right border	
	if ( snek[0].x > 319 )
		{
			snek[0].x = 0;	
		}				 
	
	// top border
	if ( snek[0].y < 1 )
		{
		
			snek[0].y = 199;					 
		}
	// bottom border
	if ( snek[0].y > 199 )
		{
			snek[0].y = 0;					 
		}
}

/*
===================================================================================
Check if the key that is pressed should do something
===================================================================================
*/

void check_key(void)
{
	switch(get_code())
		{
			case ARROW_UP: // key up
				if(snek[0].dx != 0)
				{
					snek[0].dy += -1;
					snek[0].dx = 0;
				}
				break;
			case ARROW_DOWN: // key down
				if(snek[0].dx != 0)
				{
					snek[0].dy += 1;
					snek[0].dx = 0;
				}
				break;
			case ARROW_RIGHT: // key right
				if(snek[0].dy != 0)
				{
					snek[0].dx += 1;
					snek[0].dy = 0;
				}
				break;
			case ARROW_LEFT: // key left
				if(snek[0].dy != 0)
				{
					snek[0].dx += -1;
					snek[0].dy = 0;
				}
				break;
			case ESC:							
				farfree(off_screen);
				leave_mode13h();
				exit(1);	
			default: // not pressing anything etc
				break;
		}
}

/*
===================================================================================
Draw the player pixel
This was made from the bounce_pixel function and it turned into the main game
loop apparently...
==================================================================================
*/

void draw_player(void)
{
	int done;
	long next_time;
	int keypress;
	int snek_length = 5;

	// for loop variables
	int i; 		
	int j;
	int k;

	int collect_x;				// collectible location x
	int collect_y;				// collectible location y
	int isCollectible = 0;		// is there a collectible already on screen?

	// player's lead pixel location
	snek[0].x = 150, snek[0].y = 90, snek[0].dx = 0, snek[0].dy = -1;

	// KOKEITA
	snek[1].x = 150, snek[1].y = 90, snek[1].dx = 0, snek[1].dy = -1;
	snek[2].x = 150, snek[2].y = 91, snek[2].dx = 0, snek[2].dy = -1;
	snek[3].x = 150, snek[3].y = 92, snek[3].dx = 0, snek[3].dy = -1;
	snek[4].x = 150, snek[4].y = 93, snek[4].dx = 0, snek[4].dy = -1;

	done = 0; // flag for done
	next_time = get_tick() + 1;  // a timer

	while ( !done )
		{
		// move at a steady speed on all computers
		// if not enough time has NOT passed, redraw the 
		// screen without moving
		if ( get_tick() >= next_time )
			{
				// draw snek tail
			/*	for(k = 1; k > snek_length; k++)
				{	
					snek[k].x = snek[0].x + 1;
					snek[k].y = snek[0].y + 1; 
					snek[k].dx = snek[0].dx; 
					snek[k].dy = snek[0].dy;
				} */
				// detect collision with player lead pixel and collectible
				if(snek[0].x == collect_x && snek[0].y == collect_y)
				{
					// collectible is destroyed, make a new one on the next loop
					isCollectible = 0;

					snek_length++;
				}

				// check for user input
				if(kbhit())
				{
					check_key();
				}

				// collecting a collectible -> this step will be skipped
				if(isCollectible)
				{
					// remove old snek tail IS THIS STILL NEEDED?
					draw_pixel(snek[snek_length - 1].x, snek[snek_length -1].y, 0);
				}
				
				for(k = snek_length - 1; k > 0; k--)
				{
					snek[k].x = snek[k - 1].x;
					snek[k].y = snek[k - 1].y;
					snek[k].dx = snek[k - 1].dx;
					snek[k].dy = snek[k - 1].dy;
					

					/*

					KOKEITA
					snek[4].x = snek[3].x;
					snek[4].y = snek[3].y;
					snek[4].dx = snek[3].dx;
					snek[4].dy = snek[3].dy;

					snek[3].x = snek[2].x;
					snek[3].y = snek[2].y;
					snek[3].dx = snek[2].dx;
					snek[3].dy = snek[2].dy;

					snek[2].x = snek[1].x;
					snek[2].y = snek[1].y;
					snek[2].dx = snek[1].dx;
					snek[2].dy = snek[1].dy;				

					snek[1].x = snek[0].x;
					snek[1].y = snek[0].y;
					snek[1].dx = snek[0].dx;
					snek[1].dy = snek[0].dy;
					*/
				}




				// move snek[0] of snek
				snek[0].x += snek[0].dx;
				snek[0].y += snek[0].dy;

				// check for wrapping
				check_wrap();

				next_time = get_tick();
			}
		
		if(!isCollectible)
		{
			// draw a collectible in a random location and store its location
			collect_x = random(screen_width);
			collect_y = random(screen_height);
			draw_pixel(collect_x, collect_y, 32 + random(48));

			// now there's a collectible so don't draw any new ones
			isCollectible = 1;		
		}

		// draw snek
		for(i = 0; i < snek_length; i++)
		{
			draw_pixel(snek[i].x, snek[i].y, 5);
		}

			update_buffer();
		}
}

/*
===================================================================================
Main loop
==================================================================================
*/

void main(void)
{
	/*long z;*/

	// allocate memory for the snek. This will be lengthened if necessary
	snek = malloc((64000) * sizeof(struct pixel));

	// fill the snek array with zeroes 
	/*for(z = 0; z < 64000; z++)
	{
		snek[z].x = 0;
		snek[z].y = 0;
	} */

	

	srand(time(NULL)); 			// seed the random generator

	printf("Welcome to Pekka's VGA experiment!\n");
	printf("All lefts reversed and so forth\n");
	printf("Press ESC to exit, or press any other key to continue\n");

	get_key();

	init_video_mode();

	// main game loop is this one
	draw_player();

	leave_mode13h();

	// free memory
	free(snek);
	farfree(off_screen);

}