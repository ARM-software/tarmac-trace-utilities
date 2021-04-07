/*
 * Copyright 2016-2021 Arm Limited. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file is part of Tarmac Trace Utilities
 */

/*
 * GTK front end for tarmac-browser.
 */

#include "browse.hh"

#include <algorithm>
#include <assert.h>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <math.h>
#include <memory>
#include <ostream>
#include <set>
#include <sstream>
#include <string.h>
#include <utility>
#include <vector>

using std::cerr;
using std::count;
using std::cout;
using std::endl;
using std::function;
using std::hex;
using std::ifstream;
using std::invalid_argument;
using std::make_unique;
using std::map;
using std::max;
using std::mem_fn;
using std::min;
using std::move;
using std::ostream;
using std::ostringstream;
using std::out_of_range;
using std::pair;
using std::remove;
using std::set;
using std::string;
using std::swap;
using std::unique_ptr;
using std::vector;

#include <gtk/gtk.h>

static void call_gtk_init()
{
    int argc = 1;
    char arg0[] = "gtk-tarmac-browser";
    char *args[] = {arg0};
    char **argv = args;
    gtk_init(&argc, &argv);
}

#include <iostream>

#if !GTK_CHECK_VERSION(3, 22, 0)
static void popup_position_fn(GtkMenu *menu, gint *xp, gint *yp,
                              gboolean *push_in, gpointer user_data)
{
    const GdkEventButton *event = (const GdkEventButton *)user_data;
    gint x = 0, y = 0;
    if (event->window)
        gdk_window_get_root_origin(event->window, &x, &y);
    *xp = x + event->x;
    *yp = y + event->y;
    *push_in = false;
}
static void gtk_menu_popup_at_pointer_local_impl(GtkMenu *menu,
                                                 const GdkEvent *event)
{
    gtk_menu_popup(menu, nullptr, nullptr, popup_position_fn, (gpointer)event,
                   event->button.button, event->button.time);
}
#undef gtk_menu_popup_at_pointer // just in case
#define gtk_menu_popup_at_pointer gtk_menu_popup_at_pointer_local_impl
#endif // !GTK_CHECK_VERSION(3,22,0)

template <class T> struct ListNode {
    ListNode *next = nullptr, *prev = nullptr;
    ListNode() : next(this), prev(this) {}
    bool linked() const
    {
        if (next == this) {
            assert(prev == this);
            return false;
        } else {
            assert(prev != this);
            return true;
        }
    }
    void link_before(ListNode *before)
    {
        assert(!linked());
        prev = before;
        next = prev->next;
        prev->next = next->prev = this;
    }
    void unlink()
    {
        assert(linked());
        prev->next = next;
        next->prev = prev;
        prev = next = this;
    }
    void unlink_if_linked()
    {
        if (linked())
            unlink();
    }
    T &payload() { return *static_cast<T *>(this); }
    const T &payload() const { return *static_cast<T *>(this); }

    class iterator {
        ListNode *node;

      public:
        explicit iterator(ListNode *node) : node(node) {}
        T &operator*() const { return node->payload(); }
        bool operator==(const iterator &rhs) const { return node == rhs.node; }
        bool operator!=(const iterator &rhs) const { return !(*this == rhs); }
        iterator &operator++()
        {
            node = node->next;
            return *this;
        }
    };

    iterator begin() { return iterator(next); }
    iterator end() { return iterator(this); }
};

struct WindowListNode : ListNode<WindowListNode> {
};
static WindowListNode all_windows;

class Window {
    WindowListNode listnode;

    vector<pair<gpointer, guint>> signal_handlers;

    struct CallbackHolderBase {
        virtual ~CallbackHolderBase() = default;
    };
    vector<unique_ptr<CallbackHolderBase>> callback_holders;

    template <typename RetType, typename InputFirstParam,
              typename OutputFirstParam, typename... Args>
    struct CallbackHolder : CallbackHolderBase {
        using Func = std::function<RetType(OutputFirstParam *, Args...)>;
        Func f;
        OutputFirstParam *ofp;
        CallbackHolder(Func f, OutputFirstParam *ofp) : f(f), ofp(ofp) {}
        static RetType callback(InputFirstParam *, Args... args, gpointer data)
        {

            auto &ch = *reinterpret_cast<CallbackHolder *>(data);
            return ch.f(ch.ofp, args...);
        }
    };

  protected:
    Window();
    virtual ~Window();

    template <typename GTKObjType, typename ClassType, typename RetType,
              typename... ArgTypes>
    void signal_connect(GTKObjType *gtkobj, const char *signame,
                        RetType (ClassType::*handler)(ArgTypes...),
                        vector<pair<gpointer, guint>> &signal_handlers);

    template <typename GTKObjType, typename ClassType, typename RetType,
              typename... ArgTypes>
    void signal_connect(GTKObjType *gtkobj, const char *signame,
                        RetType (ClassType::*handler)(ArgTypes...))
    {
        signal_connect(gtkobj, signame, handler, signal_handlers);
    }

    void disconnect_signals();
};

Window::Window() { listnode.link_before(&all_windows); }

Window::~Window()
{
    disconnect_signals();
    listnode.unlink();
    if (all_windows.next == &all_windows)
        gtk_main_quit();
}

void Window::disconnect_signals()
{
    // Explicitly disconnect all signals we connected, to avoid them
    // using 'this' after free.
    for (auto sig : signal_handlers)
        g_signal_handler_disconnect(sig.first, sig.second);
    signal_handlers.clear();
}

template <typename GTKObjType, typename ClassType, typename RetType,
          typename... ArgTypes>
void Window::signal_connect(GTKObjType *gtkobj, const char *signame,
                            RetType (ClassType::*handler)(ArgTypes...),
                            vector<pair<gpointer, guint>> &signal_handlers)
{
    GObject *obj = G_OBJECT(gtkobj);
    using CallbackHolderInstance =
        CallbackHolder<RetType, GTKObjType, ClassType, ArgTypes...>;
    auto ch = make_unique<CallbackHolderInstance>(
        mem_fn(handler), static_cast<ClassType *>(this));
    guint id = g_signal_connect(
        obj, signame, G_CALLBACK(CallbackHolderInstance::callback), ch.get());
    signal_handlers.push_back({obj, id});
    callback_holders.push_back(move(ch));
}

struct ColourId {
    enum Kind { TarmacHighlightClass, RegMemType, Other };
    enum OtherId {
        AreaBackground,
        DiffBackground,
        PositionMarker,
        FoldButton,
        UnfoldButton,
        LeafNode,
    };
    Kind kind;
    union {
        HighlightClass hc;
        char rmtype;
        OtherId other;
    };

    ColourId() = default;
    ColourId(HighlightClass hc) : kind(TarmacHighlightClass), hc(hc) {}
    ColourId(char rmtype) : kind(RegMemType), rmtype(rmtype) {}
    ColourId(OtherId other) : kind(Other), other(other) {}

    bool operator==(const ColourId &rhs) const { return cmp(rhs) == 0; }
    bool operator!=(const ColourId &rhs) const { return cmp(rhs) != 0; }
    bool operator<=(const ColourId &rhs) const { return cmp(rhs) <= 0; }
    bool operator>=(const ColourId &rhs) const { return cmp(rhs) >= 0; }
    bool operator<(const ColourId &rhs) const { return cmp(rhs) < 0; }
    bool operator>(const ColourId &rhs) const { return cmp(rhs) > 0; }

  private:
    int cmp(const ColourId &rhs) const
    {
        if (kind < rhs.kind)
            return -1;
        if (kind > rhs.kind)
            return +1;
        switch (kind) {
        case TarmacHighlightClass:
            if (hc < rhs.hc)
                return -1;
            if (hc > rhs.hc)
                return +1;
            break;
        case RegMemType:
            if (rmtype < rhs.rmtype)
                return -1;
            if (rmtype > rhs.rmtype)
                return +1;
            break;
        case Other:
            if (other < rhs.other)
                return -1;
            if (other > rhs.other)
                return +1;
            break;
        }
        return 0;
    }
};

// Map each colour id to its configuration-file id and default setting.
static const map<ColourId, pair<string, GdkRGBA>> colour_ids = {
    // Tarmac HighlightClasses
    {HL_NONE, {"trace-text", {0.0, 0.0, 0.0, 1}}},
    {HL_TIMESTAMP, {"trace-timestamp", {0.0, 0.5, 0.0, 1}}},
    {HL_EVENT, {"trace-event", {0.0, 0.0, 0.0, 1}}},
    {HL_PC, {"trace-pc", {0.0, 0.0, 0.5, 1}}},
    {HL_INSTRUCTION, {"trace-instruction", {0.5, 0.0, 0.5, 1}}},
    {HL_ISET, {"trace-iset", {0.5, 0.0, 0.5, 1}}},
    {HL_CPUMODE, {"trace-cpu-mode", {0.0, 0.5, 0.5, 1}}},
    {HL_CCFAIL, {"trace-cc-fail", {0.5, 0.0, 0.0, 1}}},
    {HL_DISASSEMBLY, {"trace-disassembly", {0.0, 0.5, 0.0, 1}}},
    {HL_TEXT_EVENT, {"trace-text-event", {0.0, 0.0, 0.0, 1}}},
    {HL_PUNCT, {"trace-punctuation", {0.5, 0.5, 0.0, 1}}},
    {HL_ERROR, {"trace-error", {1.0, 0.0, 0.0, 1}}},

    // Type codes in register/memory windows
    {'v', {"regmem-value", {0.0, 0.0, 0.0, 1}}},
    {'c', {"regmem-unprintable", {0.5, 0.5, 0.0, 1}}},
    {'u', {"regmem-unknown", {0.5, 0.0, 0.0, 1}}},
    {'f', {"regmem-fixed-text", {0.0, 0.5, 0.0, 1}}},

    // Miscellaneous
    {ColourId::AreaBackground, {"background", {0.0, 0.0, 0.0, 0}}},
    {ColourId::DiffBackground, {"regmem-diff", {0.0, 0.0, 0.0, 0.25}}},
    {ColourId::PositionMarker, {"position-marker", {0.0, 0.0, 0.0, 1}}},
    {ColourId::FoldButton, {"fold-button", {0.0, 0.0, 0.0, 1}}},
    {ColourId::UnfoldButton, {"unfold-button", {0.0, 0.0, 0.0, 1}}},
    {ColourId::LeafNode, {"leaf-node", {0.0, 0.0, 0.0, 1}}},
};

struct Config {
    string font = "Monospace 12";
    map<ColourId, GdkRGBA> colours;
    map<string, ColourId> colour_kws;

    string filename;
    unsigned lineno;

    Config()
    {
        for (const auto &kv : colour_ids) {
            colours[kv.first] = kv.second.second;
            colour_kws[kv.second.first] = kv.first;
        }
    }

    static bool stringok(const char *s) { return s && s[0]; }

    static bool getword(string &s, string &word)
    {
        size_t pos = 0;
        pos = s.find_first_not_of(" \t", pos);
        if (pos == string::npos)
            return false;
        size_t wordstart = pos;
        pos = s.find_first_of(" \t", pos);
        if (pos == string::npos)
            pos = s.size();
        size_t wordend = pos;
        pos = s.find_first_not_of(" \t", pos);
        if (pos == string::npos)
            pos = s.size();

        word = s.substr(wordstart, wordend);
        s = s.substr(pos);
        return true;
    }

    void read()
    {
        const char *env;

        if (stringok(env = getenv("TARMAC_BROWSER_CONFIG")))
            filename = string(env);
        else if (stringok(env = getenv("XDG_CONFIG_HOME")))
            filename = string(env) + "/tarmac-browser/config";
        else if (stringok(env = getenv("HOME")))
            filename = string(env) + "/.config/tarmac-browser/config";
        else
            return;

        ifstream ifs(filename);
        if (ifs.fail())
            return;

        lineno = 1;
        for (string line; getline(ifs, line); lineno++) {
            if (line.size() > 0 && line[0] == '#')
                continue;
            string word;
            if (!getword(line, word))
                continue;

            decltype(colour_kws)::iterator it;

            if (word == "font") {
                font = line;
            } else if ((it = colour_kws.find(word)) != colour_kws.end()) {
                if (!gdk_rgba_parse(&colours[it->second], line.c_str()))
                    cerr << filename << ":" << lineno
                         << ": unable to parse colour '" << line << "'" << endl;
            } else {
                cerr << filename << ":" << lineno
                     << ": unrecognised config directive '" << word << "'"
                     << endl;
            }
        }
    }
};
static Config config;

// Trivial convenience wrapper on the standard cairo_set_source_rgba() which
// takes a GdkRGBA instead of four separate double arguments.
static inline void cairo_set_source_gdkrgba(cairo_t *cr, const GdkRGBA &rgba)
{
    cairo_set_source_rgba(cr, rgba.red, rgba.green, rgba.blue, rgba.alpha);
}

class TextViewWindow : public Window {
  protected:
    GtkWidget *window;
    GtkWidget *grid;
    GtkWidget *toolbar;
    GtkWidget *area;
    GtkWidget *scrollbar;
    GtkAdjustment *sbadj;
    GtkIMContext *imc;
    GtkWidget *context_menu;
    GtkWidget *menu_bar = nullptr;
    GtkAccelGroup *accel_group = nullptr;
    GtkWidget *line_entry;

    PangoFontDescription *fontdesc;
    PangoContext *pangoctx;
    PangoFontset *fontset;
    PangoFontMetrics *metrics;

    cairo_t *cr{nullptr}; // only active during draw_area_shared

    unsigned defrows, defcols;

    double char_width;
    double baseline, line_height; // might be in fractional pixels!
    double underline_thickness;
    unsigned curr_area_width = 0, curr_area_height = 0;

    // wintop indicates where the top of the displayed region is, in
    // fractional _visible_ lines.
    double wintop;

