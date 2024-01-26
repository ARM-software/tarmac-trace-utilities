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

#include "browse.hh"
#include "libtarmac/argparse.hh"
#include "libtarmac/disktree.hh"
#include "libtarmac/expr.hh"
#include "libtarmac/image.hh"
#include "libtarmac/index_ds.hh"
#include "libtarmac/intl.hh"
#include "libtarmac/memtree.hh"
#include "libtarmac/misc.hh"
#include "libtarmac/parser.hh"
#include "libtarmac/registers.hh"
#include "libtarmac/reporter.hh"
#include "libtarmac/tarmacutil.hh"

#include <climits>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef WIN32
#include <curses.h>
#else
#include <ncurses.h>
#endif

using std::invalid_argument;
using std::max;
using std::min;
using std::ostringstream;
using std::string;
using std::vector;

// Arguments for each attribute type:
//  - name, e.g. ATTR_foo
//  - base attributes (e.g. A_NORMAL or A_BOLD) in no-colour mode
//  - base attributes in 8-colour mode
//  - fg,bg in 8-colour mode
//  - base attributes in 256-colour mode
//  - fg,bg in 256-colour mode
#define ATTRLIST(X)                                                            \
    X(STATUSLINE, A_REVERSE, A_BOLD, 7, 4, A_BOLD, 7, 4)                       \
    X(MINIBUF, A_NORMAL, A_NORMAL, 7, 0, A_NORMAL, 7, 0)                       \
    X(MINIBUF_ERROR, A_NORMAL, A_NORMAL, 3, 1, A_NORMAL, 3, 1)                 \
    /* Each TRACEFOO_SEL should directly follow its associated TRACEFOO */     \
    X(TRACETEXT, A_NORMAL, A_NORMAL, 7, 0, A_NORMAL, 7, 0)                     \
    X(TRACETEXT_SEL, A_NORMAL, A_NORMAL, 7, 4, A_NORMAL, 7, 4)                 \
    X(TRACETIME, A_NORMAL, A_NORMAL, 2, 0, A_NORMAL, 2, 0)                     \
    X(TRACETIME_SEL, A_NORMAL, A_NORMAL, 2, 4, A_NORMAL, 2, 4)                 \
    X(TRACEEVENT, A_BOLD, A_BOLD, 7, 0, A_BOLD, 7, 0)                          \
    X(TRACEEVENT_SEL, A_BOLD, A_BOLD, 7, 4, A_BOLD, 7, 4)                      \
    X(TRACEPC, A_BOLD, A_BOLD, 6, 0, A_BOLD, 6, 0)                             \
    X(TRACEPC_SEL, A_BOLD, A_BOLD, 6, 4, A_BOLD, 6, 4)                         \
    X(TRACEMODE, A_NORMAL, A_NORMAL, 6, 0, A_NORMAL, 6, 0)                     \
    X(TRACEMODE_SEL, A_NORMAL, A_NORMAL, 6, 4, A_NORMAL, 6, 4)                 \
    X(TRACEINSN, A_BOLD, A_BOLD, 5, 0, A_BOLD, 5, 0)                           \
    X(TRACEINSN_SEL, A_BOLD, A_BOLD, 5, 4, A_BOLD, 5, 4)                       \
    X(TRACEISET, A_NORMAL, A_NORMAL, 5, 0, A_NORMAL, 5, 0)                     \
    X(TRACEISET_SEL, A_NORMAL, A_NORMAL, 5, 4, A_NORMAL, 5, 4)                 \
    X(TRACEDISASS, A_BOLD, A_BOLD, 2, 0, A_BOLD, 2, 0)                         \
    X(TRACEDISASS_SEL, A_BOLD, A_BOLD, 2, 4, A_BOLD, 2, 4)                     \
    X(TRACESKIP, A_NORMAL, A_NORMAL, 1, 0, A_NORMAL, 1, 0)                     \
    X(TRACESKIP_SEL, A_NORMAL, A_NORMAL, 1, 4, A_NORMAL, 1, 4)                 \
    X(TRACEPUNCT, A_NORMAL, A_NORMAL, 3, 0, A_NORMAL, 3, 0)                    \
    X(TRACEPUNCT_SEL, A_NORMAL, A_NORMAL, 3, 4, A_NORMAL, 3, 4)                \
    X(TRACEERR, A_BOLD, A_BOLD, 3, 1, A_BOLD, 3, 1)                            \
    X(TRACEERR_SEL, A_BOLD, A_BOLD, 3, 1, A_BOLD, 3, 1)                        \
    X(REGDISPLAY_NAME, A_NORMAL, A_NORMAL, 6, 0, A_NORMAL, 6, 0)               \
    X(REGDISPLAY_FIXED, A_NORMAL, A_NORMAL, 6, 0, A_NORMAL, 6, 0)              \
    X(REGDISPLAY_VALUE, A_NORMAL, A_NORMAL, 7, 0, A_NORMAL, 7, 0)              \
    X(REGDISPLAY_UNKNOWN, A_NORMAL, A_NORMAL, 1, 0, A_NORMAL, 1, 0)            \
    X(REGDISPLAY_VALUE_DIFF, A_NORMAL, A_NORMAL, 7, 4, A_NORMAL, 7, 4)         \
    X(REGDISPLAY_UNKNOWN_DIFF, A_NORMAL, A_NORMAL, 1, 4, A_NORMAL, 1, 4)       \
    X(MEMDISPLAY_FIXED, A_NORMAL, A_NORMAL, 6, 0, A_NORMAL, 6, 0)              \
    X(MEMDISPLAY_VALUE, A_NORMAL, A_NORMAL, 7, 0, A_NORMAL, 7, 0)              \
    X(MEMDISPLAY_CTRLCHAR, A_NORMAL, A_NORMAL, 2, 0, A_NORMAL, 2, 0)           \
    X(MEMDISPLAY_UNKNOWN, A_NORMAL, A_NORMAL, 1, 0, A_NORMAL, 1, 0)            \
    X(MEMDISPLAY_VALUE_DIFF, A_NORMAL, A_NORMAL, 7, 4, A_NORMAL, 7, 4)         \
    X(MEMDISPLAY_CTRLCHAR_DIFF, A_NORMAL, A_NORMAL, 2, 4, A_NORMAL, 2, 4)      \
    X(MEMDISPLAY_UNKNOWN_DIFF, A_NORMAL, A_NORMAL, 1, 4, A_NORMAL, 1, 4)       \
    X(HELP_KEY, A_BOLD, A_BOLD, 2, 0, A_BOLD, 2, 0)                            \
    X(HELP_DESCRIPTION, A_NORMAL, A_NORMAL, 7, 0, A_NORMAL, 7, 0)              \
    X(HELP_SCROLL_INDICATOR, A_NORMAL, A_NORMAL, 6, 0, A_NORMAL, 6, 0)         \
    /* end of list */

#define ATTR_COLOUR_PAIR_ENUM(name, base, base8, fg8, bg8, base256, fg256,     \
                              bg256)                                           \
    CP_##name,
enum { CP_unused, ATTRLIST(ATTR_COLOUR_PAIR_ENUM) CP_dummy };
#undef ATTR_COLOUR_PAIR_ENUM

#define ATTR_BASE_ENUM(name, base, base8, fg8, bg8, base256, fg256, bg256)     \
    BASE_##name = base, BASE8_##name = base8, BASE256_##name = base256,
enum { ATTRLIST(ATTR_BASE_ENUM) BASE_dummy };
#undef ATTR_BASE_ENUM

