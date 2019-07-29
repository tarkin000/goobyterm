/* this version does away with the stack-switcher/sidebar */
#include <sstream>
#include <cstdio>
#include <cstring>

#include <gdk/gdk.h>
#include <vte/vte.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <webkit2/webkit2.h>

#define GT_INITIAL_WIDTH  640
#define GT_INITIAL_HEIGHT 480

typedef struct {
	GtkWidget *window;
	GtkWidget *header;
	GtkWidget *status;
	GtkWidget *entry;
	GtkWidget *stack;
	GtkWidget *web;
	GtkWidget *term;
	GtkWidget *spinner;
	GtkWidget *tab_label;
	GtkWidget *dialog;
	GtkWidget *extra;
  WebKitUserContentManager *manager;
	GList *views;
	int width;
	int height;
	int count;
} _App, *App;

static const char *new_tab_html = R"html(
<!doctype html>
<html>
<head>
<title>New tab</title>
<style>
	body { background-color:#EEE; color:#222; }
</style>
</head>
<body>
	<h1>New tab</h1>
  <!-- button onclick="external.invoke('exit')">exit</button -->
  <script type="text/javascript">
    function foo(n) {
      console.log('foo');
    }
  </script>
</body>
</html>
)html";

static std::string url_encode(const std::string &value) {
  const char hex[] = "0123456789ABCDEF";
  std::string escaped;
  for (char c : value) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' ||
        c == '=') {
      escaped = escaped + c;
    } else {
      escaped = escaped + '%' + hex[(c >> 4) & 0xf] + hex[c & 0xf];
    }
  }
  return escaped;
}

GtkWidget* find_child(GtkWidget* parent, const gchar* name) {
	if (g_strcasecmp(gtk_widget_get_name((GtkWidget*)parent), (gchar*)name) == 0) {
		return parent;
	}

	if (GTK_IS_BIN(parent)) {
		GtkWidget *child = gtk_bin_get_child(GTK_BIN(parent));
		return find_child(child, name);
	}

	if (GTK_IS_CONTAINER(parent)) {
		GList *children = gtk_container_get_children(GTK_CONTAINER(parent));
		while ((children = g_list_next(children)) != NULL) {
			GtkWidget* widget = find_child(GTK_WIDGET(children->data), name);
			if (widget != NULL) return widget;
		}
	}

	return NULL;
}

void my_external_message_received_cb(WebKitUserContentManager *m, WebKitJavascriptResult *r, gpointer udata) {
	/* TODO */
}

void on_stop_clicked(GtkButton *btn, App app) {
	if (gtk_stack_get_visible_child(GTK_STACK(app->stack)) == app->web) {
		webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(app->web));
	}
}

void on_back_clicked(GtkButton *btn, App app) {
	if (gtk_stack_get_visible_child(GTK_STACK(app->stack)) == app->web) {
		webkit_web_view_go_back(WEBKIT_WEB_VIEW(app->web));
	}
}

void on_forward_clicked(GtkButton *btn, App app) {
	if (gtk_stack_get_visible_child(GTK_STACK(app->stack)) == app->web) {
		webkit_web_view_go_forward(WEBKIT_WEB_VIEW(app->web));
	}
}

void on_load_changed(WebKitWebView *web, WebKitLoadEvent event, App app) {
	gboolean webfocus = gtk_stack_get_visible_child(GTK_STACK(app->stack)) == GTK_WIDGET(web);
	const gchar *uri = webkit_web_view_get_uri(web);
	GtkWidget *widget;
	cairo_surface_t *surface;

	switch (event) {
		case WEBKIT_LOAD_STARTED:
		case WEBKIT_LOAD_REDIRECTED:
		case WEBKIT_LOAD_COMMITTED:
			gtk_entry_set_text(GTK_ENTRY(app->entry),uri);
			fprintf(stderr,"loading %s\n",webkit_web_view_get_uri(web));
			if (webfocus) {
				widget = gtk_stack_get_child_by_name(GTK_STACK(app->status),"spinner");
				gtk_spinner_start(GTK_SPINNER(widget));
				gtk_stack_set_visible_child_name(GTK_STACK(app->status),"spinner");
			}
		break;
		case WEBKIT_LOAD_FINISHED:
			if (webfocus) {
				widget = gtk_stack_get_child_by_name(GTK_STACK(app->status),"favicon");
				if ((surface = webkit_web_view_get_favicon(web)) != NULL) gtk_image_set_from_surface(GTK_IMAGE(widget),surface);
				else gtk_image_set_from_icon_name(GTK_IMAGE(widget),"emblem-unreadable",GTK_ICON_SIZE_BUTTON);
				gtk_stack_set_visible_child_name(GTK_STACK(app->status),"favicon");
			}
		break;
	}
}