    TextViewWindow(unsigned defcols, unsigned defrows, bool show_scrollbar);
    virtual ~TextViewWindow();

    static gboolean finish_setup_cb(gpointer data);
    void finish_setup();

    void set_scrollbar();
    void set_line_entry(unsigned line);
    void set_title(string title);

    void destroy_win();
    gboolean draw_area_shared(cairo_t *cr);
    gboolean configure_area(GdkEventConfigure *event);
    gint key(GdkEventKey *event);
    gboolean scroll_area_shared(GdkEventScroll *event);
    bool button_down(GdkEventButton *event);
    bool button_up(GdkEventButton *event);
    bool mouse_motion(GdkEventMotion *event);
    void scrollbar_moved();
    void imc_commit_shared(gchar *str);
    void show_menu_bar();
    void line_entry_activated();
    gboolean line_entry_unfocused(GdkEvent *);

    vector<GtkClipboard *> owned_clipboards;
    void take_clipboard_ownership(GdkAtom cbid);
    void clipboard_clear(GtkClipboard *clipboard);
    void clipboard_get_shared(GtkClipboard *clipboard,
                              GtkSelectionData *selection_data, guint info);

    static void clipboard_get_func(GtkClipboard *clipboard,
                                   GtkSelectionData *selection_data, guint info,
                                   gpointer user_data)
    {
        static_cast<TextViewWindow *>(user_data)->clipboard_get_shared(
            clipboard, selection_data, info);
    }
    static void clipboard_clear_func(GtkClipboard *clipboard,
                                     gpointer user_data)
    {
        static_cast<TextViewWindow *>(user_data)->clipboard_clear(clipboard);
    }

  public:
    void close_mi() { destroy_win(); }
    void quit_mi() { gtk_main_quit(); }

  private:
    GtkWidget *add_menu_item_inner(GtkWidget *menu, GtkWidget *item);

    GtkWidget *copy_menuitem = nullptr;
    bool can_copy_to_xdg_clipboard() const;
    void copy_to_xdg_clipboard();
    void copy_mi() { copy_to_xdg_clipboard(); }

  protected:
    GtkWidget *add_accel(GtkWidget *menuitem, const string &accel_path,
                         guint accel_key, GdkModifierType accel_mods);
    GtkWidget *add_menu_separator(GtkWidget *menu);
    GtkWidget *add_menu_heading(GtkWidget *menu, GtkWidget *label);
    template <typename ClassType>
    GtkWidget *add_menu_item(GtkWidget *menu, string text,
                             void (ClassType::*handler)());
    template <typename ClassType>
    GtkWidget *add_check_menu_item(GtkWidget *menu, string text, bool checked,
                                   void (ClassType::*handler)());
    GtkWidget *add_submenu(GtkWidget *menu, string text);

    void add_edit_submenu();
    void update_edit_submenu();

    using PangoLayoutPtr = unique_ptr<PangoLayout, decltype(&g_object_unref)>;
    struct LogicalCell {
        unsigned column;
        uintmax_t y0, y1;
        bool operator==(const LogicalCell &rhs) const
        {
            return column == rhs.column && y0 == rhs.y0 && y1 == rhs.y1;
        }
        bool operator!=(const LogicalCell &rhs) const
        {
            return !(*this == rhs);
        }
    };
    struct LogicalPos {
        LogicalCell cell;
        unsigned char_index;

        bool operator==(const LogicalPos &rhs) const
        {
            return cell == rhs.cell && char_index == rhs.char_index;
        }
        bool operator!=(const LogicalPos &rhs) const { return !(*this == rhs); }
    };
    struct LayoutWithCoords {
        PangoLayoutPtr pl;
        double x, y;
        LogicalCell cell;
    };
    vector<LayoutWithCoords> layouts;
    PangoLayoutPtr new_layout();
    void apply_highlight(PangoLayoutPtr &pl, const LogicalCell &cell,
                         LogicalPos hstart, LogicalPos hend, unsigned r,
                         unsigned g, unsigned b);
    void draw_and_store_layout(PangoLayoutPtr pl, double screen_x,
                               double screen_y, LogicalCell cell);
    bool find_xy(double x, double y, LogicalPos &logpos);

    struct InWindowControl {
        double x, y, w, h;
        function<void()> callback;
    };
    vector<InWindowControl> controls;

    class PosDistance {
        static constexpr int N = 4;
        int cmp(const PosDistance &rhs) const
        {
            for (unsigned i = 0; i < N; i++) {
                if (dists[i] < rhs.dists[i])
                    return -1;
                if (dists[i] > rhs.dists[i])
                    return +1;
            }
            return 0;
        }

      public:
        int64_t dists[N];
        int sign() const
        {
            for (unsigned i = 0; i < N; i++) {
                if (dists[i] < 0)
                    return -1;
                if (dists[i] > 0)
                    return +1;
            }
            return 0;
        }
        bool operator==(const PosDistance &rhs) const { return cmp(rhs) == 0; }
        bool operator!=(const PosDistance &rhs) const { return cmp(rhs) != 0; }
        bool operator<=(const PosDistance &rhs) const { return cmp(rhs) <= 0; }
        bool operator>=(const PosDistance &rhs) const { return cmp(rhs) >= 0; }
        bool operator<(const PosDistance &rhs) const { return cmp(rhs) < 0; }
        bool operator>(const PosDistance &rhs) const { return cmp(rhs) > 0; }
    };

    static PosDistance logpos_dist_rowmajor(const LogicalPos &,
                                            const LogicalPos &);
    static PosDistance logpos_dist_colmajor(const LogicalPos &,
                                            const LogicalPos &);

    virtual unsigned n_display_lines() = 0;
    virtual void draw_area(unsigned start_line, unsigned n_lines,
                           double y_offset) = 0;

    virtual bool function_key(GdkEventKey *event) { return false; }
    virtual void imc_commit(string str) {}
    virtual bool prepare_context_menu(const LogicalPos &logpos)
    {
        return false;
    }
    virtual void click(const LogicalPos &logpos, GdkEventButton *event) {}
    virtual void update_mouseover(const LogicalPos &) {}
    virtual bool scroll_area(GdkEventScroll *event) { return false; }
    virtual void reset_line_entry() = 0;
    virtual void activate_line_entry(unsigned line) = 0;
    virtual void rewrite_selection_endpoints(LogicalPos &anchor,
                                             LogicalPos &cursor)
    {
    }
    virtual void clipboard_copy() {}
    virtual void clipboard_get_paste_data(ostream &os, LogicalPos start,
                                          LogicalPos end) = 0;
    virtual PosDistance logpos_dist(const LogicalPos &lhs,
                                    const LogicalPos &rhs) = 0;
    int logpos_cmp(const LogicalPos &lhs, const LogicalPos &rhs)
    {
        return logpos_dist(lhs, rhs).sign();
    }

  protected:
    enum class SelectionState {
        None,
        MouseDown,
        Dragging,
        Complete,
        Selected
    } selection_state = SelectionState::None;
    LogicalPos selection_anchor, selection_start, selection_end;

    bool mouseover_valid = false;
    LogicalPos mouseover_start, mouseover_end;

  private:
    bool mouseover_active() const
    {
        /*
         * Suppress mouseovers when we're in the middle of
         * drag-selecting.
         */
        return mouseover_valid && (selection_state == SelectionState::None ||
                                   selection_state == SelectionState::Selected);
    }
};

auto TextViewWindow::logpos_dist_rowmajor(const LogicalPos &lhs,
                                          const LogicalPos &rhs) -> PosDistance
{
    PosDistance d;
    d.dists[0] = (int64_t)lhs.cell.y0 - (int64_t)rhs.cell.y0;
    d.dists[1] = (int64_t)lhs.cell.y1 - (int64_t)rhs.cell.y1;
    d.dists[2] = (int64_t)lhs.cell.column - (int64_t)rhs.cell.column;
    d.dists[3] = (int64_t)lhs.char_index - (int64_t)rhs.char_index;
    return d;
}

auto TextViewWindow::logpos_dist_colmajor(const LogicalPos &lhs,
                                          const LogicalPos &rhs) -> PosDistance
{
    PosDistance d;
    d.dists[0] = (int64_t)lhs.cell.column - (int64_t)rhs.cell.column;
    d.dists[1] = (int64_t)lhs.cell.y0 - (int64_t)rhs.cell.y0;
    d.dists[2] = (int64_t)lhs.cell.y1 - (int64_t)rhs.cell.y1;
    d.dists[3] = (int64_t)lhs.char_index - (int64_t)rhs.char_index;
    return d;
}

static void add_to_toolbar(GtkWidget *toolbar, GtkWidget *widget)
{
    GtkToolItem *ti = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(ti), widget);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ti, -1);
    gtk_widget_show(GTK_WIDGET(ti));
}

static GtkWidget *add_label(GtkWidget *control, const char *label_text)
{
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    GtkWidget *label = gtk_label_new(label_text);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), control, 1, 0, 1, 1);
    gtk_widget_show(grid);
    gtk_widget_show(label);
    return grid;
}

static GtkWidget *new_toolbar_entry(GtkWidget *toolbar, const char *label_text)
{
    GtkWidget *entry = gtk_entry_new();
    gtk_widget_show(entry);
    add_to_toolbar(toolbar, add_label(entry, label_text));
    return entry;
}

TextViewWindow::TextViewWindow(unsigned defcols, unsigned defrows,
                               bool show_scrollbar)
    : Window(), defcols(defcols), defrows(defrows)
{
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    signal_connect(window, "destroy", &TextViewWindow::destroy_win);

    grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(window), grid);
    gtk_widget_show(grid);
    area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(area, true);
    gtk_widget_set_vexpand(area, true);
    gtk_grid_attach(GTK_GRID(grid), area, 0, 1, 1, 1);
    gtk_widget_show(area);
    signal_connect(area, "draw", &TextViewWindow::draw_area_shared);
    signal_connect(area, "configure_event", &TextViewWindow::configure_area);
    gtk_widget_add_events(GTK_WIDGET(area),
                          GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK | GDK_BUTTON_MOTION_MASK |
                              GDK_SMOOTH_SCROLL_MASK);
    signal_connect(area, "scroll_event", &TextViewWindow::scroll_area_shared);
    signal_connect(area, "button_press_event", &TextViewWindow::button_down);
    signal_connect(area, "button_release_event", &TextViewWindow::button_up);
    signal_connect(area, "motion_notify_event", &TextViewWindow::mouse_motion);

    unsigned grid_cols = 1;
    if (show_scrollbar) {
        scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, NULL);
        sbadj = gtk_range_get_adjustment(GTK_RANGE(scrollbar));
        gtk_widget_set_vexpand(scrollbar, true);
        gtk_grid_attach(GTK_GRID(grid), scrollbar, grid_cols++, 1, 1, 1);
        gtk_widget_show(scrollbar);
        signal_connect(sbadj, "value_changed",
                       &TextViewWindow::scrollbar_moved);
    } else {
        scrollbar = nullptr;
        sbadj = nullptr;
    }

    toolbar = gtk_toolbar_new();
    gtk_toolbar_set_icon_size(GTK_TOOLBAR(toolbar),
                              GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_grid_attach(GTK_GRID(grid), toolbar, 0, 0, grid_cols, 1);
    gtk_widget_show(toolbar);
    line_entry = new_toolbar_entry(toolbar, "Line:");
    gtk_entry_set_width_chars(GTK_ENTRY(line_entry), 10);

    signal_connect(line_entry, "activate",
                   &TextViewWindow::line_entry_activated);
    signal_connect(line_entry, "focus-out-event",
                   &TextViewWindow::line_entry_unfocused);

    imc = gtk_im_multicontext_new();
    signal_connect(imc, "commit", &TextViewWindow::imc_commit_shared);
    signal_connect(area, "key_press_event", &TextViewWindow::key);
    gtk_widget_set_can_focus(area, true);

    fontdesc = pango_font_description_from_string(config.font.c_str());
    pangoctx = gtk_widget_get_pango_context(window);
    fontset = pango_context_load_fontset(pangoctx, fontdesc,
                                         pango_context_get_language(pangoctx));
    metrics = pango_fontset_get_metrics(fontset);
    char_width = pango_font_metrics_get_approximate_digit_width(metrics) /
                 (double)PANGO_SCALE;
    baseline = pango_font_metrics_get_ascent(metrics) / (double)PANGO_SCALE;
    line_height = (pango_font_metrics_get_ascent(metrics) +
                   pango_font_metrics_get_descent(metrics)) /
                  (double)PANGO_SCALE;
    underline_thickness = pango_font_metrics_get_underline_thickness(metrics) /
                          (double)PANGO_SCALE;

    context_menu = gtk_menu_new();
    gtk_widget_show(context_menu);

    wintop = 0;

    gtk_widget_grab_focus(area);

    g_idle_add(finish_setup_cb, this);
}

void TextViewWindow::set_title(string title)
{
    title += " \xe2\x80\x93 tarmac-browser";
    gtk_window_set_title(GTK_WINDOW(window), title.c_str());
}

void TextViewWindow::show_menu_bar()
{
    gtk_grid_attach_next_to(GTK_GRID(grid), menu_bar, toolbar, GTK_POS_TOP, 2,
                            1);
    gtk_widget_show(menu_bar);
}

gboolean TextViewWindow::finish_setup_cb(gpointer data)
{
    reinterpret_cast<TextViewWindow *>(data)->finish_setup();
    return false;
}

void TextViewWindow::finish_setup()
{
    unsigned winwidth = PANGO_PIXELS(
        pango_font_metrics_get_approximate_digit_width(metrics) * defcols);
    unsigned winheight = line_height * defrows;
    for (GtkWidget *widget : {scrollbar}) {
        if (widget) {
            GtkRequisition req;
            gtk_widget_get_preferred_size(widget, &req, nullptr);
            winwidth += req.width;
        }
    }
    for (GtkWidget *widget : {menu_bar, toolbar}) {
        if (widget) {
            GtkRequisition req;
            gtk_widget_get_preferred_size(widget, &req, nullptr);
            winheight += req.height;
        }
    }
    gtk_window_set_default_size(GTK_WINDOW(window), winwidth, winheight);

    gtk_widget_show(window);
    gtk_im_context_set_client_window(imc, gtk_widget_get_window(window));
}

