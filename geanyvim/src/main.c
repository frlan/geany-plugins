/*
 * Copyright (C) 2018 Jiri Techet <techet@gmail.com>
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <geanyplugin.h>

#include "state.h"
#include "cmds.h"
#include "utils.h"

#define CONF_GROUP "Settings"
#define CONF_VI_MODE "vi_mode"

GeanyPlugin *geany_plugin;
GeanyData *geany_data;

PLUGIN_VERSION_CHECK(235)

PLUGIN_SET_TRANSLATABLE_INFO(
	LOCALEDIR,
	GETTEXT_PACKAGE,
	_("Geany Vim"),
	_("Vim mode for Geany"),
	VERSION,
	"Jiří Techet <techet@gmail.com>"
)

enum
{
	KB_TOGGLE_VIM_MODE,
	KB_PERFORM_VIM_COMMAND,
	KB_COUNT
};

struct
{
	GtkWidget *prompt;
	GtkWidget *entry;
	GtkWidget *separator_item;
	GtkWidget *toggle_vi_item;
	GtkWidget *perform_vi_item;
} vi_widgets =
{
	NULL, NULL, NULL, NULL, NULL
};

struct
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
} state =
{
	-1, TRUE, FALSE, VI_MODE_COMMAND
};

CmdContext ctx =
{
	NULL, NULL
};


static const gchar *get_mode_name(ViMode vi_mode)
{
	switch (vi_mode)
	{
		case VI_MODE_COMMAND:
			return "COMMAND";
			break;
		case VI_MODE_INSERT:
			return "INSERT";
			break;
		case VI_MODE_REPLACE:
			return "REPLACE";
			break;
		case VI_MODE_VISUAL:
			return "VISUAL";
			break;
		case VI_MODE_CMDLINE:
			return "CMDLINE";
			break;
	}
	return "";
}


void set_vi_mode(ViMode mode, ScintillaObject *sci)
{
	if (!sci)
		return;

	if (state.default_caret_style == -1)
		state.default_caret_style = SSM(sci, SCI_GETCARETSTYLE, 0, 0);

	if (!state.vi_enabled)
	{
		SSM(sci, SCI_SETCARETSTYLE, state.default_caret_style, 0);
		return;
	}

	state.vi_mode = mode;

	switch (mode)
	{
		case VI_MODE_COMMAND:
			SSM(sci, SCI_SETOVERTYPE, 0, 0);
			SSM(sci, SCI_SETCARETSTYLE, CARETSTYLE_BLOCK, 0);
			clamp_cursor_pos(sci);
			break;
		case VI_MODE_CMDLINE:
		{
			KeyPress *kp = kp_current(&ctx);
			gchar val[2] = {kp_to_char(kp), '\0'};
			gtk_widget_show(vi_widgets.prompt);
			gtk_entry_set_text(GTK_ENTRY(vi_widgets.entry), val);
			gtk_editable_set_position(GTK_EDITABLE(vi_widgets.entry), 1);
			break;
		}
		case VI_MODE_INSERT:
			SSM(sci, SCI_SETOVERTYPE, 0, 0);
			SSM(sci, SCI_SETCARETSTYLE, CARETSTYLE_LINE, 0);
			break;
		case VI_MODE_REPLACE:
			SSM(sci, SCI_SETOVERTYPE, 1, 0);
			SSM(sci, SCI_SETCARETSTYLE, CARETSTYLE_LINE, 0);
			break;
		case VI_MODE_VISUAL:
			break;
	}

	ui_set_statusbar(FALSE, "Vim Mode: -- %s --", get_mode_name(state.vi_mode));
}


static void leave_onetime_vi_mode()
{
	if (state.vi_onetime)
	{
		state.vi_enabled = FALSE;
		state.vi_onetime = FALSE;
		set_vi_mode(VI_MODE_COMMAND, get_current_doc_sci());
	}
}


static void close_prompt()
{
	leave_onetime_vi_mode();
	gtk_widget_hide(vi_widgets.prompt);
	set_vi_mode(VI_MODE_COMMAND, get_current_doc_sci());
}


static void perform_command(const gchar *cmd)
{
	guint i = 0;
	guint len = strlen(cmd);
	GeanyDocument *doc = document_get_current();
	ScintillaObject *sci = get_current_doc_sci();

	if (!sci)
		return;

	if (cmd == NULL || len == 1)
		return;

	switch (cmd[i])
	{
		case ':':
		{
			i++;
			while (i < len)
			{
				switch (cmd[i])
				{
					case 'w':
					{
						if (doc != NULL)
							document_save_file(doc, FALSE);
						break;
					}
				}
				i++;
			}
		}
		case '/':
		case '?':
			g_free(ctx.search_text);
			ctx.search_text = g_strdup(cmd);
			perform_search(sci, &ctx, TRUE);
			break;
	}
}


static gboolean on_prompt_key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer dummy)
{
	switch (event->keyval)
	{
		case GDK_KEY_Escape:
			close_prompt();
			return TRUE;

		case GDK_KEY_Tab:
			/* avoid leaving the entry */
			return TRUE;

		case GDK_KEY_Return:
		case GDK_KEY_KP_Enter:
		case GDK_KEY_ISO_Enter:
			perform_command(gtk_entry_get_text(GTK_ENTRY(vi_widgets.entry)));
			close_prompt();
			return TRUE;
	}

	return FALSE;
}


