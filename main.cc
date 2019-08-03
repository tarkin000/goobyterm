#include <sstream>
#include <cstdio>
#include <cstring>

#include <gdk/gdk.h>
#include <vte/vte.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <JavaScriptCore/JavaScript.h>
#include <webkit2/webkit2.h>
#include "config.h"
#include "icon.h"

typedef struct {
	GtkWidget *window;
	GtkWidget *header;
	GtkWidget *status;
	GtkWidget *entry;
	GtkWidget *annunc;
	GtkWidget *stack;
	GtkWidget *web;
	GtkWidget *term;
	GtkWidget *spinner;
	GtkWidget *tab_label;
	GtkWidget *dialog;
	GtkWidget *extra;
  WebKitUserContentManager *manager;
	GList *views;
	GList *whitelist;
	GtkTextBuffer *annbuf;
	int favicon_size;
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
  <button onclick="window.devbug('hello')">Say hello</button>
  <button onclick="fillLog()">Fill log</button>
  <script type="text/javascript">
		var verbs, nouns, adjectives, adverbs, preposition;
		nouns = ["bird", "clock", "boy", "plastic", "duck", "teacher", "old lady", "professor", "hamster", "dog"];
		verbs = ["kicked", "ran", "flew", "dodged", "sliced", "rolled", "died", "breathed", "slept", "killed"];
		adjectives = ["beautiful", "lazy", "professional", "lovely", "dumb", "rough", "soft", "hot", "vibrating", "slimy"];
		adverbs = ["slowly", "elegantly", "precisely", "quickly", "sadly", "humbly", "proudly", "shockingly", "calmly", "passionately"];
		preposition = ["down", "into", "up", "on", "upon", "below", "above", "through", "across", "towards"];
		function randGen() { return Math.floor(Math.random() * 5); }
		function sentence() {
			var rand1 = Math.floor(Math.random() * 10);
			var rand2 = Math.floor(Math.random() * 10);
			var rand3 = Math.floor(Math.random() * 10);
			var rand4 = Math.floor(Math.random() * 10);
			var rand5 = Math.floor(Math.random() * 10);
			var rand6 = Math.floor(Math.random() * 10);
			return ("The " + adjectives[rand1] + " " + nouns[rand2] + " " + adverbs[rand3] + " " + verbs[rand4] + " because some " + nouns[rand1] + " " + adverbs[rand1] + " " + verbs[rand1] + " " + preposition[rand1] + " a " + adjectives[rand2] + " " + nouns[rand5] + " which, became a " + adjectives[rand3] + ", " + adjectives[rand4] + " " + nouns[rand6] + ".");
		}
    function fillLog() {
      var i,s;
      for (i = 0; i < 23; i++) window.devbug(sentence());
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

static GtkWidget* find_child(GtkWidget* parent, const gchar* name) {
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

static gboolean on_delete_ephemeral_dialog(GtkDialog *dialog, GdkEvent *event, App app) {
	gtk_widget_destroy(app->dialog);
	app->dialog = NULL;
	app->extra = NULL;
	return true;
}

static void on_external_message_received(WebKitUserContentManager *m, WebKitJavascriptResult *r, App app) {
	JSGlobalContextRef context = webkit_javascript_result_get_global_context(r);
  JSValueRef value = webkit_javascript_result_get_value(r);
  JSStringRef js = JSValueToStringCopy(context, value, NULL);
  size_t n = JSStringGetMaximumUTF8CStringSize(js),l;
  char *s = g_new(char, n);
  JSStringGetUTF8CString(js, s, n);
  l = strlen(s);
  gtk_text_buffer_insert_at_cursor(app->annbuf,s,strlen(s));
  gtk_text_buffer_insert_at_cursor(app->annbuf,"\n",1);
  if (l > GOOBYTERM_ANNUNC_WIDTH) s[GOOBYTERM_ANNUNC_WIDTH] = '\0';
  gtk_label_set_text(GTK_LABEL(app->annunc),s);
  JSStringRelease(js);
  g_free(s);
}

static void on_stop_clicked(GtkButton *btn, App app) {
	if (gtk_stack_get_visible_child(GTK_STACK(app->stack)) == app->web) {
		webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(app->web));
	}
}

static void on_back_clicked(GtkButton *btn, App app) {
	if (gtk_stack_get_visible_child(GTK_STACK(app->stack)) == app->web) {
		webkit_web_view_go_back(WEBKIT_WEB_VIEW(app->web));
	}
}

static void on_forward_clicked(GtkButton *btn, App app) {
	if (gtk_stack_get_visible_child(GTK_STACK(app->stack)) == app->web) {
		webkit_web_view_go_forward(WEBKIT_WEB_VIEW(app->web));
	}
}

static gboolean on_tls_error(WebKitWebView *web, gchar *uri, GTlsCertificate *cert, GTlsCertificateFlags errors, App app) {
	GList *list_item;
	WebKitWebContext *ctx = webkit_web_view_get_context(web);

	fprintf(stderr,"TLS error for %s\n",uri);
	if (app->whitelist) {
		for (list_item = app->whitelist; list_item != NULL; list_item = list_item->next) {
			if (strstr(uri,(const char *)list_item->data)) {
				webkit_web_context_allow_tls_certificate_for_host(ctx,cert,(const gchar *)list_item->data);
				return true;
			}
		}
	}
	return false;
}

static void scale_favicon(cairo_surface_t *surface,GtkImage *img,App app) {
	GdkPixbuf *src,*dest;
	GtkWidget *widget;
	GtkAllocation rect;

	src = gdk_pixbuf_get_from_surface(surface,0,0,cairo_image_surface_get_width(surface),cairo_image_surface_get_height(surface));
	dest = gdk_pixbuf_scale_simple(src,app->favicon_size,app->favicon_size,GDK_INTERP_BILINEAR);
	gtk_image_set_from_pixbuf(img,dest);
	gtk_widget_show(GTK_WIDGET(img));
}

static void on_web_focus(WebKitWebView *web, GdkEvent *event, App app) {
	cairo_surface_t *surface;
	GtkImage *icon;

	if ((surface = webkit_web_view_get_favicon(web)) != NULL) {
		icon = GTK_IMAGE(gtk_stack_get_child_by_name(GTK_STACK(app->status),"favicon"));
		scale_favicon(surface,icon,app);
		gtk_stack_set_visible_child(GTK_STACK(app->status),GTK_WIDGET(icon));
	} else {
		gtk_stack_set_visible_child_name(GTK_STACK(app->status),"missing");
	}
	g_signal_handlers_disconnect_by_func(G_OBJECT(web),(void *)on_web_focus,app);
}

static void on_notify_favicon(WebKitWebView *web, GParamSpec *pspec, App app) {
	cairo_surface_t *surface;
	GtkImage *icon = GTK_IMAGE(gtk_stack_get_child_by_name(GTK_STACK(app->status),"favicon"));
	int w,h;
	GtkAllocation rect;

	fprintf(stderr,"icon ready for %s\n",webkit_web_view_get_uri(web));
	if (web == WEBKIT_WEB_VIEW(app->web)) {
		if ((surface = webkit_web_view_get_favicon(web)) != NULL) {
			scale_favicon(surface,icon,app);
		} else {
			icon = GTK_IMAGE(gtk_stack_get_child_by_name(GTK_STACK(app->status),"missing"));
		}
		gtk_stack_set_visible_child(GTK_STACK(app->status),GTK_WIDGET(icon));
	}
	g_signal_handlers_disconnect_by_func(G_OBJECT(web),(void *)on_notify_favicon,app);
}

static void on_load_changed(WebKitWebView *web, WebKitLoadEvent event, App app) {
	gboolean webfocus = gtk_stack_get_visible_child(GTK_STACK(app->stack)) == GTK_WIDGET(web);
	const gchar *uri = webkit_web_view_get_uri(web);
	GtkWidget *widget;
	GtkAllocation rect;

	switch (event) {
		case WEBKIT_LOAD_STARTED:
			widget = gtk_stack_get_child_by_name(GTK_STACK(app->status),"spinner");
			gtk_spinner_start(GTK_SPINNER(widget));
		break;
		case WEBKIT_LOAD_REDIRECTED:
			widget = gtk_stack_get_child_by_name(GTK_STACK(app->status),"spinner");
			gtk_spinner_start(GTK_SPINNER(widget));
		break;
		case WEBKIT_LOAD_COMMITTED:
			g_signal_connect(G_OBJECT(web),"notify::favicon",G_CALLBACK(on_notify_favicon),app);
			gtk_entry_set_text(GTK_ENTRY(app->entry),uri);
			fprintf(stderr,"committed %s\n",webkit_web_view_get_uri(web));
			widget = gtk_stack_get_child_by_name(GTK_STACK(app->status),"spinner");
			gtk_spinner_stop(GTK_SPINNER(widget));
		break;
		case WEBKIT_LOAD_FINISHED:
			widget = gtk_stack_get_child_by_name(GTK_STACK(app->status),"spinner");
			gtk_spinner_stop(GTK_SPINNER(widget));
			widget = gtk_stack_get_child_by_name(GTK_STACK(app->status),"favicon");
		break;
	}
	if (webfocus) gtk_stack_set_visible_child(GTK_STACK(app->status),widget);
}

static void on_stack_child_focus(GtkStack *widget, GdkEvent *event, App app) {
	cairo_surface_t *surface;
	GtkWidget *child = gtk_stack_get_visible_child(GTK_STACK(app->stack));
	GtkAllocation rect;
	int w,h;
	
	if (WEBKIT_IS_WEB_VIEW(child)) {
		if ((surface = webkit_web_view_get_favicon(WEBKIT_WEB_VIEW(app->web))) != NULL) {
			child = gtk_stack_get_child_by_name(GTK_STACK(app->status),"favicon");
			scale_favicon(surface,GTK_IMAGE(child),app);
			gtk_stack_set_visible_child_name(GTK_STACK(app->status),"favicon");
		} else {
			gtk_stack_set_visible_child_name(GTK_STACK(app->status),"missing");
		}
	} else if (VTE_IS_TERMINAL(child)) {
		gtk_stack_set_visible_child_name(GTK_STACK(app->status),"terminal");
	}
}

static gboolean on_url_dialog_keypress(GtkWidget *widget, GdkEventKey *event, gpointer udata) {
	GtkDialog *dialog = GTK_DIALOG(udata);

	if (!(event->state & GDK_MOD1_MASK & GDK_CONTROL_MASK)) {
		if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
			gtk_dialog_response(dialog,GTK_RESPONSE_OK);
			return true;
		}
	}
	return false;
}

static void on_url_entry_activate(GtkEntry *entry, App app) {
	const gchar *uri;

	uri = gtk_entry_get_text(entry);
	webkit_web_view_load_uri(WEBKIT_WEB_VIEW(app->web),uri);
	gtk_widget_grab_focus(app->web);
}

static gboolean on_whitelist_entry_activate(GtkEntry *entry, App app) {
	const gchar *text = gtk_entry_get_text(entry);
	GtkListBoxRow *row = GTK_LIST_BOX_ROW(gtk_widget_get_parent(GTK_WIDGET(entry)));
	int i = gtk_list_box_row_get_index(row);
	GList *list_item = g_list_nth(app->whitelist,i);

	if (list_item != NULL) {
		g_free(list_item->data);
		if (strlen(text)) {
			list_item->data = g_strdup(text);
		} else {
			app->whitelist = g_list_remove(app->whitelist,list_item);
			gtk_widget_destroy(GTK_WIDGET(row));
		}
	} else {
		fprintf(stderr,"oops: a list box row exists without a backing g_list entry\n");
	}
	return true;
}

static gboolean on_whitelist_entry_append(GtkEntry *entry, App app) {
	const gchar *text = gtk_entry_get_text(entry);
	GtkWidget *last_entry;

	if (strlen(text) > 3) {
		app->whitelist = g_list_append(app->whitelist,g_strdup(text));
		g_signal_handlers_disconnect_by_func(G_OBJECT(entry),(void *)on_whitelist_entry_append,app);
		g_signal_connect(G_OBJECT(entry),"activate",G_CALLBACK(on_whitelist_entry_activate),app);
		last_entry = gtk_entry_new();
		g_signal_connect(G_OBJECT(last_entry),"activate",G_CALLBACK(on_whitelist_entry_activate),app);
		gtk_entry_set_placeholder_text(GTK_ENTRY(last_entry),"<new domain to whitelist>");
		g_signal_connect(G_OBJECT(last_entry),"activate",G_CALLBACK(on_whitelist_entry_append),app);
		gtk_container_add(GTK_CONTAINER(app->extra),last_entry);
		gtk_widget_grab_focus(last_entry);
		gtk_widget_show_all(app->dialog);
	} else {
		gtk_widget_grab_focus(GTK_WIDGET(entry));
	}
	return true;
}

static void on_ephemeral_destroy(GtkWidget *widget, App app) {
	gtk_widget_destroy(widget);
	app->dialog = NULL;
	app->extra = NULL;
}

static void on_devlog_copy_click(GtkWidget *widget, App app) {
	gtk_label_select_region(GTK_LABEL(app->extra),0,-1);
	g_signal_emit_by_name(G_OBJECT(app->extra),"copy-clipboard",NULL);
}

static void on_devlog_clear_click(GtkWidget *widget, App app) {
	GtkTextIter sti,eti;

	widget = gtk_message_dialog_new(
			GTK_WINDOW(app->window),
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_WARNING,
			GTK_BUTTONS_OK_CANCEL,
			"really empty dev log contents?");
	gtk_dialog_set_default_response(GTK_DIALOG(widget),GTK_RESPONSE_CANCEL);
	g_signal_connect_swapped(G_OBJECT(widget),"delete-event",G_CALLBACK(gtk_widget_activate),widget);
	gtk_widget_show_all(widget);
	if (gtk_dialog_run(GTK_DIALOG(widget)) == GTK_RESPONSE_OK) {
		gtk_text_buffer_get_start_iter(app->annbuf,&sti);
		gtk_text_buffer_get_end_iter(app->annbuf,&eti);
		gtk_text_buffer_delete(app->annbuf,&sti,&eti);
		gtk_label_set_text(GTK_LABEL(app->annunc),"welcome to goobyterm");
		gtk_widget_destroy(widget);
	}
}

static gboolean on_app_keypress(GtkWidget *widget, GdkEventKey *event, App app) {
	GList *list_item;
	int i,l,m = 1;
	void *tmp;
	char str[32],*text;
	WebKitWebInspector *inspector;

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
UPDATE_TAB_LABEL:
					l = g_list_length(app->views);
					if (i == -1) i = l - 1;
					if (i == 0) snprintf(str,72,"%d:%d",i,l);
					else snprintf(str,72,"%d:%d",i,l);
					gtk_label_set_text(GTK_LABEL(app->tab_label),(const gchar *)&str[0]);
				}
				return true;
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
				GtkStyleContext *style;
				GtkTextIter sti,eti;
				gchar *text;

				gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(app->annbuf),&sti);
				gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(app->annbuf),&eti);
				text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(app->annbuf),&sti,&eti,false);

				app->dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
				gtk_widget_add_events(app->dialog,GDK_KEY_PRESS_MASK);
				g_signal_connect(G_OBJECT(app->dialog),"key_press_event",G_CALLBACK(on_ephemeral_destroy),&app);
				g_signal_connect(G_OBJECT(app->dialog),"delete-event",G_CALLBACK(on_ephemeral_destroy),app);
				gtk_window_set_transient_for(GTK_WINDOW(app->dialog),GTK_WINDOW(app->window));
				widget = gtk_scrolled_window_new(NULL,NULL);
				gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW(widget),true);
				gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW(widget),true);
				gtk_container_add(GTK_CONTAINER(app->dialog),widget);
				app->extra = gtk_label_new(text);
				gtk_label_set_selectable(GTK_LABEL(app->extra),true);
				g_signal_emit_by_name(G_OBJECT(app->dialog),"set-focus",app->extra);
				gtk_widget_set_state_flags(app->extra,GTK_STATE_FLAG_NORMAL,true);
				g_free(text);
				style = gtk_widget_get_style_context(app->extra);
				gtk_style_context_add_class(style,"annunc");
				gtk_container_add(GTK_CONTAINER(widget),app->extra);
				widget = gtk_header_bar_new();
				gtk_header_bar_set_title(GTK_HEADER_BAR(widget),"dev log");
				gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(widget),true);
				tmp = gtk_button_new_with_label("clear log");
				g_signal_connect(G_OBJECT(tmp),"clicked",G_CALLBACK(on_devlog_clear_click),app);
				gtk_header_bar_pack_end(GTK_HEADER_BAR(widget),GTK_WIDGET(tmp));
				tmp = gtk_button_new_with_label("copy all");
				g_signal_connect(G_OBJECT(tmp),"clicked",G_CALLBACK(on_devlog_copy_click),app);
				gtk_header_bar_pack_end(GTK_HEADER_BAR(widget),GTK_WIDGET(tmp));
				gtk_window_set_titlebar(GTK_WINDOW(app->dialog),widget);
				gtk_widget_show_all(app->dialog);
				return true;
			break;
			case GDK_KEY_n:
				list_item = g_list_first(app->views);
				widget = webkit_web_view_new_with_related_view(WEBKIT_WEB_VIEW(list_item->data));
				webkit_web_view_load_html(WEBKIT_WEB_VIEW(widget),new_tab_html,NULL);
				app->views = g_list_append(app->views,widget);
				g_signal_connect(G_OBJECT(widget),"load-changed",G_CALLBACK(on_load_changed),app);
				g_signal_connect(G_OBJECT(widget),"load-failed-with-tls-errors",G_CALLBACK(on_tls_error),app);
				gtk_widget_show(widget);
				gtk_container_add(GTK_CONTAINER(app->stack),widget);
				gtk_widget_grab_focus(widget);
				gtk_widget_grab_focus(app->entry);
				app->web = widget;
				i = -1;
				goto SHOW_WEB_VIEW;
			break;
			case GDK_KEY_t:
				gtk_stack_set_visible_child(GTK_STACK(app->stack),app->term);
				gtk_widget_grab_focus(app->term);
				return true;
			break;
			case GDK_KEY_u:
				gtk_widget_grab_focus(app->entry);
				return true;
			break;
			case GDK_KEY_w:
				app->dialog = gtk_dialog_new_with_buttons(
						"TLS domain whitelist",
						GTK_WINDOW(app->window),
						GTK_DIALOG_DESTROY_WITH_PARENT,
						"_OK",
						GTK_RESPONSE_OK,
						NULL);
				gtk_dialog_set_default_response(GTK_DIALOG(app->dialog),GTK_RESPONSE_OK);
				app->extra = gtk_list_box_new();
				for (list_item = app->whitelist; list_item != NULL; list_item = list_item->next) {
					tmp = gtk_entry_new();
					gtk_entry_set_text(GTK_ENTRY(tmp),(const gchar *)list_item->data);
					g_signal_connect(G_OBJECT(tmp),"activate",G_CALLBACK(on_whitelist_entry_activate),app);
					gtk_container_add(GTK_CONTAINER(app->extra),GTK_WIDGET(tmp));
				}
				tmp = gtk_entry_new();
				gtk_entry_set_placeholder_text(GTK_ENTRY(tmp),"<new domain to whitelist>");
				g_signal_connect(G_OBJECT(tmp),"activate",G_CALLBACK(on_whitelist_entry_append),app);
				gtk_container_add(GTK_CONTAINER(app->extra),GTK_WIDGET(tmp));
				gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(app->dialog))),app->extra);
				g_signal_connect_swapped(G_OBJECT(app->dialog),"destroy",G_CALLBACK(gtk_widget_activate),app->dialog);
				gtk_widget_show_all(app->dialog);
				gtk_widget_grab_focus(GTK_WIDGET(tmp));
				i = gtk_dialog_run(GTK_DIALOG(app->dialog));
				gtk_widget_destroy(app->dialog);
				app->dialog = NULL;
				app->extra = NULL;
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
		}
	} else {
		switch (event->keyval) {
			case GDK_KEY_F12:
				if (gtk_stack_get_visible_child(GTK_STACK(app->stack)) == app->web) {
					inspector = webkit_web_view_get_inspector(WEBKIT_WEB_VIEW(app->web));
					if (webkit_web_inspector_is_attached(inspector)) {
						webkit_web_inspector_close(inspector);
					} else {
						webkit_web_inspector_show(inspector);
					}
				}
			break;
		}
	}
	return false;
}