TextViewWindow::~TextViewWindow()
{
    // Disconnect signals before calling gtk_widget_destroy.
    disconnect_signals();

    // Free up things that won't free up themselves.
    g_object_unref(G_OBJECT(imc));
    gtk_widget_destroy(window);
    pango_font_metrics_unref(metrics);
    pango_font_description_free(fontdesc);
    g_object_unref(fontset);
}

void TextViewWindow::line_entry_activated()
{
    string value = gtk_entry_get_text(GTK_ENTRY(line_entry));
    unsigned line;

    try {
        line = stoul(value);
    } catch (invalid_argument) {
        return;
    } catch (out_of_range) {
        return;
    }

    activate_line_entry(line);
    gtk_widget_grab_focus(area);
}

gboolean TextViewWindow::line_entry_unfocused(GdkEvent *)
{
    reset_line_entry();
    return false;
}

void TextViewWindow::set_line_entry(unsigned line)
{
    ostringstream oss;
    oss << line;
    gtk_entry_set_text(GTK_ENTRY(line_entry), oss.str().c_str());
}

GtkWidget *TextViewWindow::add_menu_item_inner(GtkWidget *menu, GtkWidget *item)
{
    gtk_widget_show(item);
    gtk_container_add(GTK_CONTAINER(menu), item);
    return item;
}

GtkWidget *TextViewWindow::add_menu_separator(GtkWidget *menu)
{
    return add_menu_item_inner(menu, gtk_menu_item_new());
}

GtkWidget *TextViewWindow::add_menu_heading(GtkWidget *menu, GtkWidget *label)
{
    GtkWidget *item = gtk_menu_item_new();
    gtk_widget_set_sensitive(item, false);
    gtk_container_add(GTK_CONTAINER(item), label);
    gtk_widget_show(label);
    g_object_set(G_OBJECT(label), "halign", GTK_ALIGN_START,
                 (const char *)nullptr);
    return add_menu_item_inner(menu, item);
}

template <typename ClassType>
GtkWidget *TextViewWindow::add_menu_item(GtkWidget *menu, string text,
                                         void (ClassType::*handler)())
{
    auto item =
        add_menu_item_inner(menu, gtk_menu_item_new_with_label(text.c_str()));
    signal_connect(item, "activate", handler);
    return item;
}

GtkWidget *TextViewWindow::add_accel(GtkWidget *menuitem,
                                     const string &accel_path, guint accel_key,
                                     GdkModifierType accel_mods)
{
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(menuitem), accel_path.c_str());
    gtk_accel_map_add_entry(accel_path.c_str(), accel_key, accel_mods);
    return menuitem;
}

template <typename ClassType>
GtkWidget *TextViewWindow::add_check_menu_item(GtkWidget *menu, string text,
                                               bool checked,
                                               void (ClassType::*handler)())
{
    auto item = add_menu_item_inner(
        menu, gtk_check_menu_item_new_with_label(text.c_str()));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), checked);
    signal_connect(item, "activate", handler);
    return item;
}

GtkWidget *TextViewWindow::add_submenu(GtkWidget *menu, string text)
{
    auto item =
        add_menu_item_inner(menu, gtk_menu_item_new_with_label(text.c_str()));
    GtkWidget *submenu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
    return submenu;
}

void TextViewWindow::add_edit_submenu()
{
    GtkWidget *menu = add_submenu(menu_bar, "Edit");
    gtk_menu_set_accel_group(GTK_MENU(menu), accel_group);
    copy_menuitem =
        add_menu_item(menu, "Copy", &TextViewWindow::copy_to_xdg_clipboard);
    add_accel(copy_menuitem, "<TextViewWindow>/Edit/Copy", GDK_KEY_c,
              GDK_CONTROL_MASK);
    update_edit_submenu();
}

void TextViewWindow::update_edit_submenu()
{
    if (copy_menuitem)
        gtk_widget_set_sensitive(copy_menuitem, can_copy_to_xdg_clipboard());
}

void TextViewWindow::destroy_win() { delete this; }

gboolean TextViewWindow::configure_area(GdkEventConfigure *event)
{
    curr_area_width = event->width;
    curr_area_height = event->height;
    set_scrollbar();
    return true;
}

auto TextViewWindow::new_layout() -> PangoLayoutPtr
{
    PangoLayoutPtr pl(pango_layout_new(pangoctx), g_object_unref);
    pango_layout_set_font_description(pl.get(), fontdesc);
    return move(pl);
}

void TextViewWindow::apply_highlight(PangoLayoutPtr &pl,
                                     const LogicalCell &cell, LogicalPos hstart,
                                     LogicalPos hend, unsigned r, unsigned g,
                                     unsigned b)
{
    LogicalPos start{cell, 0};
    LogicalPos end{cell, (unsigned)pango_layout_get_character_count(pl.get())};
    if (logpos_cmp(start, hend) <= 0 && logpos_cmp(end, hstart) >= 0) {
        // Some part of this layout needs to be highlighted.
        if (logpos_cmp(hstart, start) < 0)
            hstart = start;
        if (logpos_cmp(hstart, end) > 0)
            hstart = end;
        if (logpos_cmp(hend, start) < 0)
            hend = start;
        if (logpos_cmp(hend, end) > 0)
            hend = end;
        PangoAttrList *list = pango_layout_get_attributes(pl.get());
        bool need_set = false;
        if (!list) {
            list = pango_attr_list_new();
            need_set = true;
        }
        auto *attr = pango_attr_background_new(r, g, b);
        attr->start_index = hstart.char_index;
        attr->end_index = hend.char_index + 1;
        pango_attr_list_insert(list, attr);
        if (need_set)
            pango_layout_set_attributes(pl.get(), list);
    }
}

void TextViewWindow::draw_and_store_layout(PangoLayoutPtr pl, double x,
                                           double y, LogicalCell cell)
{
    if (mouseover_active())
        apply_highlight(pl, cell, mouseover_start, mouseover_end, 0xC000,
                        0xC000, 0xC000);

    if (selection_state == SelectionState::Complete ||
        selection_state == SelectionState::Selected ||
        selection_state == SelectionState::Dragging)
        apply_highlight(pl, cell, selection_start, selection_end, 0x8000,
                        0x8000, 0x8000);

    assert(cr);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, pl.get());
    layouts.push_back({move(pl), x, y, cell});
}

bool TextViewWindow::find_xy(double x, double y, LogicalPos &logpos)
{
    bool got_candidate = false;
    int current_excess;

    size_t i = 0;
    for (auto &lwc : layouts) {
        int xi = (x - lwc.x) * PANGO_SCALE, yi = (y - lwc.y) * PANGO_SCALE;
        auto try_xy_to_index = [&lwc](int xi, int yi, int *index_ptr) {
            int trailing;
            return pango_layout_xy_to_index(lwc.pl.get(), xi, yi, index_ptr,
                                            &trailing);
        };

        int index, excess;
        bool found = try_xy_to_index(xi, yi, &index);
        if (found) {
            excess = 0;
        } else {
            int xi2;
            pango_layout_index_to_line_x(lwc.pl.get(), index, 0, NULL, &xi2);
            if (xi2 < xi) {
                found = try_xy_to_index(xi2, yi, &index);
                excess = xi - xi2;
            }
        }

        if (found && (!got_candidate || current_excess > excess)) {
            logpos.cell = lwc.cell;
            logpos.char_index = index;
            if (excess == 0)
                return true; // no need to look further
            current_excess = excess;
            got_candidate = true;
        }
        i++;
    }

    return got_candidate;
}

gboolean TextViewWindow::draw_area_shared(cairo_t *cr_)
{
    double line_f, fraction = modf(wintop, &line_f);
    unsigned start_line = line_f;
    double total_lines = (double)curr_area_height / line_height + fraction;

    layouts.clear();
    controls.clear();
    cr = cr_;

    const GdkRGBA &rgba = config.colours[ColourId::AreaBackground];
    if (rgba.alpha != 0) {
        cairo_set_source_gdkrgba(cr, rgba);
        cairo_paint(cr);
    }

    draw_area(start_line, ceil(total_lines), -fraction * line_height);
    cr = nullptr;

    return true;
}

bool TextViewWindow::can_copy_to_xdg_clipboard() const
{
    return selection_state == SelectionState::Selected;
}

void TextViewWindow::copy_to_xdg_clipboard()
{
    take_clipboard_ownership(GDK_SELECTION_CLIPBOARD);
}

gint TextViewWindow::key(GdkEventKey *event)
{
    if ((event->keyval == 'c' || event->keyval == 'C') &&
        (event->state & GDK_CONTROL_MASK) && can_copy_to_xdg_clipboard()) {
        copy_to_xdg_clipboard();
    }
    if (gtk_im_context_filter_keypress(imc, event))
        return true;
    return function_key(event);
}

gboolean TextViewWindow::scroll_area_shared(GdkEventScroll *event)
{
    if (scrollbar) {
        gboolean ret;
        g_signal_emit_by_name(scrollbar, "scroll-event", event, &ret);
        return ret;
    } else {
        return scroll_area(event);
    }
}

void TextViewWindow::imc_commit_shared(gchar *str)
{
    return imc_commit(string(str));
}

bool TextViewWindow::button_down(GdkEventButton *event)
{
    gtk_widget_grab_focus(area);

    LogicalPos logpos;
    if (find_xy(event->x, event->y, logpos)) {
        switch (event->button) {
        case 1:
            if ((event->state & GDK_SHIFT_MASK) &&
                selection_state == SelectionState::Selected) {
                // Shift + left-click extends the previous selection,
                // by moving whichever of its endpoints was nearer the
                // click, and setting selection_anchor to the other one.
                selection_state = SelectionState::Dragging;
                PosDistance ds = logpos_dist(logpos, selection_start);
                PosDistance de = logpos_dist(selection_end, logpos);
                if (ds.sign() <= 0 || (de.sign() > 0 && ds < de)) {
                    selection_start = logpos;
                    selection_anchor = selection_end;
                } else {
                    selection_end = logpos;
                    selection_anchor = selection_start;
                }
                gtk_widget_queue_draw(area);
                update_edit_submenu();
                return true;
            } else {
                // Otherwise, a left click (even with Shift) drops an
                // anchor so that dragging can start a new selection.
                selection_state = SelectionState::MouseDown;
                selection_anchor = logpos;
                update_edit_submenu();
                return true;
            }
        case 3:
            // Right mouse click pops up the context menu.
            if (prepare_context_menu(logpos))
                gtk_menu_popup_at_pointer(GTK_MENU(context_menu),
                                          (const GdkEvent *)event);
            return true;
        }
    }
    for (auto &control : controls) {
        double dx = event->x - control.x, dy = event->y - control.y;
        if (0 <= dx && dx < control.w && 0 <= dy && dy < control.h) {
            control.callback();
            break;
        }
    }
    return false;
}

bool TextViewWindow::mouse_motion(GdkEventMotion *event)
{
    LogicalPos logpos;
    if (!find_xy(event->x, event->y, logpos))
        return false;

    if (event->state & GDK_BUTTON1_MASK) {
        // Drag.
        if (selection_state == SelectionState::MouseDown &&
            logpos != selection_anchor) {
            selection_state = SelectionState::Dragging;
            update_edit_submenu();
        }
        if (selection_state == SelectionState::Dragging) {
            LogicalPos anchor = selection_anchor;
            rewrite_selection_endpoints(anchor, logpos);
            if (logpos_cmp(anchor, logpos) > 0)
                swap(anchor, logpos);
            if (selection_start != anchor || selection_end != logpos) {
                selection_start = anchor;
                selection_end = logpos;
                gtk_widget_queue_draw(area);
            }
        }
        return true;
    } else {
        // Non-dragging mouse motion.
        bool prev_active = mouseover_active();
        LogicalPos prev_start = mouseover_start, prev_end = mouseover_end;
        update_mouseover(logpos);
        bool new_active = mouseover_active();
        LogicalPos new_start = mouseover_start, new_end = mouseover_end;
        if (prev_active != new_active ||
            (new_active && (new_start != prev_start || new_end != prev_end)))
            gtk_widget_queue_draw(area);
    }

    return false;
}

bool TextViewWindow::button_up(GdkEventButton *event)
{
    LogicalPos logpos;
    if (find_xy(event->x, event->y, logpos)) {
        switch (event->button) {
        case 1:
            if (selection_state == SelectionState::MouseDown) {
                selection_state = SelectionState::None;
                click(logpos, event);
                gtk_widget_queue_draw(area);
            } else if (selection_state == SelectionState::Dragging) {
                selection_state = SelectionState::Complete;
                clipboard_copy();
                take_clipboard_ownership(GDK_SELECTION_PRIMARY);
                selection_state = SelectionState::Selected;
            }
            update_edit_submenu();
            return true;
        }
    }

    return false;
}

void TextViewWindow::take_clipboard_ownership(GdkAtom cbid)
{
    vector<GtkTargetEntry> targets;
    targets.push_back({(gchar *)"STRING", 0, 0});
    targets.push_back({(gchar *)"UTF8_STRING", 0, 0});
    GtkClipboard *cb = gtk_clipboard_get(cbid);
    if (gtk_clipboard_set_with_data(cb, targets.data(), targets.size(),
                                    clipboard_get_func, clipboard_clear_func,
                                    this)) {
        if (!count(owned_clipboards.begin(), owned_clipboards.end(), cb))
            owned_clipboards.push_back(cb);
    }
}