static void on_entry_text_notify(GObject *object, GParamSpec *pspec, gpointer dummy)
{
	const gchar* cmd = gtk_entry_get_text(GTK_ENTRY(vi_widgets.entry));

	if (cmd == NULL || strlen(cmd) == 0)
		close_prompt();
}


static void on_prompt_show(GtkWidget *widget, gpointer dummy)
{
	gtk_widget_grab_focus(vi_widgets.entry);
}


static gchar *get_config_filename (void)
{
	return g_build_filename (geany_data->app->configdir, "plugins", PLUGIN, PLUGIN".conf", NULL);
}


static void load_config(void)
{
	gchar *filename = get_config_filename();
	GKeyFile *kf = g_key_file_new();

	if (g_key_file_load_from_file(kf, filename, G_KEY_FILE_NONE, NULL))
		state.vi_enabled = g_key_file_get_boolean(kf, CONF_GROUP, CONF_VI_MODE, NULL);

	g_key_file_free(kf);
	g_free(filename);
}


static void save_config(void)
{
	GKeyFile *kf = g_key_file_new();
	gchar *filename = get_config_filename();
	gchar *dirname = g_path_get_dirname(filename);
	gchar *data;
	gsize length;

	g_key_file_set_boolean(kf, CONF_GROUP, CONF_VI_MODE, state.vi_enabled);

	utils_mkdir(dirname, TRUE);
	data = g_key_file_to_data(kf, &length, NULL);
	g_file_set_contents(filename, data, length, NULL);

	g_free(data);
	g_key_file_free(kf);
	g_free(filename);
	g_free(dirname);
}


static void on_toggle_vim_mode(void)
{
	state.vi_enabled = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(vi_widgets.toggle_vi_item));
	state.vi_onetime = FALSE;
	set_vi_mode(VI_MODE_COMMAND, get_current_doc_sci());
	if (!state.vi_enabled)
		ui_set_statusbar(FALSE, "Vim Mode Disabled");
	save_config();
}


static gboolean on_toggle_vim_mode_kb(GeanyKeyBinding *kb, guint key_id, gpointer data)
{
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(vi_widgets.toggle_vi_item),
			!state.vi_enabled);

	return TRUE;
}


static gboolean on_perform_vim_command(GeanyKeyBinding *kb, guint key_id, gpointer data)
{
	if (!state.vi_enabled)
	{
		state.vi_onetime = TRUE;
		state.vi_enabled = TRUE;
		set_vi_mode(VI_MODE_COMMAND, get_current_doc_sci());
	}

	return TRUE;
}



static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	ScintillaObject *sci = get_current_doc_sci();
	gboolean consumed;

	if (!state.vi_enabled || !sci)
		return FALSE;

	if (gtk_window_get_focus(GTK_WINDOW(geany->main_widgets->window)) != GTK_WIDGET(sci))
		return FALSE;

	consumed = state.vi_mode == VI_MODE_COMMAND;

	if (state.vi_mode == VI_MODE_COMMAND)
	{
		KeyPress *kp = kp_from_event_key(event);
		kp_append(&ctx, kp);
		if (process_event_cmd_mode(sci, &ctx))
			leave_onetime_vi_mode();
	}
	else
	{
		if (event->keyval == GDK_KEY_Escape)
		{
			if (!SSM(sci, SCI_AUTOCACTIVE, 0, 0) && !SSM(sci, SCI_CALLTIPACTIVE, 0, 0))
			{
				gint pos = sci_get_current_position(sci);
				gint start_pos = sci_get_position_from_line(sci, sci_get_current_line(sci));
				if (pos > start_pos)
					sci_send_command(sci, SCI_CHARLEFT);
				leave_onetime_vi_mode();
				set_vi_mode(VI_MODE_COMMAND, get_current_doc_sci());
				kp_clear(&ctx);
			}
		}
	}

	//printf("key: %d, state: %d\n", event->keyval, event->state);

	return consumed;
}


static void on_doc_open(G_GNUC_UNUSED GObject *obj, GeanyDocument *doc,
		G_GNUC_UNUSED gpointer user_data)
{
	g_return_if_fail(doc != NULL);
	set_vi_mode(state.vi_mode, doc->editor->sci);
}


static void on_doc_activate(G_GNUC_UNUSED GObject *obj, GeanyDocument *doc,
		G_GNUC_UNUSED gpointer user_data)
{
	g_return_if_fail(doc != NULL);
	set_vi_mode(state.vi_mode, doc->editor->sci);
}


