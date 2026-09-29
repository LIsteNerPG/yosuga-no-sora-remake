/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#include "SDLBitmapCompletion.h"
#include "DebugIntf.h"

TVPSDLBitmapCompletion::TVPSDLBitmapCompletion()
{
	surface = nullptr;
	update_rect.clear();
}

void TVPSDLBitmapCompletion::NotifyBitmapCompleted(iTVPLayerManager * manager,
	tjs_int x, tjs_int y, const void * bits, const class BitmapInfomation * bmpinfo,
	const tTVPRect &cliprect, tTVPLayerType type, tjs_int opacity)
{
	if (!surface)
	{
		return;
	}
	const TVPBITMAPINFO *bitmapinfo = bmpinfo->GetBITMAPINFO();
	tjs_int w = 0;
	tjs_int h = 0;
	if (!manager)
	{
		return;
	}
	if (!manager->GetPrimaryLayerSize(w, h))
	{
		w = 0;
		h = 0;
	}
	if(
		!(x < 0 || y < 0 ||
			x + cliprect.get_width() > w ||
			y + cliprect.get_height() > h) &&
		!(cliprect.left < 0 || cliprect.top < 0 ||
			cliprect.right > bitmapinfo->bmiHeader.biWidth ||
			cliprect.bottom > bitmapinfo->bmiHeader.biHeight))
	{
		// bitmapinfo で表された cliprect の領域を x,y にコピーする
		long src_y       = cliprect.top;
		long src_y_limit = cliprect.bottom;
		long src_x       = cliprect.left;
		long width_bytes   = cliprect.get_width() * sizeof(tjs_uint32); // 32bit
		long dest_y      = y;
		long dest_x      = x;
		const tjs_uint8 * src_p = (const tjs_uint8 *)bits;
		long src_pitch;

		if (bitmapinfo->bmiHeader.biHeight < 0)
		{
			// bottom-down
			src_pitch = bitmapinfo->bmiHeader.biWidth * sizeof(tjs_uint32);
			//src_pitch = -bitmapinfo->bmiHeader.biWidth * sizeof(tjs_uint32);
			//src_p += bitmapinfo->bmiHeader.biWidth * sizeof(tjs_uint32) * (bitmapinfo->bmiHeader.biHeight - 1);
		}
		else
		{
			// bottom-up
			src_pitch = -bitmapinfo->bmiHeader.biWidth * sizeof(tjs_uint32);
			src_p += bitmapinfo->bmiHeader.biWidth * sizeof(tjs_uint32) * (bitmapinfo->bmiHeader.biHeight - 1);
			//src_pitch = bitmapinfo->bmiHeader.biWidth * sizeof(tjs_uint32);
		}

		if (surface)
		{
			/* The window surface can be smaller than the layer the engine draws
			 * into: the game switches to fullscreen right after startup
			 * (CONFIG.fullScreen defaults to 1), which resizes the window while
			 * the primary layer keeps its own virtual resolution. Without this
			 * clip the rows past the end of the surface buffer are written,
			 * corrupting the heap and producing the garbled picture on screen.
			 * Columns are bounded the same way for the same reason. */
			const long surface_pitch = surface->pitch;
			const long surface_width_bytes = (long)surface->w * (long)sizeof(tjs_uint32);
			const long dest_x_bytes = dest_x * (long)sizeof(tjs_uint32);
			const long src_x_bytes = src_x * (long)sizeof(tjs_uint32);
			SDL_LockSurface(surface);
			for (; src_y < src_y_limit; src_y++, dest_y++)
			{
				if (dest_y < 0 || dest_y >= surface->h)
				{
					continue;
				}
				long copy_bytes = width_bytes;
				if (dest_x_bytes + copy_bytes > surface_width_bytes)
				{
					copy_bytes = surface_width_bytes - dest_x_bytes;
				}
				if (copy_bytes <= 0)
				{
					continue;
				}
				const void *srcp = src_p + src_pitch * src_y + src_x_bytes;
				void *destp = (tjs_uint8*)surface->pixels + surface_pitch * dest_y + dest_x_bytes;
				SDL_memcpy(destp, srcp, copy_bytes);
			}
			SDL_UnlockSurface(surface);
		}
		tTVPRect r;
		r.set_offsets(x, y);
		r.set_size(cliprect.get_width(), cliprect.get_height());
		update_rect.do_union(r);
	}

}

TVPSDLBitmapCompletion::~TVPSDLBitmapCompletion()
{
}
