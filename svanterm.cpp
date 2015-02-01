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
    - When dragging terminal to the tabcontrol from above, it doesn't dock until coming to the bottom.
    - The active terminal cursor is visible while another program has the focus
    - Flickering on changing tab (might try black background or some buffering)
    - Move all style to CSS
*/

bool broadcast_active = false;
FindWindow *find_window = NULL;
Tabcontrol *current_dragged_tab = NULL;
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
        event->keyval == GDK_KEY_KP_Enter) {
            label_entry.hide();
        return true;
    }

    return false;
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

    auto tabframe = static_cast<TabFrame *>(get_nth_page(page_num));
    tabframe->label_label.set_name("active_tab");
    if (tabframe->get_focus_child() == NULL)
        build_terminal_list(tabframe)[0]->focus_vte();
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
void update_active_terminals() {
    for (auto window : find_window->list_toplevels()) {
        if (auto terminalwindow = dynamic_cast<TerminalWindow *>(window)) {
            auto focus_child = terminalwindow->get_focus();
            Terminal *focus_terminal = NULL;

            if (find_window->is_visible())
                focus_terminal = find_window->selected_terminal;
            else if (terminalwindow->has_toplevel_focus() && focus_child != NULL)
                focus_terminal = dynamic_cast<Terminal *>(focus_child->get_parent()->get_parent());

            terminalwindow->update_title();
            auto active_page = terminalwindow->tabcontrol.get_nth_page(
                terminalwindow->tabcontrol.get_current_page());
            if (auto tabframe = dynamic_cast<TabFrame *>(active_page))
                tabframe->update_title();

            for (auto terminal : build_terminal_list(active_page)) {
                if (broadcast_active && terminalwindow->has_toplevel_focus())
                    terminal->vte_set_active(true);
                else
                    terminal->vte_set_active(terminal == focus_terminal);
            }
        }
    }
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
