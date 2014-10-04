#include <string>
#include <limits.h>
#include <unistd.h>
#include "svanterm.h"

/*
    Dependencies:
    - libgtk-3-dev
    - libgtkmm-3.0-dev
    - libnotify-dev
    - VTE 0.37.2
        - intltool
        - libgirepository1.0-dev
        - valac-0.22
        - libxml2-utils
        - sudo ln -s /usr/local/lib/libvte-2.91.so.0 /usr/lib/

    TODOS:
    - Broadcast to terminals
    - Resize splitter/terminal hotkey
    - When dragging terminal to the tabcontrol from above, it doesn't dock until coming to the bottom.
    - The active terminal cursor is visible while another program has the focus
    - Flickering on changing tab (might try black background or some buffering)
    - Move all style to CSS
    - Remember state of other splitters when going left and right again
*/

FindWindow *find_window = NULL;
Tabcontrol *current_dragged_tab = NULL;
Terminal *find_selected_terminal = NULL;
TerminalDocker *docker = NULL;

Splitter::Splitter(Gtk::Container *parent, Gtk::Widget *pane1, Gtk::Widget *pane2, Gtk::Orientation orientation) {
    set_orientation(orientation);
    Gtk::Requisition min_size, size;
    parent->get_preferred_size(min_size, size);
    if (orientation == Gtk::ORIENTATION_HORIZONTAL)
        set_position(size.width / 2);
    else
        set_position(size.height / 2);

    Frame *frame1 = manage(new Frame);
    Frame *frame2 = manage(new Frame);

    pane1->reparent(*frame1);
    pane2->reparent(*frame2);
    parent->add(*this);
    frame2->add(*pane2);
    pack1(*frame1, TRUE, FALSE);
    pack2(*frame2, TRUE, FALSE);
    show_all();
};

bool TabFrame::labelentry_lost_focus(GdkEventFocus *event) {
    auto tab_control = static_cast<Tabcontrol *>(get_parent());
    tab_control->set_tab_label(*this, label_eventbox);

    if (!get_focus_child())
        build_terminal_list(this)[0]->focus_vte();

    update_title();
}
bool TabFrame::labelentry_keypress(GdkEventKey* event) {
    if (event->keyval == GDK_KEY_Escape || event->keyval == GDK_KEY_Return ||
        event->keyval == GDK_KEY_KP_Enter)
            label_entry.hide();
}
TabFrame::TabFrame(Terminal *terminal) {
    g_signal_connect(label_eventbox.gobj(), "realize", G_CALLBACK(add_to_docker_gdkwindow_map), this);
    g_signal_connect(label_eventbox.gobj(), "unrealize", G_CALLBACK(remove_from_docker_gdkwindow_map), NULL);

    label_entry.signal_focus_out_event().connect(mem_fun(this, &TabFrame::labelentry_lost_focus));
    label_entry.signal_key_press_event().connect(mem_fun(this, &TabFrame::labelentry_keypress), false);
    label_eventbox.signal_button_press_event().connect(mem_fun(this, &TabFrame::label_button_press));

    label_eventbox.add(label_label);
    label_eventbox.show_all();
    terminal->reparent(*this);
    add(*terminal);
    label_entry.show();
}
void TabFrame::update_title() {
    auto label_text = label_entry.get_text();
    if (label_text != "") {
        label_label.set_label(label_text);
        return;
    }

    Gtk::Widget *focus_child = this;

    while (dynamic_cast<Gtk::Container *>(focus_child))
        focus_child = dynamic_cast<Gtk::Container *>(focus_child)->get_focus_child();

    if (focus_child != 0) {
        const char *title = vte_terminal_get_window_title(VTE_TERMINAL(focus_child->gobj()));
        if (title != NULL) {
            label_label.set_label(std::string(title).substr(0, 20).c_str());
        }
    }
}
void TabFrame::edit_title() {
    auto tab_control = static_cast<Tabcontrol *>(get_parent());
    tab_control->set_tab_label(*this, label_entry);
    static_cast<TerminalWindow *>(get_toplevel())->set_focus(label_entry);
}
TabFrame::~TabFrame() {
    is_killing_terminals = true;

    for (auto it : build_terminal_list(this))
        it->kill_vte();
}
bool TabFrame::label_button_press(GdkEventButton* event) {
    if (event->button == 2)
        delete this;

    return FALSE;
}