void TextViewWindow::clipboard_clear(GtkClipboard *cb)
{
    owned_clipboards.erase(
        remove(owned_clipboards.begin(), owned_clipboards.end(), cb),
        owned_clipboards.end());
    if (selection_state == SelectionState::Selected &&
        cb == gtk_clipboard_get(GDK_SELECTION_PRIMARY)) {
        selection_state = SelectionState::None;
        update_edit_submenu();
        gtk_widget_queue_draw(area);
    }
}

void TextViewWindow::clipboard_get_shared(GtkClipboard *,
                                          GtkSelectionData *selection_data,
                                          guint)
{
    ostringstream oss;
    clipboard_get_paste_data(oss, selection_start, selection_end);
    const string &s = oss.str();

    // just in case of integer overflow, if pasting a huge amount of
    // data from a really big trace file
    gint size = std::min(s.size(),
                         static_cast<size_t>(std::numeric_limits<gint>::max()));

    gtk_selection_data_set_text(selection_data, s.data(), size);
}

void TextViewWindow::scrollbar_moved()
{
    assert(sbadj);
    wintop = gtk_adjustment_get_value(sbadj);
    gtk_widget_queue_draw(area);
}

void TextViewWindow::set_scrollbar()
{
    if (!sbadj)
        return;

    double total_lines = n_display_lines();
    double screenful = (double)curr_area_height / line_height;

    gtk_adjustment_set_lower(sbadj, 0);
    gtk_adjustment_set_upper(sbadj, total_lines);
    gtk_adjustment_set_value(sbadj, wintop);
    gtk_adjustment_set_page_size(sbadj, screenful);
    gtk_adjustment_set_step_increment(sbadj, 1);
    gtk_adjustment_set_page_increment(sbadj, screenful * (2.0 / 3.0));
}

class TraceWindow;

class SubsidiaryView;
class SubsidiaryViewListNode : public ListNode<SubsidiaryView> {
};
struct SubsidiaryView : public TextViewWindow, public SubsidiaryViewListNode {
    Browser &br;

    static vector<SubsidiaryView *> all_subviews;
    static set<unsigned> all_subviews_free_indices;
    unsigned all_subviews_our_index;
    static void close_all();

    off_t memroot = 0, diff_memroot = 0;
    unsigned line, diff_minline;

    TraceWindow *tw;
    GtkWidget *link_combo;

    off_t clipboard_memroot;
    virtual void clipboard_copy() override { clipboard_memroot = memroot; }

    SubsidiaryView(pair<unsigned, unsigned> wh, bool sb, Browser &br,
                   TraceWindow *tw);
    virtual ~SubsidiaryView();

    virtual void reset_line_entry() override { set_line_entry(line); }
    virtual void activate_line_entry(unsigned line_) override
    {
        SeqOrderPayload node;
        if (br.node_at_line(line_, &node)) {
            tw = nullptr;
            unlink_if_linked();
            update_trace_window_list();
            update_line(node.memory_root, node.trace_file_firstline);
        }
    }

    void update_line(off_t memroot_, unsigned line_)
    {
        memroot = memroot_;
        line = line_;
        reset_line_entry();
        gtk_widget_queue_draw(area);

        // clear previous diff highlighting
        diff_memroot = 0;
        diff_minline = 0;
    }

    void diff_against(off_t diff_memroot_, unsigned diff_minline_)
    {
        diff_memroot = diff_memroot_;
        diff_minline = diff_minline_;
    }

    void link_combo_changed();
    void update_trace_window_list();
    static void update_all_trace_window_lists();
};

vector<SubsidiaryView *> SubsidiaryView::all_subviews;
set<unsigned> SubsidiaryView::all_subviews_free_indices;

SubsidiaryView::SubsidiaryView(pair<unsigned, unsigned> wh, bool sb,
                               Browser &br, TraceWindow *tw)
    : TextViewWindow(wh.first, wh.second, sb), br(br), tw(tw)
{
    link_combo = gtk_combo_box_text_new();
    gtk_widget_show(link_combo);
    add_to_toolbar(toolbar, add_label(link_combo, "Locked to:"));
    signal_connect(link_combo, "changed", &SubsidiaryView::link_combo_changed);

    if (all_subviews_free_indices.empty()) {
        all_subviews_our_index = all_subviews.size();
        all_subviews.push_back(this);
    } else {
        all_subviews_our_index = *all_subviews_free_indices.begin();
        all_subviews_free_indices.erase(all_subviews_our_index);
        assert(all_subviews_our_index < all_subviews.size());
        assert(all_subviews[all_subviews_our_index] == nullptr);
        all_subviews[all_subviews_our_index] = this;
    }
    update_trace_window_list();

    accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);

    menu_bar = gtk_menu_bar_new();
    GtkWidget *posmenu = add_submenu(menu_bar, "File");
    gtk_menu_set_accel_group(GTK_MENU(posmenu), accel_group);
    add_accel(add_menu_item(posmenu, "Close", &TextViewWindow::close_mi),
              "<TextViewWindow>/File/Close", GDK_KEY_w, GDK_CONTROL_MASK);
    add_accel(add_menu_item(posmenu, "Quit", &TextViewWindow::quit_mi),
              "<TextViewWindow>/File/Quit", GDK_KEY_q, GDK_CONTROL_MASK);
    add_edit_submenu();
    show_menu_bar();
}

SubsidiaryView::~SubsidiaryView()
{
    assert(all_subviews_our_index < all_subviews.size());
    assert(all_subviews[all_subviews_our_index] == this);
    all_subviews[all_subviews_our_index] = nullptr;
    all_subviews_free_indices.insert(all_subviews_our_index);
    while (all_subviews.size() > 0 && !all_subviews.back()) {
        all_subviews.pop_back();
        all_subviews_free_indices.erase(all_subviews.size());
    }

    unlink_if_linked();
}

void SubsidiaryView::update_all_trace_window_lists()
{
    for (auto *svp : all_subviews)
        if (svp)
            svp->update_trace_window_list();
}

void SubsidiaryView::close_all()
{
    for (auto &svp : all_subviews) {
        if (svp) {
            delete svp;
            svp = nullptr;
        }
    }
    all_subviews_free_indices.clear();
}

class RegisterWindow : public SubsidiaryView {
    GtkWidget *reglabel;
    vector<GtkWidget *> menuitems;

    unsigned cols, colwid, rows;

    virtual unsigned n_display_lines() override { return rows; }

    virtual void draw_area(unsigned start_line, unsigned n_lines,
                           double y_offset) override;
    virtual bool prepare_context_menu(const LogicalPos &logpos) override;

    void provenance_query(RegisterId r);

    RegisterId context_menu_reg;
    void register_provenance_mi();

    pair<unsigned, unsigned> compute_size(const vector<RegisterId> &regs);

    virtual PosDistance logpos_dist(const LogicalPos &lhs,
                                    const LogicalPos &rhs) override
    {
        return logpos_dist_colmajor(lhs, rhs);
    }

    virtual void clipboard_get_paste_data(ostream &os, LogicalPos start,
                                          LogicalPos end) override;

  protected:
    vector<RegisterId> regs;
    size_t max_name_len;

  public:
    RegisterWindow(const vector<RegisterId> &regs, Browser &br,
                   TraceWindow *tw);
};

class RegisterWindow32 : public RegisterWindow {
  public:
    RegisterWindow32(Browser &br, TraceWindow *tw);
};
class RegisterWindow64 : public RegisterWindow {
  public:
    RegisterWindow64(Browser &br, TraceWindow *tw);
};
class RegisterWindowSP : public RegisterWindow {
  public:
    RegisterWindowSP(Browser &br, TraceWindow *tw);
};
class RegisterWindowDP : public RegisterWindow {
  public:
    RegisterWindowDP(Browser &br, TraceWindow *tw);
};
class RegisterWindowNeon : public RegisterWindow {
  public:
    RegisterWindowNeon(Browser &br, TraceWindow *tw, bool aarch64);
};
class RegisterWindowMVE : public RegisterWindow {
  public:
    RegisterWindowMVE(Browser &br, TraceWindow *tw);
};

class MemoryWindow : public SubsidiaryView {
    GtkWidget *memlabel;
    vector<GtkWidget *> menuitems;

    GtkWidget *addr_entry;
    void reset_addr_entry();
    void addr_entry_activated();
    gboolean addr_entry_unfocused(GdkEvent *);

    virtual unsigned n_display_lines() override;
    virtual void draw_area(unsigned start_line, unsigned n_lines,
                           double y_offset) override;
    virtual bool scroll_area(GdkEventScroll *event) override;
    virtual bool prepare_context_menu(const LogicalPos &logpos) override;
    virtual bool function_key(GdkEventKey *event) override;
    virtual void rewrite_selection_endpoints(LogicalPos &anchor,
                                             LogicalPos &cursor) override;
    virtual void clipboard_get_paste_data(ostream &os, LogicalPos start,
                                          LogicalPos end) override;

    void provenance_query(Addr start, Addr size);

    pair<unsigned, unsigned> compute_size(int bpl, bool sfb);

    unsigned byte_index(const LogicalPos &pos);

    Addr context_menu_addr, context_menu_size;
    void provenance_mi()
    {
        provenance_query(context_menu_addr, context_menu_size);
    }

    virtual PosDistance logpos_dist(const LogicalPos &lhs,
                                    const LogicalPos &rhs) override
    {
        return logpos_dist_colmajor(lhs, rhs);
    }

    Addr addr_from_valid_logpos(const LogicalPos &logpos);
    bool get_region_under_pos(const LogicalPos &logpos, Addr &start,
                              Addr &size);
    virtual void update_mouseover(const LogicalPos &) override;

  protected:
    int addr_chars, bytes_per_line;
    Addr start_addr;

  public:
    MemoryWindow(Addr addr, int bpl, bool sfb, Browser &br, TraceWindow *tw);
};

class TraceWindow : public TextViewWindow {
    Browser &br;

  public:
    Browser::TraceView vu;

    // Visual distinguisher for when we have more than one trace
    // window open on the same file. It's also our index in the
    // all_trace_windows vector.
    unsigned window_index;
    static vector<TraceWindow *> all_trace_windows;

  private:
    static set<unsigned> all_trace_windows_free_indices;

    bool substitute_branch_targets = true;
    bool syntax_highlight = true;
    bool depth_indentation = true;

    GtkWidget *containing_fn_menulabel;
    vector<GtkWidget *> containing_fn_menuitems;
    GtkWidget *called_fn_menulabel;
    vector<GtkWidget *> called_fn_menuitems;
    GtkWidget *memaccess_menulabel;
    vector<GtkWidget *> memaccess_menuitems;
    GtkWidget *context_memwindow_menuitem;

    GtkWidget *prompt_dialog = nullptr, *prompt_dialog_entry;
    vector<GtkWidget *> prompt_dialog_menu_items;
    vector<pair<gpointer, guint>> prompt_dialog_signal_handlers;
    enum class PromptDialogType { Memory };
    PromptDialogType prompt_dialog_type;

    GtkWidget *indent_check, *highlight_check, *branch_check;

    GtkWidget *time_entry, *pc_entry;

    char context_menu_memtype;
    Addr context_menu_start, context_menu_size;
    off_t context_menu_memroot;

    SubsidiaryViewListNode subview_list;

    enum class UpdateLocationType { NewLog, NewVis, BothNew };

    // Information about the containing function call (if any) for the
    // last place the context menu popped up.
    struct FunctionRange {
        Browser::TraceView &vu;
        Browser &br;

        FunctionRange(Browser::TraceView &vu) : vu(vu), br(vu.br) {}

        unsigned firstline, lastline, depth;
        SeqOrderPayload callnode, firstnode, lastnode;
        bool initialised = false;

        bool set_to_container(SeqOrderPayload &node)
        {
            initialised = false;
            return vu.physline_range_for_containing_function(
                       node, &firstline, &lastline, &depth) &&
                   finish_setup();
        }
        bool set_to_callee(SeqOrderPayload &node)
        {
            initialised = false;
            return vu.physline_range_for_folded_function_after(
                       node, &firstline, &lastline, &depth) &&
                   finish_setup();
        }

      private:
        bool finish_setup()
        {
            if ((firstline > 1 &&
                 !vu.br.get_node_by_physline(firstline - 1, &callnode)) ||
                !vu.br.get_node_by_physline(firstline, &firstnode) ||
                !vu.br.get_node_by_physline(lastline, &lastnode))
                return false;
            initialised = true;
            return true;
        }
    };
    FunctionRange container_fnrange, callee_fnrange;

    struct FoldChangeWrapper {
        TraceWindow &tw;
        unsigned physline_wintop;
        double fraction_wintop;
        bool done = false;
        FoldChangeWrapper(TraceWindow *twp) : tw(*twp)
        {
            double phys_f;
            fraction_wintop = modf(tw.wintop, &phys_f);
            physline_wintop = tw.vu.visible_to_physical_line(phys_f);
        }
        bool progress()
        {
            if (!done) {
                done = true;
                return true;
            }

            tw.wintop = (tw.vu.physical_to_visible_line(physline_wintop) +
                         fraction_wintop);
            tw.vu.update_visible_node();
            tw.vu.update_logical_node();
            tw.set_scrollbar();
            tw.update_location(UpdateLocationType::BothNew);

            return false;
        }
    };

    enum class ContainerFoldType { Fold, FoldSubrs, Unfold };

    virtual unsigned n_display_lines() override;

    virtual void imc_commit(string str) override;
    virtual bool function_key(GdkEventKey *event) override;
    virtual void click(const LogicalPos &logpos,
                       GdkEventButton *event) override;

    virtual void draw_area(unsigned start_line, unsigned n_lines,
                           double y_offset) override;

    virtual bool prepare_context_menu(const LogicalPos &logpos) override;

    virtual void reset_line_entry() override;
    virtual void activate_line_entry(unsigned line) override;

