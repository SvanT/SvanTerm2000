#include "svanterm.h"

extern bool broadcast_active;
extern FindWindow *find_window;

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
void TerminalWindow::resize_terminal(int direction) {
    auto widget = get_focus();

    while (!dynamic_cast<TabFrame *>(widget)) {
        if (auto splitter = dynamic_cast<Splitter *>(widget->get_parent())) {
            if ((splitter->get_orientation() == Gtk::ORIENTATION_VERTICAL &&
                 (direction == GDK_KEY_Down || direction == GDK_KEY_Up)) ||
                (splitter->get_orientation() == Gtk::ORIENTATION_HORIZONTAL &&
                 (direction == GDK_KEY_Left || direction == GDK_KEY_Right))) {
                    auto position = splitter->get_position();
                    if (direction == GDK_KEY_Up || direction == GDK_KEY_Left)
                        position -= 50;
                    else
                        position += 50;
                    splitter->set_position(position);
                    return;
            }
        }
        widget = widget->get_parent();
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
bool TerminalWindow::confirm_broadcast() {
  Gtk::MessageDialog dialog("This will broadcast your keyboard input to all terminals in the current tab, continue?",
                                      false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_YES_NO, true);
  return dialog.run() == Gtk::RESPONSE_YES;
}
bool TerminalWindow::KeyPress(GdkEventKey* event) {
    TerminalWindow *window;
    auto focus_child = get_focus();
    if (focus_child == NULL)
        return false;
    Terminal *terminal = dynamic_cast<Terminal *>(focus_child->get_parent()->get_parent());
    if (terminal == NULL)
        return false;

    /* Shift-Insert is implemented in VTE, however it seems to fail under certain circumstances.
        We implement it here instead */
    if (event->state & GDK_SHIFT_MASK && (event->keyval == GDK_KEY_Insert ||
        event->state & GDK_CONTROL_MASK && event->keyval == GDK_KEY_V)) {
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

            case GDK_KEY_B:
                if (broadcast_active || confirm_broadcast()) {
                    broadcast_active = !broadcast_active;
                    update_active_terminals();
                }
                return true;

            case GDK_KEY_Up:
            case GDK_KEY_Down:
            case GDK_KEY_Left:
            case GDK_KEY_Right:
                resize_terminal(event->keyval);
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
            case GDK_KEY_Up:
                cycle_windows(-1);
                return true;

            case GDK_KEY_Down:
                cycle_windows(1);
                return true;

            case GDK_KEY_Left:
                tabcontrol.prev_page();
                return true;

            case GDK_KEY_Right:
                tabcontrol.next_page();
                return true;
        }
    }

    if (broadcast_active) {
        gboolean ret;
        for (auto terminal : build_terminal_list(tabcontrol.get_nth_page(tabcontrol.get_current_page()))) {
            g_signal_emit_by_name(terminal->vte, "key-press-event", event, &ret);
        }
        return true;
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