void Tabcontrol::tab_drag_begin(const Glib::RefPtr<Gdk::DragContext>& context) {
    current_dragged_tab = this;
}
gboolean Tabcontrol::tab_drag_drop(const Glib::RefPtr<Gdk::DragContext>& context,
                                   int x, int y, guint time) {
    if (current_dragged_tab == this) {
        /* Dragging a tab to itself causes the window to close itself because of tab count 0,
           cancel the action */
        gtk_drag_finish(context->gobj(), FALSE, FALSE, GDK_CURRENT_TIME);
        return TRUE;
    } else
        return FALSE;
}
GtkNotebook* Tabcontrol::detach_to_desktop(GtkNotebook *widget, GtkWidget *frame, gint x, gint y, gpointer user_data) {
    TerminalWindow *window = new TerminalWindow;
    window->move(x, y);
    window->show_all();

    return window->tabcontrol.gobj();;
}
void Tabcontrol::page_removed(Widget* page, guint page_num) {
    if (get_n_pages() == 0)
        static_cast<Gtk::Window *>(get_parent())->close();
};
void Tabcontrol::switch_page(Widget* page, guint page_num) {
    for (int i=0; i<get_n_pages(); i++)
        static_cast<TabFrame *>(get_nth_page(i))->label_label.set_name("");

    static_cast<TabFrame *>(get_nth_page(page_num))->label_label.set_name("active_tab");
}
Tabcontrol::Tabcontrol() {
    set_group_name("svanterm");
    signal_page_removed().connect(mem_fun(this, &Tabcontrol::page_removed));
    signal_drag_begin().connect(mem_fun(this, &Tabcontrol::tab_drag_begin));
    signal_drag_drop().connect(mem_fun(this, &Tabcontrol::tab_drag_drop), false);
    signal_page_added().connect(mem_fun(this, &Tabcontrol::page_added));
    signal_switch_page().connect(mem_fun(this, &Tabcontrol::switch_page));
    set_can_focus(false);
    set_scrollable(true);

    g_signal_connect(gobj(), "create-window", G_CALLBACK(Tabcontrol::detach_to_desktop), NULL);
    g_signal_connect(gobj(), "realize", G_CALLBACK(add_to_docker_gdkwindow_map), this);
    g_signal_connect(gobj(), "unrealize", G_CALLBACK(remove_from_docker_gdkwindow_map), NULL);
}
TabFrame *Tabcontrol::add_tab(TabFrame *tab) {
    append_page(*tab, tab->label_eventbox);
    return tab;
};
void Tabcontrol::page_added(Widget* page, guint page_num) {
    TabFrame *tab = static_cast<TabFrame *>(page);
    set_tab_detachable(*tab, true);
    set_tab_reorderable(*tab, true);
    show_all_children();
    set_current_page(page_num);
}