    virtual void clipboard_get_paste_data(ostream &os, LogicalPos start,
                                          LogicalPos end) override;

    void fold_ui_action(const FunctionRange &fnrange, ContainerFoldType type);
    void unfold_ui_action(const FunctionRange &fnrange, bool full);
    void fold_all_ui_action(bool fold);

    void fold_mi()
    {
        fold_ui_action(container_fnrange, ContainerFoldType::Fold);
    }
    void fold_subrs_mi()
    {
        fold_ui_action(container_fnrange, ContainerFoldType::FoldSubrs);
    }
    void unfold_container_mi()
    {
        fold_ui_action(container_fnrange, ContainerFoldType::Unfold);
    }
    void unfold_mi() { unfold_ui_action(callee_fnrange, false); }
    void unfold_full_mi() { unfold_ui_action(callee_fnrange, true); }
    void fold_all_mi() { fold_all_ui_action(true); }
    void unfold_all_mi() { fold_all_ui_action(false); }
    void provenance_mi();
    void context_memwindow_mi();
    void trace_view_mi();
    void mem_view_mi();
    void corereg_view_mi();
    void spreg_view_mi();
    void dpreg_view_mi();
    void neonreg_view_mi();
    void mvereg_view_mi();
    void opt_toggle_mi();
    void recentre_mi();

    void update_location(UpdateLocationType type);
    void keep_visnode_in_view(bool strict_centre = false);
    void update_subviews();
    void tell_subviews_to_diff_against(off_t memroot, unsigned line);

    bool prompt_dialog_prepare();
    void prompt_dialog_setup();
    void prompt_dialog_teardown();
    void prompt_response(gint response_id);

    void set_time_entry(Time time);
    void reset_time_entry();
    void reset_pc_entry();

    void time_entry_activated();
    void pc_entry_activated();
    gboolean time_entry_unfocused(GdkEvent *);
    gboolean pc_entry_unfocused(GdkEvent *);
    template <int direction> void pc_nextprev();

    virtual PosDistance logpos_dist(const LogicalPos &lhs,
                                    const LogicalPos &rhs) override
    {
        return logpos_dist_rowmajor(lhs, rhs);
    }

    void goto_time(Time t);

  public:
    TraceWindow(Browser &br);
    ~TraceWindow();

    // To be called from subviews
    void goto_physline(unsigned line);
    void add_subview(SubsidiaryView *sv);
};

// We number windows from 1, for vague user-friendliness
vector<TraceWindow *> TraceWindow::all_trace_windows{nullptr};
set<unsigned> TraceWindow::all_trace_windows_free_indices;

TraceWindow::TraceWindow(Browser &br)
    : TextViewWindow(80, 50, true), br(br), vu(br), container_fnrange(vu),
      callee_fnrange(vu)
{
    /*
     * Allocate a new window index
     */
    if (all_trace_windows_free_indices.empty()) {
        window_index = all_trace_windows.size();
        all_trace_windows.push_back(this);
    } else {
        window_index = *all_trace_windows_free_indices.begin();
        all_trace_windows_free_indices.erase(window_index);
        assert(window_index < all_trace_windows.size());
        assert(all_trace_windows[window_index] == nullptr);
        all_trace_windows[window_index] = this;
    }

    SubsidiaryView::update_all_trace_window_lists();

    {
        ostringstream oss;
        oss << br.get_tarmac_filename() << " [#" << window_index << "]";
        set_title(oss.str());
    }

    subview_list.next = subview_list.prev = &subview_list;

    add_menu_item(context_menu, "Fold all", &TraceWindow::fold_all_mi);
    add_menu_item(context_menu, "Unfold all", &TraceWindow::unfold_all_mi);

    containing_fn_menulabel = gtk_label_new("");
    containing_fn_menuitems.push_back(add_menu_separator(context_menu));
    containing_fn_menuitems.push_back(
        add_menu_heading(context_menu, containing_fn_menulabel));
    containing_fn_menuitems.push_back(
        add_menu_item(context_menu, "Fold up", &TraceWindow::fold_mi));
    containing_fn_menuitems.push_back(add_menu_item(
        context_menu, "Fold all subroutines", &TraceWindow::fold_subrs_mi));
    containing_fn_menuitems.push_back(add_menu_item(
        context_menu, "Unfold completely", &TraceWindow::unfold_container_mi));

    called_fn_menulabel = gtk_label_new("");
    called_fn_menuitems.push_back(add_menu_separator(context_menu));
    called_fn_menuitems.push_back(
        add_menu_heading(context_menu, called_fn_menulabel));
    called_fn_menuitems.push_back(add_menu_item(
        context_menu, "Unfold one level", &TraceWindow::unfold_mi));
    called_fn_menuitems.push_back(add_menu_item(
        context_menu, "Unfold completely", &TraceWindow::unfold_full_mi));

    memaccess_menulabel = gtk_label_new("");
    memaccess_menuitems.push_back(add_menu_separator(context_menu));
    memaccess_menuitems.push_back(
        add_menu_heading(context_menu, memaccess_menulabel));
    memaccess_menuitems.push_back(add_menu_item(
        context_menu, "Go to previous write", &TraceWindow::provenance_mi));
    context_memwindow_menuitem =
        add_menu_item(context_menu, "Open a memory window here",
                      &TraceWindow::context_memwindow_mi);

    accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);

    menu_bar = gtk_menu_bar_new();
    GtkWidget *posmenu = add_submenu(menu_bar, "File");
    gtk_menu_set_accel_group(GTK_MENU(posmenu), accel_group);
    prompt_dialog_menu_items.push_back(add_accel(
        add_menu_item(posmenu, "New trace view", &TraceWindow::trace_view_mi),
        "<TraceWindow>/File/New", GDK_KEY_n, GDK_CONTROL_MASK));
    GtkWidget *submenu = add_submenu(posmenu, "New...");
    gtk_menu_set_accel_group(GTK_MENU(submenu), accel_group);
    prompt_dialog_menu_items.push_back(add_accel(
        add_menu_item(submenu, "Memory view", &TraceWindow::mem_view_mi),
        "<TraceWindow>/File/New/Memory", GDK_KEY_m, GDK_CONTROL_MASK));
    prompt_dialog_menu_items.push_back(add_menu_item(
        submenu, "Core register view", &TraceWindow::corereg_view_mi));
    prompt_dialog_menu_items.push_back(add_menu_item(
        submenu, "Single-precision FP reg view", &TraceWindow::spreg_view_mi));
    prompt_dialog_menu_items.push_back(add_menu_item(
        submenu, "Double-precision FP reg view", &TraceWindow::dpreg_view_mi));
    prompt_dialog_menu_items.push_back(add_menu_item(
        submenu, "Neon vector reg view", &TraceWindow::neonreg_view_mi));
    prompt_dialog_menu_items.push_back(add_menu_item(
        submenu, "MVE vector reg view", &TraceWindow::mvereg_view_mi));
    add_menu_separator(posmenu);
    prompt_dialog_menu_items.push_back(
        add_accel(add_menu_item(posmenu, "Close", &TraceWindow::close_mi),
                  "<TextViewWindow>/File/Close", GDK_KEY_w, GDK_CONTROL_MASK));
    prompt_dialog_menu_items.push_back(
        add_accel(add_menu_item(posmenu, "Quit", &TraceWindow::quit_mi),
                  "<TextViewWindow>/File/Quit", GDK_KEY_q, GDK_CONTROL_MASK));

    add_edit_submenu();

    posmenu = add_submenu(menu_bar, "View");
    gtk_menu_set_accel_group(GTK_MENU(posmenu), accel_group);
    prompt_dialog_menu_items.push_back(add_accel(
        add_menu_item(posmenu, "Re-centre", &TraceWindow::recentre_mi),
        "<TraceViewWindow>/View/Recentre", GDK_KEY_l, GDK_CONTROL_MASK));
    add_menu_separator(posmenu);
    prompt_dialog_menu_items.push_back(
        indent_check = add_check_menu_item(posmenu, "Call-depth indentation",
                                           depth_indentation,
                                           &TraceWindow::opt_toggle_mi));
    prompt_dialog_menu_items.push_back(
        highlight_check =
            add_check_menu_item(posmenu, "Syntax highlighting",
                                syntax_highlight, &TraceWindow::opt_toggle_mi));
    if (br.has_image()) {
        prompt_dialog_menu_items.push_back(
            branch_check = add_check_menu_item(
                posmenu, "Symbolic branch targets", substitute_branch_targets,
                &TraceWindow::opt_toggle_mi));
    } else {
        branch_check = nullptr;
    }

    time_entry = new_toolbar_entry(toolbar, "Time:");
    gtk_entry_set_width_chars(GTK_ENTRY(time_entry), 10);
    pc_entry = new_toolbar_entry(toolbar, "PC:");
    gtk_entry_set_width_chars(GTK_ENTRY(pc_entry), 30);

    signal_connect(time_entry, "activate", &TraceWindow::time_entry_activated);
    signal_connect(time_entry, "focus-out-event",
                   &TraceWindow::time_entry_unfocused);

    signal_connect(pc_entry, "activate", &TraceWindow::pc_entry_activated);
    signal_connect(pc_entry, "focus-out-event",
                   &TraceWindow::pc_entry_unfocused);

    GtkToolItem *pc_prev_button = gtk_tool_button_new(NULL, NULL);
    gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(pc_prev_button),
                                  "go-previous");
    gtk_tool_item_set_tooltip_text(pc_prev_button, "Previous visit to this PC");
    gtk_widget_show(GTK_WIDGET(pc_prev_button));
    signal_connect(pc_prev_button, "clicked", &TraceWindow::pc_nextprev<-1>);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), pc_prev_button, -1);

    GtkToolItem *pc_next_button = gtk_tool_button_new(NULL, NULL);
    gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(pc_next_button), "go-next");
    gtk_tool_item_set_tooltip_text(pc_next_button, "Next visit to this PC");
    gtk_widget_show(GTK_WIDGET(pc_next_button));
    signal_connect(pc_next_button, "clicked", &TraceWindow::pc_nextprev<+1>);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), pc_next_button, -1);

    show_menu_bar();

    vu.goto_visline(1);

    if (br.index.isAArch64())
        add_subview(new RegisterWindow64(br, this));
    else
        add_subview(new RegisterWindow32(br, this));

    reset_time_entry();
    reset_line_entry();
    reset_pc_entry();
    update_subviews();
}

TraceWindow::~TraceWindow()
{
    prompt_dialog_teardown();
    while (true) {
        auto it = subview_list.begin();
        if (it == subview_list.end())
            break;
        delete &*it;
    }

    /*
     * Free our window index
     */
    assert(window_index < all_trace_windows.size());
    assert(all_trace_windows[window_index] == this);
    all_trace_windows[window_index] = nullptr;
    all_trace_windows_free_indices.insert(window_index);
    while (all_trace_windows.size() > 1 && !all_trace_windows.back()) {
        all_trace_windows.pop_back();
        all_trace_windows_free_indices.erase(all_trace_windows.size());
    }

    // If all trace windows are closed, clean up detached subviews
    if (all_trace_windows.size() == 1)
        SubsidiaryView::close_all();

    SubsidiaryView::update_all_trace_window_lists();
}

void TraceWindow::add_subview(SubsidiaryView *sv)
{
    sv->unlink_if_linked();
    sv->link_before(&subview_list);
    sv->update_line(vu.curr_logical_node.memory_root,
                    vu.curr_logical_node.trace_file_firstline);
}

void SubsidiaryView::link_combo_changed()
{
    const char *idtext =
        gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(link_combo));
    if (idtext && *idtext == '#') {
        unsigned new_index = stoull(string(idtext + 1));
        assert(new_index < TraceWindow::all_trace_windows.size());
        TraceWindow *new_tw = TraceWindow::all_trace_windows[new_index];
        assert(new_tw);
        unlink_if_linked();
        tw = new_tw;
        tw->add_subview(this);
    }
}

void SubsidiaryView::update_trace_window_list()
{
    GtkComboBoxText *cb = GTK_COMBO_BOX_TEXT(link_combo);
    gtk_combo_box_text_remove_all(cb);
    gtk_combo_box_text_append_text(cb, "None");
    int curr_id = 0;
    int id = curr_id++;
    for (auto *tw : TraceWindow::all_trace_windows) {
        if (tw) {
            ostringstream oss;
            oss << "#" << tw->window_index;
            gtk_combo_box_text_append_text(cb, oss.str().c_str());
            if (tw == this->tw)
                id = curr_id;
            curr_id++;
        }
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(link_combo), id);
}

void TraceWindow::set_time_entry(Time time)
{
    ostringstream oss;
    oss << time;
    gtk_entry_set_text(GTK_ENTRY(time_entry), oss.str().c_str());
}

void TraceWindow::reset_time_entry()
{
    set_time_entry(vu.curr_logical_node.mod_time);
}

void TraceWindow::time_entry_activated()
{
    string value = gtk_entry_get_text(GTK_ENTRY(time_entry));
    Time time;

    try {
        time = stoull(value);
    } catch (invalid_argument) {
        return;
    } catch (out_of_range) {
        return;
    }

    if (!vu.goto_time(time))
        return;

    update_location(UpdateLocationType::NewVis);
    keep_visnode_in_view();
    gtk_widget_grab_focus(area);
}

void TraceWindow::reset_line_entry()
{
    // We report line numbers as the last line of the node, i.e. the
    // line shown just above the separator. This also means you can
    // regard the line number as a Python-style index of a gap
    // _between_ lines - it counts the number of lines above the
    // separator.
    unsigned line = (vu.curr_logical_node.trace_file_firstline +
                     vu.curr_logical_node.trace_file_lines - 1);
    ostringstream oss;
    oss << line;
    gtk_entry_set_text(GTK_ENTRY(line_entry), oss.str().c_str());
}

