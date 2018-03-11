/*
 * Copyright 2018 Jiri Techet <techet@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __GEANYVIM_STATE_H__
#define __GEANYVIM_STATE_H__

#include <gtk/gtk.h>

typedef enum {
	VI_MODE_COMMAND,
	VI_MODE_INSERT,
	VI_MODE_VISUAL,
	VI_MODE_REPLACE,
} ViMode;

typedef struct
{
	/* the last full search command, including '/' or '?' */
	gchar *search_text;

	/* input accumulated over time (e.g. for commands like 100dd) */
	gchar *accumulator;
} ViState;


typedef struct
{
	/* caret style used by Geany we can revert to when disabling vi mode */
	gint default_caret_style;

	/* whether vi mode is enabled or disabled */
	gboolean vi_enabled; 
	/* if vi mode is valid for a single command and will be disabled automatically
	 * after performing it */
	gboolean vi_onetime;
	/* vi mode */
	ViMode vi_mode;
} ViUi;

void enter_cmdline_mode(void);

#endif