void TerminalWindow::update_title() {
    auto focus = get_focus();
    if (focus == NULL)
        return;
    Terminal *terminal = dynamic_cast<Terminal *>(focus->get_parent()->get_parent());
    if (terminal == NULL)
        return;

    if (const char *terminal_title = vte_terminal_get_window_title((VteTerminal *)terminal->vte)) {
        auto title = std::string(terminal_title);
        title.append(" - SvanTerm2000");
        set_title(title);
    }
}
void TerminalWindow::walk_terminal(int direction) {
    auto focus_widget = get_focus();
    auto widget = focus_widget;
    std::vector<Terminal *> possible_terminals;

    while (!dynamic_cast<TabFrame *>(widget)) {
        if (auto splitter = dynamic_cast<Splitter *>(widget->get_parent())) {
            if ((splitter->get_orientation() == Gtk::ORIENTATION_VERTICAL &&
                 (direction == GDK_KEY_Down || direction == GDK_KEY_Up)) ||
                (splitter->get_orientation() == Gtk::ORIENTATION_HORIZONTAL &&
                 (direction == GDK_KEY_Left || direction == GDK_KEY_Right))) {
                if (splitter->get_child1() == widget &&
                    (direction == GDK_KEY_Down || direction == GDK_KEY_Right)) {
                        possible_terminals = build_terminal_list(splitter->get_child2());
                        if (possible_terminals.size())
                            break;
                } else if (splitter->get_child2() == widget &&
                    (direction == GDK_KEY_Up || direction == GDK_KEY_Left)) {
                        possible_terminals = build_terminal_list(splitter->get_child1());
                        if (possible_terminals.size())
                            break;
                }
            }
        }
        widget = widget->get_parent();
    }

    if (possible_terminals.size()) {
        Terminal *nearest_terminal = possible_terminals[0];
        int nearest_x, nearest_y, this_x, this_y;
        focus_widget->translate_coordinates(*nearest_terminal, 0, 0, nearest_x, nearest_y);
        for (auto terminal : possible_terminals) {
            focus_widget->translate_coordinates(*terminal, 0, 0, this_x, this_y);
            if (abs(this_x) + abs(this_y) < abs(nearest_x) + abs(nearest_y)) {
                nearest_terminal = terminal;
                nearest_x = this_x;
                nearest_y = this_y;
            }
        }
        nearest_terminal->focus_vte();
    }
}
bool TerminalWindow::KeyPress(GdkEventKey* event) {
    // for (auto terminal : build_terminal_list(tabcontrol.get_nth_page(tabcontrol.get_current_page()))) {
    //     g_signal_emit_by_name(terminal->vte, "key-press-event", event, NULL);
    // }

    TerminalWindow *window;
    auto focus_child = get_focus();
    if (focus_child == NULL)
        return false;
    Terminal *terminal = dynamic_cast<Terminal *>(focus_child->get_parent()->get_parent());
    if (terminal == NULL)
        return false;

    /* Shift-Insert is implemented in VTE, however it seems to fail under certain circumstances.
        We implement it here instead */
    if (event->state & GDK_SHIFT_MASK && event->keyval == GDK_KEY_Insert) {
            vte_terminal_paste_clipboard(VTE_TERMINAL(focus_child->gobj()));
            return true;
    }

    if ((event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK)) {
        switch (event->keyval) {
            case GDK_KEY_W:
                delete get_tab_frame(get_focus());
                return true;

            case GDK_KEY_F:
                find_window->show_find(this);
                return true;

            case GDK_KEY_I:
                terminal->searchbar.set_search_mode();
                return true;

            case GDK_KEY_T:
                tabcontrol.add_tab();
                show_all_children();
                return true;

            case GDK_KEY_J:
                cycle_terminals(-1);
                return true;

            case GDK_KEY_K:
                cycle_terminals(1);
                return true;

            case GDK_KEY_Left:
                cycle_windows(-1);
                return true;

            case GDK_KEY_Right:
                cycle_windows(1);
                return true;

            case GDK_KEY_N:
                window = new TerminalWindow;
                window->tabcontrol.add_tab();
                window->show_all();
                return true;

            case GDK_KEY_M:
                get_tab_frame(terminal)->edit_title();
                return true;

            case GDK_KEY_D:
                terminal->kill_vte();
                return true;

            case GDK_KEY_S:
                terminal->notifications_enabled = !terminal->notifications_enabled;
                terminal->update_title();
                return true;

            case GDK_KEY_E:
            case GDK_KEY_R:
                Gtk::Container *parent = terminal->get_parent();
                Terminal *new_terminal = manage(new Terminal);

                if (event->keyval == GDK_KEY_E)
                    manage(new Splitter(parent, terminal, new_terminal, Gtk::ORIENTATION_HORIZONTAL));
                else
                    manage(new Splitter(parent, terminal, new_terminal, Gtk::ORIENTATION_VERTICAL));

                show_all_children();
                new_terminal->focus_vte();
                return true;
        }
    }

    if ((event->state & GDK_CONTROL_MASK) && !(event->state & GDK_SHIFT_MASK)) {
        switch (event->keyval) {
            case GDK_KEY_Up:
            case GDK_KEY_Down:
            case GDK_KEY_Left:
            case GDK_KEY_Right:
                walk_terminal(event->keyval);
                return true;
        }
    }

    if (!(event->state & GDK_CONTROL_MASK) && (event->state & GDK_SHIFT_MASK)) {
        switch (event->keyval) {
            case GDK_KEY_Left:
                tabcontrol.prev_page();
                return true;

            case GDK_KEY_Right:
                tabcontrol.next_page();
                return true;
        }
    }

    return false;
}
void TerminalWindow::cycle_terminals(int offset) {
    Gtk::Widget *focused = get_focus();
    if (!focused)
        return;

    Terminal *terminal = dynamic_cast<Terminal *>(focused->get_parent()->get_parent());
    auto list = build_terminal_list(get_tab_frame(terminal));
    auto it = find(list.begin(), list.end(), terminal);
    it += offset;
    if (it >= list.end())
        it = list.begin();
    else if (it < list.begin())
        it = list.end() - 1;

    (*it)->focus_vte();
}
void TerminalWindow::cycle_windows(int offset) {
    auto list = list_toplevels();
    auto it = find(list.begin(), list.end(), this);

    while (true) {
        it += offset;
        if (it >= list.end())
            it = list.begin();
        else if (it < list.begin())
            it = list.end() - 1;

        if (dynamic_cast<TerminalWindow *>(*it))
            break;
    }

    (*it)->present();
}
TerminalWindow::TerminalWindow() {
    resize(900, 600);
    signal_delete_event().connect(mem_fun(this, &TerminalWindow::delete_event));
    signal_key_press_event().connect(mem_fun(this, &TerminalWindow::KeyPress), false);
    signal_focus_in_event().connect(mem_fun(this, &TerminalWindow::got_focus));
    add(tabcontrol);
    set_title("SvanTerm2000");
    try {
        set_icon_from_file(getexepath().append("/svanterm.png"));
    } catch (...) {
        printf("Failed to set icon");
        fflush(stdout);
    }
};
bool TerminalWindow::delete_event(GdkEventAny *event) {
    for (auto tabframe : tabcontrol.get_children())
        delete tabframe;

    for (auto it : list_toplevels()) {
        if (TerminalWindow *terminal = dynamic_cast<TerminalWindow *>(it)) {
            if (terminal != this) {
                delete this;
                return FALSE;
            }
        }
    }

    notify_uninit();
    Gtk::Main::quit();
    return FALSE;
}
bool TerminalWindow::got_focus(GdkEventFocus* event) {
    if (get_focus_child() == NULL) {
        auto terminal_list = build_terminal_list(tabcontrol.get_nth_page(tabcontrol.get_current_page()));

        if (terminal_list.size() > 0)
            terminal_list[0]->focus_vte();
    }
    return false;
}

