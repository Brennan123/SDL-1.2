/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2006 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_config.h"

#include "SDL_video.h"
#include "SDL_mouse.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_events_c.h"

#include "SDL_libogcvideo.h"
#include "SDL_libogcevents_c.h"
#include "SDL_libogcmouse_c.h"

#include <malloc.h>
#define LIBOGCVID_DRIVER_NAME "libogc"

/*** SDL ***/
static SDL_Rect mode_320;
static SDL_Rect mode_640;

static SDL_Rect* modes_descending[] =
{
        &mode_640,
        &mode_320,
        NULL
};

/* Initialization/Query functions */
static int libogc_VideoInit(_THIS, SDL_PixelFormat *vformat);
static SDL_Rect **libogc_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags);
static SDL_Surface *libogc_SetVideoMode(_THIS, SDL_Surface *current, int width, int height, int bpp, Uint32 flags);
static int libogc_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color *colors);
static void libogc_VideoQuit(_THIS);

/* Hardware surface functions */
static int libogc_AllocHWSurface(_THIS, SDL_Surface *surface);
static int libogc_LockHWSurface(_THIS, SDL_Surface *surface);
static void libogc_UnlockHWSurface(_THIS, SDL_Surface *surface);
static void libogc_FreeHWSurface(_THIS, SDL_Surface *surface);

/* etc. */
static void libogc_UpdateRects(_THIS, int numrects, SDL_Rect *rects);

/* libogc driver bootstrap functions */

static int libogc_Available(void)
{
	return(1);
}

static void libogc_DeleteDevice(SDL_VideoDevice *device)
{
	SDL_free(device->hidden);
	SDL_free(device);
}

static SDL_VideoDevice *libogc_CreateDevice(int devindex)
{
	SDL_VideoDevice *device;

	/* Initialize all variables that we clean on shutdown */
	device = (SDL_VideoDevice *)SDL_malloc(sizeof(SDL_VideoDevice));
	if ( device ) {
		SDL_memset(device, 0, (sizeof *device));
		device->hidden = (struct SDL_PrivateVideoData *)
				SDL_malloc((sizeof *device->hidden));
	}
	if ( (device == NULL) || (device->hidden == NULL) ) {
		SDL_OutOfMemory();
		if ( device ) {
			SDL_free(device);
		}
		return(0);
	}
	SDL_memset(device->hidden, 0, (sizeof *device->hidden));

	/* Set the function pointers */
	device->VideoInit = libogc_VideoInit;
	device->ListModes = libogc_ListModes;
	device->SetVideoMode = libogc_SetVideoMode;
	device->CreateYUVOverlay = NULL;
	device->SetColors = libogc_SetColors;
	device->UpdateRects = libogc_UpdateRects;
	device->VideoQuit = libogc_VideoQuit;
	device->AllocHWSurface = libogc_AllocHWSurface;
	device->CheckHWBlit = NULL;
	device->FillHWRect = NULL;
	device->SetHWColorKey = NULL;
	device->SetHWAlpha = NULL;
	device->LockHWSurface = libogc_LockHWSurface;
	device->UnlockHWSurface = libogc_UnlockHWSurface;
	device->FlipHWSurface = NULL;
	device->FreeHWSurface = libogc_FreeHWSurface;
	device->SetCaption = NULL;
	device->SetIcon = NULL;
	device->IconifyWindow = NULL;
	device->GrabInput = NULL;
	device->GetWMInfo = NULL;
	device->InitOSKeymap = libogc_InitOSKeymap;
	device->PumpEvents = libogc_PumpEvents;

	device->free = libogc_DeleteDevice;

	return device;
}

VideoBootStrap libogc_bootstrap = {
	LIBOGCVID_DRIVER_NAME, "SDL libogc video driver",
	libogc_Available, libogc_CreateDevice
};