#define ATTR_COMPLETE_ARRAY(name, base, base8, fg8, bg8, base256, fg256,       \
                            bg256)                                             \
    base, (base8) | COLOR_PAIR(CP_##name), (base256) | COLOR_PAIR(CP_##name),
static int ncurses_attrs[] = {ATTRLIST(ATTR_COMPLETE_ARRAY)};

#define ATTR_INDEX_ENUM(name, base, base8, fg8, bg8, base256, fg256, bg256)    \
    ATTR_##name,
enum { ATTRLIST(ATTR_INDEX_ENUM) ATTR_dummy };

static unsigned colour_mode;
void setattr(int attr) { attrset(ncurses_attrs[3 * attr + colour_mode]); }

struct HelpItem {
    string key, description;
};

class Screen;

struct cursorpos {
    bool visible;
    int x, y;
};

class Window {
  protected:
    Screen *screen;

  public:
    Window() : screen(NULL) {}
    virtual ~Window() = default;
    virtual void set_screen(Screen *screen_) { screen = screen_; }
    virtual void set_size(int w, int h) = 0;
    virtual void draw(int x, int y, cursorpos *cp) = 0;
    virtual bool process_key(int c) { return false; }
    virtual void minibuf_reply(string text) {}
    virtual int get_height_for_width(int w) = 0;
    virtual vector<HelpItem> help_text() { return {}; }
};

class HelpWindow : public Window {
    vector<HelpItem> content;
    vector<string> lines;
    vector<size_t> key_prefix_len;
    int w, h, topline;

    void clamp_topline()
    {
        topline = min(topline, (int)lines.size() - h);
        topline = max(topline, 0);
    }

  public:
    HelpWindow(const vector<HelpItem> &content)
        : Window(), content(content), topline(0)
    {
    }

    void set_size(int w_, int h_)
    {
        w = w_;
        h = h_;

        size_t key_colwidth = 0;
        for (auto &hi : content)
            key_colwidth = max(key_colwidth, hi.key.size() + 2);

        lines.clear();
        key_prefix_len.clear();
        for (auto &hi : content) {
            string line = rpad(hi.key, key_colwidth);
            size_t next_indent = key_colwidth;
            auto &lines_ref = lines;
            key_prefix_len.push_back(hi.key.size());

            auto emit = [&lines_ref, &line, next_indent]() {
                lines_ref.push_back(line);
                line = string(next_indent, ' ');
            };

            string &text = hi.description;
            size_t pos = 0;
            while (true) {
                while (pos < text.size() && isspace((unsigned char)text[pos]))
                    pos++;
                if (pos >= text.size()) {
                    emit();
                    break;
                }
                size_t wordstart = pos;
                while (pos < text.size() && !isspace((unsigned char)text[pos]))
                    pos++;
                string word = text.substr(wordstart, pos - wordstart);
                if (line.size() > 0 &&
                    !isspace((unsigned char)line[line.size() - 1]))
                    line += " ";
                if (line.size() + word.size() > w) {
                    emit();
                }
                line += word;
            }

            while (key_prefix_len.size() < lines.size())
                key_prefix_len.push_back(0);
        }

        clamp_topline();
    }

    int get_height_for_width(int w)
    {
        assert(0 && "Never call this");
        return 0;
    }

    void draw(int x, int y, cursorpos *cp)
    {
        cp->visible = false;
        for (int i = 0; i < h; i++) {
            int whichline = i + topline;
            string line;
            size_t prefixlen;
            if (whichline < lines.size()) {
                line = lines[whichline];
                prefixlen = key_prefix_len[whichline];
            } else {
                prefixlen = 0;
            }
            int prefixattr = ATTR_HELP_KEY, tailattr = ATTR_HELP_DESCRIPTION;
            if (i == 0 && whichline > 0) {
                prefixattr = ATTR_HELP_SCROLL_INDICATOR;
                prefixlen = w;
                line = "(scroll up for more)";
            } else if (i == h - 1 && whichline < lines.size() - 1) {
                prefixattr = ATTR_HELP_SCROLL_INDICATOR;
                prefixlen = w;
                line = "(scroll down for more)";
            }

            line = rpad(line, w);
            prefixlen = min(prefixlen, line.size());
            move(y + i, x);
            setattr(prefixattr);
            addstr(line.substr(0, prefixlen).c_str());
            setattr(tailattr);
            addstr(line.substr(prefixlen).c_str());
        }
    }

    bool process_key(int c)
    {
        int dy = 0;
        if (c == KEY_DOWN)
            dy = +1;
        else if (c == KEY_UP)
            dy = -1;
        else if (c == KEY_NPAGE)
            dy = h - 1;
        else if (c == KEY_PPAGE)
            dy = -(h - 1);
        else
            return false;
        topline += dy;
        clamp_topline();
        return true;
    }
};

class Screen : public Window {
    int w, h;

    Window *win_main;
    int main_height;
    vector<Window *> win_subs;
    vector<int> sub_heights;

    Window *win_selected;
    Window *win_help;

    bool minibuf_active;
    Window *minibuf_asker;
    string minibuf_prompt, minibuf_text;
    string minibuf_message;
    bool minibuf_message_is_error;

    bool terminated;

  public:
    Screen(Browser &br)
        : Window(), w(0), h(0), win_main(NULL), win_selected(NULL),
          win_help(NULL), minibuf_active(false), terminated(false)
    {
    }

    int get_height_for_width(int w)
    {
        assert(0 && "Never call this");
        return 0;
    }

    bool done() const { return terminated; }

    void resize_wins()
    {
        int total_height = h - 1;
        sub_heights.clear();
        for (auto win : win_subs) {
            int swh = win->get_height_for_width(w);
            swh = min(swh, total_height);
            win->set_size(w, swh);
            sub_heights.push_back(swh);
            total_height -= swh;
        }
        if (win_main) {
            win_main->set_size(w, total_height);
        }
        main_height = total_height;

        if (win_help)
            win_help->set_size(w, h);
    }

    void set_main_window(Window *mainwin)
    {
        mainwin->set_screen(this);
        win_main = mainwin;
        if (!win_selected)
            win_selected = win_main;
        resize_wins();
    }

    void add_subwin(Window *win)
    {
        win->set_screen(this);
        win_subs.push_back(win);
        resize_wins();
    }

    void remove_subwin(Window *win)
    {
        if (win_selected == win)
            win_selected = win_main;
        for (int i = 0; i < win_subs.size(); i++) {
            if (win_subs[i] == win) {
                win_subs.erase(win_subs.begin() + i);
                resize_wins();
                return;
            }
        }
    }

    void set_size(int w_, int h_)
    {
        w = w_;
        h = h_;
        resize_wins();
    }

    void draw(int x, int y, cursorpos *cp)
    {
        bool error = false;
        string minibuf_line;
        if (!minibuf_active) {
            if (minibuf_message.size() > 0) {
                minibuf_line = minibuf_message;
                error = minibuf_message_is_error;
            }
            cp->visible = false;
        } else {
            minibuf_line = minibuf_prompt + minibuf_text;

            cp->visible = true;
            cp->x = minibuf_line.size();
            cp->y = y + h - 1;

            // FIXME: would be nice here to do some kind of sensible
            // panning/wrapping/whatever once the input line gets too
            // long
        }
        minibuf_line = rpad(minibuf_line, w);
        move(y + h - 1, x);
        if (error)
            setattr(ATTR_MINIBUF_ERROR);
        else
            setattr(ATTR_MINIBUF);
        addstr(minibuf_line.c_str());

        if (win_help) {
            win_help->draw(x, y, cp);
        } else if (win_main || !win_subs.empty()) {
            cursorpos cp2;

            if (win_main) {
                win_main->draw(x, y, &cp2);
                if (!minibuf_active && win_main == win_selected)
                    *cp = cp2;
            }
            int yy = y + main_height;
            for (int i = 0; i < win_subs.size(); i++) {
                win_subs[i]->draw(x, yy, &cp2);
                if (win_subs[i] == win_selected)
                    *cp = cp2;
                yy += sub_heights[i];
            }
        } else {
            string blank = rpad("", w);
            attrset(A_NORMAL);
            for (int i = 0; i < h - 1; i++) {
                move(i, x);
                addstr(blank.c_str());
            }
        }
    }

    bool process_key(int c)
    {
        if (c == KEY_RESIZE) {
            int w, h;
            getmaxyx(stdscr, h, w);
            set_size(w, h);
            return true;
        }

        // Any keypress clears the last error or status message displayed.
        minibuf_message = "";

        if (win_help) {
            if (win_help->process_key(c))
                return true;
            delete win_help;
            win_help = NULL;
            return true;
        }

        if (c == KEY_F(1) || c == KEY_F(10)) {
            vector<HelpItem> help;

            if (minibuf_active) {
                help = {
                    {"Backspace", "Erase the last character"},
                    {"^W", "Erase the last word of the input line"},
                    {"^U", "Erase the whole input line"},
                    {"ESC, ^G", "Cancel the minibuffer input operation"},
                    {"Return", "Accept the current minibuffer contents"},
                };
            } else if (win_selected) {
                help = win_selected->help_text();
            }

            if (!help.empty()) {
                if (win_help)
                    delete win_help;
                win_help = new HelpWindow(help);
                resize_wins();
                return true;
            }
        }

        if (minibuf_active) {
            if (c == '\033' || c == '\x07') { // ESC or ^G cancel minibuf input
                minibuf_active = false;
            } else if (c == '\n' || c == '\r') {
                minibuf_asker->minibuf_reply(minibuf_text);
                minibuf_active = false;
            } else if (c == '\x15') { // ^U to erase whole line
                minibuf_text = "";
            } else if (c == '\x17') { // ^W to erase a word
                size_t s = minibuf_text.size();
                while (s > 0 && isspace((unsigned char)minibuf_text[s - 1]))
                    s--;
                while (s > 0 && !isspace((unsigned char)minibuf_text[s - 1]))
                    s--;
                minibuf_text.resize(s);
            } else if (c == '\x7F' || c == '\b' || c == KEY_BACKSPACE) {
                // ^H / ^? to erase a char
                size_t s = minibuf_text.size();
                if (s > 0)
                    minibuf_text.resize(s - 1);
            } else if (c >= ' ' && c < '\x7F') {
                minibuf_text.push_back(c);
            }
            return true;
        }

        if (c == '\t') {
            if (win_selected == win_main) {
                if (win_subs.size() > 0)
                    win_selected = win_subs[0];
            } else {
                for (int i = 0; i < win_subs.size(); i++) {
                    if (win_selected == win_subs[i]) {
                        if (i + 1 < win_subs.size()) {
                            win_selected = win_subs[i + 1];
                        } else {
                            win_selected = win_main;
                        }
                        break;
                    }
                }
            }
            return true;
        }

        if (win_selected && win_selected->process_key(c))
            return true;

        if (c == 'q') {
            terminated = true;
            return true;
        }

        return false;
    }

    void minibuf_ask(string prompt, Window *asker)
    {
        minibuf_active = true;
        minibuf_asker = asker;
        minibuf_prompt = prompt;
        minibuf_text = "";
    }

    void minibuf_error(string error)
    {
        minibuf_message = error;
        minibuf_message_is_error = true;
    }

    void minibuf_info(string info)
    {
        minibuf_message = info;
        minibuf_message_is_error = false;
    }
};

class CoreRegisterDisplay;
class DoubleRegisterDisplay;
class SingleRegisterDisplay;
class NeonRegisterDisplay;
class MVERegisterDisplay;
class MemoryDisplay;

struct MemoryDisplayStartAddr {
    // If this code base could assume C++17, it would be nicer to
    // make this a std::variant. The point is that it holds
    // _either_ an expression and its string form (indicated by
    // expr not being null) _or_ a constant (if expr is null).
    ExprPtr expr;
    string exprstr;
    Addr constant;
    MemoryDisplayStartAddr() : expr(nullptr), constant(0) {}
    MemoryDisplayStartAddr(Addr addr) : expr(nullptr), constant(addr) {}
    bool parse(const string &s, Browser &br, ostringstream &error)
    {
        expr = br.parse_expression(s, error);
        if (!expr)
            return false;

        exprstr = s;
        return true;
    }
};

void curses_hl_display(const HighlightedLine &line, bool highlight,
                       bool selected, bool underlined)
{
    int offset = selected ? ATTR_TRACETEXT_SEL - ATTR_TRACETEXT : 0;
    for (size_t i = 0; i < line.display_len; i++) {
        int index;
        HighlightClass hc = line.highlight_at(i, highlight);

        switch (hc) {
        case HL_TIMESTAMP:
            index = ATTR_TRACETIME;
            break;
        case HL_EVENT:
            index = ATTR_TRACEEVENT;
            break;
        case HL_PC:
            index = ATTR_TRACEPC;
            break;
        case HL_INSTRUCTION:
            index = ATTR_TRACEINSN;
            break;
        case HL_ISET:
            index = ATTR_TRACEISET;
            break;
        case HL_CPUMODE:
            index = ATTR_TRACEMODE;
            break;
        case HL_CCFAIL:
            index = ATTR_TRACESKIP;
            break;
        case HL_DISASSEMBLY:
            index = ATTR_TRACEDISASS;
            break;
        case HL_PUNCT:
            index = ATTR_TRACEPUNCT;
            break;
        case HL_ERROR:
            index = ATTR_TRACEERR;
            break;
        default:
            index = ATTR_TRACETEXT;
            break;
        }
        setattr(index + offset);
        if (underlined)
            attron(A_UNDERLINE);
        char c = i < line.text.size() ? line.text[i] : ' ';
        addch(c);
    }
}

class TraceBuffer : public Window {
    Browser &br;
    Browser::TraceView vu;
    int w, h, hm1;

    // Index of the visible line currently at top of screen.
    //
    // For reasons of cross-program convention (i.e. matching the line
    // numbers used by other utilities like pagers and editors),
    // _physical_ lines in the trace file are numbered from 1. But
    // visible lines are numbered from zero, because that's more
    // sensible in the absence of conventions saying otherwise.
    unsigned visline_scrtop;

    // Highlighted individual event (trace line) within the current
    // visible node, if any. Indexed from 0 (first event of the node)
    // to curr_visible_node.trace_file_lines-1 (last one). UINT_MAX
    // indicates no highlight (the usual situation).
    unsigned selected_event;

    CoreRegisterDisplay *crdisp; // 32- or 64-bit core registers
    DoubleRegisterDisplay *drdisp;
    SingleRegisterDisplay *srdisp;
    NeonRegisterDisplay *neondisp;
    MVERegisterDisplay *mvedisp;
    vector<MemoryDisplay *> mdisps;
    char minibuf_reqtype;

    int last_keystroke;
    int ctrl_l_state;

    bool syntax_highlighting, substitute_branch_targets;

  public:
    TraceBuffer(Browser &br)
        : Window(), br(br), vu(br), syntax_highlighting(true),
          substitute_branch_targets(true)
    {

        hm1 = 0;
        visline_scrtop = 0;

        crdisp = NULL;
        drdisp = NULL;
        srdisp = NULL;
        neondisp = NULL;
        mvedisp = NULL;

        goto_physline(1);
        set_crdisp(true);
    }

    ~TraceBuffer();

    int get_height_for_width(int w)
    {
        assert(0 && "Never call this");
        return 0;
    }

    void update_scrtop(bool force, int posn, int posd)
    {
        // Find the range of visible lines corresponding to
        // curr_visible_node that we definitely want to see on the
        // screen. Usually this is the whole of curr_visible_node, but
        // we make a special case if that node by itself is taller
        // than the whole screen.
        //
        // These two values form a [top,bot) half-open interval.
        unsigned visline_top = vu.physical_to_visible_line(
            vu.curr_visible_node.trace_file_firstline);
        unsigned visline_bot =
            visline_top +
            min((unsigned)hm1, (unsigned)vu.curr_visible_node.trace_file_lines);

        // First check: if 'force' is false and this range already
        // fits on the screen, no need to do any recentring at all.
        if (!force && visline_top >= visline_scrtop &&
            visline_bot <= visline_scrtop + hm1)
            return;

        // We do need to recentre, in which case, use posn and posd to
        // work out how many visible lines we want to place above
        // visline_top.
        unsigned linesabove = (hm1 - (visline_bot - visline_top)) * posn / posd;
        // Special case to avoid going off the top of the file.
        linesabove = min(linesabove, visline_top);

        // And now we're done - we know what visible line should be at
        // the top of the screen.
        visline_scrtop = visline_top - linesabove;
    }

    void set_size(int w_, int h_)
    {
        w = w_;
        h = h_;
        hm1 = h - 1;
        update_scrtop(false, 1, 2);
    }

    void set_screen(Screen *screen_);

    void set_crdisp(bool wanted);
    void set_drdisp(bool wanted);
    void set_srdisp(bool wanted);
    void set_neondisp(bool wanted);
    void set_mvedisp(bool wanted);
    void add_mdisp(MemoryDisplayStartAddr address);
    void remove_mdisp(MemoryDisplay *mdisp);
    void update_other_windows();
    void update_other_windows_diff(unsigned prev_line);

    void goto_time(Time t)
    {
        if (vu.goto_time(t)) {
            selected_event = UINT_MAX;
            update_scrtop(false, 1, 2);
            update_other_windows();
        }
    }

    void goto_physline(unsigned t)
    {
        if (vu.goto_physline(t)) {
            selected_event = UINT_MAX;
            update_scrtop(false, 1, 2);
            update_other_windows();
        }
    }

    void goto_buffer_limit(bool end)
    {
        if (vu.goto_buffer_limit(end)) {
            selected_event = UINT_MAX;
            update_scrtop(false, end ? 1 : 0, 1);
            update_other_windows();
        }
    }

    void goto_pc(unsigned long long pc, int dir)
    {
        if (vu.goto_pc(pc, dir)) {
            selected_event = UINT_MAX;
            update_scrtop(false, 1, 2);
            update_other_windows();
        }
    }

    void draw(int x, int y, cursorpos *cp)
    {
        cp->visible = false;

        {
            ostringstream statusline;
            statusline << "Tarmac file: " << br.get_tarmac_filename();
            statusline << "   Time:" << vu.curr_logical_node.mod_time;
            statusline << "   Line:"
                       << (vu.curr_visible_node.trace_file_firstline +
                           vu.curr_visible_node.trace_file_lines +
                           br.index.lineno_offset);

            string addr;
            {
                unsigned long long pc;
                if (vu.get_current_pc(pc)) {
                    addr = br.get_symbolic_address(pc, true);
                } else {
                    addr = "[none]";
                }
            }

            statusline << "   PC:" << addr;
            if (vu.position_hidden())
                statusline << "   [HIDDEN]";
            string statusstr = rpad(statusline.str(), w);
            move(y + hm1, x);
            setattr(ATTR_STATUSLINE);
            addstr(statusstr.c_str());
        }

        SeqOrderPayload payload;
        unsigned lineoffset;
        bool ok;
        int yy = 0;

        ok = vu.get_node_by_visline(visline_scrtop, &payload, &lineoffset);
        while (ok) {
            vector<string> tracelines = br.index.get_trace_lines(payload);

            for (unsigned i = lineoffset; i < tracelines.size(); i++) {
                string &line = tracelines[i];

                HighlightedLine hl(line, br.index.parseParams(), w);
                if (br.has_image() && substitute_branch_targets)
                    hl.replace_instruction(br);

                bool selected = (payload.cmp(vu.curr_visible_node) == 0 &&
                                 i == selected_event);
                bool underlined = (payload.cmp(vu.curr_visible_node) == 0 &&
                                   i == tracelines.size() - 1);

                move(y + yy, x);
                curses_hl_display(hl, syntax_highlighting, selected,
                                  underlined);
                yy++;
                if (yy >= hm1)
                    return;
            }

            ok = vu.get_node_by_visline(visline_scrtop + yy, &payload,
                                        &lineoffset);
            assert(lineoffset == 0);
        }

        while (yy < hm1) {
            string blank = rpad("", w);
            attrset(A_NORMAL);
            move(y + yy, x);
            addstr(blank.c_str());
            yy++;
        }
    }

    vector<HelpItem> help_text()
    {
        return {
            {"Up, Down", "Step by one visible instruction"},
            {"PgUp, PgDn", "Move by a screenful of visible trace"},
            {"Home, End", "Jump to the start or end of the trace"},
            {"^L", "Scroll to cycle the current location between middle, top "
                   "and bottom"},
            {"t", "Jump to a specified time position"},
            {"l", "Jump to a specified line number of the trace file"},
            {"p, P", "Jump to the next / previous visit to a PC location"},
            {"", ""},
            {"r", "Toggle display of the core registers"},
            {"S, D", "Toggle display of the single / double FP registers"},
            {"m", "Open a memory view at a specified address"},
            {"", ""},
            {"a", "Highlight a single event within the current time"},
            {"Return", "Jump to the previous change to the memory accessed by "
                       "the highlighted event"},
            {"", ""},
            {"-, _",
             "Fold the innermost unfolded function call at this position"},
            {"+, =",
             "Unfold the outermost folded function call at this position"},
            {"[, ]", "Fold / unfold everything nested inside the innermost "
                     "unfolded function call at this position"},
            {"{, }", "Maximally fold / unfold the entire trace buffer"},
            {"", ""},
            {"F6", "Toggle syntax highlighting"},
            {"F7", "Toggle symbolic display of branch targets"},
        };
    }

    bool process_key(int c)
    {
        // Sort out setting of last_keystroke first, in case code
        // below wants to do an early return.
        int last_keystroke_real = last_keystroke;
        last_keystroke = c;

        if (c == KEY_DOWN) {
            unsigned prev_line = vu.curr_logical_node.trace_file_firstline;
            if (vu.next_visible_node(&vu.curr_visible_node)) {
                vu.update_logical_node();
                update_scrtop(false, 1, 1);
                update_other_windows();
                update_other_windows_diff(prev_line);
                selected_event = UINT_MAX;
            }
            return true;
        } else if (c == KEY_UP) {
            unsigned prev_line = vu.curr_logical_node.trace_file_firstline;
            if (vu.prev_visible_node(&vu.curr_visible_node)) {
                vu.update_logical_node();
                update_scrtop(false, 0, 1);
                update_other_windows();
                update_other_windows_diff(prev_line);
                selected_event = UINT_MAX;
            }
            return true;
        } else if (c == KEY_HOME) {
            goto_buffer_limit(false);
            return true;
        } else if (c == KEY_END) {
            goto_buffer_limit(true);
            return true;
        } else if (c == KEY_NPAGE) {
            if (vu.goto_visline(visline_scrtop + h)) {
                update_scrtop(false, 0, 1);
                selected_event = UINT_MAX;
            } else {
                goto_buffer_limit(true);
            }
            return true;
        } else if (c == KEY_PPAGE) {
            if (vu.goto_visline(visline_scrtop - 1)) {
                update_scrtop(false, 1, 1);
                selected_event = UINT_MAX;
            } else {
                goto_buffer_limit(false);
            }
            return true;
        } else if (c == 'a') {
            // Cycle selected_event through the lines of
            // curr_visible_node.
            selected_event++;
            if (selected_event >= vu.curr_visible_node.trace_file_lines)
                selected_event = UINT_MAX;
            return true;
        } else if (c == '\r' || c == '\n') {
            SeqOrderPayload ref_node;
            if (!br.get_previous_node(vu.curr_visible_node, &ref_node))
                ref_node = vu.curr_visible_node;

            DecodedTraceLine dtl(
                br.index.parseParams(),
                br.index.get_trace_line(vu.curr_visible_node, selected_event));
            unsigned line = 0;
            if (dtl.mev) {
                line = br.getmem(ref_node.memory_root, 'm', dtl.mev->addr,
                                 dtl.mev->size, NULL, NULL);
            } else if (dtl.rev) {
                unsigned iflags = br.get_iflags(ref_node.memory_root);
                line = br.getmem(ref_node.memory_root, 'r',
                                 reg_offset(dtl.rev->reg, iflags),
                                 reg_size(dtl.rev->reg), NULL, NULL);
            }
            if (line)
                goto_physline(line);
            return true;
        } else if (c == '-' || c == '_' || c == '[' || c == ']') {
            // Fold up the function call containing the cursor
            // position, if '-' (or its shifted version '_') was
            // pressed.
            //
            // The variants '[' and ']' also affect the fold state for
            // the same range of the file, but set it to different
            // things. '[' folds up all _subfunctions_ called from
            // within the current function, but leaves the function
            // itself visible; ']' completely unfolds everything from
            // the start to the end of this function's execution.

            unsigned firstline, lastline, depth;
            if (!vu.physline_range_for_containing_function(
                    vu.curr_visible_node, &firstline, &lastline, &depth)) {
                screen->minibuf_error("No function call to fold up here");
                return true;
            }

            // Set the folding state in this range to a uniform value
            // depending on which keystroke we're handling.
            if (c == ']')
                depth = UINT_MAX; // unfold everything
            else if (c != '[')
                depth--; // fold up even this function
            else
                ; // depth already has the right value
            vu.set_fold_state(firstline, lastline, 0, depth);

            // And restore display sanity.
            vu.update_visible_node();
            vu.update_logical_node();
            update_scrtop(false, 1, 1);
            update_other_windows();
            selected_event = UINT_MAX;
            return true;
        } else if (c == '+' || c == '=') {
            // Unfold one function call at the cursor position.
            unsigned firstline, lastline, depth;
            if (!vu.physline_range_for_folded_function_after(
                    vu.curr_visible_node, &firstline, &lastline, &depth)) {
                screen->minibuf_error("No function call to unfold here");
            } else {
                bool position_was_hidden = vu.position_hidden();

                vu.set_fold_state(firstline, lastline, 0, depth);

                /*
                 * Slightly odd policy for deciding what node to put
                 * under the cursor after we finish the unfold. If the
                 * current position is 'hidden' (that is,
                 * curr_logical_node is somewhere inside a folded
                 * section and not where update_logical_node would put
                 * it given the current visible node), then we make
                 * sure we go back to _that_ exact position after the
                 * unfold (so that you can jump to a time that's
                 * hidden, and keep unfolding until you reach that
                 * position). But if the current position is not
                 * hidden, and we're logically at the _end_ of a
                 * function, then we actually use the current
                 * _visible_ node as the position to return to after
                 * the unfold, i.e. we come in at the start rather
                 * than the end of the unfolded function call.
                 */
                if (position_was_hidden)
                    vu.update_visible_node();
                else
                    vu.update_logical_node();

                update_scrtop(false, 1, 1);
                update_other_windows();
                selected_event = UINT_MAX;
            }
            return true;
        } else if (c == '{' || c == '}') {
            // Unfold or fold the whole buffer, to the maximum extent
            // possible.
            SeqOrderPayload last;
            if (vu.br.find_buffer_limit(true, &last)) {
                vu.set_fold_state(
                    1, last.trace_file_firstline + last.trace_file_lines - 1, 0,
                    c == '}' ? UINT_MAX : 1);
            }
            // And restore display sanity.
            vu.update_visible_node();
            vu.update_logical_node();
            update_scrtop(false, 1, 2);
            update_other_windows();
            selected_event = UINT_MAX;
            return true;
        } else if (c == 't') {
            screen->minibuf_ask(_("Go to time: "), this);
            minibuf_reqtype = 't';
            return true;
        } else if (c == 'l') {
            screen->minibuf_ask(_("Go to line: "), this);
            minibuf_reqtype = 'l';
            return true;
        } else if (c == 'p' || c == 'P') {
            screen->minibuf_ask(
                (c == 'p' ?
                 "Go to previous visit to PC: " :
                 "Go to next visit to PC: "), this);
            minibuf_reqtype = c;
            return true;
        } else if (c == 'm') {
            screen->minibuf_ask("Show memory at address: ", this);
            minibuf_reqtype = 'm';
            return true;
        } else if (c == 'n' || c == 'N') {
            unsigned long long pc;
            if (vu.get_current_pc(pc))
                goto_pc(pc, c == 'n' ? +1 : -1);
            return true;
        } else if (c == 'r') {
            // Toggle core register display window on/off
            set_crdisp(crdisp == NULL);
            return true;
        } else if (c == 'D') {
            // Toggle d-register display window on/off
            set_drdisp(drdisp == NULL);
            return true;
        } else if (c == 'S') {
            // Toggle s-register display window on/off
            set_srdisp(srdisp == NULL);
            return true;
        } else if (c == 'V') {
            // Toggle NEON vector register display window on/off
            set_neondisp(neondisp == NULL);
            return true;
        } else if (c == 'M') {
            // Toggle MVE vector register display window on/off
            set_mvedisp(mvedisp == NULL);
            return true;
        } else if (c == '\x0C') { // Ctrl-L
            // Emacs-like ^L handling: on the first press, centre the
            // cursor on the screen, and subsequent presses with no
            // intervening other keystrokes cycle through moving the
            // current cursor position to the top, bottom and middle
            // again.
            if (last_keystroke_real != '\x0C')
                ctrl_l_state = 1;
            else
                ctrl_l_state = (ctrl_l_state + 2) % 3;
            update_scrtop(true, ctrl_l_state, 2);
            return true;
        } else if (c == KEY_F(6)) {
            syntax_highlighting = !syntax_highlighting;
            screen->minibuf_info(syntax_highlighting
                                     ? "Syntax highlighting on"
                                     : "Syntax highlighting off");
            return true;
        } else if (c == KEY_F(7)) {
            if (!br.has_image()) {
                screen->minibuf_error(
                    "No image to look up symbolic branch targets");
            } else {
                substitute_branch_targets = !substitute_branch_targets;
                screen->minibuf_info(
                    substitute_branch_targets
                        ? "Symbolic branch-target display on"
                        : "Symbolic branch-target display off");
            }
            return true;
        } else {
            return false;
        }
    }

    void minibuf_reply(string text)
    {
        try {
            switch (minibuf_reqtype) {
            case 't':
                goto_time(evaluate_expression_plain(text));
                break;
            case 'l':
                goto_physline(evaluate_expression_plain(text) -
                              br.index.lineno_offset);
                break;
            case 'p':
                goto_pc(vu.evaluate_expression_addr(text), +1);
                break;
            case 'P':
                goto_pc(vu.evaluate_expression_addr(text), -1);
                break;
            case 'm': {
                MemoryDisplayStartAddr addr;
                ostringstream error;
                if (addr.parse(text, br, error))
                    add_mdisp(addr);
                else
                    screen->minibuf_error(
                        format("Error parsing expression: {}", error.str()));
                break;
            }
            }
        } catch (invalid_argument) {
            if (text.size() > 0)
                screen->minibuf_error("Invalid format for parameter");
        }
    }

    Addr evaluate_expression_addr(ExprPtr expr)
    {
        return vu.evaluate_expression_addr(expr);
    }
};

class RegisterDisplay : public Window {
  protected:
    Browser &br;
    IndexReader &index;

  private:
    bool interpret_address;
    bool locked;
    OFF_T memroot, ext_memroot;
    unsigned line, ext_line;
    int w, h;
    int reg_selected;
    TraceBuffer *tbuf;
    char minibuf_reqtype;
    int top_line;
    vector<int> regs_per_line, reg_to_line;
    OFF_T diff_memroot;
    unsigned diff_minline;

  protected:
    vector<RegisterId> regs;
    int desired_visible_regs;
    string status_prefix, time_prompt, line_prompt;

  public:
    RegisterDisplay(Browser &br, TraceBuffer *tbuf)
        : Window(), br(br), index(br.index), interpret_address(false),
          locked(false), memroot(0), reg_selected(0), tbuf(tbuf),
          diff_memroot(0)
    {
    }

    void set_size(int w_, int h_)
    {
        w = w_;
        h = h_;
        top_line = 0;
    }

    void set_memroot(OFF_T memroot_, unsigned line_)
    {
        ext_memroot = memroot_;
        ext_line = line_;
        if (!locked) {
            memroot = ext_memroot;
            line = ext_line;
            diff_memroot = 0; // clear previous diff highlighting
        }
    }

    void goto_physline(unsigned line_)
    {
        SeqOrderPayload found_node;
        if (br.node_at_line(line_, &found_node)) {
            memroot = found_node.memory_root;
            line = found_node.trace_file_firstline;
            diff_memroot = 0; // clear previous diff highlighting
        }
    }

    void goto_time(Time time)
    {
        SeqOrderPayload found_node;
        if (br.node_at_time(time, &found_node)) {
            memroot = found_node.memory_root;
            line = found_node.trace_file_firstline;
            diff_memroot = 0; // clear previous diff highlighting
        }
    }

    void setup_diff_lines(unsigned line1, unsigned line2)
    {
        unsigned linemin = min(line1, line2), linemax = max(line1, line2);
        SeqOrderPayload found_node;
        if (linemin != linemax && br.node_at_line(linemax, &found_node)) {
            diff_memroot = found_node.memory_root;
            diff_minline = linemin + 1;
        } else {
            diff_memroot = 0;
        }
    }

    void diff_against_if_not_locked(unsigned line_)
    {
        if (!locked)
            setup_diff_lines(line_, line);
    }

    int get_height_for_width(int w)
    {
        int currlinelen = 0;
        int yy = 0;
        unsigned n = 0;

        for (const RegisterId &r : regs) {

            if (n >= desired_visible_regs)
                break;
            n++;

            int dispstrlen = reg_name(r).size() + 1 + format_reg_length(r);
            if (currlinelen == 0) {
                currlinelen = dispstrlen;
            } else if (currlinelen + 1 + dispstrlen <= w) {
                currlinelen += 1 + dispstrlen;
            } else {
                yy++;
                currlinelen = dispstrlen;
            }
        }
        yy++;          // count the final partial line
        return yy + 1; // and the status line
    }

    void attrshow(const string &line, const string &type)
    {
        for (int i = 0; i < line.size(); i++) {
            switch (type[i]) {
            case 'r':
                setattr(ATTR_REGDISPLAY_NAME);
                break;
            case 'f':
                setattr(ATTR_REGDISPLAY_FIXED);
                break;
            case 'u':
                setattr(ATTR_REGDISPLAY_UNKNOWN);
                break;
            case 'v':
                setattr(ATTR_REGDISPLAY_VALUE);
                break;
            case 'U':
                setattr(ATTR_REGDISPLAY_UNKNOWN_DIFF);
                break;
            case 'V':
                setattr(ATTR_REGDISPLAY_VALUE_DIFF);
                break;
            }
            addch(line[i]);
        }
    }

    void draw(int x, int y, cursorpos *cp)
    {
        string currline, currtype;
        int yy = -top_line;
        int regs_on_line = 0;

        cp->visible = false;
        regs_per_line.clear();
        reg_to_line.clear();

        for (unsigned i = 0; i < regs.size(); i++) {
            const RegisterId &r = regs[i];
            string dispstr, disptype;

            br.format_reg(dispstr, disptype, r, memroot, diff_memroot,
                          diff_minline);
            size_t valstart = dispstr.size() - format_reg_length(r);

            if (currline.size() == 0) {
                currline = dispstr;
                currtype = disptype;
                regs_on_line = 1;
            } else if (currline.size() + 1 + dispstr.size() <= w) {
                currline += " ";
                type_extend(currtype, currline, 'f');
                currline += dispstr;
                currtype += disptype;
                regs_on_line++;
            } else {
                regs_per_line.push_back(regs_on_line);
                if (yy >= 0 && yy < h - 1) {
                    currline = rpad(currline, w);
                    currtype = rpad(currtype, w, 'f');
                    move(y + yy, x);
                    attrshow(currline, currtype);
                }
                yy++;
                currline = dispstr;
                currtype = disptype;
                regs_on_line = 1;
            }

            reg_to_line.push_back(yy + top_line);

            if (i == reg_selected) {
                cp->visible = true;
                cp->x = x + currline.size() - dispstr.size() + valstart;
                cp->y = y + yy;
            }
        }
        while (yy < h - 1) {
            if (yy >= 0) {
                regs_per_line.push_back(regs_on_line);
                currline = rpad(currline, w);
                currtype = rpad(currtype, w, 'f');
                move(y + yy, x);
                attrshow(currline, currtype);
            }
            currline = "";
            currtype = "";
            regs_on_line = 0;
            yy++;
        }
        // In case that loop didn't push the remaining value of
        // regs_on_line, push it now, to ensure regs_per_line contains
        // enough entries to completely cover the register file we're
        // displaying.
        if (regs_on_line)
            regs_per_line.push_back(regs_on_line);

        {
            ostringstream statusline;
            statusline << status_prefix << line;
            if (locked)
                statusline << " (LOCKED)";

            // add symbol name of selected register to the status line, if the
            // register's value can be retrieved in integer form
            if (interpret_address) {
                const RegisterId &r = regs[reg_selected];
                auto r_name = reg_name(r);
                auto r_content = br.get_reg_value(memroot, r);
                if (r_content.first) {
                    string addr = br.get_symbolic_address(r_content.second);
                    if (!addr.empty())
                        statusline << "   " << r_name << " = " << addr;
                }
            }
            string statusstr = rpad(statusline.str(), w);
            move(y + h - 1, x);
            setattr(ATTR_STATUSLINE);
            addstr(statusstr.c_str());
        }
    }

    void keep_cursor_in_view()
    {
        int line = 0, r = reg_selected;
        while (r >= regs_per_line[line]) {
            r -= regs_per_line[line];
            line++;
        }
        top_line = min(top_line, line);
        top_line = max(top_line, line - (h - 2));
    }

    vector<HelpItem> help_text()
    {
        return {
            {"Left, Right, Up, Down", "Change the selected register"},
            {"<, >", "Shrink / grow this register window by one screen line"},
            {"", ""},
            {"t", "Lock this register window to a specified trace line number"},
            {"l", "Lock this register window to the current time, or unlock it "
                  "to track the current trace position again"},
            {"", ""},
            {"Return", "Jump to the previous change to the selected register"},
            {"a",
             "Toggle interpretation of selected register via ELF symbol table"},
        };
    }

    bool process_key(int c)
    {
        if (c == KEY_RIGHT) {
            reg_selected = (reg_selected + 1) % regs.size();
            keep_cursor_in_view();
            return true;
        } else if (c == KEY_LEFT) {
            reg_selected = (reg_selected + regs.size() - 1) % regs.size();
            keep_cursor_in_view();
            return true;
        } else if (c == KEY_UP) {
            int line = reg_to_line[reg_selected];
            if (line > 0) {
                reg_selected -= regs_per_line[line - 1];
                reg_selected = max(reg_selected, 0);
            } else {
                reg_selected = 0;
            }
            keep_cursor_in_view();
            return true;
        } else if (c == KEY_DOWN) {
            int line = reg_to_line[reg_selected];
            reg_selected += regs_per_line[line];
            reg_selected = min(reg_selected, (int)(regs.size() - 1));
            keep_cursor_in_view();
            return true;
        } else if (c == 'a') {
            interpret_address = !interpret_address;
            return true;
        } else if (c == '\x0C') { // Ctrl-L
            locked = !locked;
            if (!locked) {
                memroot = ext_memroot;
                line = ext_line;
                diff_memroot = 0; // clear previous diff highlighting
            }
            return true;
        } else if (c == '<') {
            int old_h = get_height_for_width(w);
            while (desired_visible_regs > 1) {
                desired_visible_regs--;
                int new_h = get_height_for_width(w);
                if (new_h < old_h)
                    break;
            }
            screen->resize_wins();
            return true;
        } else if (c == '>') {
            int old_h = get_height_for_width(w);
            while (desired_visible_regs < regs.size()) {
                desired_visible_regs++;
                int new_h = get_height_for_width(w);
                if (new_h > old_h)
                    break;
            }
            screen->resize_wins();
            return true;
        } else if (c == 'l') {
            screen->minibuf_ask(line_prompt, this);
            minibuf_reqtype = 'l';
            return true;
        } else if (c == 't') {
            screen->minibuf_ask(time_prompt, this);
            minibuf_reqtype = 't';
            return true;
        } else if (c == '\r' || c == '\n') {
            const RegisterId &r = regs[reg_selected];
            unsigned iflags = br.get_iflags(memroot);
            unsigned line = br.getmem(memroot, 'r', reg_offset(r, iflags),
                                      reg_size(r), NULL, NULL);
            if (line)
                tbuf->goto_physline(line);
            return true;
        } else {
            return false;
        }
    }

    void minibuf_reply(string text)
    {
        try {
            switch (minibuf_reqtype) {
            case 'l':
                goto_physline(evaluate_expression_plain(text));
                locked = true;
                break;
            case 't':
                goto_time(evaluate_expression_plain(text));
                locked = true;
                break;
            }
        } catch (invalid_argument) {
            if (text.size() > 0)
                screen->minibuf_error("Invalid format for parameter");
        }
    }
};

class CoreRegisterDisplay : public RegisterDisplay {
  protected:
    CoreRegisterDisplay(Browser &br, TraceBuffer *tbuf)
        : RegisterDisplay(br, tbuf)
    {

        // Subclass constructor will fill this in, and 'regs' too
        desired_visible_regs = 0;

        status_prefix = "Core regs at line: ";
        time_prompt = "Show core registers at time: ";
        line_prompt = "Show core registers at line number: ";
    }
};

class CoreRegister32Display : public CoreRegisterDisplay {
  public:
    CoreRegister32Display(Browser &br, TraceBuffer *tbuf)
        : CoreRegisterDisplay(br, tbuf)
    {
        for (unsigned i = 0; i < 15; i++)
            regs.push_back(RegisterId{RegPrefix::r, i});
        regs.push_back(RegisterId{RegPrefix::psr, 0});
        desired_visible_regs = regs.size();
    }
};

class CoreRegister64Display : public CoreRegisterDisplay {
  public:
    CoreRegister64Display(Browser &br, TraceBuffer *tbuf)
        : CoreRegisterDisplay(br, tbuf)
    {
        for (unsigned i = 0; i < 31; i++)
            regs.push_back(RegisterId{RegPrefix::x, i});
        regs.push_back(RegisterId{RegPrefix::xsp, 0});
        regs.push_back(RegisterId{RegPrefix::psr, 0});
        desired_visible_regs = regs.size();
    }
};

class FPRegisterDisplay : public RegisterDisplay {
    using RegisterDisplay::RegisterDisplay;
};

class DoubleRegisterDisplay : public FPRegisterDisplay {
  public:
    DoubleRegisterDisplay(Browser &br, TraceBuffer *tbuf)
        : FPRegisterDisplay(br, tbuf)
    {
        for (unsigned i = 0; i < 32; i++)
            regs.push_back(RegisterId{RegPrefix::d, i});
        desired_visible_regs = 4;
        status_prefix = "FP double regs at line: ";
        time_prompt = "Show FP double registers at time: ";
        line_prompt = "Show FP double registers at line number: ";
    }
};

class SingleRegisterDisplay : public FPRegisterDisplay {
  public:
    SingleRegisterDisplay(Browser &br, TraceBuffer *tbuf)
        : FPRegisterDisplay(br, tbuf)
    {
        for (unsigned i = 0; i < 32; i++)
            regs.push_back(RegisterId{RegPrefix::s, i});
        desired_visible_regs = 8;
        status_prefix = "FP single regs at line: ";
        time_prompt = "Show FP single registers at time: ";
        line_prompt = "Show FP single registers at line number: ";
    }
};

class VectorRegisterDisplay : public RegisterDisplay {
    using RegisterDisplay::RegisterDisplay;
};

class NeonRegisterDisplay : public VectorRegisterDisplay {
  public:
    NeonRegisterDisplay(Browser &br, TraceBuffer *tbuf, bool aarch64)
        : VectorRegisterDisplay(br, tbuf)
    {
        unsigned n_q_regs = aarch64 ? 32 : 16;
        for (unsigned i = 0; i < n_q_regs; i++)
            regs.push_back(RegisterId{RegPrefix::q, i});
        desired_visible_regs = 8;
        status_prefix = "NEON vector regs at line ";
        time_prompt = "Show NEON vector registers at time: ";
        line_prompt = "Show NEON vector registers at line number: ";
    }
};

class MVERegisterDisplay : public VectorRegisterDisplay {
  public:
    MVERegisterDisplay(Browser &br, TraceBuffer *tbuf)
        : VectorRegisterDisplay(br, tbuf)
    {
        for (unsigned i = 0; i < 8; i++)
            regs.push_back(RegisterId{RegPrefix::q, i});
        regs.push_back(RegisterId{RegPrefix::vpr, 0});
        desired_visible_regs = 9;
        status_prefix = "MVE vector regs at line: ";
        time_prompt = "Show MVE vector registers at time: ";
        line_prompt = "Show MVE vector registers at line number: ";
    }
};

class MemoryDisplay : public Window {
    Browser &br;
    bool locked;
    OFF_T memroot, ext_memroot;
    unsigned line, ext_line;
    int w, h;
    Addr start_addr, cursor_addr;
    bool addrs_known;
    string cursor_addr_exprstr;
    ExprPtr cursor_addr_expr;
    TraceBuffer *tbuf;
    int bytes_per_line;
    int desired_height;
    char minibuf_reqtype;
    OFF_T diff_memroot;
    unsigned diff_minline;

  public:
    MemoryDisplay(Browser &br, TraceBuffer *tbuf, MemoryDisplayStartAddr addr)
        : Window(), br(br), locked(false), memroot(0),
          tbuf(tbuf), diff_memroot(0)
    {
        bytes_per_line = 16; // FIXME: configurability

        if (addr.expr) {
            set_cursor_addr_expr(addr.expr, addr.exprstr);
        } else {
            addrs_known = true;
            cursor_addr_expr = nullptr;
            cursor_addr = addr.constant;
            start_addr = cursor_addr - (cursor_addr % bytes_per_line);
        }

        desired_height = 4;
    }

    void set_cursor_addr_expr(ExprPtr expr, const string &exprstr)
    {
        cursor_addr_expr = expr;
        compute_cursor_addr();

        if (expr->is_constant() && addrs_known) {
            /*
             * Normalise constant expressions to a plain number. This
             * way, there's no hidden state: when you scroll the
             * window, the new start address you obtained by scrolling
             * will be the official start address, so that the window
             * will stay pointing there even when the trace position
             * changes.
             *
             * We only keep the start address in expression form if
             * it's actually variable.
             */
            cursor_addr_expr = nullptr;
        } else {
            /*
             * Keep the string version of the address expression, so that
             * it's obvious from looking at the toolbar that this memory
             * window has a variable start point.
             */
            cursor_addr_exprstr = exprstr;
        }
    }

    void compute_cursor_addr()
    {
        try {
            if (tbuf)
                cursor_addr = tbuf->evaluate_expression_addr(cursor_addr_expr);
            else
                cursor_addr = br.evaluate_expression_addr(cursor_addr_expr);

            /*
             * The cursor now points at the exact value of the
             * expression. Round down to a multiple of 16 bytes to get
             * the window start address.
             */
            start_addr = cursor_addr - (cursor_addr % bytes_per_line);

            addrs_known = true;
        } catch (invalid_argument) {
            addrs_known = false;
        }
    }

    void set_size(int w_, int h_)
    {
        w = w_;
        h = h_;
    }

    void set_memroot(OFF_T memroot_, unsigned line_)
    {
        ext_memroot = memroot_;
        ext_line = line_;
        if (!locked) {
            memroot = ext_memroot;
            line = ext_line;
            diff_memroot = 0; // clear previous diff highlighting
        }

        if (cursor_addr_expr)
            compute_cursor_addr();
    }

    void goto_physline(unsigned line_)
    {
        SeqOrderPayload found_node;
        if (br.node_at_line(line_, &found_node)) {
            memroot = found_node.memory_root;
            line = found_node.trace_file_firstline;
            diff_memroot = 0; // clear previous diff highlighting
        }
    }

    void goto_time(Time time)
    {
        SeqOrderPayload found_node;
        if (br.node_at_time(time, &found_node)) {
            memroot = found_node.memory_root;
            line = found_node.trace_file_firstline;
            diff_memroot = 0; // clear previous diff highlighting
        }
    }

    void setup_diff_lines(unsigned line1, unsigned line2)
    {
        Time linemin = min(line1, line2), linemax = max(line1, line2);
        SeqOrderPayload found_node;
        if (linemin != linemax && br.node_at_line(linemax, &found_node)) {
            diff_memroot = found_node.memory_root;
            diff_minline = linemin + 1;
        } else {
            diff_memroot = 0;
        }
    }

    void diff_against_if_not_locked(unsigned line_)
    {
        if (!locked)
            setup_diff_lines(line_, line);
    }

    void set_addr(Addr new_start_addr) { start_addr = new_start_addr; }

    int get_height_for_width(int w)
    {
        return desired_height + 1; // always reserve space for a status line
    }

    void draw(int x, int y, cursorpos *cp)
    {
        Addr addr = start_addr;
        cp->visible = false;
        assert(memroot);

        for (int yy = 0; yy < h - 1; yy++) {
            string line, type;
            size_t hexpos;

            br.format_memory(line, type, addr, addrs_known, bytes_per_line, 8,
                             hexpos, memroot, diff_memroot, diff_minline);

            if (addr <= cursor_addr && cursor_addr < addr + bytes_per_line) {
                cp->visible = true;
                cp->y = y + yy;
                cp->x = hexpos + 3 * (cursor_addr - addr);
            }

            addr += bytes_per_line;

            line = rpad(line, w);
            type = rpad(type, w, 'f');

            for (int i = 0; i < line.size(); i++) {
                switch (type[i]) {
                case 'f':
                    setattr(ATTR_MEMDISPLAY_FIXED);
                    break;
                case 'u':
                    setattr(ATTR_MEMDISPLAY_UNKNOWN);
                    break;
                case 'v':
                    setattr(ATTR_MEMDISPLAY_VALUE);
                    break;
                case 'c':
                    setattr(ATTR_MEMDISPLAY_CTRLCHAR);
                    break;
                case 'U':
                    setattr(ATTR_MEMDISPLAY_UNKNOWN_DIFF);
                    break;
                case 'V':
                    setattr(ATTR_MEMDISPLAY_VALUE_DIFF);
                    break;
                case 'C':
                    setattr(ATTR_MEMDISPLAY_CTRLCHAR_DIFF);
                    break;
                }
                addch(line[i]);
            }
        }

        {
            ostringstream statusline;
            statusline << "Memory at line: " << line;
            if (locked)
                statusline << " (LOCKED)";
            string addr = br.get_symbolic_address(cursor_addr);
            if (!addr.empty())
                statusline << "   cursor at " << addr;
            if (cursor_addr_expr)
                statusline << "   following: " << cursor_addr_exprstr;
            string statusstr = rpad(statusline.str(), w);
            move(y + h - 1, x);
            setattr(ATTR_STATUSLINE);
            addstr(statusstr.c_str());
        }
    }

    void ensure_cursor_on_screen()
    {
        Addr line_start = cursor_addr - (cursor_addr % bytes_per_line);
        start_addr = min(start_addr, line_start);
        start_addr =
            max(start_addr,
                line_start - min(line_start, (Addr)bytes_per_line * (h - 2)));
    }

    vector<HelpItem> help_text()
    {
        return {
            {"Left, Right, Up, Down", "Change the selected address"},
            {"<, >", "Shrink / grow this memory window by one screen line"},
            {"t", "Lock this memory window to a specified trace line number"},
            {"l", "Lock this memory window to the current time, or unlock it "
                  "to track the current trace position again"},
            {"F", "Stop this memory window from following a variable address "
                  "expression, if it previously was"},
            {"1, Return", "Jump to the previous change to this byte"},
            {"2, 4, 8",
             "Jump to the previous change to this aligned {2,4,8}-byte word"},
            {"", ""},
            {"d",
             "Highlight memory changes between now and another specified time"},
            {"[, ]",
             "Jump to the previous / next address highlighted as a change"},
            {"", ""},
            {"x", "Close this memory window"},
        };
    }

    bool process_key(int c)
    {
        if (c == 'x') {
            tbuf->remove_mdisp(this);
            delete this;
            return true;
        } else if (c == '<') {
            if (desired_height > 1) {
                desired_height--;
                ensure_cursor_on_screen();
                screen->resize_wins();
            }
            return true;
        } else if (c == '>') {
            desired_height++;
            screen->resize_wins();
            return true;
        } else if (c == KEY_UP) {
            cursor_addr -= bytes_per_line;
            ensure_cursor_on_screen();
            return true;
        } else if (c == KEY_DOWN) {
            cursor_addr += bytes_per_line;
            ensure_cursor_on_screen();
            return true;
        } else if (c == KEY_LEFT) {
            cursor_addr--;
            ensure_cursor_on_screen();
            return true;
        } else if (c == KEY_RIGHT) {
            cursor_addr++;
            ensure_cursor_on_screen();
            return true;
        } else if (c == ']' || c == '[') {
            Addr diff_lo, diff_hi;
            int sign = c == ']' ? +1 : -1;
            if (diff_memroot &&
                br.find_next_mod(diff_memroot, 'm', cursor_addr + sign,
                                 diff_minline, sign, diff_lo, diff_hi)) {
                if (sign > 0)
                    cursor_addr = max(diff_lo, cursor_addr + 1);
                else
                    cursor_addr = min(diff_hi, cursor_addr - 1);
                ensure_cursor_on_screen();
            }
            return true;
        } else if (c == '\x0C') { // Ctrl-L
            locked = !locked;
            if (!locked) {
                memroot = ext_memroot;
                line = ext_line;
                diff_memroot = 0; // clear previous diff highlighting
            }
            return true;
        } else if (c == 'l') {
            screen->minibuf_ask("Show memory at line number: ", this);
            minibuf_reqtype = 'l';
            return true;
        } else if (c == 't') {
            screen->minibuf_ask("Show memory at time: ", this);
            minibuf_reqtype = 't';
            return true;
        } else if (c == 'd') {
            screen->minibuf_ask("Diff memory against line number: ", this);
            minibuf_reqtype = 'd';
            return true;
        } else if (c == '\r' || c == '\n' || c == '1' || c == '2' || c == '4' ||
                   c == '8') {
            if (c == '\r' || c == '\n')
                c = '1';
            Addr prov_size = c - '0';
            Addr prov_start = cursor_addr & ~(prov_size - 1);
            unsigned line =
                br.getmem(memroot, 'm', prov_start, prov_size, NULL, NULL);
            if (line)
                tbuf->goto_physline(line);
            return true;
        } else if (c == 'F') {
            cursor_addr_expr = nullptr;
            cursor_addr_exprstr.clear();
            return true;
        } else {
            return false;
        }
    }

    void minibuf_reply(string text)
    {
        try {
            switch (minibuf_reqtype) {
            case 'l':
                goto_physline(evaluate_expression_plain(text));
                locked = true;
                break;
            case 't':
                goto_time(evaluate_expression_plain(text));
                locked = true;
                break;
            case 'd':
                setup_diff_lines(evaluate_expression_plain(text), line);
                break;
            }
        } catch (invalid_argument) {
            if (text.size() > 0)
                screen->minibuf_error("Invalid format for parameter");
        }
    }
};

TraceBuffer::~TraceBuffer()
{
    if (crdisp) {
        if (screen)
            screen->remove_subwin(crdisp);
        delete crdisp;
    }
    if (drdisp) {
        if (screen)
            screen->remove_subwin(drdisp);
        delete drdisp;
    }
    if (srdisp) {
        if (screen)
            screen->remove_subwin(srdisp);
        delete srdisp;
    }
    if (neondisp) {
        if (screen)
            screen->remove_subwin(neondisp);
        delete neondisp;
    }
    if (mvedisp) {
        if (screen)
            screen->remove_subwin(mvedisp);
        delete mvedisp;
    }
    for (auto mdisp : mdisps) {
        screen->remove_subwin(mdisp);
        delete mdisp;
    }
}

void TraceBuffer::set_screen(Screen *screen_)
{
    if (screen && screen != screen_) {
        if (crdisp)
            screen->remove_subwin(crdisp);
        if (drdisp)
            screen->remove_subwin(drdisp);
        if (srdisp)
            screen->remove_subwin(srdisp);
        if (neondisp)
            screen->remove_subwin(neondisp);
        if (mvedisp)
            screen->remove_subwin(mvedisp);
    }
    Window::set_screen(screen_);
    if (crdisp)
        screen->add_subwin(crdisp);
    if (drdisp)
        screen->add_subwin(drdisp);
    if (srdisp)
        screen->add_subwin(srdisp);
    if (neondisp)
        screen->add_subwin(neondisp);
    if (mvedisp)
        screen->add_subwin(mvedisp);
}

void TraceBuffer::set_crdisp(bool wanted)
{
    if (wanted && !crdisp) {
        if (br.index.isAArch64())
            crdisp = new CoreRegister64Display(br, this);
        else
            crdisp = new CoreRegister32Display(br, this);
        if (screen)
            screen->add_subwin(crdisp);
        update_other_windows();
    } else if (!wanted && crdisp) {
        if (screen)
            screen->remove_subwin(crdisp);
        delete crdisp;
        crdisp = NULL;
    }
}

void TraceBuffer::set_drdisp(bool wanted)
{
    if (wanted && !drdisp) {
        drdisp = new DoubleRegisterDisplay(br, this);
        if (screen)
            screen->add_subwin(drdisp);
        update_other_windows();
    } else if (!wanted && drdisp) {
        if (screen)
            screen->remove_subwin(drdisp);
        delete drdisp;
        drdisp = NULL;
    }
}

void TraceBuffer::set_srdisp(bool wanted)
{
    if (wanted && !srdisp) {
        srdisp = new SingleRegisterDisplay(br, this);
        if (screen)
            screen->add_subwin(srdisp);
        update_other_windows();
    } else if (!wanted && srdisp) {
        if (screen)
            screen->remove_subwin(srdisp);
        delete srdisp;
        srdisp = NULL;
    }
}

void TraceBuffer::set_neondisp(bool wanted)
{
    if (wanted && !srdisp) {
        neondisp = new NeonRegisterDisplay(br, this, br.index.isAArch64());
        if (screen)
            screen->add_subwin(neondisp);
        update_other_windows();
    } else if (!wanted && neondisp) {
        if (screen)
            screen->remove_subwin(neondisp);
        delete neondisp;
        neondisp = NULL;
    }
}

void TraceBuffer::set_mvedisp(bool wanted)
{
    if (wanted && !srdisp) {
        mvedisp = new MVERegisterDisplay(br, this);
        if (screen)
            screen->add_subwin(mvedisp);
        update_other_windows();
    } else if (!wanted && mvedisp) {
        if (screen)
            screen->remove_subwin(mvedisp);
        delete mvedisp;
        mvedisp = NULL;
    }
}

void TraceBuffer::add_mdisp(MemoryDisplayStartAddr addr)
{
    auto mdisp = new MemoryDisplay(br, this, addr);
    if (screen)
        screen->add_subwin(mdisp);
    mdisps.push_back(mdisp);
    update_other_windows();
}

void TraceBuffer::remove_mdisp(MemoryDisplay *mdisp)
{
    int i;
    for (i = 0; mdisps.size() > i; i++)
        if (mdisps[i] == mdisp)
            goto found;
    return;
found:
    mdisps.erase(mdisps.begin() + i);
    screen->remove_subwin(mdisp);
}

void TraceBuffer::update_other_windows()
{
    if (crdisp)
        crdisp->set_memroot(vu.curr_logical_node.memory_root,
                            vu.curr_logical_node.trace_file_firstline);
    if (drdisp)
        drdisp->set_memroot(vu.curr_logical_node.memory_root,
                            vu.curr_logical_node.trace_file_firstline);
    if (srdisp)
        srdisp->set_memroot(vu.curr_logical_node.memory_root,
                            vu.curr_logical_node.trace_file_firstline);
    if (neondisp)
        neondisp->set_memroot(vu.curr_logical_node.memory_root,
                              vu.curr_logical_node.trace_file_firstline);
    if (mvedisp)
        mvedisp->set_memroot(vu.curr_logical_node.memory_root,
                             vu.curr_logical_node.trace_file_firstline);
    for (auto mdisp : mdisps)
        mdisp->set_memroot(vu.curr_logical_node.memory_root,
                           vu.curr_logical_node.trace_file_firstline);
}

void TraceBuffer::update_other_windows_diff(unsigned line)
{
    if (crdisp)
        crdisp->diff_against_if_not_locked(line);
    if (drdisp)
        drdisp->diff_against_if_not_locked(line);
    if (srdisp)
        srdisp->diff_against_if_not_locked(line);
    if (neondisp)
        neondisp->diff_against_if_not_locked(line);
    if (mvedisp)
        mvedisp->diff_against_if_not_locked(line);
    for (auto mdisp : mdisps)
        mdisp->diff_against_if_not_locked(line);
}

void run_browser(Browser &br, bool use_terminal_colours)
{
    Screen scr(br);
    TraceBuffer tbuf(br);
    scr.set_main_window(&tbuf);

    initscr();
    if (!has_colors()) // override if colour isn't even available
        use_terminal_colours = false;
    if (use_terminal_colours)
        start_color();
    noecho();
    keypad(stdscr, true);

    if (!use_terminal_colours) {
        colour_mode = 0;
    } else {
        if (COLORS == 8)
            colour_mode = 1;
        else
            colour_mode = 2;
#define ATTR_COLOUR_PAIR_INIT(name, base, base8, fg8, bg8, base256, fg256,     \
                              bg256)                                           \
    init_pair(CP_##name, colour_mode == 2 ? fg256 : fg8,                       \
              colour_mode == 2 ? bg256 : bg8);
        ATTRLIST(ATTR_COLOUR_PAIR_INIT);
#undef ATTR_COLOUR_PAIR_INIT
    }

    int w, h;
    getmaxyx(stdscr, h, w);
    scr.set_size(w, h);

    while (!scr.done()) {
        cursorpos cp;

        scr.draw(0, 0, &cp);

        if (cp.visible) {
            curs_set(1);
            move(cp.y, cp.x);
        } else {
            curs_set(0);
        }

        scr.process_key(getch());
    }

    endwin();
}

std::unique_ptr<Reporter> reporter = make_cli_reporter();

int main(int argc, char **argv)
{
    gettext_setup();

    // Default to using colour if available, but permit an override by
    // setting NO_COLOR in the environment (to any non-empty string),
    // per https://no-color.org/ . This is done before option
    // processing, so that the command line can override that in turn.
    bool use_terminal_colours = true;
    {
        string no_color;
        if (get_environment_variable("NO_COLOR", no_color) &&
            !no_color.empty())
            use_terminal_colours = false;
    }

    Argparse ap("tarmac-browser", argc, argv);
    TarmacUtility tu;
    tu.add_options(ap);
    ap.optnoval({"--colour", "--color"}, "use colour in the terminal",
                [&]() { use_terminal_colours = true; });
    ap.optnoval({"--no-colour", "--no-color"},
                "don't use colour in the terminal",
                [&]() { use_terminal_colours = false; });
    ap.parse();
    tu.setup();

    Browser br(tu.trace, tu.image_filename, tu.load_offset);
    run_browser(br, use_terminal_colours);

    return 0;
}
