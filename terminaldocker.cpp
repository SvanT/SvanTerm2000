#include "svanterm.h"

extern TerminalDocker *docker;

void add_to_docker_gdkwindow_map(GtkWidget *widget, gpointer user_data) {
    docker->gdkwindow_to_widget[gtk_widget_get_window(widget)] = static_cast<Gtk::Widget *>(user_data);
}
void remove_from_docker_gdkwindow_map(GtkWidget *widget, gpointer user_data) {
    docker->gdkwindow_to_widget.erase(gtk_widget_get_window(widget));
}

bool TerminalDocker::button_release_event(GdkEventButton *event) {
    gdk_device_ungrab(gdk_event_get_device((GdkEvent *)event), GDK_CURRENT_TIME);
    hide();

    Frame *old_frame = static_cast<Frame *>(dock_from->get_parent());
    Tabcontrol *tabcontrol;
    if (dock_to == NULL) {
        TerminalWindow *window = new TerminalWindow;
        window->tabcontrol.add_tab(manage(new TabFrame(dock_from)));
        window->show_all();

        GdkEventButton *button_event = (GdkEventButton *)event;
        window->move(button_event->x_root, button_event->y_root);
        window->show();

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
    return FALSE;
}

bool TerminalDocker::motion_notify_event(GdkEventMotion *event) {
    GdkWindow *window = gdk_device_get_window_at_position(event->device, NULL, NULL);
    Gtk::Widget *widget = NULL;

    if (window) {
        try {
            widget = gdkwindow_to_widget.at(window);
        } catch (const std::out_of_range& oor) {
            // Some other control inside svanterm (main window), ignore this
            return FALSE;
        }
    }

    if (TabFrame *tabframe = dynamic_cast<TabFrame *>(widget)) {
        Tabcontrol *tabcontrol = static_cast<Tabcontrol *>(tabframe->get_parent());
        tabcontrol->set_current_page(tabcontrol->page_num(*tabframe));
        dock_to = tabcontrol;
    } else if (widget != this)
        dock_to = widget;

    move_dock_hint(dock_to, (int)event->x_root, (int)event->y_root);
    return FALSE;
}

TerminalDocker::TerminalDocker() : Gtk::Window(Gtk::WINDOW_POPUP) {
    set_opacity(0.5);
    set_decorated(FALSE);
    signal_button_release_event().connect(mem_fun(this, &TerminalDocker::button_release_event));
    signal_motion_notify_event().connect(mem_fun(this, &TerminalDocker::motion_notify_event));
    g_signal_connect(gobj(), "realize", G_CALLBACK(add_to_docker_gdkwindow_map), this);
    g_signal_connect(gobj(), "unrealize", G_CALLBACK(add_to_docker_gdkwindow_map), NULL);
}

void TerminalDocker::init_drag(Terminal *terminal, GdkEventButton *event) {
    dock_to = dock_from = terminal;
    show_now();
    move_dock_hint(dock_from, (int)event->x_root, (int)event->y_root);

    gdk_device_grab(gdk_event_get_device((GdkEvent *)event), gtk_widget_get_window((GtkWidget *)gobj()),
                    GDK_OWNERSHIP_APPLICATION, FALSE,
                    (GdkEventMask)(GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK),
                    NULL, GDK_CURRENT_TIME);
}

void TerminalDocker::move_dock_hint(Gtk::Widget *widget, int x, int y) {
    if (widget == NULL) {
        move(x + 5, y + 5);
        resize(900, 600);
        return;
    }

    int notebook_x, notebook_y, widget_x, widget_y;
    TerminalWindow *window = static_cast<TerminalWindow *>(widget->get_toplevel());
    widget->translate_coordinates(window->tabcontrol, 0, 0, widget_x, widget_y);
    gdk_window_get_origin(gtk_widget_get_window(GTK_WIDGET(window->tabcontrol.gobj())), &notebook_x, &notebook_y);
    widget_x += notebook_x;
    widget_y += notebook_y;

    if (Tabcontrol *tabcontrol = dynamic_cast<Tabcontrol *>(widget)) {
        resize(tabcontrol->get_width(), 3);
        move(widget_x, widget_y);
        return;
    }

    int width = widget->get_width();
    int height = widget->get_height();
    int from_center_x = x - (widget_x + width/2);
    int from_center_y = y - (widget_y + height/2);

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

    if (dock_pos == Gtk::POS_TOP || dock_pos == Gtk::POS_BOTTOM)
        resize(width, height/2);
    else
        resize(width/2, height);

    switch (dock_pos) {
        case Gtk::POS_TOP:
        case Gtk::POS_LEFT:
            move(widget_x, widget_y);
            break;
        case Gtk::POS_RIGHT:
            move(widget_x + width/2, widget_y);
            break;
        case Gtk::POS_BOTTOM:
            move(widget_x, widget_y + height/2);
    }
}