int libogc_VideoInit(_THIS, SDL_PixelFormat *vformat)
{
	// Set up the modes.
	//mode_640.w = display_mode->fbWidth;
	//mode_640.h = display_mode->xfbHeight;
	mode_320.w = mode_640.w / 2;
	mode_320.h = mode_640.h / 2;

	// Set the current format.
	vformat->BitsPerPixel   = 16;
	vformat->BytesPerPixel  = 2;

	this->hidden->buffer = NULL;
	this->hidden->w = 0;
	this->hidden->h = 0;

	/* We're done! */
	return(0);
}

SDL_Rect **libogc_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags)
{
   	 return &modes_descending[0];
}

SDL_Surface *libogc_SetVideoMode(_THIS, SDL_Surface *current,
				int width, int height, int bpp, Uint32 flags)
{
	SDL_Rect*	mode;
	size_t		bytes_per_pixel;
	Uint32		r_mask = 0;
	Uint32		b_mask = 0;
	Uint32		g_mask = 0;

	// Find a mode big enough to store the requested resolution
	mode = modes_descending[0];
	while (mode)
	{
		if (mode->w == width && mode->h == height)
			break;
		else
			++mode;
	}

	// Didn't find a mode?
	if (!mode)
	{
		SDL_SetError("Display mode (%dx%d) is unsupported.", width, height);
		return NULL;
	}

	if(bpp != 8 && bpp != 16 && bpp != 24)
	{
		SDL_SetError("Resolution (%d bpp) is unsupported (8/16/24 bpp only).",bpp);
		return NULL;
	}

	bytes_per_pixel = bpp / 8;

	// Free any existing buffer.
	if (this->hidden->buffer)
	{
		free(this->hidden->buffer);
		this->hidden->buffer = NULL;
	}

	// Allocate the new buffer.
	this->hidden->buffer = memalign(32, width * height * bytes_per_pixel);
	if (!this->hidden->buffer )
	{
		SDL_SetError("Couldn't allocate buffer for requested mode");
		return(NULL);
	}

	// Allocate the new pixel format for the screen
	if (!SDL_ReallocFormat(current, bpp, r_mask, g_mask, b_mask, 0))
	{
		free(this->hidden->buffer);
		this->hidden->buffer = NULL;

		SDL_SetError("Couldn't allocate new pixel format for requested mode");
		return(NULL);
	}

	// Clear the buffer
	SDL_memset(this->hidden->buffer, 0, width * height * bytes_per_pixel);

	// Set up the new mode framebuffer
	current->flags = SDL_DOUBLEBUF | (flags & SDL_FULLSCREEN) | (flags & SDL_HWPALETTE);
	current->w = width;
	current->h = height;
	current->pitch = current->w * bytes_per_pixel;
	current->pixels = this->hidden->buffer;

	/* Set the hidden data */
	this->hidden->w = current->w;
	this->hidden->h = current->h;

	//draw_init(current);

	/* We're done */
	return(current);
}

/* We don't actually allow hardware surfaces other than the main one */
static int libogc_AllocHWSurface(_THIS, SDL_Surface *surface)
{
	return(-1);
}
static void libogc_FreeHWSurface(_THIS, SDL_Surface *surface)
{
	/* do nothing. */
}

/* We need to wait for vertical retrace on page flipped displays */
static int libogc_LockHWSurface(_THIS, SDL_Surface *surface)
{
	return(0);
}

static void libogc_UnlockHWSurface(_THIS, SDL_Surface *surface)
{
	/* do nothing. */
}

static void libogc_UpdateRects(_THIS, int numrects, SDL_Rect *rects)
{
	/* do nothing. */
}

int libogc_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color *colors)
{
	/* do nothing of note. */
	return(1);
}

/* Note:  If we are terminated, this could be called in the middle of
   another SDL video routine -- notably UpdateRects.
*/
void libogc_VideoQuit(_THIS)
{
	if (this->screen->pixels != NULL)
	{
		SDL_free(this->screen->pixels);
		this->screen->pixels = NULL;
	}
}