static gboolean on_editor_notify(GObject *object, GeanyEditor *editor,
		SCNotification *nt, gpointer data)
{
	ScintillaObject *sci = get_current_doc_sci();

	if (!state.vi_enabled || !sci || state.vi_mode != VI_MODE_COMMAND)
		return FALSE;

	if (sci_get_selected_text_length(sci) > 0)
		return FALSE;

	/* this makes sure that when we click behind the end of line in command mode,
	 * the cursor is not placed BEHIND the last character but ON the last character */
	if (nt->nmhdr.code == SCN_UPDATEUI && nt->updated == SC_UPDATE_SELECTION)
		clamp_cursor_pos(sci);

	return FALSE;
}


PluginCallback plugin_callbacks[] = {
	{"document-open", (GCallback) &on_doc_open, TRUE, NULL},
	{"document-activate", (GCallback) &on_doc_activate, TRUE, NULL},
	{"editor-notify", (GCallback) &on_editor_notify, TRUE, NULL},
	{"key-press", (GCallback) &on_key_press, TRUE, NULL},
	{NULL, NULL, FALSE, NULL}
};


void plugin_init(GeanyData *data)
{
	GeanyKeyGroup *group;
	GtkWidget *frame;

	load_config();

	/* menu items and keybindings */
	group = plugin_set_key_group(geany_plugin, "geanyvim", KB_COUNT, NULL);

	vi_widgets.separator_item = gtk_separator_menu_item_new();
	gtk_container_add(GTK_CONTAINER(geany->main_widgets->tools_menu), vi_widgets.separator_item);

	vi_widgets.toggle_vi_item = gtk_check_menu_item_new_with_mnemonic(_("Enable _Vim Mode"));
	gtk_widget_show(vi_widgets.toggle_vi_item);
	gtk_container_add(GTK_CONTAINER(geany->main_widgets->tools_menu), vi_widgets.toggle_vi_item);
	g_signal_connect((gpointer) vi_widgets.toggle_vi_item, "activate", G_CALLBACK(on_toggle_vim_mode), NULL);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(vi_widgets.toggle_vi_item),
			state.vi_enabled);
	keybindings_set_item_full(group, KB_TOGGLE_VIM_MODE, 0, 0, "toggle_vim",
			_("Enable Vim Mode"), NULL, on_toggle_vim_mode_kb, NULL, NULL);

	vi_widgets.perform_vi_item = gtk_menu_item_new_with_mnemonic(_("_Perform Vim Command"));
	gtk_widget_show(vi_widgets.perform_vi_item);
	gtk_container_add(GTK_CONTAINER(geany->main_widgets->tools_menu), vi_widgets.perform_vi_item);
	g_signal_connect((gpointer) vi_widgets.perform_vi_item, "activate", G_CALLBACK(on_perform_vim_command), NULL);
	keybindings_set_item_full(group, KB_PERFORM_VIM_COMMAND, 0, 0, "vim_command",
			_("Perform Vim Command"), NULL, on_perform_vim_command, NULL, NULL);

	/* prompt */
	vi_widgets.prompt = g_object_new(GTK_TYPE_WINDOW,
			"decorated", FALSE,
			"default-width", 500,
			"transient-for", geany_data->main_widgets->window,
			"window-position", GTK_WIN_POS_CENTER_ON_PARENT,
			"type-hint", GDK_WINDOW_TYPE_HINT_DIALOG,
			"skip-taskbar-hint", TRUE,
			"skip-pager-hint", TRUE,
			NULL);
	g_signal_connect(vi_widgets.prompt, "focus-out-event", G_CALLBACK(close_prompt), NULL);
	g_signal_connect(vi_widgets.prompt, "show", G_CALLBACK(on_prompt_show), NULL);
	g_signal_connect(vi_widgets.prompt, "key-press-event", G_CALLBACK(on_prompt_key_press_event), NULL);

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(vi_widgets.prompt), frame);

	vi_widgets.entry = gtk_entry_new();
	gtk_container_add(GTK_CONTAINER(frame), vi_widgets.entry);

	g_signal_connect(vi_widgets.entry, "notify::text", G_CALLBACK(on_entry_text_notify), NULL);

	gtk_widget_show_all(frame);

	set_vi_mode(VI_MODE_COMMAND, get_current_doc_sci());
}


void plugin_cleanup(void)
{
	if (state.default_caret_style != -1)
	{
		gsize i = 0;
		foreach_document(i)
		{
			ScintillaObject *sci = documents[i]->editor->sci;
			SSM(sci, SCI_SETCARETSTYLE, state.default_caret_style, 0);
		}
	}

	gtk_widget_destroy(vi_widgets.prompt);
	gtk_widget_destroy(vi_widgets.separator_item);
	gtk_widget_destroy(vi_widgets.toggle_vi_item);
	gtk_widget_destroy(vi_widgets.perform_vi_item);

	g_free(ctx.search_text);
}


void plugin_help(void)
{
	utils_open_browser("http://plugins.geany.org/geanyvim.html");
}
