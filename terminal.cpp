#define PCRE2_CODE_UNIT_WIDTH 0

#include <pcre2.h>
#include "svanterm.h"

long last_notification_timestamp;
extern FindWindow *find_window;
extern Terminal *dock_from;

void Terminal::kill_vte() {
    kill(child_pid, 9);
}
bool Terminal::header_button_press(GdkEventButton* event) {
    focus_vte();
    if (event->button == 2) {
        kill_vte();
    }

    return TRUE;
}
gboolean Terminal::vte_child_exited(GtkWidget *widget, GdkEventExpose *event, gpointer user_data) {
    Terminal *_this = static_cast<Terminal *>(user_data);
    static_cast<Frame *>(_this->get_parent())->destroy();
}
gboolean Terminal::vte_click(GtkWidget *vte, GdkEvent *event, gpointer user_data) {
    GdkEventButton *button_event = (GdkEventButton *)event;
    if (button_event->button != 1 || !(button_event->state & GDK_CONTROL_MASK))
        return FALSE;

    const char *url = vte_terminal_match_check_event(VTE_TERMINAL(vte), event, 0);
    if (!url)
        return FALSE;

    if (strcasestr(url, "http") != url)
        url = std::string(url).insert(0, "http://").c_str();

    if (vfork() == 0) {
        execlp("xdg-open", "xdg-open", url, NULL);
        _exit(0);
    }
}
gboolean Terminal::vte_got_focus(GtkWidget *vte, GdkEvent *event, gpointer user_data) {
    // To make sure this terminal is selected next time this tab is selected
    Terminal *_this = static_cast<Terminal *>(user_data);
    get_tab_frame(_this)->set_focus_chain(std::vector<Gtk::Widget *>(1, _this));

    update_active_terminals();
    return FALSE;
}
gboolean Terminal::vte_lost_focus(GtkWidget *vte, GdkEvent *event, gpointer user_data) {
    update_active_terminals();
    return FALSE;
}
void Terminal::vte_selection_changed(VteTerminal *vte, gpointer user_data) {
    if (vte_terminal_get_has_selection(vte)) {
        vte_terminal_copy_clipboard(vte);
    }
}
void Terminal::vte_beep(VteTerminal *vte, gpointer user_data) {
    Terminal *_this = static_cast<Terminal *>(user_data);
    if (time(NULL) < last_notification_timestamp + 10)
        return;

    last_notification_timestamp = time(NULL);

    if (_this->notifications_enabled) {
        auto notification = notify_notification_new("Terminal Bell", vte_terminal_get_window_title(vte), "dialog-information");
        notify_notification_show(notification, NULL);
        g_object_unref(G_OBJECT(notification));
    }
}
void Terminal::update_title() {
    find_label.set_label(vte_terminal_get_window_title((VteTerminal *)vte));
    find_window->list_box.invalidate_filter();

    auto title = std::string(vte_terminal_get_window_title((VteTerminal *)vte));
    if (notifications_enabled)
        title.append(" (notif. enabled)");
    label.set_label(title.c_str());

    get_tab_frame(this)->update_title();

    auto window = static_cast<TerminalWindow *>(get_tab_frame(this)->get_parent()->get_parent());
    window->update_title();
}
void Terminal::vte_title_changed(VteTerminal *widget, gpointer user_data) {
    Terminal *_this = static_cast<Terminal *>(user_data);
    _this->update_title();
}
Terminal::Terminal() {
    dock_hint = GdkRectangle{0, 0, 0, 0};
    vte = vte_terminal_new();
    putenv((char *)"BASHOPTS=checkwinsize");
    putenv((char *)"HISTCONTROL=ignoreboth:erasedups");
    char *argv[] = { vte_get_user_shell(), NULL };
    vte_terminal_spawn_sync(VTE_TERMINAL(vte), VTE_PTY_DEFAULT, NULL, argv, NULL,
                            (GSpawnFlags)0, NULL,
                            NULL, &child_pid, NULL, NULL);
    set_orientation(Gtk::ORIENTATION_VERTICAL);
    scrollbox.set_orientation(Gtk::ORIENTATION_HORIZONTAL);
    scrollbar.set_orientation(Gtk::ORIENTATION_VERTICAL);

    eventbox.add(label);
    eventbox.signal_button_press_event().connect(mem_fun(this, &Terminal::header_button_press));

    VteRegex *regex = vte_regex_new_for_match("(https?://|www\\.)[^\\s]*", -1, PCRE2_MULTILINE, NULL);
    vte_terminal_match_add_regex(VTE_TERMINAL(vte), regex, 0);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(vte), 10000);
    g_signal_connect(vte, "bell", G_CALLBACK(Terminal::vte_beep), this);
    g_signal_connect(vte, "child-exited", G_CALLBACK(Terminal::vte_child_exited), this);
    g_signal_connect(vte, "button-press-event", G_CALLBACK(Terminal::vte_click), this);
    g_signal_connect(vte, "focus-in-event", G_CALLBACK(Terminal::vte_got_focus), this);
    g_signal_connect(vte, "focus-out-event", G_CALLBACK(Terminal::vte_lost_focus), this);
    g_signal_connect(vte, "selection-changed", G_CALLBACK(Terminal::vte_selection_changed), this);
    g_signal_connect(vte, "window-title-changed", G_CALLBACK(Terminal::vte_title_changed), this);

    searchbar.add(searchentry);
    searchbar.connect_entry(searchentry);
    searchentry.signal_focus_out_event().connect(mem_fun(this, &Terminal::searchentry_lost_focus));
    searchentry.signal_key_release_event().connect(mem_fun(this, &Terminal::searchentry_keypress));

    pack_start(eventbox, false, false, 0);
    pack_start(scrollbox, true, true, 0);
    pack_start(searchbar, false, false, 0);
    gtk_box_pack_start(GTK_BOX(scrollbox.gobj()), vte, true, true, 0);
    scrollbox.pack_start(scrollbar, false, false, 0);
    gtk_range_set_adjustment(GTK_RANGE(scrollbar.gobj()), gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(vte)));
    show_all_children();

    find_label.terminal = this;
    find_label.set_alignment(0.0, 0.5);
    find_window->list_box.prepend(find_label);

    std::vector<Gtk::TargetEntry> listTargets;
    listTargets.push_back(Gtk::TargetEntry("SvanTerminal", Gtk::TARGET_SAME_APP, 0));

    eventbox.drag_source_set(listTargets);

    drag_dest_set(listTargets);
    eventbox.signal_drag_begin().connect(sigc::mem_fun(this, &Terminal::on_my_drag_begin));
    eventbox.signal_drag_failed().connect(sigc::mem_fun(this, &Terminal::on_my_drag_failed));
    eventbox.signal_drag_end().connect(sigc::mem_fun(this, &Terminal::on_my_drag_end));
    signal_drag_motion().connect(sigc::mem_fun(this, &Terminal::on_my_drag_motion));
    signal_drag_drop().connect(sigc::mem_fun(this, &Terminal::on_my_drag_drop));
    signal_drag_leave().connect(sigc::mem_fun(this, &Terminal::on_my_drag_leave));
}
Terminal::~Terminal() {
    find_window->list_box.remove(*find_label.get_parent());
    gtk_widget_destroy(vte);
}
bool Terminal::searchentry_keypress(GdkEventKey* event) {
    if (event->keyval == GDK_KEY_Shift_L || event->keyval == GDK_KEY_Shift_R)
        return false;

    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        if (event->state & GDK_SHIFT_MASK) {
            if (!vte_terminal_search_find_next((VteTerminal *)vte)) {
                vte_terminal_unselect_all((VteTerminal *)vte);
                vte_terminal_search_find_next((VteTerminal *)vte);
            }
        } else {
            if (!vte_terminal_search_find_previous((VteTerminal *)vte)) {
                vte_terminal_unselect_all((VteTerminal *)vte);
                vte_terminal_search_find_previous((VteTerminal *)vte);
            }
        }
        return false;
    }

    GRegex *regex = g_regex_new(searchentry.get_text().c_str(), G_REGEX_CASELESS, (GRegexMatchFlags)0, 0);
    vte_terminal_unselect_all((VteTerminal *)vte);
    vte_terminal_search_set_gregex((VteTerminal *)vte, regex, (GRegexMatchFlags)0);
    vte_terminal_search_find_previous((VteTerminal *)vte);
    return false;
}
void Terminal::vte_set_active(gboolean active) {
    GtkWidget *terminal = gtk_widget_get_parent(gtk_widget_get_parent(vte));
    GdkRGBA terminal_color;

    if (active) {
        set_name("terminal_active");
        terminal_color = (GdkRGBA){.85, .85, .85, 1};
    } else {
        set_name("terminal_inactive");
        terminal_color = (GdkRGBA){.6, .6, .6, 1};
    }

    vte_terminal_set_color_foreground((VteTerminal *)vte, &terminal_color);
}
void Terminal::focus_vte() {
    gtk_window_set_focus(GTK_WINDOW(get_toplevel()->gobj()), vte);
}
bool Terminal::searchentry_lost_focus(GdkEventFocus *event) {
    if (!get_tab_frame(this)->get_focus_child())
        focus_vte();
}
bool Terminal::on_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
    Box::on_draw(cr);

    cr->set_source_rgb(1, 0, 0);
    cr->rectangle(dock_hint.x, dock_hint.y, dock_hint.x+dock_hint.width, dock_hint.y+dock_hint.height);
    cr->fill();
}
bool Terminal::on_my_drag_motion(const Glib::RefPtr<Gdk::DragContext>& context, int x, int y, guint time) {
    int width = get_width();
    int height = get_height();
    int from_center_x = x - width/2;
    int from_center_y = y - height/2;

    if (ABS(from_center_x) > ABS(from_center_y))
        if (from_center_x > 0)
            dock_pos = Gtk::POS_RIGHT;
        else
            dock_pos = Gtk::POS_LEFT;
    else
        if (from_center_y > 0)
            dock_pos = Gtk::POS_BOTTOM;
        else
            dock_pos = Gtk::POS_TOP;

    switch (dock_pos) {
        case Gtk::POS_TOP:
            dock_hint = GdkRectangle{0, 0, width, height/2};
            break;
        case Gtk::POS_LEFT:
            dock_hint = GdkRectangle{0, 0, width/2, height};
            break;
        case Gtk::POS_RIGHT:
            dock_hint = GdkRectangle{width/2, 0, width/2, height};
            break;
        case Gtk::POS_BOTTOM:
            dock_hint = GdkRectangle{0, height/2, width, height/2};
    }

    queue_draw();
}
bool Terminal::on_my_drag_drop(const Glib::RefPtr<Gdk::DragContext>& context, int x, int y, guint time) {
    dock_hint = GdkRectangle{0, 0, 0, 0};
    queue_draw();

    if (dock_from == this) {
        return FALSE;
    }
    Frame *old_frame = static_cast<Frame *>(dock_from->get_parent());

    switch (dock_pos) {
        case GTK_POS_TOP:
            manage(new Splitter(get_parent(), dock_from, this, Gtk::ORIENTATION_VERTICAL));
            break;
        case GTK_POS_BOTTOM:
            manage(new Splitter(get_parent(), this, dock_from, Gtk::ORIENTATION_VERTICAL));
            break;
        case GTK_POS_LEFT:
            manage(new Splitter(get_parent(), dock_from, this, Gtk::ORIENTATION_HORIZONTAL));
            break;
        case GTK_POS_RIGHT:
            manage(new Splitter(get_parent(), this, dock_from, Gtk::ORIENTATION_HORIZONTAL));
    }


    old_frame->destroy();
    static_cast<TerminalWindow *>(this->get_toplevel())->present();
    dock_from->focus_vte();
}
void Terminal::on_my_drag_begin(const Glib::RefPtr<Gdk::DragContext>& context) {
    dock_from = this;
}
void Terminal::on_my_drag_leave(const Glib::RefPtr<Gdk::DragContext>& context, guint time) {
    dock_hint = GdkRectangle{0, 0, 0, 0};
    queue_draw();
}
bool Terminal::on_my_drag_failed(const Glib::RefPtr<Gdk::DragContext>& context, Gtk::DragResult result) {
    Frame *old_frame = static_cast<Frame *>(dock_from->get_parent());

    if (result == Gtk::DRAG_RESULT_NO_TARGET) {
        TerminalWindow *window = new TerminalWindow;
        window->show();
        window->tabcontrol.add_tab(manage(new TabFrame(dock_from)));
        window->show_all();

        old_frame->destroy();
    }
}
void Terminal::on_my_drag_end(const Glib::RefPtr<Gdk::DragContext>& context) {
    dock_from = NULL;
}