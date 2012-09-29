/*
 * plugin.c
 *
 * Copyright 2012 Marco Constâncio <kmarco100@gmail.com>
 * Copyright 2012 Frank Lanitz <frank@frank.uvena.de>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */
#define LIBXML_SCHEMAS_ENABLED
#define USE_XSD_VALIDATION 0

#include "geanyplugin.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include <stdio.h>
#include <ctype.h>

#include <glib.h>
#include <libxml/xmlschemastypes.h>
#include <libxml/xpath.h>
#include <string.h>

/*
INSTALL:
   libxml2-dev
RUN TO COMPILE:
   gcc -c plugin.c -fPIC `pkg-config --cflags geany` -I/usr/include/libxml2 &&  gcc plugin.o -o plugin.so -shared `pkg-config --libs geany` -lxml2
*/

/* GEANY SPECIFIC */
GeanyPlugin *geany_plugin;
GeanyData *geany_data;
GeanyFunctions *geany_functions;

PLUGIN_VERSION_CHECK(147)
PLUGIN_SET_INFO(_("Snippets Menu"), _("Snippets Menu."), "0.2" , _("Marco Constâncio"))

/* UI */
static GtkWidget *menubar = NULL;

static void generate_generic_toolbar(void);
static GtkWidget *add_dialog_input_widgets(GtkWidget *, GtkWidget *, const xmlChar *, xmlDoc *, xmlNode *);

/* CODE, ACTIONS */
static GHashTable *file_locations;
static const gchar *plugin_data_dir = "/etc/geany/snippetsmenu";
static const gchar *snippet_dir = "snippets/";

static GtkWidget *read_code_folder(const gchar *, gint);
static gboolean editor_notify_cb(GObject *object, GeanyEditor *editor, SCNotification *nt, gpointer data);
static void code_action(gchar *);
static gchar *get_code_content(gchar *file);
static char *run_external_script (const char *, const char *);
static void insert_code(GeanyDocument *, const gchar *);

/* Util */
static char *str_replace(const char *, const char *, const char *);
static void msgbox(gchar *);


PluginCallback plugin_callbacks[] =
{
  { "editor-notify", (GCallback) &editor_notify_cb, FALSE, NULL },
  { NULL, NULL, FALSE, NULL }
};


/* Add toolbar, created hashtable to be used to store filenames and file path */
void plugin_init(GeanyData *data){
  file_locations = g_hash_table_new(g_str_hash, g_str_equal);
  generate_generic_toolbar();
}

void plugin_cleanup(void){
  if (file_locations != NULL){
    g_hash_table_destroy(file_locations);
  }
  if (menubar != NULL){
    gtk_widget_destroy(menubar);
  }
}

static void generate_generic_toolbar(){
  GtkWidget *vbox;

  menubar = gtk_menu_bar_new();
  vbox = ui_lookup_widget(geany->main_widgets->window, "vbox1");
  gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 3);
  gtk_box_reorder_child(GTK_BOX(vbox),menubar, 2);

  read_code_folder("", 0);
  gtk_widget_show_all(menubar);
}

/* Recursive that reads code folder and its subfolders. records filenames
 * and path to hashtable for easier later access */