int main(int argc, char **argv) {
	_App app;
	GtkAllocation rect;
  GtkWidget *tmp;
  GtkStyleContext *style;
  GtkCssProvider *css;
  GdkPixbufLoader *loader;
	GdkPixbuf *pb;
	WebKitSettings *settings;
	WebKitWebContext *webctx;
	gchar *command[] = {g_strdup(GOOBYTERM_SHELL_CMD), NULL };
	const gchar **wl,*li;

	// init
	app = {};
	gtk_init(&argc, &argv);

	// window
	app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(app.window,"delete-event",G_CALLBACK(gtk_main_quit),NULL);
	gtk_window_set_default_size(GTK_WINDOW(app.window), GOOBYTERM_INITIAL_WIDTH, GOOBYTERM_INITIAL_HEIGHT);
	gtk_window_set_resizable(GTK_WINDOW(app.window), true);
	gtk_window_maximize(GTK_WINDOW(app.window));
  gtk_window_set_position(GTK_WINDOW(app.window), GTK_WIN_POS_CENTER);


	// app icon
	loader = gdk_pixbuf_loader_new();
	gdk_pixbuf_loader_write(loader,icon240x240_png,icon240x240_png_len,NULL);
	pb = gdk_pixbuf_loader_get_pixbuf(loader);
	gtk_window_set_icon(GTK_WINDOW(app.window),pb);
	
	// global styling
	css = gtk_css_provider_new();
	(void)gtk_css_provider_load_from_data(css,GOOBYTERM_ANNUNC_CSS,-1,NULL);
	gtk_style_context_add_provider_for_screen(
			GDK_SCREEN(gtk_window_get_screen(GTK_WINDOW(app.window))),
			GTK_STYLE_PROVIDER(css),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	// app TLS domain whitelist; see config.h
#ifdef GOOBYTERM_WHITELIST
	wl = GOOBYTERM_WHITELIST;
	while ((li = *wl++)) app->whitelist = g_list_append(app->whitelist,g_strdup(li));
#endif
	// default favicon size
	app.favicon_size = GOOBYTERM_FAVICON_SIZE;

	// header bar
	app.header = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(app.header),true);
	gtk_window_set_titlebar(GTK_WINDOW(app.window),app.header);

	// favicon
	app.status = gtk_stack_new();
	gtk_stack_set_homogeneous(GTK_STACK(app.status),true);
	tmp = gtk_image_new_from_icon_name("image-missing",GTK_ICON_SIZE_BUTTON);
	gtk_stack_add_named(GTK_STACK(app.status),tmp,"missing");
	tmp = gtk_image_new_from_icon_name("image-missing",GTK_ICON_SIZE_BUTTON);
	gtk_stack_add_named(GTK_STACK(app.status),tmp,"favicon");
	tmp = gtk_image_new_from_icon_name("utilities-terminal",GTK_ICON_SIZE_BUTTON);
	gtk_stack_add_named(GTK_STACK(app.status),tmp,"terminal");
	tmp = gtk_spinner_new(); 
	gtk_stack_add_named(GTK_STACK(app.status),tmp,"spinner");
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app.header),app.status);

	// url entry
	app.entry = gtk_entry_new();
	gtk_entry_set_width_chars(GTK_ENTRY(app.entry),72);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app.header),app.entry);
	g_signal_connect(G_OBJECT(app.entry),"activate",G_CALLBACK(on_url_entry_activate),&app);

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

	// annunciator widget
	app.annunc = gtk_label_new("welcome to goobyterm");
	app.annbuf = gtk_text_buffer_new(NULL);
	gtk_label_set_width_chars(GTK_LABEL(app.annunc),GOOBYTERM_ANNUNC_WIDTH);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(app.header),app.annunc);

	// styling for the annunciator widget
	style = gtk_widget_get_style_context(app.annunc);
	gtk_style_context_add_class(style,"annunc");

	// tab indicator (label)
	app.tab_label = gtk_label_new("0:1");
	gtk_label_set_width_chars(GTK_LABEL(app.tab_label),5);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(app.header),app.tab_label);

	// the stack - main mechanism for switching web tab view & terminal view
	app.stack = gtk_stack_new();
	gtk_stack_set_homogeneous(GTK_STACK(app.stack),true);
	gtk_stack_set_transition_type(GTK_STACK(app.stack),GTK_STACK_TRANSITION_TYPE_NONE);
	gtk_container_add(GTK_CONTAINER(app.window),app.stack);

	// the browser view, courtesy of WebKit2Gtk
  app.manager = webkit_user_content_manager_new();
  webkit_user_content_manager_register_script_message_handler(app.manager,"external");
  g_signal_connect(app.manager, "script-message-received::external",G_CALLBACK(on_external_message_received),&app);
  app.web = webkit_web_view_new_with_user_content_manager(app.manager);
	webctx = webkit_web_view_get_context(WEBKIT_WEB_VIEW(app.web));
	webkit_web_context_set_favicon_database_directory(webctx,NULL);
