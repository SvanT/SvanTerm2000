#include "svanterm.h"

extern TerminalDocker *docker;

void add_to_docker_gdkwindow_map(GtkWidget *widget, gpointer user_data) {
    docker->gdkwindow_to_widget[gtk_widget_get_window(widget)] = static_cast<Gtk::Widget *>(user_data);
}
void remove_from_docker_gdkwindow_map(GtkWidget *widget, gpointer user_data) {
    docker->gdkwindow_to_widget.erase(gtk_widget_get_window(widget));
}

bool TerminalDocker::drag_drop_finish(Gtk::Widget *widget, int x, int y) {
    dock_to = widget;
    Frame *old_frame = static_cast<Frame *>(dock_from->get_parent());
    Tabcontrol *tabcontrol;
    if (dock_to == NULL) {
        TerminalWindow *window = new TerminalWindow;
        window->show();
        window->tabcontrol.add_tab(manage(new TabFrame(dock_from)));
        window->show_all();
        window->move(x, y);

        old_frame->destroy();

    } else if (dock_from != dock_to) {
        if (tabcontrol = dynamic_cast<Tabcontrol *>(dock_to)) {
            tabcontrol->add_tab(manage(new TabFrame(dock_from)));
        } else {
            switch (dock_pos) {
                case GTK_POS_TOP:
                    manage(new Splitter(dock_to->get_parent(), dock_from, dock_to, Gtk::ORIENTATION_VERTICAL));
                    break;
                case GTK_POS_BOTTOM:
                    manage(new Splitter(dock_to->get_parent(), dock_to, dock_from, Gtk::ORIENTATION_VERTICAL));
                    break;
                case GTK_POS_LEFT:
                    manage(new Splitter(dock_to->get_parent(), dock_from, dock_to, Gtk::ORIENTATION_HORIZONTAL));
                    break;
                case GTK_POS_RIGHT:
                    manage(new Splitter(dock_to->get_parent(), dock_to, dock_from, Gtk::ORIENTATION_HORIZONTAL));
            }
        }
        old_frame->destroy();

        static_cast<TerminalWindow *>(dock_to->get_toplevel())->present();
        dock_from->focus_vte();
    }
    dock_from = NULL;
    return FALSE;
}

bool TerminalDocker::motion_notify_event(GdkEventMotion *event) {
    GdkWindow *window = gdk_device_get_window_at_position(event->device, NULL, NULL);
    Gtk::Widget *widget = NULL;

    if (window) {
        try {
            widget = gdkwindow_to_widget.at(window);
        } catch (const std::out_of_range& oor) {
            // Some other control inside svanterm (main window), get the toplevel
            widget = gdkwindow_to_widget.at(gdk_window_get_toplevel(window));
        }
    }

    if (TabFrame *tabframe = dynamic_cast<TabFrame *>(widget)) {
        Tabcontrol *tabcontrol = static_cast<Tabcontrol *>(tabframe->get_parent());
        tabcontrol->set_current_page(tabcontrol->page_num(*tabframe));
        dock_to = tabcontrol;
    } else if (widget != this)
        dock_to = widget;

    move_dock_hint(dock_to, (int)event->x, (int)event->y);
    return FALSE;
}

TerminalDocker::TerminalDocker() : Gtk::Window(Gtk::WINDOW_POPUP) {
}

void TerminalDocker::init_drag(Terminal *terminal, GdkEventButton *event) {
    dock_to = dock_from = terminal;
}

void TerminalDocker::move_dock_hint(Gtk::Widget *widget, int x, int y) {
    if (auto terminal = dynamic_cast<Terminal *>(dock_to)) {
        terminal->dock_hint = GdkRectangle{0, 0, 0, 0};
        terminal->queue_draw();
    }

    if (widget == NULL) {
        move(x + 5, y + 5);
        resize(900, 600);
        return;
    }

    dock_to = widget;

    if (Tabcontrol *tabcontrol = dynamic_cast<Tabcontrol *>(widget)) {
        resize(tabcontrol->get_width(), 3);
        return;
    }

    int width = widget->get_width();
    int height = widget->get_height();
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

    GdkRectangle rect;
    switch (dock_pos) {
        case Gtk::POS_TOP:
            rect = GdkRectangle{0, 0, width, height/2};
            break;
        case Gtk::POS_LEFT:
            rect = GdkRectangle{0, 0, width/2, height};
            break;
        case Gtk::POS_RIGHT:
            rect = GdkRectangle{width/2, 0, width/2, height};
            break;
        case Gtk::POS_BOTTOM:
            rect = GdkRectangle{0, height/2, width, height/2};
    }

    dynamic_cast<Terminal *>(widget)->dock_hint = rect;

    widget->queue_draw();
}