static GtkWidget *read_code_folder(const gchar *path, gint depth){
  GDir *dir;
  gchar *full_path;
  const gchar *filename;
  char *filename_ext;

  GtkWidget *item;
  GtkWidget *menu = gtk_menu_new();
  GtkWidget *submenu = gtk_menu_new();

  full_path = g_build_filename(plugin_data_dir, snippet_dir, path, NULL);
  dir = g_dir_open(full_path, 0, NULL);

  for (filename = g_dir_read_name(dir); filename; filename = g_dir_read_name(dir)){
    gchar *file_fullpath = g_build_filename(full_path, filename, NULL);

    if (g_file_test(file_fullpath, G_FILE_TEST_IS_DIR)){
      gchar * new_path;

      item = gtk_menu_item_new_with_label(g_strdup(filename));

      new_path = g_build_filename(path, filename, NULL);
      submenu = read_code_folder(new_path, depth + 1);
      gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
      g_free(new_path);

      if (depth == 0){
        gtk_menu_shell_append(GTK_MENU_SHELL(menubar), item);
      }else{
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
      }
    }
    else if (g_file_test(file_fullpath, G_FILE_TEST_IS_REGULAR)){
      filename_ext = strrchr(filename, '.');
      if (filename_ext && strcasecmp(filename_ext, ".xml") == 0){

        gchar *file_basename = str_replace(filename, ".xml", "");

        const gchar * slash = strchr(path, '/');
        gchar *temp1 = slash ? g_strndup(path, slash - path) : g_strdup(path);
        gchar *temp2 = g_ascii_strdown(temp1, -1);
        gchar *file_path = g_build_filename(temp2, file_basename, NULL);

        g_hash_table_insert(file_locations, file_path, file_fullpath);

        item = gtk_menu_item_new_with_label(file_basename);
        if(depth == 0){
          gtk_menu_shell_append(GTK_MENU_SHELL(menubar), item);
        }else{
          gtk_container_add(GTK_CONTAINER(menu), item);
        }
        g_signal_connect_swapped(G_OBJECT(item), "activate", G_CALLBACK(code_action), (gpointer)g_strdup(file_path));

        g_free(temp1);
        g_free(temp2);
        file_fullpath = NULL;
      }
    }

    g_free(file_fullpath);
  }

  g_dir_close(dir);
  g_free(full_path);

  return menu;
}

/* Function called by Geany when a user changed something in an editor.
 * If the document is HTML or XML, and user typed in a tag, we are
 * going to apply a snippet matching the tag name */
static gboolean editor_notify_cb(GObject *object, GeanyEditor *editor, SCNotification *nt, gpointer data){
  gint lexer, pos, style, end;
  gchar c;
  const gchar *snippet_subdir;
  gchar *tagname, *file_location, *code_content;

  if (nt->nmhdr.code != SCN_CHARADDED || nt->ch != '>'){
    return FALSE;
  }

  lexer = sci_get_lexer(editor->sci);
  if (lexer == SCLEX_XML){
    snippet_subdir = "xml/";
  }else if (lexer == SCLEX_HTML){
    snippet_subdir = "html/";
  }else{
    return FALSE;
  }

  pos = sci_get_current_position(editor->sci);
  style = sci_get_style_at(editor->sci, pos);
  if ((style > SCE_H_XCCOMMENT && ! highlighting_is_string_style(lexer, style)) ||
    highlighting_is_comment_style(lexer, style)){
    return FALSE;
  }

  g_assert(sci_get_char_at(editor->sci, pos - 1) == '>');
  for (pos = end = pos - 2; pos > 0; pos--){
    c = sci_get_char_at(editor->sci, pos);
    if (!strchr(":_-.", c) && !isalnum(c))
      break;
  }
  if (pos == end || (c = sci_get_char_at(editor->sci, pos)) != '<'){
    return FALSE;
  }
  pos++;

  tagname = sci_get_contents_range(editor->sci, pos, end + 1);
  file_location = g_strconcat(snippet_subdir, tagname, NULL);
  g_free(tagname);

  code_content = get_code_content(file_location);
  g_free(file_location);
  if (code_content == NULL){
    return FALSE;
  }

  /* remove typed in opening tag */
  pos--;
  sci_set_selection_start(editor->sci, pos);
  sci_set_selection_end(editor->sci, end + 2);
  sci_replace_sel(editor->sci, "");

  insert_code(editor->document, code_content);
  return TRUE;
}

/* Reads data from xml code file, generate form dialog, insert code to geany document */
static void code_action(gchar *file){
  GeanyDocument *geany_doc;
  gchar *code_content;

  geany_doc = document_get_current();
  if (geany_doc == NULL){
    return;
  }

  code_content = get_code_content(file);
  if (code_content == NULL){
    return;
  }

  insert_code(geany_doc, code_content);
}

