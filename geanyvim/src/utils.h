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

#ifndef __GEANYVIM_UTILS_H__
#define __GEANYVIM_UTILS_H__

#include <glib.h>
#include <geanyplugin.h>

#include "state.h"

#define SSM(s, m, w, l) scintilla_send_message(s, m, w, l)

void accumulator_append(ViState *vi_state, const gchar *val);
void accumulator_clear(ViState *vi_state);
guint accumulator_len(ViState *vi_state);
gchar accumulator_current_char(ViState *vi_state);
gchar accumulator_previous_char(ViState *vi_state);
gint accumulator_get_int(ViState *vi_state, gint start_pos, gint default_val);

ScintillaObject *get_current_doc_sci(void);
gchar *get_current_word(ScintillaObject *sci);

void prepare_vi_mode(ScintillaObject *sci, ViState *vi_state, ViUi *vi_ui);
void clamp_cursor_pos(ScintillaObject *sci, ViState *vi_state, ViUi *vi_ui);
void perform_search(ScintillaObject *sci, ViState *vi_state, gboolean forward);

#endif