void my_stack_child_focus_handler(GtkStack *stack, App app) {
	cairo_surface_t *surface;
	GtkWidget *child = gtk_stack_get_visible_child(stack);
	
	if (WEBKIT_IS_WEB_VIEW(child)) {
		if ((surface = webkit_web_view_get_favicon(WEBKIT_WEB_VIEW(child))) != NULL) {
			child = gtk_stack_get_child_by_name(GTK_STACK(stack),"favicon");
			gtk_image_set_from_surface(GTK_IMAGE(child),surface);
			gtk_stack_set_visible_child(GTK_STACK(app->stack),child);
		}
	} else if (VTE_IS_TERMINAL(child)) {
		child = gtk_stack_get_child_by_name(GTK_STACK(stack),"favicon");
		gtk_image_set_from_icon_name(GTK_IMAGE(child),"utilities-terminal",GTK_ICON_SIZE_BUTTON);
		gtk_stack_set_visible_child(GTK_STACK(app->stack),child);
	}
}

gboolean my_url_dialog_keypress_handler(GtkWidget *widget, GdkEventKey *event, gpointer udata) {
	GtkDialog *dialog = GTK_DIALOG(udata);

	if (!(event->state & GDK_MOD1_MASK & GDK_CONTROL_MASK)) {
		if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
			gtk_dialog_response(dialog,GTK_RESPONSE_OK);
			return true;
		}
	}
	return false;
}

gboolean my_entry_focus_out_handler(GtkEntry *entry, GdkEvent *event, gpointer udata) {
	return false;	
}

void my_entry_activate_handler(GtkEntry *entry, App app) {
	const gchar *uri;

	uri = gtk_entry_get_text(entry);
	webkit_web_view_load_uri(WEBKIT_WEB_VIEW(app->web),uri);
	gtk_widget_grab_focus(app->web);
}

