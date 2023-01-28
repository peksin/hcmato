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

-Sanitize snek length input
-Add snek collision with itself
-Prevent game from drawing collectibles on top of the snek

-Divide subroutines to modules
-Sound system
-Check for latency

NEXT LEVEL TODO:
Moving collectibles?
Multiplayer?
Explosion graphics?
High score -table?
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

// codes for the keys used to control the game
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
	int x, y, dx, dy, colour;
};

// snek declaration
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

int done;						// flag for done
long int snek_length = 5;		// length of snek
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
of video memory is common to most modern video systems. While it is natural to 
think of video memory as a grid like this, it is actually just a long array of
memory.

Some useful BIOS interrupts:

0x10      the BIOS video interrupt. 
0x0C      BIOS func to plot a pixel. 
0x00      BIOS func to set the video mode. 
0x13      use to set 256-colour mode. 
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

Mode 13h doesn't support page flipping so double buffering is the way to go.
===============================================================================
*/

int init_video_mode(void)
{
	off_screen = farmalloc(64000u);

	if (off_screen)
	{
		screen = MK_FP (0xa000, 0); // create a far pointer to video memory
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
"Press any key to continue" in the splash screen
===============================================================================
*/

void get_key(void)
{
	int key;

	key = getch();
	if (key == ESC)
	{
		free(snek);
		exit(1);
	}
}

/*
===================================================================================
Get integer
Prompt the user for integer input and sanitize it
==================================================================================
*/


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
========================================================================
Check if the key that is pressed should do something.
Head of snek is the only pixel that needs to be moved here, the rest
will follow automatically. This is used ingame ONLY.
========================================================================
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
				done = 1;
			default: // not pressing anything etc
				break;
		}
}

/*
===================================================================================
Draw the player pixel
This was made from the now deprecated bounce_pixel() function and it turned into 
the main game loop apparently...
==================================================================================
*/

void draw_player(void)
{
	long next_time;

	// for loop variables
	int h, k, i;		

	int collect_x;				// collectible location x
	int collect_y;				// collectible location y
	int collect_colour;			// collectible colour
	int isCollectible = 0;		// is there a collectible already on screen?

	// save the initial snek location in memory
	for(h = 0; h < snek_length; h++)
	{
		snek[h].x = 150, snek[h].y = 90 + h; 
		snek[h].dx = 0, snek[h].dy = -1;
		snek[h].colour = 5;
	} 

	done = 0; // flag for done
	next_time = get_tick() + 1;  // a timer

	while ( !done )
		{
		// move at a steady speed on all computers
		// if not enough time has NOT passed, redraw the 
		// screen without moving
		if ( get_tick() >= next_time )
			{
				// detect collision with player lead pixel and collectible
				if(snek[0].x == collect_x && snek[0].y == collect_y)
				{
					// collectible is destroyed, make a new one on the next loop
					isCollectible = 0;
					snek_length++;
					snek[snek_length -1].colour = collect_colour;
				}

				// check for user input
				if(kbhit())
				{
					check_key();
				}

				// remove old snek tail
				draw_pixel(snek[snek_length - 1].x, snek[snek_length -1].y, 0);

				// move snek pixel info one step backwards so that each pixel
				// follows the one that came before it
				for(k = snek_length - 1; k > 0; k--)
				{
					snek[k].x = snek[k - 1].x, snek[k].y = snek[k - 1].y;
					snek[k].dx = snek[k - 1].dx, snek[k].dy = snek[k - 1].dy;
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
			collect_colour = 32 + random(48);
			draw_pixel(collect_x, collect_y, collect_colour);

			// now there's a collectible so don't draw any new ones
			isCollectible = 1;		
		}

		// draw snek (both head and tail)
		for(i = 0; i < snek_length; i++)
		{
			draw_pixel(snek[i].x, snek[i].y, snek[i].colour);
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
	// allocate memory for the snek. This will be lengthened if necessary
	snek = malloc((64000) * sizeof(struct pixel));

	// seed the random generator
	srand(time(NULL)); 			

	printf("Welcome to Pekka's VGA experiment!\n");
	printf("All lefts reversed and so forth\n");
	printf("\n");
	printf("This is a hardcore snake game with wrapping borders\n");
	printf("Use the arrow keys to move and pause button to pause\n");
	printf("You can always escape the game by pressing ESC\n\n");
	printf("Press ESC to exit, or press any other key to continue\n");

	get_key();

	//printf("How long would you like the initial snek to be?\n");
	
	snek_length = 50;

	//get_integer();

	init_video_mode();

	// main game loop is this one
	draw_player();

	// once you press esc in draw_player(), program continues here
	leave_mode13h();

	// free memory
	free(snek);
	farfree(off_screen);

	printf("Thanks for playing!\n");
	printf("Made with Borland Turbo C++ 3.0 in 2019\n");
	printf("-Pekka\n");

}