gboolean TraceWindow::time_entry_unfocused(GdkEvent *)
{
    reset_time_entry();
    return false;
}

void TraceWindow::pc_entry_activated()
{
    string value = gtk_entry_get_text(GTK_ENTRY(pc_entry));
    Addr pc;

    try {
        pc = vu.evaluate_expression_addr(value);
    } catch (invalid_argument) {
        return;
    }

    if (!vu.goto_pc(pc, +1))
        return;

    update_location(UpdateLocationType::NewVis);
    keep_visnode_in_view();
    gtk_widget_grab_focus(area);
}

void TraceWindow::reset_pc_entry()
{
    ostringstream oss;
    unsigned long long pc;
    if (vu.get_current_pc(pc)) {
        oss << "0x" << hex << pc;
        string sym = br.get_symbolic_address(pc, false);
        if (!sym.empty())
            oss << " = " << sym;
    }
    gtk_entry_set_text(GTK_ENTRY(pc_entry), oss.str().c_str());
}

gboolean TraceWindow::pc_entry_unfocused(GdkEvent *)
{
    reset_pc_entry();
    return false;
}

template <int direction> void TraceWindow::pc_nextprev()
{
    unsigned long long pc;
    if (vu.get_current_pc(pc) && vu.goto_pc(pc, direction)) {
        update_location(UpdateLocationType::NewVis);
        keep_visnode_in_view();
    }
    gtk_widget_grab_focus(area);
}

void TraceWindow::activate_line_entry(unsigned line) { goto_physline(line); }

void TraceWindow::update_location(UpdateLocationType type)
{
    if (type == UpdateLocationType::NewVis)
        vu.update_logical_node();
    else if (type == UpdateLocationType::NewLog)
        vu.update_visible_node();
    reset_time_entry();
    reset_line_entry();
    reset_pc_entry();
    update_subviews();
    gtk_widget_queue_draw(area);
}

void TraceWindow::opt_toggle_mi()
{
    depth_indentation =
        gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(indent_check));
    syntax_highlight =
        gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(highlight_check));
    if (branch_check)
        substitute_branch_targets =
            gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(branch_check));
    gtk_widget_queue_draw(area);
}

void TraceWindow::recentre_mi() { keep_visnode_in_view(true); }

void TraceWindow::keep_visnode_in_view(bool strict_centre)
{
    auto &vis = vu.curr_visible_node;
    unsigned phystop = vis.trace_file_firstline;
    unsigned physbot = phystop + vis.trace_file_lines;
    double screen_lines = static_cast<double>(curr_area_height) / line_height;

    if (strict_centre) {
        // Try to precisely centre the separator line in the window.
        wintop = vu.physical_to_visible_line(physbot) - screen_lines / 2;
    } else {
        // Force the visible node to be within the screen bounds. We add
        // half a line below it so that the current-position underline is
        // visible.
        wintop = min(wintop, (double)vu.physical_to_visible_line(phystop));
        wintop = max(wintop,
                     vu.physical_to_visible_line(physbot) + 0.5 - screen_lines);
    }

    // Override that by not going off the bottom or top of the
    // document (but the latter takes priority if the document is
    // shorter than a screenful).
    wintop = min(wintop, vu.total_visible_lines() - screen_lines);
    wintop = max(wintop, 0.0);

    set_scrollbar();
}

void TraceWindow::update_subviews()
{
    for (auto &node : subview_list)
        node.update_line(vu.curr_logical_node.memory_root,
                         vu.curr_logical_node.trace_file_firstline);
}

void TraceWindow::tell_subviews_to_diff_against(off_t memroot, unsigned line)
{
    // We have to provide the memory root from the later of the two
    // times, and the line number from the earlier one (because diff
    // lookups are done by looking in the later tree for a list of
    // changes dated after a given line).
    off_t curr_root = vu.curr_logical_node.memory_root;
    unsigned curr_line = vu.curr_logical_node.trace_file_firstline;

    if (curr_line < line)
        line = curr_line;
    else
        memroot = curr_root;

    for (auto &node : subview_list)
        node.diff_against(memroot, line + 1);
}

static void add_pango_fg(PangoAttrList *list, size_t start, size_t end,
                         const GdkRGBA &rgba)
{
    auto *attr = pango_attr_foreground_new(
        0xFFFF * rgba.red, 0xFFFF * rgba.green, 0xFFFF * rgba.blue);
    attr->start_index = start;
    attr->end_index = end;
    pango_attr_list_insert(list, attr);

    if (rgba.alpha != 1) {
        attr = pango_attr_foreground_alpha_new(0xFFFF * rgba.alpha);
        attr->start_index = start;
        attr->end_index = end;
        pango_attr_list_insert(list, attr);
    }
}

static void add_pango_bg(PangoAttrList *list, size_t start, size_t end,
                         const GdkRGBA &rgba)
{
    auto *attr = pango_attr_background_new(
        0xFFFF * rgba.red, 0xFFFF * rgba.green, 0xFFFF * rgba.blue);
    attr->start_index = start;
    attr->end_index = end;
    pango_attr_list_insert(list, attr);

    if (rgba.alpha != 1) {
        attr = pango_attr_background_alpha_new(0xFFFF * rgba.alpha);
        attr->start_index = start;
        attr->end_index = end;
        pango_attr_list_insert(list, attr);
    }
}

static PangoAttrList *pango_attrs_for_hl(const HighlightedLine &line,
                                         bool highlight)
{
    PangoAttrList *list = pango_attr_list_new();

    for (size_t i = 0; i < line.display_len;) {
        HighlightClass hc = line.highlight_at(i, highlight);

        size_t start = i++;
        while (i < line.display_len && line.highlight_at(i, highlight) == hc)
            i++;
        size_t end = i;

        if (hc == HL_DISASSEMBLY && line.iev && !line.iev->executed)
            hc = HL_CCFAIL;

        add_pango_fg(list, start, end, config.colours[hc]);
    }

    return list;
}

static PangoAttrList *pango_attrs_for_rm(const string &t)
{
    PangoAttrList *list = pango_attr_list_new();

    for (size_t i = 0; i < t.size();) {
        char c = t[i];

        size_t start = i++;
        while (i < t.size() && t[i] == c)
            i++;
        size_t end = i;

        if (c >= 'A' && c <= 'Z') {
            c += 'a' - 'A';

            add_pango_bg(list, start, end,
                         config.colours[ColourId::DiffBackground]);
        }

        add_pango_fg(list, start, end, config.colours[c]);
    }

    return list;
}

void TraceWindow::draw_area(unsigned line, unsigned n_lines, double y_start)
{
    SeqOrderPayload node;
    vector<string> node_lines;
    unsigned lineofnode = 0;

    for (double y = y_start; n_lines > 0; line++, n_lines--, y += line_height) {
        if (lineofnode >= node_lines.size()) {
            // If we've run off the end of the previous node, or if
            // this is the first time round the loop, fetch a new node
            // to display.
            if (!vu.get_node_by_visline(line, &node, &lineofnode))
                break;
            node_lines = br.index.get_trace_lines(node);
        }

        unsigned lineofnode_old = lineofnode;

        HighlightedLine hl(node_lines[lineofnode++]);
        if (br.has_image() && substitute_branch_targets)
            hl.replace_instruction(br);

        unsigned display_depth = 0;
        if (depth_indentation)
            display_depth = node.call_depth;

        auto pl = new_layout();
        pango_layout_set_text(pl.get(), hl.text.c_str(), hl.text.size());
        pango_layout_set_attributes(pl.get(),
                                    pango_attrs_for_hl(hl, syntax_highlight));
        LogicalCell cell = {0, node.trace_file_firstline, lineofnode - 1};
        draw_and_store_layout(move(pl), (display_depth + 2) * char_width, y,
                              cell);

        if (lineofnode_old == 0) {
            auto foldstate = vu.node_fold_state(node);
            double x = (display_depth + 0.25) * char_width;
            double hs = char_width / 2; // half side length of square
            double ha = hs * 0.7;       // half arm length of +
            double cx = x + hs;
            double cy = y + baseline / 2;

            if (foldstate != Browser::TraceView::NodeFoldState::Leaf) {
                InWindowControl ctrl = {x, y, char_width, line_height, nullptr};

                // Draw a boxed + or - sign. First the box.
                cairo_new_path(cr);
                cairo_move_to(cr, cx - hs, cy - hs);
                cairo_line_to(cr, cx - hs, cy + hs);
                cairo_line_to(cr, cx + hs, cy + hs);
                cairo_line_to(cr, cx + hs, cy - hs);
                cairo_close_path(cr);

                // Then the horizontal stroke, unconditionally.
                cairo_move_to(cr, cx - ha, cy);
                cairo_line_to(cr, cx + ha, cy);

                // If the function call is folded, draw a vertical
                // stroke to turn the - into a +.
                if (foldstate == Browser::TraceView::NodeFoldState::Folded) {
                    cairo_move_to(cr, cx, cy - ha);
                    cairo_line_to(cr, cx, cy + ha);
                }

                cairo_set_line_width(cr, hs / 8);
                cairo_set_source_gdkrgba(
                    cr,
                    config.colours
                        [foldstate == Browser::TraceView::NodeFoldState::Folded
                             ? ColourId::FoldButton
                             : ColourId::UnfoldButton]);
                cairo_stroke(cr);

                FunctionRange fnrange(vu);
                if (foldstate == Browser::TraceView::NodeFoldState::Unfolded &&
                    fnrange.set_to_container(node)) {
                    ctrl.callback = [this, fnrange]() {
                        fold_ui_action(fnrange, ContainerFoldType::Fold);
                    };
                    controls.push_back(move(ctrl));
                }
                if (foldstate == Browser::TraceView::NodeFoldState::Folded &&
                    fnrange.set_to_callee(node)) {
                    ctrl.callback = [this, fnrange]() {
                        unfold_ui_action(fnrange, false);
                    };
                    controls.push_back(move(ctrl));
                }
            } else {
                // For the start of a node that isn't a function call,
                // draw a bullet-like blob anyway.
                cairo_new_path(cr);
                cairo_arc(cr, cx, cy, ha / 2, 0, 2 * M_PI);
                cairo_set_source_gdkrgba(cr,
                                         config.colours[ColourId::LeafNode]);
                cairo_fill(cr);
            }
        }

        if (lineofnode == node_lines.size() &&
            !node.cmp(vu.curr_visible_node)) {
            cairo_new_path(cr);
            cairo_move_to(cr, 0, y + line_height);
            cairo_rel_line_to(cr, curr_area_width, 0);
            cairo_set_line_width(cr, 2 * underline_thickness);
            cairo_set_source_gdkrgba(cr,
                                     config.colours[ColourId::PositionMarker]);
            cairo_stroke(cr);
        }
    }
}

void TraceWindow::imc_commit(string str) {}

bool TraceWindow::function_key(GdkEventKey *event)
{
    switch (event->keyval) {
    case GDK_KEY_Up:
    case GDK_KEY_KP_Up: {
        off_t prev_memroot = vu.curr_logical_node.memory_root;
        unsigned prev_line = vu.curr_logical_node.trace_file_firstline;
        if (vu.prev_visible_node(&vu.curr_visible_node)) {
            update_location(UpdateLocationType::NewVis);
            tell_subviews_to_diff_against(prev_memroot, prev_line);
            keep_visnode_in_view();
        }
        return true;
    }
    case GDK_KEY_Down:
    case GDK_KEY_KP_Down: {
        off_t prev_memroot = vu.curr_logical_node.memory_root;
        unsigned prev_line = vu.curr_logical_node.trace_file_firstline;
        if (vu.next_visible_node(&vu.curr_visible_node)) {
            update_location(UpdateLocationType::NewVis);
            tell_subviews_to_diff_against(prev_memroot, prev_line);
            keep_visnode_in_view();
        }
        return true;
    }
    case GDK_KEY_Page_Up:
    case GDK_KEY_KP_Page_Up:
        if (vu.goto_visline(vu.physical_to_visible_line(
                                vu.curr_visible_node.trace_file_firstline) -
                            curr_area_height / line_height) ||
            vu.goto_buffer_limit(false)) {
            update_location(UpdateLocationType::NewVis);
            keep_visnode_in_view();
        }
        return true;
    case GDK_KEY_Page_Down:
    case GDK_KEY_KP_Page_Down:
        if (vu.goto_visline(vu.physical_to_visible_line(
                                vu.curr_visible_node.trace_file_firstline +
                                vu.curr_visible_node.trace_file_lines - 1) +
                            curr_area_height / line_height) ||
            vu.goto_buffer_limit(true)) {
            update_location(UpdateLocationType::NewVis);
            keep_visnode_in_view();
        }
        return true;
    case GDK_KEY_Home:
    case GDK_KEY_KP_Home:
        if (vu.goto_buffer_limit(false)) {
            update_location(UpdateLocationType::NewVis);
            keep_visnode_in_view();
        }
        return true;
    case GDK_KEY_End:
    case GDK_KEY_KP_End:
        if (vu.goto_buffer_limit(true)) {
            update_location(UpdateLocationType::NewVis);
            keep_visnode_in_view();
        }
        return true;
    }
    return false;
}

void TraceWindow::click(const LogicalPos &logpos, GdkEventButton *event)
{
    // A left-click updates the current position. We're expecting
    // a click on a line _between_ nodes, so round differently
    // from the way we would in other situations.

    double click_pos = wintop + (double)event->y / line_height;
    unsigned click_line = click_pos;
    SeqOrderPayload node;
    unsigned nodeline_i;
    if (!vu.get_node_by_visline(click_line, &node, &nodeline_i))
        return;
    double nodeline_f = nodeline_i + (click_pos - click_line);
    if (nodeline_f * 2 < node.trace_file_lines) {
        // Deliberately ignore failure, which will give us the
        // special case that you can't put the underline above the
        // very first node in the file.
        vu.prev_visible_node(node, &node);
    }
    vu.curr_visible_node = node;
    update_location(UpdateLocationType::NewVis);
}