/* Reads data from xml code file, generate form dialog, get code to insert */
static gchar *get_code_content(gchar *file){
  char *file_path = g_hash_table_lookup(file_locations, file);
  char *script_param = "";
  gchar *code_content = NULL;

  /* Xml Reading */
  xmlDoc *xml_doc;
  xmlXPathContext *xpathContext;

  /* Xml Validation - valid xml */
  xmlSchemaParserCtxtPtr parserContext;
  xmlSchemaPtr schema;

  /* Xml Validation - validation against the xsd file */
  xmlSchemaValidCtxtPtr validationContext;
  int validation_result;

  xml_doc = xmlParseFile(file_path);
  if(xml_doc == NULL){
    return NULL;
  }
  xpathContext = xmlXPathNewContext(xml_doc);

  parserContext = xmlSchemaNewParserCtxt(g_strconcat(plugin_data_dir,"validation.xsd", NULL));
  xmlSchemaSetParserErrors(parserContext, (xmlSchemaValidityErrorFunc) fprintf, (xmlSchemaValidityWarningFunc) fprintf, stderr);
  schema = xmlSchemaParse(parserContext);
  xmlSchemaFreeParserCtxt(parserContext);

  validationContext = xmlSchemaNewValidCtxt(schema);
  xmlSchemaSetValidErrors(validationContext, (xmlSchemaValidityErrorFunc) fprintf, (xmlSchemaValidityWarningFunc) fprintf, stderr);
  validation_result = xmlSchemaValidateDoc(validationContext, xml_doc);

  /* Check if the xsd validation is to be ignore */
  if (USE_XSD_VALIDATION != 1){
    validation_result = 0;
  }

  if (validation_result == 0){
    /* Code Node */
    xmlXPathObject * xpathObj_code = xmlXPathEvalExpression((xmlChar*)"/doc/code", xpathContext);
    xmlNode *node_code = xpathObj_code->nodesetval->nodeTab[0];
    xmlXPathObject * xpathObj_form = xmlXPathEvalExpression((xmlChar*)"/doc/form", xpathContext);

    code_content = xmlNodeGetContent(node_code);

    if(! xmlXPathNodeSetIsEmpty(xpathObj_form->nodesetval)){
      /* Initialiaze form dialog */
      gint form_dialog_result, chid_id = 0, num_fields = 0;

      GtkWidget *form_dialog, *vbox;
      xmlNode *node_form = xpathObj_form->nodesetval->nodeTab[0], *field_node;

      form_dialog = gtk_dialog_new_with_buttons("Form Options",
                    GTK_WINDOW(geany->main_widgets->window), GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                    GTK_STOCK_OK, GTK_RESPONSE_OK,
                    NULL);
      vbox = ui_dialog_vbox_new(GTK_DIALOG(form_dialog));
      gtk_box_set_spacing(GTK_BOX(vbox), 6);

      field_node = node_form->xmlChildrenNode;
      GtkWidget *form_fields[xmlChildElementCount(node_form)];

      /* First xml read, generates form_fields ands store widgets data to array */
      while (field_node != NULL){
        if ((!xmlStrcmp(field_node->name, (const xmlChar *)"textbox")) ||
          (!xmlStrcmp(field_node->name, (const xmlChar *)"combobox"))){

          form_fields[chid_id] = add_dialog_input_widgets(form_dialog, vbox, field_node->name, xml_doc, field_node);

          gtk_container_add(GTK_CONTAINER(vbox), form_fields[chid_id]);
          num_fields++;
        }
        chid_id++;
        field_node = field_node->next;
      }
      /* Doesn't show dialog when no form fields there is a form tag but no form field tags*/
      if (num_fields > 0){
        gtk_dialog_set_default_response(GTK_DIALOG(form_dialog), GTK_RESPONSE_OK);

        gtk_widget_show_all(form_dialog);
        form_dialog_result = gtk_dialog_run(GTK_DIALOG (form_dialog));
      }

      if (form_dialog_result == GTK_RESPONSE_OK || num_fields == 0){
        chid_id = 0;
        field_node = node_form->xmlChildrenNode;

        /* Check response from dialog, replace field name with response from dialog or script output*/
        while (field_node != NULL){
          if ((!xmlStrcmp(field_node->name, (const xmlChar *)"textbox"))){
            script_param = g_strconcat(script_param, " ", gtk_entry_get_text(GTK_ENTRY(form_fields[chid_id])), NULL);

            code_content = str_replace(code_content,
                           g_strdup(xmlGetProp(field_node, "name")),
                           gtk_entry_get_text(GTK_ENTRY (form_fields[chid_id])));
          }
          else if ((!xmlStrcmp(field_node->name, (const xmlChar *)"combobox"))){
            const gchar *active_cb_node = gtk_combo_box_get_active_text(GTK_COMBO_BOX(form_fields[chid_id]));

            /* Append option to script_param for later use when necessary */
            if (active_cb_node != NULL){
              script_param = g_strconcat(script_param, active_cb_node, " ", NULL);
            }

            /* No option selected, replaces fieldname with empty string */
            if (active_cb_node == NULL){
              code_content = str_replace(code_content,
                             g_strdup(xmlGetProp(field_node,"name")),
                             "");
            }else{
              xmlNode *selected_cb_node = field_node->xmlChildrenNode;
              const gchar *cb_label = "";

              while (selected_cb_node != NULL){
                /* gets value from value/label from combobox*/
                cb_label = g_strdup(xmlGetProp(selected_cb_node,"label"));
                if (cb_label == NULL){
                  cb_label = xmlNodeListGetString(xml_doc, selected_cb_node->xmlChildrenNode, 1);
                }

                if ((!xmlStrcmp(selected_cb_node->name, (const xmlChar *)"option")) &&
                  (!xmlStrcmp(cb_label, active_cb_node))){

                  code_content = str_replace(code_content,
                                 g_strdup(xmlGetProp(field_node,"name")),
                                 xmlNodeListGetString(xml_doc, selected_cb_node->xmlChildrenNode, 1));
                  break;
                }
                selected_cb_node = selected_cb_node->next;
              }
            }
          }
          else if ((!xmlStrcmp(field_node->name, (const xmlChar *)"script"))){
            /* Gets output from script */
            xmlChar *script_name = xmlNodeListGetString(xml_doc, field_node->xmlChildrenNode, 1);

            code_content = str_replace(code_content,
                           g_strdup(xmlGetProp(field_node,"name")),
                           run_external_script(script_name,script_param));
            xmlFree(script_name);
          }
          chid_id++;
          field_node = field_node->next;
        }
      }
      else{
        /* USER PRESSED CANCEL/CLOSED DIALOG BOX*/
      }
      gtk_widget_destroy(form_dialog);
    }
  }
  else if (validation_result > 0){
    msgbox("This code file is not valid. Checks its content.");
  }
  else{
    msgbox("This code file validation generated an internal error.");
  }
  return code_content;
}

