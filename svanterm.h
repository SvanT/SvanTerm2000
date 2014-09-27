#include <gtkmm.h>
#include <libnotify/notify.h>
#include <vte-2.91/vte/vte.h>

class Terminal;

class FindLabel : public Gtk::Label {
    public:
        Terminal *terminal;
};

class Terminal : public Gtk::Box {
    private:
        Gtk::EventBox eventbox;
        FindLabel find_label;
        Gtk::Label label;
        Gtk::Box scrollbox;
        Gtk::Scrollbar scrollbar;
        Gtk::SearchEntry searchentry;

        bool header_button_press(GdkEventButton* event);
        bool searchentry_keypress(GdkEventKey* event);

        static void vte_beep(VteTerminal *vte, gpointer user_data);
        static gboolean vte_child_exited(GtkWidget *widget, GdkEventExpose *event, gpointer user_data);
        static gboolean vte_click(GtkWidget *vte, GdkEvent *event, gpointer user_data);
        static gboolean vte_got_focus(GtkWidget *vte, GdkEvent *event, gpointer user_data);
        static gboolean vte_lost_focus(GtkWidget *vte, GdkEvent *event, gpointer user_data);
        static void vte_selection_changed(VteTerminal *terminal, gpointer user_data);
        static void vte_title_changed(VteTerminal *widget, gpointer user_data);

        bool searchentry_lost_focus(GdkEventFocus *event);

    public:
        int child_pid;
        bool notifications_enabled = false;
        Gtk::SearchBar searchbar;
        GtkWidget *vte;

        Terminal();
        ~Terminal();
        void vte_set_active(gboolean active);
        void focus_vte();
        void kill_vte();
        void update_title();
};

class Frame : public Gtk::Frame {
    public:
        Frame();
        void destroy();
};


class TabFrame : public Frame {
    public:
        bool is_killing_terminals = false;

        Gtk::EventBox label_eventbox;
        Gtk::Label label_label;

        bool label_button_press(GdkEventButton* event);
        TabFrame(Terminal *terminal = manage(new Terminal));
        ~TabFrame();
        void update_title();
};


class FindWindow : public Gtk::Window {
    private:
        Gtk::Box box;
        Gtk::ScrolledWindow scroll_window;
        Gtk::Entry search_box;

        void row_selected(Gtk::ListBoxRow* row);
        bool KeyPress(GdkEventKey* event);
        void search_box_changed();
        bool filter_function(Gtk::ListBoxRow *row);
        void row_activated(Gtk::ListBoxRow* row);

    public:
        Gtk::ListBox list_box;

        FindWindow();
        void show_find(Gtk::Window *calling_window);
};

class Splitter : public Gtk::Paned {
    public:
        Splitter(Gtk::Container *parent, Gtk::Widget *pane1, Gtk::Widget *pane2, Gtk::Orientation orientation);
};

class Tabcontrol : public Gtk::Notebook {
    private:
        void tab_drag_begin(const Glib::RefPtr<Gdk::DragContext>& context);
        gboolean tab_drag_drop(const Glib::RefPtr<Gdk::DragContext>& context, int x, int y, guint time);
        static GtkNotebook* detach_to_desktop(GtkNotebook *widget, GtkWidget *frame, gint x, gint y, gpointer user_data);
        void page_removed(Widget* page, guint page_num);
        void switch_page(Widget* page, guint page_num);

    public:
        Tabcontrol();
        TabFrame *add_tab(TabFrame *tab = manage(new TabFrame));
        void page_added(Widget* page, guint page_num);
};

class TerminalDocker : public Gtk::Window {
    Gtk::PositionType dock_pos;

    public:
        std::map<GdkWindow*,Gtk::Widget*>gdkwindow_to_widget;

        Terminal *dock_from;
        Gtk::Widget *dock_to;

        TerminalDocker();
        bool button_release_event(GdkEventButton* event);
        bool motion_notify_event(GdkEventMotion* event);
        void init_drag(Terminal *, GdkEventButton *event);
        void move_dock_hint(Gtk::Widget *widget, int x, int y);
};

class TerminalWindow : public Gtk::Window {
    private:
        bool KeyPress(GdkEventKey* event);

    public:
        Tabcontrol tabcontrol;

        void cycle_terminals(int offset);
        void cycle_windows(int offset);
        TerminalWindow();
        bool delete_event(GdkEventAny *event);
        bool got_focus(GdkEventFocus* event);
        void update_title();
};

void add_to_docker_gdkwindow_map(GtkWidget *widget, gpointer user_data);
void remove_from_docker_gdkwindow_map(GtkWidget *widget, gpointer user_data);
TabFrame *get_tab_frame(Gtk::Widget *widget);
std::vector<Terminal *> build_terminal_list(Gtk::Widget *widget, std::vector<Terminal *> *list = new std::vector<Terminal *>);
std::string getexepath();
void load_css();
int main(int argc, char *argv[]);