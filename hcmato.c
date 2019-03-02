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
Collectibles and snek behaviour
Divide subroutines to modules
Sound system
Check if the global variables for player pixel actually need to be global

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
Global player pixel variables (do I really need these here?)
===========================================================================
*/

// player's lead pixel location
int play_x = 150, play_y = 90, play_dx = 0, play_dy = -1;

// player's tail pixel location
int tail_x = 150, tail_y = 90, tail_dx = 0, tail_dy = -1;

// player snek length
int play_length = 5;

/*
=================================================================================
Get TICK
This returns a number that increases by one 18 times a second so it is useful
to us as a timer to bind animation speed to something else than CPU clock speed
*memories of Bethesda tying their physics engine to FPS*
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

Note that '*' means both 'de-reference pointer' and multiplication in c. The
compiler can tell which meaning is to be used from the context of the code.
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
	_fmemset(p, colour, length);								// fill in the line
}

/*
===============================================================================
Draw a vertical line

For vertical lines we have to move down one line each pixel. This is the same
as moving the width of the screen into the off screen buffer.

The pixel at (3, 0) is at offset:
Off set = y*320 + x
Off set = 0*320 + 3
Off set = 3


The pixel  at (3,1) is at offset:
Off set = y*320 + x
Off set = 1*320 + 3
Off set = 323

As you can see, as we move down one row the offset will increase by the width
of the screen.
===============================================================================
*/

void ver_line(int x, int y, int len, int colour)
{
	unsigned char far *p;

	p = off_screen + y * screen_width + x;	// make p point to the start of the line
	while (len--)														// repeat for entire line length
	{
		*p = colour;													// set one pixel
		p += screen_width;										// move down one row
	}
}

/*
===============================================================================
Draw a filled rectangle
Filled rectangles are drawn by drawing horizontal lines to fill the rectangle.
The off set of the first line is calculated, a horizontal line drawn, then we 
move down one row and repeat. 
===============================================================================
*/

void rect_fill(int x, int y, int width, int height, int colour)
{
	unsigned char far *p;

	p = off_screen + y * screen_width + x;	// make p point to the start of the line
	while (height--)									// repeat for the entire line height
	{
		_fmemset(p, colour, width);			// set one line
		p += screen_width;							// move down one row
	}
}

/*
===============================================================================
Draw arbitrary lines
The obvious way to draw an arbitrary line is to code the equation of a line. 
This works, but is quite slow in practice. In 1965 J. E. Bresenham presented a 
much faster way to draw lines using discrete pixels. Instead of calculating the 
position of each pixel from the equation of a line, the line is drawn by moving 
in one direction at a constant rate, and moving in the other in proportion to the 
slope of the line. For example, a line that starts at (0,0) and goes to (200, 10) 
you draw a pixel at (0,0), move x to the left then check if y should be increased 
using pre-calculated variables. A bit of algebra is required to calculate these 
variables, but it turns out it is a very simple and quick operation.

The c code presents a straight forward implementation of Bresenham's line 
drawing algorithm. There are faster ways to code it (in assembler this algorithm 
can be very optimized), and there are now faster algorithms, e.g. "line slicing", 
but as hardware does more and more for us, it becomes less important.

Note: for unsigned integers x<<1 is the same as x*2, and x>>1 is the same as x/2. 
===============================================================================
*/