/* Adds widgets to form dialog */
static GtkWidget *add_dialog_input_widgets(GtkWidget *dialog, GtkWidget *vbox, const xmlChar *type, xmlDoc *xml_doc, xmlNode *node){
  GtkWidget *entry = NULL;
  const gchar *label_text = g_strdup(xmlGetProp(node, "label"));
  const gchar *default_text = xmlNodeListGetString(xml_doc, node->xmlChildrenNode, 1);

  if (label_text){
    GtkWidget *label = gtk_label_new(label_text);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_container_add(GTK_CONTAINER(vbox), label);
  }

  if (!xmlStrcmp(type, (const xmlChar *)"textbox")){
    entry = gtk_entry_new();
    ui_entry_add_clear_icon(GTK_ENTRY(entry));

    if (default_text != NULL){
      gtk_entry_set_text(GTK_ENTRY(entry), default_text);
    }

    gtk_entry_set_max_length(GTK_ENTRY(entry), 255);
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 30);
  }
  else if (!xmlStrcmp(type, (const xmlChar *)"combobox")){
    xmlNode *combo_node = node->xmlChildrenNode;
    const gchar *cb_label = "";

    entry = gtk_combo_box_new_text();
    while (combo_node != NULL){
      if ((!xmlStrcmp(combo_node->name, (const xmlChar *)"option"))) {
        cb_label = g_strdup(xmlGetProp(combo_node,"label"));

        /* Check if there is label, when there isn't one uses option value */
        if(cb_label == NULL){
          gtk_combo_box_append_text(GTK_COMBO_BOX(entry),xmlNodeListGetString(xml_doc, combo_node->xmlChildrenNode, 1));
        }else{
          gtk_combo_box_append_text(GTK_COMBO_BOX(entry),cb_label);
        }
      }
      combo_node = combo_node->next;
    }
  }

  return entry;
}