gboolean my_keypress_handler(GtkWidget *widget, GdkEventKey *event, gpointer udata) {
	GList *list_item;
	int i,l,m = 1;
	void *tmp;
	App app = (App)udata;
	char str[32],*text;
	cairo_surface_t *surface;

	if (event->state & GDK_MOD1_MASK) {
		switch (event->keyval) {
			case GDK_KEY_0:
			case GDK_KEY_1:
			case GDK_KEY_2:
			case GDK_KEY_3:
			case GDK_KEY_4:
			case GDK_KEY_5:
			case GDK_KEY_6:
			case GDK_KEY_7:
			case GDK_KEY_8:
				i = event->keyval - GDK_KEY_0;
				if ((tmp = g_list_nth_data(app->views,i)) != NULL) {
					app->web = GTK_WIDGET(tmp);
SHOW_WEB_VIEW:
					gtk_stack_set_visible_child(GTK_STACK(app->stack),app->web);
					if (event->keyval != GDK_KEY_n) gtk_widget_grab_focus(app->web);
					widget = gtk_stack_get_child_by_name(GTK_STACK(app->status),"favicon");
					if ((surface = webkit_web_view_get_favicon(WEBKIT_WEB_VIEW(app->web))) != NULL) gtk_image_set_from_surface(GTK_IMAGE(widget),surface);
					else gtk_image_set_from_icon_name(GTK_IMAGE(widget),"emblem-unreadable",GTK_ICON_SIZE_BUTTON);
					gtk_stack_set_visible_child_name(GTK_STACK(app->status),"favicon");
UPDATE_TAB_LABEL:
					l = g_list_length(app->views);
					if (i == -1) i = l - 1;
					if (i == 0) snprintf(str,72,"%d:%d",i,l);
					else snprintf(str,72,"%d:%d",i,l);
					gtk_label_set_text(GTK_LABEL(app->tab_label),(const gchar *)&str[0]);
					return true;
				}
				return false;
			break;
			case GDK_KEY_9:
				list_item = g_list_last(app->views);
				i = g_list_position(app->views,list_item);
				app->web = GTK_WIDGET(list_item->data);
				goto SHOW_WEB_VIEW;
			break;
			case GDK_KEY_j:
				l = g_list_length(app->views);
				if (true) {
					snprintf(str,32,"0-%d",l);
					app->dialog = gtk_dialog_new_with_buttons(
							"tab #",
							GTK_WINDOW(app->window),
							GTK_DIALOG_DESTROY_WITH_PARENT,
							"_OK",
							GTK_RESPONSE_OK,
							"_Cancel",
							GTK_RESPONSE_CANCEL,
							NULL
						);
					gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(app->dialog))),gtk_label_new(str));
					app->extra = gtk_entry_new();
					gtk_entry_set_input_purpose(GTK_ENTRY(app->extra),GTK_INPUT_PURPOSE_DIGITS);
					gtk_dialog_add_action_widget(GTK_DIALOG(app->dialog),app->extra,GTK_RESPONSE_OK);
					gtk_widget_show_all(app->dialog);
					gtk_widget_grab_focus(app->extra);
					if (gtk_dialog_run(GTK_DIALOG(app->dialog)) == GTK_RESPONSE_OK) {
						text = (char *)gtk_entry_get_text(GTK_ENTRY(app->extra));
						if ((l = (int)strlen(text))) {
							i = 0;
							while ((text[--l])) {
								i += m * (text[l] - 0x30);
								m *= 10;
							}
							l = g_list_length(app->views);
							if (i < l && (widget = GTK_WIDGET(g_list_nth_data(app->views,i)))) {
								gtk_stack_set_visible_child(GTK_STACK(app->stack),widget);
								gtk_widget_grab_focus(widget);
							}
						}
					}
					gtk_widget_destroy(app->dialog);
					app->dialog = NULL;
					app->extra = NULL;
				}
				return true;
			break;
			case GDK_KEY_l:
				gtk_widget_grab_focus(app->entry);
				return true;
			break;
			case GDK_KEY_n:
				list_item = g_list_first(app->views);
				widget = webkit_web_view_new_with_related_view(WEBKIT_WEB_VIEW(list_item->data));
				webkit_web_view_load_html(WEBKIT_WEB_VIEW(widget),new_tab_html,NULL);
				app->views = g_list_append(app->views,widget);
				g_signal_connect(G_OBJECT(widget),"load-changed",G_CALLBACK(on_load_changed),app);
				gtk_widget_show(widget);
				gtk_container_add(GTK_CONTAINER(app->stack),widget);
				gtk_widget_grab_focus(app->entry);
				app->web = widget;
				i = -1;
				goto SHOW_WEB_VIEW;
			break;
			case GDK_KEY_t:
				widget = gtk_stack_get_child_by_name(GTK_STACK(app->status),"favicon");
				gtk_image_set_from_icon_name(GTK_IMAGE(widget),"utilities-terminal",GTK_ICON_SIZE_BUTTON);
				gtk_stack_set_visible_child(GTK_STACK(app->status),widget);
				gtk_stack_set_visible_child(GTK_STACK(app->stack),app->term);
				gtk_widget_grab_focus(app->term);
				return true;
			break;
			case GDK_KEY_x:
				if (gtk_stack_get_visible_child(GTK_STACK(app->stack)) == app->web) {
					if ((void *)g_list_first(app->views) != (void *)app->web) {
						gtk_widget_destroy(app->web);
						tmp = g_list_previous(app->web);
						app->views = g_list_remove(app->views,(GList *)app->web);
						app->web = GTK_WIDGET(tmp);
						goto SHOW_WEB_VIEW;
					}
				}
				return true;
			break;
			default:
				(void)app;
				//fputs("unhandled key\n",stderr);
		}
	}
	return false;
}

