#include "svanterm.h"

FindWindow::FindWindow() {
    resize(600, 300);
    list_box.set_filter_func(mem_fun(this, &FindWindow::filter_function));
    set_opacity(0.9);
    set_decorated(false);
    box.set_orientation(Gtk::ORIENTATION_VERTICAL);
    box.pack_start(search_box, false, false, 0);
    scroll_window.add(list_box);
    box.pack_start(scroll_window, true, true, 0);
    search_box.signal_changed().connect(mem_fun(this, &FindWindow::search_box_changed));
    list_box.signal_row_selected().connect(mem_fun(this, &FindWindow::row_selected));
    add(box);

    signal_key_press_event().connect(mem_fun(this, &FindWindow::KeyPress), false);
    list_box.signal_row_activated().connect(mem_fun(this, &FindWindow::row_activated));
}
void FindWindow::row_selected(Gtk::ListBoxRow* row) {
    if (!is_visible())
        return;

    Terminal *terminal = static_cast<FindLabel *>(row->get_child())->terminal;
    TabFrame *tab_frame= get_tab_frame(terminal);
    Tabcontrol *tabcontrol = static_cast<Tabcontrol *>(tab_frame->get_parent());
    tabcontrol->set_current_page(tabcontrol->page_num(*tab_frame));
    selected_terminal = terminal;
    terminal->focus_vte();

    static_cast<Gtk::Window *>(tabcontrol->get_parent())->present();
    present();
}
bool FindWindow::KeyPress(GdkEventKey* event) {
    if (event->keyval == GDK_KEY_Escape || event->keyval == GDK_KEY_Return ||
        event->keyval == GDK_KEY_KP_Enter)
            hide();

    if (event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_Down) {
        Gtk::ListBoxRow *selected_row = list_box.get_selected_row();
        Gtk::ListBoxRow *new_row;
        int selected_index = selected_row->get_index();
        int new_index = selected_index;
        int length = list_box.get_children().size();

        do {
            if (event->keyval == GDK_KEY_Up)
                new_index = (new_index + length - 1) % length;
            else
                new_index = (new_index + 1) % length;

            new_row = list_box.get_row_at_index(new_index);
            if (new_index == selected_index)
                break; // Didn't find a matching row

        } while (!filter_function(new_row));

        list_box.select_row(*new_row);
        set_focus(search_box);

        return TRUE;
    }

    return FALSE;
}
void FindWindow::search_box_changed() {
    list_box.invalidate_filter();
}
bool FindWindow::filter_function(Gtk::ListBoxRow *row) {
    return (strcasestr(static_cast<Gtk::Label *>(row->get_child())->get_label().c_str(),
                       search_box.get_text().c_str()) != NULL);
}
void FindWindow::show_find(Gtk::Window *calling_window) {
    search_box.set_text("");

    int x, y, width, height, find_width, find_height;
    calling_window->get_position(x, y);
    calling_window->get_size(width, height);
    get_size(find_width, find_height);
    move(x + width/2 - find_width/2, y + height/2 - find_height/2);
    show_all();

    Gtk::ListBoxRow *row = list_box.get_row_at_index(0);
    list_box.select_row(*row);
    set_focus(search_box);
}
void FindWindow::row_activated(Gtk::ListBoxRow* row) {
    hide();
}