/* Inserts code field (already processed) to the geany document */
static void insert_code(GeanyDocument *doc, const gchar *string){
  gint pos = sci_get_current_position(doc->editor->sci);
  sci_insert_text(doc->editor->sci, pos, string);
}

static char *run_external_script(const char *script_location, const char *param){
  FILE *file;
  char *output;
  char buffer[512];

  output = realloc(NULL, 1);
  output[0] = '\0';

  file = popen(g_strconcat(script_location, " ", param, NULL), "r");
  if (file == NULL){
    msgbox("Error opening script file.");
  }else{
    while (fgets(buffer, sizeof(buffer)-1, file) != NULL) {
      output = realloc(output, (strlen(output) + strlen(buffer) + 1));
      strcat(output, buffer);
    }
    pclose(file);
  }

  return output;
}

/******************* UTIL FUNCTIONS ***********************/

static void msgbox(gchar *data){
  GtkWidget *dialog;
  dialog = gtk_message_dialog_new(GTK_WINDOW(geany->main_widgets->window),
           GTK_DIALOG_DESTROY_WITH_PARENT,
           GTK_MESSAGE_INFO,
           GTK_BUTTONS_OK,
           data, "title");
  gtk_window_set_title(GTK_WINDOW(dialog), "Info");
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

static char *str_replace(const char *string, const char *substr, const char *replacement){
  char *tok = NULL;
  char *newstr = NULL;
  char *oldstr = NULL;

  /* if either substr or replacement is NULL, duplicate string a let caller handle it */
  if (substr == NULL || replacement == NULL){
    return strdup (string);
  }
  newstr = strdup (string);
  while ((tok = strstr(newstr, substr))){
    oldstr = newstr;
    newstr = malloc(strlen(oldstr) - strlen(substr) + strlen(replacement) + 1);
    /*failed to alloc mem, free old string and return NULL */
    if (newstr == NULL){
      free(oldstr);
      return NULL;
    }
    memcpy(newstr, oldstr, tok - oldstr );
    memcpy(newstr + (tok - oldstr), replacement, strlen(replacement));
    memcpy(newstr + (tok - oldstr) + strlen(replacement), tok + strlen(substr), strlen(oldstr) - strlen(substr) - (tok - oldstr));
    memset(newstr + strlen(oldstr) - strlen(substr) + strlen(replacement) , 0, 1 );
    free(oldstr);
  }
  return newstr;
}

/*
SOURCES:
http://charette.no-ip.com:81/programming/2010-01-03_LibXml2/
http://www.inkstain.net/fleck/tutorial/ar01s05.html
http://coding.debuntu.org/c-implementing-str_replace-replace-all-occurrences-substring
*/