void TraceWindow::goto_time(Time t)
{
    if (vu.goto_time(t)) {
        update_location(UpdateLocationType::NewVis);
        keep_visnode_in_view();
    }
}

void TraceWindow::goto_physline(unsigned line)
{
    if (vu.goto_physline(line)) {
        update_location(UpdateLocationType::NewVis);
        keep_visnode_in_view();
    }
}

bool TraceWindow::prepare_context_menu(const LogicalPos &logpos)
{
    // Find out as much as we can about the context in question.
    SeqOrderPayload node;
    if (vu.br.get_node_by_physline(logpos.cell.y0, &node, nullptr)) {
        if (container_fnrange.set_to_container(node)) {
            for (auto *widget : containing_fn_menuitems)
                gtk_widget_show(widget);
            ostringstream oss;
            oss << "Containing call (lines "
                << container_fnrange.callnode.trace_file_firstline << "–"
                << container_fnrange.lastnode.trace_file_firstline << " to "
                << br.get_symbolic_address(container_fnrange.firstnode.pc, true)
                << ")";
            gtk_label_set_text(GTK_LABEL(containing_fn_menulabel),
                               oss.str().c_str());
        } else {
            for (auto *widget : containing_fn_menuitems)
                gtk_widget_hide(widget);
        }

        if (callee_fnrange.set_to_callee(node)) {
            for (auto *widget : called_fn_menuitems)
                gtk_widget_show(widget);
            ostringstream oss;
            oss << "Folded call (lines "
                << callee_fnrange.callnode.trace_file_firstline << "–"
                << callee_fnrange.lastnode.trace_file_firstline << " to "
                << br.get_symbolic_address(callee_fnrange.firstnode.pc, true)
                << ")";
            gtk_label_set_text(GTK_LABEL(called_fn_menulabel),
                               oss.str().c_str());
        } else {
            for (auto *widget : called_fn_menuitems)
                gtk_widget_hide(widget);
        }

        context_menu_memroot = node.memory_root;
        DecodedTraceLine dtl(br.index.isBigEndian(),
                             br.index.get_trace_line(node, logpos.cell.y1));
        if (dtl.mev) {
            for (auto *widget : memaccess_menuitems)
                gtk_widget_show(widget);
            gtk_widget_show(context_memwindow_menuitem);
            ostringstream oss;
            oss << "Memory access: " << dtl.mev->size << " bytes at 0x" << hex
                << dtl.mev->addr;
            gtk_label_set_text(GTK_LABEL(memaccess_menulabel),
                               oss.str().c_str());
            context_menu_memtype = 'm';
            context_menu_start = dtl.mev->addr;
            context_menu_size = dtl.mev->size;
        } else if (dtl.rev) {
            for (auto *widget : memaccess_menuitems)
                gtk_widget_show(widget);
            gtk_widget_hide(context_memwindow_menuitem);
            ostringstream oss;
            oss << "Register access: " << reg_name(dtl.rev->reg);
            gtk_label_set_text(GTK_LABEL(memaccess_menulabel),
                               oss.str().c_str());
            context_menu_memtype = 'r';
            unsigned iflags = br.get_iflags(context_menu_memroot);
            context_menu_start = reg_offset(dtl.rev->reg, iflags);
            context_menu_size = reg_size(dtl.rev->reg);
        } else {
            context_menu_memtype = '\0';
            for (auto *widget : memaccess_menuitems)
                gtk_widget_hide(widget);
            gtk_widget_hide(context_memwindow_menuitem);
        }
    }
    return true;
}

void TraceWindow::provenance_mi()
{
    if (!context_menu_memtype)
        return; // just in case

    unsigned line =
        br.getmem(context_menu_memroot, context_menu_memtype,
                  context_menu_start, context_menu_size, nullptr, nullptr);
    if (line)
        goto_physline(line);
}

void TraceWindow::context_memwindow_mi()
{
    if (context_menu_memtype != 'm')
        return; // just in case

    Addr addr = context_menu_start;
    addr ^= (addr & 15);
    addr -= 16 * 8; // centre the address in the new memory view
    add_subview(new MemoryWindow(addr, 16, br.index.isAArch64(), br, this));
    update_subviews();
}

unsigned TraceWindow::n_display_lines() { return vu.total_visible_lines(); }

void TraceWindow::fold_ui_action(const FunctionRange &fnrange,
                                 ContainerFoldType type)
{
    if (!fnrange.initialised)
        return;

    unsigned depth;
    switch (type) {
    case ContainerFoldType::Fold:
        depth = fnrange.depth;
        break;
    case ContainerFoldType::FoldSubrs:
        depth = fnrange.depth + 1;
        break;
    case ContainerFoldType::Unfold:
        depth = UINT_MAX;
        break;
    }

    for (FoldChangeWrapper fcw(this); fcw.progress();)
        vu.set_fold_state(fnrange.firstline, fnrange.lastline, 0, depth);
}

void TraceWindow::unfold_ui_action(const FunctionRange &fnrange, bool full)
{
    if (!fnrange.initialised)
        return;

    unsigned depth = full ? UINT_MAX : fnrange.depth;

    for (FoldChangeWrapper fcw(this); fcw.progress();)
        vu.set_fold_state(fnrange.firstline, fnrange.lastline, 0, depth);
}

void TraceWindow::fold_all_ui_action(bool fold)
{
    SeqOrderPayload last;
    if (!vu.br.find_buffer_limit(true, &last))
        return;

    unsigned depth = fold ? 1 : UINT_MAX;

    for (FoldChangeWrapper fcw(this); fcw.progress();)
        vu.set_fold_state(
            1, last.trace_file_firstline + last.trace_file_lines - 1, 0, depth);
}

bool TraceWindow::prompt_dialog_prepare()
{
    if (prompt_dialog)
        return false;
    for (auto menuitem : prompt_dialog_menu_items)
        gtk_widget_set_sensitive(menuitem, false);
    return true;
}

void TraceWindow::prompt_dialog_setup()
{
    signal_connect(prompt_dialog, "response", &TraceWindow::prompt_response,
                   prompt_dialog_signal_handlers);
    gtk_widget_show_all(prompt_dialog);
}

void TraceWindow::prompt_response(gint response_id)
{
    if (response_id == GTK_RESPONSE_OK) {
        assert(prompt_dialog_entry);
        auto text = gtk_entry_get_text(GTK_ENTRY(prompt_dialog_entry));

        switch (prompt_dialog_type) {
        case PromptDialogType::Memory: {
            Addr addr;
            try {
                addr = vu.evaluate_expression_addr(text);
            } catch (invalid_argument) {
                goto done;
            }
            add_subview(
                new MemoryWindow(addr, 16, br.index.isAArch64(), br, this));
            update_subviews();
            break;
        }
        }
    }

done:
    prompt_dialog_teardown();
}

void TraceWindow::prompt_dialog_teardown()
{
    if (!prompt_dialog)
        return;

    for (auto sig : prompt_dialog_signal_handlers)
        g_signal_handler_disconnect(sig.first, sig.second);
    prompt_dialog_signal_handlers.clear();
    for (auto menuitem : prompt_dialog_menu_items)
        gtk_widget_set_sensitive(menuitem, true);
    gtk_widget_destroy(prompt_dialog);
    prompt_dialog = nullptr;
}

void TraceWindow::trace_view_mi() { new TraceWindow(br); }

void TraceWindow::mem_view_mi()
{
    if (!prompt_dialog_prepare())
        return;

    prompt_dialog = gtk_dialog_new_with_buttons(
        "Open memory window", GTK_WINDOW(window),
        GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL, "_OK",
        GTK_RESPONSE_OK, (const char *)nullptr);
    auto content = gtk_dialog_get_content_area(GTK_DIALOG(prompt_dialog));
    auto label = gtk_label_new("Enter memory address to display");
    gtk_container_add(GTK_CONTAINER(content), label);
    prompt_dialog_entry = gtk_entry_new();
    prompt_dialog_type = PromptDialogType::Memory;
    gtk_container_add(GTK_CONTAINER(content), prompt_dialog_entry);
    gtk_dialog_set_default_response(GTK_DIALOG(prompt_dialog), GTK_RESPONSE_OK);
    gtk_entry_set_activates_default(GTK_ENTRY(prompt_dialog_entry), true);

    prompt_dialog_setup();
}

void TraceWindow::corereg_view_mi()
{
    if (br.index.isAArch64())
        add_subview(new RegisterWindow64(br, this));
    else
        add_subview(new RegisterWindow32(br, this));
}

void TraceWindow::spreg_view_mi()
{
    add_subview(new RegisterWindowSP(br, this));
}

void TraceWindow::dpreg_view_mi()
{
    add_subview(new RegisterWindowDP(br, this));
}

void TraceWindow::neonreg_view_mi()
{
    add_subview(new RegisterWindowNeon(br, this, br.index.isAArch64()));
}

void TraceWindow::mvereg_view_mi()
{
    add_subview(new RegisterWindowMVE(br, this));
}

void TraceWindow::clipboard_get_paste_data(ostream &os, LogicalPos start,
                                           LogicalPos end)
{
    SeqOrderPayload node;
    vector<string> node_lines;
    LogicalPos pos = start;

    if (!br.get_node_by_physline(pos.cell.y0, &node, nullptr))
        return;
    node_lines = br.index.get_trace_lines(node);

    for (; logpos_cmp(pos, end) <= 0; pos.cell.y1++) {
        if (pos.cell.y1 >= node_lines.size()) {
            if (!vu.next_visible_node(node, &node))
                return;
            node_lines = br.index.get_trace_lines(node);
            pos.cell.y0 = node.trace_file_firstline;
            pos.cell.y1 = 0;
        }

        string &s = node_lines[pos.cell.y1];

        if (pos.cell == end.cell) {
            s = s.substr(0, min((size_t)end.char_index + 1, s.size()));
        } else {
            s += "\n";
        }

        if (pos.cell == start.cell) {
            s = s.substr(min(s.size(), (size_t)start.char_index));
        }

        os << s;
    }
}

pair<unsigned, unsigned>
RegisterWindow::compute_size(const vector<RegisterId> &regs)
{
    const unsigned gutter = 4, minwid = 60, maxwid = 80;

    max_name_len = 0;
    for (const RegisterId &r : regs)
        max_name_len = max(reg_name(r).size(), max_name_len);

    size_t maxrlen = 0;
    for (auto reg : regs)
        maxrlen = max(maxrlen, format_reg_length(reg));
    maxrlen += max_name_len + 1;

    colwid = maxrlen + gutter;
    cols = (maxwid + gutter) / colwid;
    rows = (regs.size() + cols - 1) / cols;

    return pair<unsigned, unsigned>(max(cols * colwid - gutter, minwid), rows);
}

RegisterWindow::RegisterWindow(const vector<RegisterId> &regs, Browser &br,
                               TraceWindow *tw)
    : SubsidiaryView(compute_size(regs), true, br, tw), regs(regs)
{
    reglabel = gtk_label_new("");
    menuitems.push_back(add_menu_heading(context_menu, reglabel));
    menuitems.push_back(add_menu_item(context_menu,
                                      "Go to last write to this register",
                                      &RegisterWindow::register_provenance_mi));
}

bool RegisterWindow::prepare_context_menu(const LogicalPos &logpos)
{
    unsigned reg_index = logpos.cell.y0 + logpos.char_index / colwid * rows;
    if (reg_index >= regs.size())
        return false;

    context_menu_reg = regs[reg_index];
    {
        ostringstream oss;
        oss << "Register " << reg_name(context_menu_reg);
        gtk_label_set_text(GTK_LABEL(reglabel), oss.str().c_str());
    }
    for (auto *widget : menuitems)
        gtk_widget_show(widget);

    return true;
}

void RegisterWindow::provenance_query(RegisterId r)
{
    if (!tw)
        return;
    unsigned iflags = br.get_iflags(memroot);
    Addr roffset = reg_offset(r, iflags);
    size_t rsize = reg_size(r);
    unsigned line = br.getmem(memroot, 'r', roffset, rsize, nullptr, nullptr);
    if (line)
        tw->goto_physline(line);
}

void RegisterWindow::register_provenance_mi()
{
    provenance_query(context_menu_reg);
}

void RegisterWindow::draw_area(unsigned line, unsigned n_lines, double y_start)
{
    for (double y = y_start; n_lines > 0 && line < rows;
         line++, n_lines--, y += line_height) {
        for (unsigned col = 0; line + col * rows < regs.size(); col++) {
            unsigned regindex = line + col * rows;
            const RegisterId &r = regs[regindex];

            string dispstr, disptype;
            br.format_reg(dispstr, disptype, r, memroot, diff_memroot,
                          diff_minline);
            size_t valstart = dispstr.size() - format_reg_length(r);
            size_t xoffset = max_name_len + 1 - valstart;

            auto pl = new_layout();
            pango_layout_set_text(pl.get(), dispstr.c_str(), dispstr.size());
            pango_layout_set_attributes(pl.get(), pango_attrs_for_rm(disptype));
            draw_and_store_layout(move(pl),
                                  (colwid * col + xoffset) * char_width, y,
                                  LogicalCell{col, line, regindex});
        }
    }
}

void RegisterWindow::clipboard_get_paste_data(ostream &os, LogicalPos start,
                                              LogicalPos end)
{
    for (unsigned i = start.cell.y1; i <= end.cell.y1; i++) {
        const RegisterId &r = regs[i];

        string s, type;
        br.format_reg(s, type, r, clipboard_memroot);

        if (i == end.cell.y1) {
            s = s.substr(0, min((size_t)end.char_index + 1, s.size()));
        } else {
            s += "\n";
        }

        if (i == start.cell.y1) {
            s = s.substr(min(s.size(), (size_t)start.char_index));
        }

        os << s;
    }
}