Frame::Frame() {
    set_can_focus(FALSE);
    set_shadow_type(Gtk::SHADOW_NONE);
}
void Frame::destroy() {
    // If this is the last element in a splitter, destroy it!
    Splitter *splitter = dynamic_cast<Splitter *>(get_parent());

    if (splitter) {
        if (splitter->get_children().size() == 1) {
            static_cast<Frame *>(splitter->get_parent())->destroy();
            return;
        } else if (!get_tab_frame(this)->is_killing_terminals) {
            if (this == splitter->get_child1())
                static_cast<TerminalWindow *>(get_toplevel())->cycle_terminals(1);
            else
                static_cast<TerminalWindow *>(get_toplevel())->cycle_terminals(-1);
        }
    }
    delete this;
}

std::string getexepath() {
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    auto exefile = std::string(result, (count > 0) ? count : 0);
    return exefile.substr(0, exefile.find_last_of("/"));
}
TabFrame *get_tab_frame(Gtk::Widget *widget) {
    while (!dynamic_cast<TabFrame *>(widget))
        widget = widget->get_parent();

    return static_cast<TabFrame *>(widget);
}
std::vector<Terminal *> build_terminal_list(Gtk::Widget *widget, std::vector<Terminal *> *list) {
    if (Gtk::Bin *bin = dynamic_cast<Gtk::Bin *>(widget)) {
        build_terminal_list(bin->get_child(), list);
    } else if (Splitter *splitter = dynamic_cast<Splitter *>(widget)) {
        build_terminal_list(splitter->get_child1(), list);
        build_terminal_list(splitter->get_child2(), list);
    } else if (Terminal *terminal = dynamic_cast<Terminal *>(widget))
        list->push_back(terminal);

    return *list;
}
void load_css() {
    auto context = Gtk::StyleContext::create();
    auto css = Gtk::CssProvider::create();
    auto screen = Gdk::Screen::get_default();

    try {
        css->load_from_path(getexepath().append("/svanterm.css"));
    } catch (...) {
        printf("Failed to load CSS");
        fflush(stdout);
    }
    context->add_provider_for_screen(screen, css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}
int main(int argc, char *argv[]) {
    Gtk::Main app(argc, argv);
    notify_init("SvanTerm");
    docker = new TerminalDocker;
    find_window = new FindWindow;
    load_css();
    TerminalWindow *window = new TerminalWindow;
    window->tabcontrol.add_tab();
    window->show_all();

    Gtk::Main::run();
    return 0;
}