#ifdef GOOBYTERM_HOME_URL
  webkit_web_view_load_uri(WEBKIT_WEB_VIEW(app.web),GOOBYTERM_HOME_URL);
#else
  webkit_web_view_load_html(WEBKIT_WEB_VIEW(app.web),new_tab_html,NULL);
#endif
	app.views = g_list_append(app.views,app.web);

	// settings for the default view
  settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(app.web));
  webkit_settings_set_enable_write_console_messages_to_stdout(settings, true);
  webkit_settings_set_enable_developer_extras(settings, true);
	webkit_settings_set_enable_javascript(settings,true);
	webkit_settings_set_allow_modal_dialogs(settings,true);

	// signals for the default webview
  g_signal_connect(G_OBJECT(app.web),"load-changed",G_CALLBACK(on_load_changed),&app);
	g_signal_connect(G_OBJECT(app.web),"load-failed-with-tls-errors",G_CALLBACK(on_tls_error),&app);

	// bind our message-passing function to window.external in the root webview
	webkit_web_view_run_javascript(
      WEBKIT_WEB_VIEW(app.web),
      "window.devbug=function(x){window.webkit.messageHandlers.external.postMessage(x)}",
      NULL, NULL, NULL);

	// add it to the app stack
	gtk_container_add(GTK_CONTAINER(app.stack),app.web);

	// the terminal view, courtesy of the Vte-2.91
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
	// terminal settings
	vte_terminal_set_scroll_on_output(VTE_TERMINAL(app.term), TRUE);
	vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(app.term), TRUE);
	vte_terminal_set_rewrap_on_resize(VTE_TERMINAL(app.term), TRUE);
	vte_terminal_set_mouse_autohide(VTE_TERMINAL(app.term), TRUE);
	gtk_container_add(GTK_CONTAINER(app.stack),app.term);

	/* window (global) keypress handler */
	gtk_widget_add_events(app.window,GDK_KEY_PRESS_MASK);
	g_signal_connect(G_OBJECT(app.window),"key_press_event",G_CALLBACK(on_app_keypress),&app);

	// off to the races
	g_signal_connect(G_OBJECT(app.stack),"set-focus-child",G_CALLBACK(on_stack_child_focus),&app);
	gtk_widget_grab_focus(GTK_WIDGET(app.entry));
	gtk_widget_show_all(GTK_WIDGET(app.window));
	
	gtk_main();

	return 0;
}