void line(int x0, int y0, int x1, int y1, int colour)
{
	int  inc1, inc2, i;
	int cnt, y_adj, dy, dx, x_adj;
	unsigned char far *p;

	if ( x0 == x1 )
		{
		// vertical line
		if ( y0 > y1 )
			{
			i = y0;
			y0 = y1;
			y1 = i;
			}

		p = off_screen + y0 * screen_width + x0;
		i = y1 - y0 + 1;
		while ( i-- )
			{
			*p = colour;
			p += screen_width;
			}
		}
	else
		{
		if ( y0 == y1 )
			{
			// horizontal line
			if ( x0 > x1 )
				{
				i=x0;
				x0=x1;
				x1=i;
				}
			p = off_screen + y0 * screen_width + x0;
			i = x1 - x0 + 1;
			_fmemset(p, colour, i);
			}
		else
			{
			// general line --------------------------------------
			dy = y1 - y0; 
			dx  =x1 - x0; 
			// is it a shallow, or steep line?
			if ( abs(dy) < abs(dx) )
				{
				// lo slope, shallow line
				// we always want to draw from left to right
				if ( x0 > x1 )
					{
					// swap x's, and y's
					i = x0;
					x0 = x1;
					x1 = i;
					i = y0;
					y0 = y1;
					y1 = i;
					}
				dy = y1 - y0;  // dy is used to calculate the increments
				dx = x1 - x0;  // dx is line length
				if ( dy < 0 )
					{
					// going up the screen
					dy =- dy;
					y_adj =- screen_width;
					}
				else
					y_adj = screen_width;	 // going down

				// calulate the increments
				inc1 = dy << 1;
				inc2 = (dy - dx) << 1;
				cnt = (dy << 1) - dx;

				// set p to start pixel
				p = off_screen + y0 * screen_width + x0;
				dx++;
				while ( dx-- )  // for the length of the line
					{
					*p++ = colour;  
					// set one pixel, move right one pixel

					if ( cnt >= 0 ) // is it time to adjust y?
						{
						cnt += inc2;
						p += y_adj;
						}
					else
						cnt += inc1;
					}
				}
			else
				{
				// hi slope - like lo slope turned on its side
				// always draw top to bottom
				if ( y0 > y1 )
					{
					// swap x's, and y's
					i = x0;
					x0 = x1;
					x1 = i;
					i = y0;
					y0 = y1;
					y1 = i;
					}

				dy = y1 - y0;  // dy is line length
				dx = x1 - x0;  // dx is used to calculate incr's

				if ( dx < 0)
					{
					dx =- dx;
					x_adj =- 1;  // moving left
					}
				else
					x_adj = 1;   // moving right

				inc1=dx << 1;
				inc2 = (dx - dy) << 1;
				cnt=(dx << 1) - dy;

				// set p to first pixel position
				p=off_screen + y0 * screen_width + x0;
				dy++;
				while ( dy-- )  // for height of line
					{
					*p = colour;   // set one pixel
					p += screen_width;  // move down one pixel

					if ( cnt >= 0 )  // is it time to move x?
						{
						cnt += inc2;
						p += x_adj;
						}
					else
						cnt += inc1;
					}
				}
			}
		}
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
Make a pixel bounce around without leaving a trail

TODO: figure out why it leaves a trail when bouncing from a border
==================================================================================
*/

void bounce_pixel1(void)
{
	int done;
	int x, y, dx, dy;
	long next_time;

	x = 10;  // current x position
	y = 20;  // current y position
	done = 0; // flag for done
	dx = 1;   // amount to move in x direction
	dy = 1;   // amount to move in y direction
	next_time = get_tick() + 1;  // a timer

	while ( !done )
		{
		// move at a steady speed on all computers
		// if not enough time has NOT passed, redraw the 
		// screen with out moving
		if ( get_tick() >= next_time )
			{
			// move
			x += dx;
			y += dy;

		// remove the old pixel (by drawing a black pixel in its stead mehmehmeh)
		// this is done to remove the trailing
		// this has to be done before bounce checks so that it will also remove
		// pixels from the bounce points along the border
		draw_pixel(x - dx, y - dy, 0);

			// check for bouncing
			// left border
			if ( x < 0 )
				{
				x = 0;
				dx =- dx;  // move in other direction
				}
	
			// right border	
			if ( x > 319 )
				{
				x = 319;
				dx =- dx; // move in other direction 
				}
			
			// top border
			if ( y < 0 )
				{
				y = 0;
				dy =- dy;   // move in other direction 
				}
			// bottom border
			if ( y > 199 )
				{
				y = 199;
				dy =- dy;   // move in other direction 
				}


			next_time = get_tick();
			}

		// draw, as fast as we can
		draw_pixel(x, y, 5);
		update_buffer();

		// check for user input
		if ( kbhit() )
			if ( getch() == ESC )
				done = 1;
		}

}

/*
===================================================================================
Random number generator from stack overflow
Turns out random() isn't actually random if you don't seed it first
Returns a random number between 0 and limit
==================================================================================
*/
/*
int rand_lim(int limit) 
{
    int divisor = RAND_MAX/(limit+1);
    int result;

    do 
	{ 
        result = rand() / divisor;
    } while (result > limit);

    return result;
}
*/
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

	int collect_x;				// collectible location x
	int collect_y;				// collectible location y
	int isCollectible = 0;		// is there a collectible already on screen?

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
				if(play_x == collect_x && play_y == collect_y)
				{
					// collectible is destroyed, make a new one on the next loop
					isCollectible = 0;
				}

				if(kbhit()) // check for user input
				{
					switch(get_code())
					{
						case ARROW_UP: // key up
							if(play_dx != 0)
							{
								play_dy += -1;
								play_dx = 0;
							}
							break;
						case ARROW_DOWN: // key down
							if(play_dx != 0)
							{
								play_dy += 1;
								play_dx = 0;
							}
							break;
						case ARROW_RIGHT: // key right
							if(play_dy != 0)
							{
								play_dx += 1;
								play_dy = 0;
							}
							break;
						case ARROW_LEFT: // key left
							if(play_dy != 0)
							{
								play_dx += -1;
								play_dy = 0;
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
				// move
				play_x += play_dx;
				play_y += play_dy;

				// remove the old pixel (by drawing a black pixel in its stead mehmehmeh)
				// this is done to remove the trailing
				// this has to be done before border collision or bounce
				// checks so that it will also remove pixels from the bounce points along
				// the border
				draw_pixel(play_x - play_dx, play_y - play_dy, 0);

				// make the player pixel wrap to the other side
				// left border
				if ( play_x < 1 )
					{
						play_x = 319;					 
					}

				// right border	
				if ( play_x > 319 )
					{
						play_x = 0;					 
					}

				// top border
				if ( play_y < 1 )
					{
						play_y = 199;					 
					}
				// bottom border
				if ( play_y > 199 )
					{
						play_y = 0;					 
					}


				next_time = get_tick();
			}
		
		if(!isCollectible)
		{
			// draw a collectible in a random location and store its location
			collect_x = random(screen_width);
			collect_y = random(screen_height);
			draw_pixel(collect_x, collect_y, random(256));

			// now there's a collectible so don't draw any new ones
			isCollectible = 1;		
		}

		// TODO: retrieve location of that collectible
		/* if <pixel location on memory> != 0(black) -> */
		



		// draw, as fast as we can
		draw_pixel(play_x, play_y, 5);
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
	srand(time(NULL)); 			// seed the random generator

	printf("Welcome to Pekka's VGA experiment!\n");
	printf("All lefts reversed and so forth\n");
	printf("Press ESC to exit, or press any other key to continue\n");

	get_key();

	init_video_mode();

	// main game loop is this one
	draw_player();

	leave_mode13h();
	farfree(off_screen);

}