int main(int argc, char **argv) {
	_App app;
  GtkWidget *tmp;
	GtkImage *img;
	WebKitSettings *settings;
	WebKitWebContext *webctx;
	gchar *command[] = {g_strdup("/bin/bash"), NULL };

	app = {};
	gtk_init(&argc, &argv);

	// window
	app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	//gtk_window_set_title(GTK_WINDOW(app.window),"goobyterm");
	gtk_window_set_default_size(GTK_WINDOW(app.window), GT_INITIAL_WIDTH, GT_INITIAL_HEIGHT);
	gtk_window_set_resizable(GTK_WINDOW(app.window), true);
	gtk_window_maximize(GTK_WINDOW(app.window));
  gtk_window_set_position(GTK_WINDOW(app.window), GTK_WIN_POS_CENTER);

	// header bar
	app.header = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(app.header),true);
	gtk_window_set_titlebar(GTK_WINDOW(app.window),app.header);

	// favicon
	app.status = gtk_stack_new();
	tmp = gtk_image_new_from_icon_name("image-missing",GTK_ICON_SIZE_BUTTON);
	gtk_stack_add_named(GTK_STACK(app.status),tmp,"missing");
	tmp = gtk_spinner_new(); 
	gtk_stack_add_named(GTK_STACK(app.status),tmp,"spinner");
	tmp = gtk_image_new_from_icon_name("image-missing",GTK_ICON_SIZE_BUTTON);
	gtk_stack_add_named(GTK_STACK(app.status),tmp,"favicon");
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app.header),app.status);

	// url area
	app.entry = gtk_entry_new();
	gtk_entry_set_width_chars(GTK_ENTRY(app.entry),72);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app.header),app.entry);
	g_signal_connect(G_OBJECT(app.entry),"activate",G_CALLBACK(my_entry_activate_handler),&app);

	// stop loading button
	tmp = gtk_button_new_from_stock(GTK_STOCK_STOP);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app.header),tmp);
	g_signal_connect(G_OBJECT(tmp),"clicked",G_CALLBACK(on_stop_clicked),&app);

	// back button
	tmp = gtk_button_new_from_stock(GTK_STOCK_GO_BACK);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app.header),tmp);
	g_signal_connect(G_OBJECT(tmp),"clicked",G_CALLBACK(on_back_clicked),&app);

	// forward button
	tmp = gtk_button_new_from_stock(GTK_STOCK_GO_FORWARD);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app.header),tmp);
	g_signal_connect(G_OBJECT(tmp),"clicked",G_CALLBACK(on_forward_clicked),&app);

	// tab indicator (label)
	app.tab_label = gtk_label_new("0:1");
	gtk_header_bar_pack_end(GTK_HEADER_BAR(app.header),app.tab_label);

	// spinner (activity indicator)
	app.spinner = gtk_spinner_new();
	gtk_header_bar_pack_end(GTK_HEADER_BAR(app.header),app.spinner);
	
	app.stack = gtk_stack_new();
	gtk_stack_set_homogeneous(GTK_STACK(app.stack),true);
	gtk_stack_set_transition_type(GTK_STACK(app.stack),GTK_STACK_TRANSITION_TYPE_NONE);
	gtk_container_add(GTK_CONTAINER(app.window),app.stack);

  app.manager = webkit_user_content_manager_new();
  webkit_user_content_manager_register_script_message_handler(app.manager,"external");
  g_signal_connect(app.manager, "script-message-received::external",G_CALLBACK(my_external_message_received_cb),&app);
  app.web = webkit_web_view_new_with_user_content_manager(app.manager);
	webctx = webkit_web_view_get_context(WEBKIT_WEB_VIEW(app.web));
	webkit_web_context_set_favicon_database_directory(webctx,NULL);
	gtk_widget_show(GTK_WIDGET(app.web));
  webkit_web_view_load_html(WEBKIT_WEB_VIEW(app.web),new_tab_html,NULL);
	app.views = g_list_append(app.views,app.web);
	
	gtk_container_add(GTK_CONTAINER(app.stack),app.web);

  g_signal_connect(G_OBJECT(app.web),"load-changed",G_CALLBACK(on_load_changed),&app);
  settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(app.web));
  webkit_settings_set_enable_write_console_messages_to_stdout(settings, true);
  webkit_settings_set_enable_developer_extras(settings, true);
	webkit_settings_set_enable_javascript(settings,true);
	//webkit_settings_set_(settings,true);
	//WebKitWindowProperties *props = webkit_web_view_get_window_properties((WebKitWebView *)app.web);

	app.term = vte_terminal_new();
#ifdef HAVE_VTE_ASYNC
	/* newer async call */
	vte_terminal_spawn_async(VTE_TERMINAL(app.term),
			VTE_PTY_DEFAULT,
			NULL,       /* working directory  */
			command,    /* command */
			NULL,       /* environment */
			G_SPAWN_DO_NOT_REAP_CHILD,          /* spawn flags */
			NULL, NULL, /* child setup */
			NULL,       /* child pid */
			-1,         /* timeout */
			NULL, NULL, NULL); // for async
#else
	vte_terminal_spawn_sync(VTE_TERMINAL(app.term),
			VTE_PTY_DEFAULT,
			NULL,       /* working directory  */
			command,    /* command */
			NULL,       /* environment */
			G_SPAWN_DO_NOT_REAP_CHILD,          /* spawn flags */
			NULL, NULL, /* child setup */
			NULL,       /* child pid */
			(GCancellable *)NULL, (GError **)NULL);
#endif
	vte_terminal_set_scroll_on_output(VTE_TERMINAL(app.term), TRUE);
	vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(app.term), TRUE);
	vte_terminal_set_rewrap_on_resize(VTE_TERMINAL(app.term), TRUE);
	vte_terminal_set_mouse_autohide(VTE_TERMINAL(app.term), TRUE);
	gtk_container_add(GTK_CONTAINER(app.stack),app.term);


	g_signal_connect(app.window,"destroy",G_CALLBACK(gtk_main_quit),NULL);

	/* window (global) keypress handler */
	gtk_widget_add_events(app.window,GDK_KEY_PRESS_MASK);
	g_signal_connect(G_OBJECT(app.window),"key_press_event",G_CALLBACK(my_keypress_handler),&app);

	gtk_widget_grab_focus(GTK_WIDGET(app.entry));
	gtk_widget_show_all(GTK_WIDGET(app.window));
	gtk_main();
	return 0;
}