static vector<RegisterId> core_regs_32()
{
    vector<RegisterId> regs;
    for (unsigned i = 0; i < 15; i++)
        regs.push_back(RegisterId{RegPrefix::r, i});
    regs.push_back(RegisterId{RegPrefix::psr, 0});
    return regs;
}

RegisterWindow32::RegisterWindow32(Browser &br, TraceWindow *tw)
    : RegisterWindow(core_regs_32(), br, tw)
{
    set_title("Core regs");
}

static vector<RegisterId> core_regs_64()
{
    vector<RegisterId> regs;
    for (unsigned i = 0; i < 31; i++)
        regs.push_back(RegisterId{RegPrefix::x, i});
    regs.push_back(RegisterId{RegPrefix::xsp, 0});
    regs.push_back(RegisterId{RegPrefix::psr, 0});
    return regs;
}

RegisterWindow64::RegisterWindow64(Browser &br, TraceWindow *tw)
    : RegisterWindow(core_regs_64(), br, tw)
{
    set_title("Core regs");
}

static vector<RegisterId> float_regs_sp()
{
    vector<RegisterId> regs;
    for (unsigned i = 0; i < 32; i++)
        regs.push_back(RegisterId{RegPrefix::s, i});
    return regs;
}

RegisterWindowSP::RegisterWindowSP(Browser &br, TraceWindow *tw)
    : RegisterWindow(float_regs_sp(), br, tw)
{
    set_title("FP regs (single precision)");
}

static vector<RegisterId> float_regs_dp()
{
    vector<RegisterId> regs;
    for (unsigned i = 0; i < 32; i++)
        regs.push_back(RegisterId{RegPrefix::d, i});
    return regs;
}

RegisterWindowDP::RegisterWindowDP(Browser &br, TraceWindow *tw)
    : RegisterWindow(float_regs_dp(), br, tw)
{
    set_title("FP regs (double precision)");
}

static vector<RegisterId> vector_regs_neon(unsigned limit)
{
    vector<RegisterId> regs;
    for (unsigned i = 0; i < limit; i++)
        regs.push_back(RegisterId{RegPrefix::q, i});
    return regs;
}

RegisterWindowNeon::RegisterWindowNeon(Browser &br, TraceWindow *tw,
                                       bool aarch64)
    : RegisterWindow(vector_regs_neon(aarch64 ? 32 : 16), br, tw)
{
    set_title("Vector regs (Neon)");
}

static vector<RegisterId> vector_regs_mve()
{
    vector<RegisterId> regs;
    for (unsigned i = 0; i < 8; i++)
        regs.push_back(RegisterId{RegPrefix::q, i});
    regs.push_back(RegisterId{RegPrefix::vpr, 0});
    return regs;
}

RegisterWindowMVE::RegisterWindowMVE(Browser &br, TraceWindow *tw)
    : RegisterWindow(vector_regs_mve(), br, tw)
{
    set_title("Vector regs (MVE)");
}

pair<unsigned, unsigned> MemoryWindow::compute_size(int bpl, bool sfb)
{
    unsigned h = 16;
    unsigned w = ((sfb ? 16 : 8) + // address
                  2 +              // 2 spaces between address and hex
                  3 * bpl - 1 +    // 2 hex digits/byte + spaces between
                  2 +              // 2 spaces between hex and ASCII
                  bpl);            // 1 ASCII char/byte
    return pair<unsigned, unsigned>(w, h);
}

MemoryWindow::MemoryWindow(Addr addr, int bpl, bool sfb, Browser &br,
                           TraceWindow *tw)
    : SubsidiaryView(compute_size(bpl, sfb), false, br, tw),
      addr_chars(sfb ? 16 : 8), bytes_per_line(bpl)
{
    set_title("Memory");

    start_addr = addr % bytes_per_line;
    wintop = addr / bytes_per_line;

    memlabel = gtk_label_new("");
    menuitems.push_back(add_menu_heading(context_menu, memlabel));
    menuitems.push_back(add_menu_item(context_menu,
                                      "Go to last write to this region",
                                      &MemoryWindow::provenance_mi));

    addr_entry = new_toolbar_entry(toolbar, "Address:");
    gtk_entry_set_width_chars(GTK_ENTRY(addr_entry), 16);
    signal_connect(addr_entry, "activate", &MemoryWindow::addr_entry_activated);
    signal_connect(addr_entry, "focus-out-event",
                   &MemoryWindow::addr_entry_unfocused);

    reset_addr_entry();
}

unsigned MemoryWindow::n_display_lines() { return 16; }

void MemoryWindow::draw_area(unsigned line, unsigned n_lines, double y_start)
{
    Addr addr = start_addr + line * bytes_per_line;

    for (double y = y_start; n_lines > 0;
         line++, n_lines--, y += line_height, addr += bytes_per_line) {
        string dispaddr, typeaddr, disphex, typehex, dispchars, typechars;

        br.format_memory_split(dispaddr, typeaddr, disphex, typehex, dispchars,
                               typechars, addr, bytes_per_line, addr_chars,
                               memroot, diff_memroot, diff_minline);

        auto pl = new_layout();
        pango_layout_set_text(pl.get(), dispaddr.c_str(), dispaddr.size());
        pango_layout_set_attributes(pl.get(), pango_attrs_for_rm(typeaddr));
        draw_and_store_layout(move(pl), 0, y, LogicalCell{0, addr, 0});

        pl = new_layout();
        pango_layout_set_text(pl.get(), disphex.c_str(), disphex.size());
        pango_layout_set_attributes(pl.get(), pango_attrs_for_rm(typehex));
        draw_and_store_layout(move(pl), char_width * (addr_chars + 2), y,
                              LogicalCell{1, addr, 0});

        pl = new_layout();
        pango_layout_set_text(pl.get(), dispchars.c_str(), dispchars.size());
        pango_layout_set_attributes(pl.get(), pango_attrs_for_rm(typechars));
        draw_and_store_layout(
            move(pl), char_width * (addr_chars + bytes_per_line * 3 + 3), y,
            LogicalCell{2, addr, 0});
    }
}

bool MemoryWindow::scroll_area(GdkEventScroll *event)
{
    gdouble dx, dy;
    if (!gdk_event_get_scroll_deltas((GdkEvent *)event, &dx, &dy))
        dy = event->delta_y;

    wintop += dy;
    reset_addr_entry();
    gtk_widget_queue_draw(area);

    return true;
}

Addr MemoryWindow::addr_from_valid_logpos(const LogicalPos &logpos)
{
    Addr line_addr = logpos.cell.y0;

    switch (logpos.cell.column) {
    case 1: // hex display
        return line_addr + logpos.char_index / 3;
    case 2: // character display
        return line_addr + logpos.char_index;
    default:
        assert(false && "Invalid logpos in addr_from_valid_logpos");
    }
}

bool MemoryWindow::get_region_under_pos(const LogicalPos &logpos, Addr &start,
                                        Addr &size)
{
    if (selection_state == SelectionState::Selected &&
        logpos_cmp(logpos, selection_start) >= 0 &&
        logpos_cmp(logpos, selection_end) <= 0) {
        // The mouse is in the selected region, so return that whole region.
        start = addr_from_valid_logpos(selection_start);
        size = addr_from_valid_logpos(selection_end) - start + 1;
        return true;
    }

    // Find a naturally aligned word of up to 8 bytes identified by this
    // position.
    Addr line_addr = logpos.cell.y0;
    Addr addr = 0;
    unsigned wordsize = 0;
    unsigned mult;

    switch (logpos.cell.column) {
    case 1: // hex display
        mult = 3;
        addr = line_addr + logpos.char_index / 3;
        if (logpos.char_index % 3 != 2) {
            // The mouse is directly over a hex byte. Highlight just that byte.
            wordsize = 1;
        } else {
            // The mouse is on the space between addr and addr+1. Determine the
            // size of highlight by looking at the number of factors of two in
            // the byte to our right (addr+1).
            addr++;
            unsigned lowbit = addr & -addr;
            if (lowbit <= 4) {
                wordsize = lowbit * 2;
                addr &= ~(Addr)(wordsize - 1);
            }
        }
        break;
    case 2: // character display
        mult = 1;
        addr = line_addr + logpos.char_index;
        wordsize = 1;
        break;
    }

    if (wordsize == 0)
        return false;

    start = addr;
    size = wordsize;
    return true;
}

void MemoryWindow::update_mouseover(const LogicalPos &logpos)
{
    Addr start, size;
    if (get_region_under_pos(logpos, start, size)) {
        assert(logpos.cell.column == 1 || logpos.cell.column == 2);
        unsigned mult = logpos.cell.column == 1 ? 3 : 1;
        unsigned end = logpos.cell.column == 1 ? 1 : 0;

        mouseover_start = mouseover_end = logpos;
        mouseover_start.char_index = (start - logpos.cell.y0) * mult;
        mouseover_end.char_index =
            mouseover_start.char_index + (size - 1) * mult + end;

        mouseover_valid = true;
    } else {
        mouseover_valid = false;
    }
}

bool MemoryWindow::prepare_context_menu(const LogicalPos &logpos)
{
    Addr start, size;
    if (get_region_under_pos(logpos, start, size)) {
        context_menu_addr = start;
        context_menu_size = size;
        ostringstream oss;
        oss << size << "-byte region at address 0x" << hex << start;
        gtk_label_set_text(GTK_LABEL(memlabel), oss.str().c_str());
    }
    for (auto *widget : menuitems)
        gtk_widget_show(widget);

    return true;
}

void MemoryWindow::provenance_query(Addr start, Addr size)
{
    if (!tw)
        return;
    unsigned line = br.getmem(memroot, 'm', start, size, nullptr, nullptr);
    if (line)
        tw->goto_physline(line);
}

bool MemoryWindow::function_key(GdkEventKey *event)
{
    switch (event->keyval) {
    case GDK_KEY_Up:
    case GDK_KEY_KP_Up:
        wintop -= 1;
        reset_addr_entry();
        gtk_widget_queue_draw(area);
        return true;
    case GDK_KEY_Down:
    case GDK_KEY_KP_Down:
        wintop += 1;
        reset_addr_entry();
        gtk_widget_queue_draw(area);
        return true;
    case GDK_KEY_Page_Up:
    case GDK_KEY_KP_Page_Up:
        wintop -= curr_area_height / line_height;
        reset_addr_entry();
        gtk_widget_queue_draw(area);
        return true;
    case GDK_KEY_Page_Down:
    case GDK_KEY_KP_Page_Down:
        wintop += curr_area_height / line_height;
        reset_addr_entry();
        gtk_widget_queue_draw(area);
        return true;
    }
    return false;
}

void MemoryWindow::reset_addr_entry()
{
    Addr addr = start_addr + ceil(wintop) * bytes_per_line;
    ostringstream oss;
    oss << "0x" << hex << addr;
    gtk_entry_set_text(GTK_ENTRY(addr_entry), oss.str().c_str());
}

void MemoryWindow::addr_entry_activated()
{
    string value = gtk_entry_get_text(GTK_ENTRY(addr_entry));
    Addr addr;

    try {
        if (tw)
            addr = tw->vu.evaluate_expression_addr(value);
        else
            addr = br.evaluate_expression_addr(value);
    } catch (invalid_argument) {
        return;
    }

    wintop = ((addr - start_addr) / bytes_per_line -
              curr_area_height / line_height / 2);
    reset_addr_entry();
    gtk_widget_queue_draw(area);
    gtk_widget_grab_focus(area);
}

gboolean MemoryWindow::addr_entry_unfocused(GdkEvent *)
{
    reset_addr_entry();
    return false;
}

inline unsigned MemoryWindow::byte_index(const LogicalPos &pos)
{
    switch (pos.cell.column) {
    case 1:
        return pos.char_index / 3;
    case 2:
        return pos.char_index;
    default: // treat address column as on the left edge
        return 0;
    }
}

void MemoryWindow::rewrite_selection_endpoints(LogicalPos &anchor,
                                               LogicalPos &cursor)
{
    if (anchor.cell.column == 0) {
        // Permit selecting just one address at a time
        if (cursor.cell.column != 0) {
            cursor.cell.column = 0;
            cursor.char_index = UINT_MAX;
        }
        if (cursor.cell.y0 > anchor.cell.y0) {
            cursor.cell.y0 = anchor.cell.y0;
            cursor.char_index = UINT_MAX;
        }
        if (cursor.cell.y0 < anchor.cell.y0) {
            cursor.cell.y0 = anchor.cell.y0;
            cursor.char_index = 0;
        }
        return;
    }

    if (cursor.cell.column != anchor.cell.column) {
        unsigned byte = byte_index(cursor);
        cursor.cell.column = anchor.cell.column;
        cursor.char_index = (cursor.cell.column == 1 ? 3 : 1) * byte;
        if (cursor.cell.column == 1 && logpos_cmp(cursor, anchor) > 0)
            cursor.char_index++; // select 2nd character of end byte
    }
}

void MemoryWindow::clipboard_get_paste_data(ostream &os, LogicalPos start,
                                            LogicalPos end)
{
    unsigned col = start.cell.column;
    assert(col < 3);

    for (Addr addr = start.cell.y0; addr <= end.cell.y0;
         addr += bytes_per_line) {
        string disp[3], type[3];

        br.format_memory_split(disp[0], type[0], disp[1], type[1], disp[2],
                               type[2], addr, bytes_per_line, addr_chars,
                               memroot);

        string &s = disp[col];

        if (addr == end.cell.y0) {
            s = s.substr(0, min((size_t)end.char_index + 1, s.size()));
        } else {
            s += "\n";
        }

        if (addr == start.cell.y0) {
            s = s.substr(min(s.size(), (size_t)start.char_index));
        }

        os << s;
    }
}

void run_browser(Browser &br)
{
    config.read();
    call_gtk_init();
    new TraceWindow(br);
    gtk_main();
}
