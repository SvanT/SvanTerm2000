#include "svanterm.h"

long last_notification_timestamp;
extern FindWindow *find_window;
extern TerminalDocker *docker;

void Terminal::kill_vte() {
    kill(child_pid, 9);
}
bool Terminal::header_button_press(GdkEventButton* event) {
    focus_vte();
    if (event->button == 1)
        docker->init_drag(this, event);
    else if (event->button == 2)
        kill_vte();

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
gboolean Terminal::vte_focus_event(GtkWidget *vte, GdkEvent *event, gpointer user_data) {
    update_active_terminals();
    return FALSE;
}
void Terminal::vte_selection_changed(VteTerminal *vte, gpointer user_data) {
    vte_terminal_copy_clipboard(vte);
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
    vte = vte_terminal_new();
    char *argv[] = { vte_get_user_shell(), NULL };
    vte_terminal_spawn_sync(VTE_TERMINAL(vte), VTE_PTY_DEFAULT, NULL, argv, NULL,
                            (GSpawnFlags)0, NULL,
                            NULL, &child_pid, NULL, NULL);
    set_orientation(Gtk::ORIENTATION_VERTICAL);
    scrollbox.set_orientation(Gtk::ORIENTATION_HORIZONTAL);
    scrollbar.set_orientation(Gtk::ORIENTATION_VERTICAL);

    eventbox.add(label);
    eventbox.signal_button_press_event().connect(mem_fun(this, &Terminal::header_button_press));

    GRegex *regex = g_regex_new("(https?://|www\\.)[^\\s]*", (GRegexCompileFlags)0, (GRegexMatchFlags)0, 0);
    vte_terminal_match_add_gregex(VTE_TERMINAL(vte), regex, (GRegexMatchFlags)0);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(vte), 10000);
    g_signal_connect(vte, "beep", G_CALLBACK(Terminal::vte_beep), this);
    g_signal_connect(vte, "child-exited", G_CALLBACK(Terminal::vte_child_exited), this);
    g_signal_connect(vte, "button-press-event", G_CALLBACK(Terminal::vte_click), this);
    g_signal_connect(vte, "focus-in-event", G_CALLBACK(Terminal::vte_focus_event), this);
    g_signal_connect(vte, "focus-out-event", G_CALLBACK(Terminal::vte_focus_event), this);
    g_signal_connect(vte, "selection-changed", G_CALLBACK(Terminal::vte_selection_changed), this);
    g_signal_connect(vte, "window-title-changed", G_CALLBACK(Terminal::vte_title_changed), this);

    g_signal_connect(eventbox.gobj(), "realize", G_CALLBACK(add_to_docker_gdkwindow_map), this);
    g_signal_connect(eventbox.gobj(), "unrealize", G_CALLBACK(remove_from_docker_gdkwindow_map), NULL);
    g_signal_connect(vte, "realize", G_CALLBACK(add_to_docker_gdkwindow_map), this);
    g_signal_connect(vte, "unrealize", G_CALLBACK(remove_from_docker_gdkwindow_map), NULL);

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
    // To make sure this terminal is selected next time this tab is selected
    //get_tab_frame(this)->set_focus_chain(std::vector<Gtk::Widget *>(1, this));

    gtk_window_set_focus(GTK_WINDOW(get_toplevel()->gobj()), vte);
}
bool Terminal::searchentry_lost_focus(GdkEventFocus *event) {
    if (!get_tab_frame(this)->get_focus_child())
        focus_vte();
}
