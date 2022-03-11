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
 * wxWidgets front end for tarmac-browser.
 */

#include "browse.hh"
#include "libtarmac/argparse.hh"
#include "libtarmac/reporter.hh"
#include "libtarmac/tarmacutil.hh"

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif
#include "wx/clipbrd.h"
#include "wx/cmdline.h"
#include "wx/dcbuffer.h"
#include "wx/filepicker.h"
#include "wx/pen.h"
#include "wx/progdlg.h"
#if wxUSE_GRAPHICS_CONTEXT
#include "wx/dcgraph.h"
#endif

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
using std::streampos;
using std::string;
using std::swap;
using std::unique_ptr;
using std::vector;

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

struct RGBA {
    double r, g, b, a;
    bool parse(const string &s)
    {
        wxColour c(s);
        if (!c.IsOk())
            return false;
        r = c.Red() / 255.0;
        g = c.Green() / 255.0;
        b = c.Blue() / 255.0;
        a = c.Alpha() / 255.0;
        return true;
    }
};

// Map each colour id to its configuration-file id and default setting.
static const map<ColourId, pair<string, RGBA>> colour_ids = {
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
    string font = "";
    map<ColourId, RGBA> colours;
    map<string, ColourId> colour_kws;

    Config()
    {
        for (const auto &kv : colour_ids) {
            colours[kv.first] = kv.second.second;
            colour_kws[kv.second.first] = kv.first;
        }
    }

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
        string conf_path;
        if (!get_conf_path("gui-browser.conf", conf_path))
            return;

        ifstream ifs(conf_path);
        if (ifs.fail())
            return;

        unsigned lineno = 1;
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
                if (!colours[it->second].parse(line)) {
                    ostringstream oss;
                    oss << conf_path << ":" << lineno
                        << ": unable to parse colour '" << line << "'";
                    reporter->warnx(oss.str().c_str());
                }
            } else {
                ostringstream oss;
                oss << conf_path << ":" << lineno
                    << ": unrecognised config directive '" << word << "'";
                reporter->warnx(oss.str().c_str());
            }
        }
    }

    wxColour get_colour(ColourId colour)
    {
        auto &rgba = colours[colour];
        return wxColour(255 * rgba.r, 255 * rgba.g, 255 * rgba.b, 255 * rgba.a);
    }
};
static Config config;

static wxColour alpha_combine(wxColour a, wxColour b)
{
    /*
     * General function to combine two wxColours, counting their
     * alpha, to give a single wxColour equivalent to blending a and
     * then b on top of an arbitrary input.
     *
     * For a given colour channel (red, green or blue), if a pixel
     * previously had value x, and then you blend value aV with alpha aA
     * on top of it, you end up with the value
     *
     *    aA aV + (1-aA) x
     *
     * Then if you blend value bV with alpha BA on top of _that_ you
     * get the value
     *
     *    bA bV + (1-bA) (aA aV + (1-aA) x)
     *
     * So now we want to recover a single pair (rV, rA) that is
     * equivalent to that, i.e. so that
     *
     *    rA rV + (1-rA) x = bA bV + (1-bA) (aA aV + (1-aA) x)
     *
     * Equating coefficients of x and solving, you get the formulae
     *
     *    rA = aA + bA - aA bA
     *    rV = (bA bV - (1-bA) aA aV) / rA
     */

    double aA = a.Alpha() / 255.0;
    double bA = b.Alpha() / 255.0;
    double rA = aA + bA - aA * bA;

    if (rA == 0)
        return wxColour(0, 0, 0, 0);

    wxColour result(0.5 + (bA * b.Red() - (1 - bA) * aA * a.Red()) / rA,
                    0.5 + (bA * b.Green() - (1 - bA) * aA * a.Green()) / rA,
                    0.5 + (bA * b.Blue() - (1 - bA) * aA * a.Blue()) / rA,
                    0.5 + 255 * rA);

    return result;
}

static void clear_menu(wxMenu *menu)
{
    while (menu->GetMenuItemCount() > 0) {
        wxMenuItem *item = menu->FindItemByPosition(0);
        menu->Destroy(item);
    }
}

template <typename Var> class TemporaryPointerAssignment {
    Var *varp;

  public:
    TemporaryPointerAssignment(Var &var, Var value) : varp(&var)
    {
        *varp = value;
    }
    ~TemporaryPointerAssignment() { *varp = nullptr; }
};

struct LogicalPos {
    unsigned column;
    uintmax_t y0, y1;
    unsigned char_index;

    LogicalPos operator+(int i) const
    {
        LogicalPos toret = *this;
        toret.char_index += i;
        return toret;
    }

    bool operator==(const LogicalPos &rhs) const
    {
        return (column == rhs.column && y0 == rhs.y0 && y1 == rhs.y1 &&
                char_index == rhs.char_index);
    }
    bool operator!=(const LogicalPos &rhs) const { return !(*this == rhs); }
};

class GuiTarmacBrowserApp : public wxApp {
    unique_ptr<Browser> br;

  public:
    bool OnInit() override;
    void make_trace_window();
};

wxSize edit_size(const string &s)
{
    wxMemoryDC temp_dc;
    wxSize size = temp_dc.GetTextExtent(s);
    size.SetHeight(-1); // edit boxes always want default height
    return size;
};

class TextViewWindow;

class TextViewCanvas : public wxWindow {
    TextViewWindow *parent;

    void erase_event(wxEraseEvent &event) {}
    void paint_event(wxPaintEvent &event);

    wxDC *curr_dc;

    struct TextWithCoords {
        wxArrayInt positions;
        wxSize size;
        int x, y;
        LogicalPos startpos;
    };
    vector<TextWithCoords> drawn_text;

    bool AcceptsFocus() const override { return true; }

    int add_text(int x, int y, ColourId fg, ColourId bg, const string &str,
                 LogicalPos startpos);

  public:
    TextViewCanvas(TextViewWindow *parent, wxSize size, bool show_scrollbar);

    void add_highlighted_line(int x, int y, const HighlightedLine &hl,
                              LogicalPos logpos, bool highlight);
    void add_regmem_text(int x, int y, const string &text, const string &type,
                         LogicalPos logpos);
    void add_separator_line(int y);
    void add_fold_control(int x, int y, bool folded);
    void add_leaf_node_marker(int x, int y);

    bool find_xy(int x, int y, LogicalPos &logpos);

    int width();
    int height();

    friend class TextViewWindow;
};

class TextViewWindow : public wxFrame {
  protected:
    GuiTarmacBrowserApp *app;

    unsigned defrows, defcols;

    // wintop indicates where the top of the displayed region is, in
    // _visible_ lines.
    int wintop;

    bool show_scrollbar;

    TextViewCanvas *drawing_area;

    TextViewWindow(GuiTarmacBrowserApp *app, unsigned defcols, unsigned defrows,
                   bool show_scrollbar);
    virtual ~TextViewWindow() = default;

    void set_scrollbar();
    void set_title(string title);

    virtual unsigned n_display_lines() = 0;

    wxMenuBar *menubar;
    wxMenu *filemenu, *editmenu;

    wxToolBar *toolbar;
    wxTextCtrl *lineedit;

    void set_lineedit(unsigned line);

    wxMenu *contextmenu;

    virtual bool prepare_context_menu(const LogicalPos &logpos)
    {
        return false;
    }

  private:
    void close_menuaction(wxCommandEvent &event);
    void exit_menuaction(wxCommandEvent &event);
    void copy_menuaction(wxCommandEvent &event);
    void mouse_out_of_canvas(wxMouseEvent &event);

    void left_down(wxMouseEvent &event);
    void mouse_move(wxMouseEvent &event);
    void left_up(wxMouseEvent &event);
    void right_down(wxMouseEvent &event);
    void scroll(wxScrollWinEvent &event);

    virtual bool keypress(wxKeyEvent &event) { return false; }
    void key_event(wxKeyEvent &event);

    void lineedit_activated(wxCommandEvent &event);
    void lineedit_unfocused(wxFocusEvent &event);
    virtual void reset_lineedit() = 0;
    virtual void activate_lineedit(unsigned line) = 0;

    virtual void redraw_canvas(unsigned line_start, unsigned line_limit) = 0;

    friend class TextViewCanvas;

  public:
    static wxFont font;
    static int char_width;
    static int baseline, line_height;

  protected:
    struct InWindowControl {
        int x, y, w, h;
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

    virtual PosDistance logpos_dist(const LogicalPos &lhs,
                                    const LogicalPos &rhs) = 0;
    int logpos_cmp(const LogicalPos &lhs, const LogicalPos &rhs)
    {
        return logpos_dist(lhs, rhs).sign();
    }

    virtual void rewrite_selection_endpoints(LogicalPos &anchor,
                                             LogicalPos &cursor)
    {
    }

    virtual void clipboard_get_paste_data(ostream &os, LogicalPos start,
                                          LogicalPos end) = 0;

    virtual void click(const LogicalPos &logpos, const wxMouseEvent &event,
                       const wxPoint &p)
    {
    }

    enum class SelectionState {
        None,
        MouseDown,
        Dragging,
        Selected
    } selection_state;
    LogicalPos selection_anchor, selection_start, selection_end;

    void set_selection_state(SelectionState state)
    {
        selection_state = state;
        menubar->Enable(wxID_COPY, selection_state == SelectionState::Selected);
    }

    bool mouseover_valid = false;
    LogicalPos mouseover_start, mouseover_end;
    virtual void update_mouseover(const LogicalPos &) {}

    double text_darken(const LogicalPos &p);

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

    string selected_text;
};

TextViewCanvas::TextViewCanvas(TextViewWindow *parent_, wxSize size,
                               bool show_scrollbar)
    : wxWindow(parent_, wxID_ANY, wxDefaultPosition, size,
               (show_scrollbar ? wxVSCROLL | wxALWAYS_SHOW_SB : 0))
{
    parent = parent_;

    Bind(wxEVT_ERASE_BACKGROUND, &TextViewCanvas::erase_event, this);
    Bind(wxEVT_PAINT, &TextViewCanvas::paint_event, this);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
}

int TextViewCanvas::width()
{
    int client_width, client_height;
    GetClientSize(&client_width, &client_height);
    return client_width;
}

int TextViewCanvas::height()
{
    int client_width, client_height;
    GetClientSize(&client_width, &client_height);
    return client_height;
}

void TextViewCanvas::paint_event(wxPaintEvent &event)
{
    wxBufferedPaintDC dc(this);
    PrepareDC(dc);
    dc.SetBrush(*wxTheBrushList->FindOrCreateBrush(GetBackgroundColour()));
    dc.Clear();
#if wxUSE_GRAPHICS_CONTEXT
    wxGCDC gdc(dc);
    TemporaryPointerAssignment<decltype(curr_dc)> tpa(curr_dc, &gdc);
#else
    TemporaryPointerAssignment<decltype(curr_dc)> tpa(curr_dc, &dc);
#endif
    curr_dc->SetFont(parent->font);

    int lines_to_redraw =
        (height() + parent->line_height - 1) / parent->line_height;

    drawn_text.clear();
    parent->redraw_canvas(parent->wintop, parent->wintop + lines_to_redraw);
}

double TextViewWindow::text_darken(const LogicalPos &p)
{
    double level = 0.0;

    if (mouseover_active() && logpos_cmp(p, mouseover_start) >= 0 &&
        logpos_cmp(p, mouseover_end) <= 0)
        level = 0.25; // slightly dark for a mouseover

    if ((selection_state == SelectionState::Selected ||
         selection_state == SelectionState::Dragging) &&
        logpos_cmp(p, selection_start) >= 0 &&
        logpos_cmp(p, selection_end) <= 0)
        level = 0.5; // darker still for selected text

    return level;
}

int TextViewCanvas::add_text(int x, int y, ColourId fg, ColourId bg,
                             const string &str, LogicalPos startpos)
{
    assert(curr_dc);

    wxSize size = curr_dc->GetTextExtent(str);
    wxArrayInt widths;
    bool success = curr_dc->GetPartialTextExtents(str, widths);
    wxASSERT_MSG(success, wxT("GetPartialTextExtents failed"));

    curr_dc->SetTextForeground(config.get_colour(fg));
    wxColour bgColour = config.get_colour(bg);
    curr_dc->SetBackgroundMode(wxBRUSHSTYLE_SOLID);
    for (size_t i = 0, e = str.size(); i < e;) {
        double level = parent->text_darken(startpos + i);
        curr_dc->SetTextBackground(
            alpha_combine(bgColour, wxColour(0, 0, 0, 255 * level)));
        size_t start = i++;
        while (i < e && parent->text_darken(startpos + i) == level)
            i++;
        curr_dc->DrawText(str.substr(start, i - start),
                          x + (start ? widths[start - 1] : 0), y);
    }

    drawn_text.push_back({widths, size, x, y, startpos});

    return size.GetWidth();
}

void TextViewCanvas::add_highlighted_line(int x, int y,
                                          const HighlightedLine &hl,
                                          LogicalPos logpos, bool highlight)
{
    for (size_t i = 0; i < hl.display_len;) {
        HighlightClass hc = hl.highlight_at(i, highlight);

        size_t start = i++;
        while (i < hl.display_len && hl.highlight_at(i, highlight) == hc)
            i++;
        size_t len = i - start;

        logpos.char_index = start;
        x += add_text(x, y, hc, ColourId::AreaBackground,
                      hl.text.substr(start, len), logpos);
    }
}

void TextViewCanvas::add_regmem_text(int x, int y, const string &text,
                                     const string &type, LogicalPos logpos)
{
    assert(text.size() == type.size());

    for (size_t i = 0, e = text.size(); i < e;) {
        char hc = type[i];

        size_t start = i++;
        while (i < e && type[i] == hc)
            i++;
        size_t len = i - start;

        ColourId bg = ColourId::AreaBackground;
        if (isupper((unsigned char)hc)) {
            bg = ColourId::DiffBackground;
            hc = tolower((unsigned char)hc);
        }

        logpos.char_index = start;
        x += add_text(x, y, hc, bg, text.substr(start, len), logpos);
    }
}

void TextViewCanvas::add_separator_line(int y)
{
    assert(curr_dc);
    curr_dc->SetPen(*wxThePenList->FindOrCreatePen(
        config.get_colour(ColourId::PositionMarker),
        1 /* FIXME: underline thickness? */));
    curr_dc->DrawLine(0, y, width(), y);
}

void TextViewCanvas::add_fold_control(int x, int y, bool folded)
{
    assert(curr_dc);
    curr_dc->SetPen(*wxThePenList->FindOrCreatePen(
        config.get_colour(folded ? ColourId::FoldButton
                                 : ColourId::UnfoldButton),
        1));
    curr_dc->SetBrush(wxNullBrush);

    int hs = parent->char_width / 2; // half side length of square
    int ha = hs * 0.7;               // half arm length of +
    int cx = x + hs;
    int cy = y + parent->baseline / 2;

    curr_dc->DrawRectangle(cx - hs, cy - hs, 2 * hs + 1, 2 * hs + 1);
    curr_dc->DrawLine(cx - ha, cy, cx + ha, cy);
    if (folded)
        curr_dc->DrawLine(cx, cy - ha, cx, cy + ha);
}

void TextViewCanvas::add_leaf_node_marker(int x, int y)
{
    assert(curr_dc);
    curr_dc->SetPen(wxNullPen);
    curr_dc->SetBrush(*wxTheBrushList->FindOrCreateBrush(
        config.get_colour(ColourId::LeafNode)));
    curr_dc->DrawCircle(x + parent->char_width / 2, y + parent->baseline / 2,
                        parent->char_width * 0.35);
}

bool TextViewCanvas::find_xy(int x, int y, LogicalPos &logpos)
{
    bool got_candidate = false;
    int current_excess;

    size_t i = 0;
    for (auto &twc : drawn_text) {
        int xi = x - twc.x, yi = y - twc.y;
        if (!(0 <= xi /* && xi < twc.size.GetWidth() */ && 0 <= yi &&
              yi < twc.size.GetHeight()))
            continue;

        auto it =
            std::upper_bound(twc.positions.begin(), twc.positions.end(), xi);
        size_t index = it - twc.positions.begin();
        int excess;
        if (it != twc.positions.end()) {
            excess = 0;
        } else {
            excess = xi - twc.size.GetWidth();
        }

        if (!got_candidate || current_excess > excess) {
            logpos = twc.startpos;
            logpos.char_index += index;
            if (excess == 0)
                return true; // no need to look further
            current_excess = excess;
            got_candidate = true;
        }
        i++;
    }

    return got_candidate;
}

TextViewWindow::TextViewWindow(GuiTarmacBrowserApp *app, unsigned defcols,
                               unsigned defrows, bool show_scrollbar)
    : wxFrame(NULL, wxID_ANY, "", wxDefaultPosition, wxDefaultSize), app(app),
      defrows(defrows), defcols(defcols), wintop(0),
      show_scrollbar(show_scrollbar)
{
    menubar = new wxMenuBar;
    filemenu = new wxMenu;
    menubar->Append(filemenu, wxGetStockLabel(wxID_FILE));
    {
        wxAcceleratorEntry accel(wxACCEL_CTRL, (int)'W', wxID_CLOSE);
        filemenu->Append(wxID_CLOSE)->SetAccel(&accel);
    }
    {
        wxAcceleratorEntry accel(wxACCEL_CTRL, (int)'Q', wxID_CLOSE);
        filemenu->Append(wxID_EXIT)->SetAccel(&accel);
    }

    editmenu = new wxMenu;
    menubar->Append(editmenu, wxGetStockLabel(wxID_EDIT));
    editmenu->Append(wxID_COPY);

    SetMenuBar(menubar);

    set_selection_state(SelectionState::None);

    Bind(wxEVT_MENU, &TextViewWindow::close_menuaction, this, wxID_CLOSE);
    Bind(wxEVT_MENU, &TextViewWindow::exit_menuaction, this, wxID_EXIT);
    Bind(wxEVT_MENU, &TextViewWindow::copy_menuaction, this, wxID_COPY);

    toolbar = CreateToolBar();
    toolbar->AddControl(new wxStaticText(toolbar, wxID_ANY, "Line: "));
    lineedit =
        new wxTextCtrl(toolbar, wxID_ANY, wxEmptyString, wxDefaultPosition,
                       edit_size("1234567890123"), wxTE_PROCESS_ENTER);
    toolbar->AddControl(lineedit);

    lineedit->Bind(wxEVT_TEXT_ENTER, &TextViewWindow::lineedit_activated, this);
    lineedit->Bind(wxEVT_KILL_FOCUS, &TextViewWindow::lineedit_unfocused, this);

    contextmenu = new wxMenu;

    drawing_area = new TextViewCanvas(
        this, wxSize(defcols * char_width, defrows * line_height),
        show_scrollbar);
    drawing_area->Bind(wxEVT_LEFT_DOWN, &TextViewWindow::left_down, this);
    drawing_area->Bind(wxEVT_RIGHT_DOWN, &TextViewWindow::right_down, this);
    drawing_area->Bind(wxEVT_MOTION, &TextViewWindow::mouse_move, this);
    drawing_area->Bind(wxEVT_LEFT_UP, &TextViewWindow::left_up, this);
    drawing_area->Bind(wxEVT_LEAVE_WINDOW, &TextViewWindow::mouse_out_of_canvas,
                       this);
    for (auto evt : {wxEVT_SCROLLWIN_TOP, wxEVT_SCROLLWIN_BOTTOM,
                     wxEVT_SCROLLWIN_LINEUP, wxEVT_SCROLLWIN_LINEDOWN,
                     wxEVT_SCROLLWIN_PAGEUP, wxEVT_SCROLLWIN_PAGEDOWN,
                     wxEVT_SCROLLWIN_THUMBTRACK, wxEVT_SCROLLWIN_THUMBRELEASE})
        drawing_area->Bind(evt, &TextViewWindow::scroll, this);

    drawing_area->Bind(wxEVT_KEY_DOWN, &TextViewWindow::key_event, this);

    auto *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(drawing_area, wxSizerFlags().Expand());
    SetSizerAndFit(sizer);

    drawing_area->SetFocus();
}

void TextViewWindow::close_menuaction(wxCommandEvent &event) { Close(true); }

void TextViewWindow::exit_menuaction(wxCommandEvent &event)
{
    app->ExitMainLoop();
}

void TextViewWindow::left_down(wxMouseEvent &event)
{
    wxPoint p;
    {
        wxClientDC dc(drawing_area);
        p = event.GetLogicalPosition(dc);
    }

    drawing_area->SetFocus();

    for (auto &control : controls) {
        int dx = p.x - control.x, dy = p.y - control.y;
        if (0 <= dx && dx < control.w && 0 <= dy && dy < control.h) {
            control.callback();
            set_scrollbar();
            drawing_area->Refresh();
            return;
        }
    }

    LogicalPos logpos;
    if (drawing_area->find_xy(p.x, p.y, logpos)) {
        if (event.ShiftDown() && selection_state == SelectionState::Selected) {
            // Shift + left-click extends the previous selection,
            // by moving whichever of its endpoints was nearer the
            // click, and setting selection_anchor to the other one.
            set_selection_state(SelectionState::Dragging);
            PosDistance ds = logpos_dist(logpos, selection_start);
            PosDistance de = logpos_dist(selection_end, logpos);
            if (ds.sign() <= 0 || (de.sign() > 0 && ds < de)) {
                selection_start = logpos;
                selection_anchor = selection_end;
            } else {
                selection_end = logpos;
                selection_anchor = selection_start;
            }
        } else {
            // Otherwise, a left click (even with Shift) drops an
            // anchor so that dragging can start a new selection.
            set_selection_state(SelectionState::MouseDown);
            selection_anchor = logpos;
        }
        drawing_area->Refresh();
    }
}

void TextViewWindow::mouse_move(wxMouseEvent &event)
{
    wxPoint p;
    {
        wxClientDC dc(drawing_area);
        p = event.GetLogicalPosition(dc);
    }

    LogicalPos logpos;
    if (!drawing_area->find_xy(p.x, p.y, logpos))
        return;

    if (event.Dragging()) {
        // Drag.
        if (selection_state == SelectionState::MouseDown &&
            logpos != selection_anchor) {
            set_selection_state(SelectionState::Dragging);
        }
        if (selection_state == SelectionState::Dragging) {
            LogicalPos anchor = selection_anchor;
            rewrite_selection_endpoints(anchor, logpos);
            if (logpos_cmp(anchor, logpos) > 0)
                swap(anchor, logpos);
            if (selection_start != anchor || selection_end != logpos) {
                selection_start = anchor;
                selection_end = logpos;
                drawing_area->Refresh();
            }
        }
    } else {
        // Non-dragging mouse motion.
        bool prev_active = mouseover_active();
        LogicalPos prev_start = mouseover_start, prev_end = mouseover_end;
        update_mouseover(logpos);
        bool new_active = mouseover_active();
        LogicalPos new_start = mouseover_start, new_end = mouseover_end;
        if (prev_active != new_active ||
            (new_active && (new_start != prev_start || new_end != prev_end)))
            drawing_area->Refresh();
    }
}

void TextViewWindow::mouse_out_of_canvas(wxMouseEvent &event)
{
    if (mouseover_valid) {
        mouseover_valid = false;
        drawing_area->Refresh();
    }
}

static void write_clipboard(bool x11_primary, const string &text)
{
    wxTheClipboard->UsePrimarySelection(x11_primary);
    if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(text));
        wxTheClipboard->Close();
    }
    wxTheClipboard->UsePrimarySelection(false);
}

void TextViewWindow::left_up(wxMouseEvent &event)
{
    if (selection_state == SelectionState::MouseDown) {
        set_selection_state(SelectionState::None);

        wxPoint p;
        {
            wxClientDC dc(drawing_area);
            p = event.GetLogicalPosition(dc);
        }

        LogicalPos logpos;
        if (drawing_area->find_xy(p.x, p.y, logpos))
            click(logpos, event, p);

        drawing_area->Refresh();
    } else if (selection_state == SelectionState::Dragging) {
        set_selection_state(SelectionState::Selected);

        ostringstream oss;
        clipboard_get_paste_data(oss, selection_start, selection_end);
        selected_text = oss.str();
        write_clipboard(true, selected_text);
    }
}

void TextViewWindow::copy_menuaction(wxCommandEvent &event)
{
    if (selection_state == SelectionState::Selected)
        write_clipboard(false, selected_text);
}

void TextViewWindow::key_event(wxKeyEvent &event)
{
    // Call subclass's virtual handler method to actually handle the
    // keystroke. If it says it did, then stop there.
    if (keypress(event))
        return;

    // Ensure unhandled keypresses are also turned into menu accelerators
    event.Skip();
}

void TextViewWindow::right_down(wxMouseEvent &event)
{
    wxPoint p;
    {
        wxClientDC dc(drawing_area);
        p = event.GetLogicalPosition(dc);
    }

    LogicalPos logpos;
    if (drawing_area->find_xy(p.x, p.y, logpos)) {
        if (prepare_context_menu(logpos))
            PopupMenu(contextmenu);
    }
}

void TextViewWindow::set_scrollbar()
{
    if (show_scrollbar) {
        drawing_area->SetScrollbar(wxVERTICAL, wintop,
                                   drawing_area->height() / line_height,
                                   n_display_lines());
    }
}

void TextViewWindow::scroll(wxScrollWinEvent &event)
{
    wintop = event.GetPosition();
    drawing_area->Refresh();
}

void TextViewWindow::set_title(string title)
{
    title += " \xe2\x80\x93 tarmac-browser";
    SetTitle(wxString::FromUTF8(title.c_str(), title.size()));
}

auto TextViewWindow::logpos_dist_rowmajor(const LogicalPos &lhs,
                                          const LogicalPos &rhs) -> PosDistance
{
    PosDistance d;
    d.dists[0] = (int64_t)lhs.y0 - (int64_t)rhs.y0;
    d.dists[1] = (int64_t)lhs.y1 - (int64_t)rhs.y1;
    d.dists[2] = (int64_t)lhs.column - (int64_t)rhs.column;
    d.dists[3] = (int64_t)lhs.char_index - (int64_t)rhs.char_index;
    return d;
}

auto TextViewWindow::logpos_dist_colmajor(const LogicalPos &lhs,
                                          const LogicalPos &rhs) -> PosDistance
{
    PosDistance d;
    d.dists[0] = (int64_t)lhs.column - (int64_t)rhs.column;
    d.dists[1] = (int64_t)lhs.y0 - (int64_t)rhs.y0;
    d.dists[2] = (int64_t)lhs.y1 - (int64_t)rhs.y1;
    d.dists[3] = (int64_t)lhs.char_index - (int64_t)rhs.char_index;
    return d;
}

void TextViewWindow::lineedit_activated(wxCommandEvent &event)
{
    string value = lineedit->GetValue().ToStdString();
    unsigned line;

    try {
        line = stoul(value);
    } catch (invalid_argument) {
        return;
    } catch (out_of_range) {
        return;
    }

    activate_lineedit(line);
    drawing_area->SetFocus();
}

void TextViewWindow::lineedit_unfocused(wxFocusEvent &event)
{
    reset_lineedit();
}

void TextViewWindow::set_lineedit(unsigned line)
{
    ostringstream oss;
    oss << line;
    lineedit->SetValue(oss.str());
}

class TraceWindow;
class MemPromptDialog;

class SubsidiaryView;
class SubsidiaryViewListNode : public ListNode<SubsidiaryView> {
};
class SubsidiaryView : public TextViewWindow, public SubsidiaryViewListNode {
  public:
    Browser &br;

    static vector<SubsidiaryView *> all_subviews;
    static set<unsigned> all_subviews_free_indices;
    unsigned all_subviews_our_index;
    static void close_all();

    off_t memroot = 0, diff_memroot = 0;
    unsigned line, diff_minline;

    TraceWindow *tw;
    wxChoice *linkcombo;

    SubsidiaryView(GuiTarmacBrowserApp *app, pair<unsigned, unsigned> wh,
                   bool sb, Browser &br, TraceWindow *tw);
    virtual ~SubsidiaryView();

    virtual void reset_lineedit() override { set_lineedit(line); }
    virtual void activate_lineedit(unsigned line_) override
    {
        SeqOrderPayload node;
        if (br.node_at_line(line_, &node)) {
            tw = nullptr;
            unlink_if_linked();
            update_trace_window_list();
            update_line(node.memory_root, node.trace_file_firstline);
        }
    }

    virtual void memroot_changed() { }

    void update_line(off_t memroot_, unsigned line_)
    {
        memroot = memroot_;
        line = line_;
        memroot_changed();
        reset_lineedit();
        drawing_area->Refresh();

        // clear previous diff highlighting
        diff_memroot = 0;
        diff_minline = 0;
    }

    void diff_against(off_t diff_memroot_, unsigned diff_minline_)
    {
        diff_memroot = diff_memroot_;
        diff_minline = diff_minline_;
    }

    void linkcombo_changed(wxCommandEvent &event);
    void update_trace_window_list();
    static void update_all_trace_window_lists();
};

vector<SubsidiaryView *> SubsidiaryView::all_subviews;
set<unsigned> SubsidiaryView::all_subviews_free_indices;

class RegisterWindow : public SubsidiaryView {
    unsigned cols, colwid, rows;

    virtual unsigned n_display_lines() override { return rows; }

    virtual void redraw_canvas(unsigned line_start,
                               unsigned line_limit) override;

    virtual bool prepare_context_menu(const LogicalPos &logpos) override;

    void provenance_query(RegisterId r);

    RegisterId context_menu_reg;
    wxWindowID mi_ctx_provenance;
    void provenance_menuaction(wxCommandEvent &event)
    {
        provenance_query(context_menu_reg);
    }

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
    RegisterWindow(GuiTarmacBrowserApp *app, const vector<RegisterId> &regs,
                   Browser &br, TraceWindow *tw);
};

class RegisterWindow32 : public RegisterWindow {
  public:
    RegisterWindow32(GuiTarmacBrowserApp *app, Browser &br, TraceWindow *tw);
};
class RegisterWindow64 : public RegisterWindow {
  public:
    RegisterWindow64(GuiTarmacBrowserApp *app, Browser &br, TraceWindow *tw);
};
class RegisterWindowSP : public RegisterWindow {
  public:
    RegisterWindowSP(GuiTarmacBrowserApp *app, Browser &br, TraceWindow *tw);
};
class RegisterWindowDP : public RegisterWindow {
  public:
    RegisterWindowDP(GuiTarmacBrowserApp *app, Browser &br, TraceWindow *tw);
};
class RegisterWindowNeon : public RegisterWindow {
  public:
    RegisterWindowNeon(GuiTarmacBrowserApp *app, Browser &br, TraceWindow *tw,
                       bool aarch64);
};
class RegisterWindowMVE : public RegisterWindow {
  public:
    RegisterWindowMVE(GuiTarmacBrowserApp *app, Browser &br, TraceWindow *tw);
};

class MemoryWindow : public SubsidiaryView {
    wxTextCtrl *addredit;
    void reset_addredit();
    void addredit_activated(wxCommandEvent &event);
    void addredit_unfocused(wxFocusEvent &event);

    virtual unsigned n_display_lines() override;
    virtual void redraw_canvas(unsigned line_start,
                               unsigned line_limit) override;
    virtual bool prepare_context_menu(const LogicalPos &logpos) override;
    virtual bool keypress(wxKeyEvent &event) override;
    virtual void rewrite_selection_endpoints(LogicalPos &anchor,
                                             LogicalPos &cursor) override;
    virtual void clipboard_get_paste_data(ostream &os, LogicalPos start,
                                          LogicalPos end) override;

    int mousewheel_accumulator = 0;
    void mousewheel(wxMouseEvent &event);

    void provenance_query(Addr start, Addr size);

    pair<unsigned, unsigned> compute_size(int bpl, bool sfb);

    unsigned byte_index(const LogicalPos &pos);

    Addr context_menu_addr, context_menu_size;
    wxWindowID mi_ctx_provenance;
    void provenance_menuaction(wxCommandEvent &event)
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

    void set_start_addr_expr(ExprPtr expr, const string &exprstr);
    void compute_start_addr();
    virtual void memroot_changed() override;

  protected:
    int addr_chars, bytes_per_line;
    Addr start_addr;
    bool start_addr_known;
    string start_addr_exprstr;
    ExprPtr start_addr_expr;

  public:
    struct StartAddr {
        // If this code base could assume C++17, it would be nicer to
        // make this a std::variant. The point is that it holds
        // _either_ an expression and its string form (indicated by
        // expr not being null) _or_ a constant (if expr is null).
        ExprPtr expr;
        string exprstr;
        Addr constant;
        StartAddr() : expr(nullptr), constant(0) {}
        StartAddr(Addr addr) : expr(nullptr), constant(addr) {}
        bool parse(const string &s, Browser &br, ostringstream &error);
    };

    MemoryWindow(GuiTarmacBrowserApp *app, StartAddr addr, int bpl,
                 bool sfb, Browser &br, TraceWindow *tw);
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

    void update_location(UpdateLocationType type);
    void keep_visnode_in_view(bool strict_centre = false);

    virtual void click(const LogicalPos &logpos, const wxMouseEvent &event,
                       const wxPoint &p) override;

    void fold_ui_action(const FunctionRange &fnrange, ContainerFoldType type);
    void unfold_ui_action(const FunctionRange &fnrange, bool full);
    void fold_all_ui_action(bool fold);

    unsigned n_display_lines() override;
    void redraw_canvas(unsigned line_start, unsigned line_limit) override;

    virtual bool prepare_context_menu(const LogicalPos &logpos) override;

    virtual PosDistance logpos_dist(const LogicalPos &lhs,
                                    const LogicalPos &rhs) override
    {
        return logpos_dist_rowmajor(lhs, rhs);
    }

    virtual void clipboard_get_paste_data(ostream &os, LogicalPos start,
                                          LogicalPos end) override;

    wxWindowID mi_calldepth, mi_highlight, mi_branchtarget;
    void newtrace_menuaction(wxCommandEvent &event);
    void newmem_menuaction(wxCommandEvent &event);
    void newstk_menuaction(wxCommandEvent &event);
    void newcorereg_menuaction(wxCommandEvent &event);
    void newspreg_menuaction(wxCommandEvent &event);
    void newdpreg_menuaction(wxCommandEvent &event);
    void newneonreg_menuaction(wxCommandEvent &event);
    void newmvereg_menuaction(wxCommandEvent &event);
    void recentre_menuaction(wxCommandEvent &event);
    void calldepth_menuaction(wxCommandEvent &event);
    void highlight_menuaction(wxCommandEvent &event);
    void branchtarget_menuaction(wxCommandEvent &event);

    MemPromptDialog *mem_prompt_dialog = nullptr;
    void mem_prompt_dialog_ended(bool ok);
    void mem_prompt_dialog_button(wxCommandEvent &event);
    void mem_prompt_dialog_closed(wxCloseEvent &event);

    virtual void reset_lineedit() override;
    virtual void activate_lineedit(unsigned line) override;

    wxWindowID mi_fold_all;
    wxWindowID mi_unfold_all;
    wxWindowID mi_fold;
    wxWindowID mi_fold_subrs;
    wxWindowID mi_unfold_container;
    wxWindowID mi_unfold;
    wxWindowID mi_unfold_full;
    wxWindowID mi_provenance;
    wxWindowID mi_contextmem;

    void fold_all_menuaction(wxCommandEvent &event)
    {
        fold_all_ui_action(true);
    }
    void unfold_all_menuaction(wxCommandEvent &event)
    {
        fold_all_ui_action(false);
    }
    void fold_menuaction(wxCommandEvent &event)
    {
        fold_ui_action(container_fnrange, ContainerFoldType::Fold);
    }
    void fold_subrs_menuaction(wxCommandEvent &event)
    {
        fold_ui_action(container_fnrange, ContainerFoldType::FoldSubrs);
    }
    void unfold_container_menuaction(wxCommandEvent &event)
    {
        fold_ui_action(container_fnrange, ContainerFoldType::Unfold);
    }
    void unfold_menuaction(wxCommandEvent &event)
    {
        unfold_ui_action(callee_fnrange, false);
    }
    void unfold_full_menuaction(wxCommandEvent &event)
    {
        unfold_ui_action(callee_fnrange, true);
    }
    void provenance_menuaction(wxCommandEvent &event);
    void contextmem_menuaction(wxCommandEvent &event);

    char context_menu_memtype;
    Addr context_menu_start, context_menu_size;
    off_t context_menu_memroot;

    wxTextCtrl *timeedit, *pcedit;
    void timeedit_activated(wxCommandEvent &event);
    void timeedit_unfocused(wxFocusEvent &event);
    void set_timeedit(Time time);
    void reset_timeedit();
    void pcedit_activated(wxCommandEvent &event);
    void pcedit_unfocused(wxFocusEvent &event);
    void reset_pcedit();
    template <int direction> void pc_nextprev(wxCommandEvent &event);

    virtual bool keypress(wxKeyEvent &event) override;

    SubsidiaryViewListNode subview_list;
    void update_subviews();
    void tell_subviews_to_diff_against(off_t memroot, unsigned line);

  public:
    TraceWindow(GuiTarmacBrowserApp *app, Browser &br);
    ~TraceWindow();

    void goto_physline(unsigned line);
    void add_subview(SubsidiaryView *sv);
};

// We number windows from 1, for vague user-friendliness
vector<TraceWindow *> TraceWindow::all_trace_windows{nullptr};
set<unsigned> TraceWindow::all_trace_windows_free_indices;

SubsidiaryView::SubsidiaryView(GuiTarmacBrowserApp *app,
                               pair<unsigned, unsigned> wh, bool sb,
                               Browser &br, TraceWindow *tw)
    : TextViewWindow(app, wh.first, wh.second, sb), br(br), tw(tw)
{
    toolbar->AddControl(new wxStaticText(toolbar, wxID_ANY, "Locked to: "));
    linkcombo = new wxChoice(toolbar, wxID_ANY, wxDefaultPosition,
                             edit_size("1234567890"), 0, nullptr);
    toolbar->AddControl(linkcombo);
    linkcombo->Bind(wxEVT_CHOICE, &SubsidiaryView::linkcombo_changed, this);
    toolbar->Realize();

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

void SubsidiaryView::linkcombo_changed(wxCommandEvent &event)
{
    int which = linkcombo->GetCurrentSelection();
    intptr_t data = reinterpret_cast<intptr_t>(linkcombo->GetClientData(which));
    if (data != -1) {
        uintptr_t new_index = data;
        assert(new_index < TraceWindow::all_trace_windows.size());
        TraceWindow *new_tw = TraceWindow::all_trace_windows[new_index];
        assert(new_tw);
        unlink_if_linked();
        tw = new_tw;
        tw->add_subview(this);
    } else {
        unlink_if_linked();
    }
}

void SubsidiaryView::update_trace_window_list()
{
    linkcombo->Clear();
    linkcombo->Append("None", reinterpret_cast<void *>(-1));
    int curr_id = 0;
    int id = curr_id++;
    for (auto *tw : TraceWindow::all_trace_windows) {
        if (tw) {
            ostringstream oss;
            oss << "#" << tw->window_index;
            linkcombo->Append(oss.str(), reinterpret_cast<void *>(
                                  static_cast<intptr_t>(tw->window_index)));
            if (tw == this->tw)
                id = curr_id;
            curr_id++;
        }
    }
    linkcombo->SetSelection(id);
}

TraceWindow::TraceWindow(GuiTarmacBrowserApp *app, Browser &br)
    : TextViewWindow(app, 80, 50, true), br(br), vu(br), container_fnrange(vu),
      callee_fnrange(vu)
{
    wxWindowID mi_newtrace = NewControlId();
    wxWindowID mi_newmem = NewControlId();
    wxWindowID mi_newstk = NewControlId();
    wxWindowID mi_newcorereg = NewControlId();
    wxWindowID mi_newspreg = NewControlId();
    wxWindowID mi_newdpreg = NewControlId();
    wxWindowID mi_newneonreg = NewControlId();
    wxWindowID mi_newmvereg = NewControlId();
    wxWindowID mi_recentre = NewControlId();
    mi_calldepth = NewControlId();
    mi_highlight = NewControlId();
    mi_branchtarget = wxID_NONE;

    size_t pos = 0;
    {
        wxAcceleratorEntry accel(wxACCEL_CTRL, (int)'N', wxID_CLOSE);
        filemenu->Insert(pos++, mi_newtrace, "New trace view")
            ->SetAccel(&accel);
    }
    wxMenu *newmenu = new wxMenu;
    filemenu->Insert(pos++, wxID_ANY, "New...", newmenu);
    {
        wxAcceleratorEntry accel(wxACCEL_CTRL, (int)'M', wxID_CLOSE);
        newmenu->Append(mi_newmem, "Memory view")->SetAccel(&accel);
    }
    newmenu->Append(mi_newstk, "Stack view");
    newmenu->Append(mi_newcorereg, "Core register view");
    newmenu->Append(mi_newspreg, "Single-precision FP reg view");
    newmenu->Append(mi_newdpreg, "Double-precision FP reg view");
    newmenu->Append(mi_newneonreg, "Neon vector reg view");
    newmenu->Append(mi_newmvereg, "MVE vector reg view");
    filemenu->InsertSeparator(pos++);

    wxMenu *viewmenu = new wxMenu;
    menubar->Append(viewmenu, "&View");
    {
        wxAcceleratorEntry accel(wxACCEL_CTRL, (int)'L', wxID_CLOSE);
        viewmenu->Append(mi_recentre, "Re-centre")->SetAccel(&accel);
    }
    viewmenu->AppendSeparator();
    viewmenu->AppendCheckItem(mi_calldepth, "Call-depth indentation");
    viewmenu->AppendCheckItem(mi_highlight, "Syntax highlighting");
    if (br.has_image()) {
        mi_branchtarget = NewControlId();
        viewmenu->AppendCheckItem(mi_branchtarget, "Symbolic branch targets");
    }

    Bind(wxEVT_MENU, &TraceWindow::newtrace_menuaction, this, mi_newtrace);
    Bind(wxEVT_MENU, &TraceWindow::newmem_menuaction, this, mi_newmem);
    Bind(wxEVT_MENU, &TraceWindow::newstk_menuaction, this, mi_newstk);
    Bind(wxEVT_MENU, &TraceWindow::newcorereg_menuaction, this, mi_newcorereg);
    Bind(wxEVT_MENU, &TraceWindow::newspreg_menuaction, this, mi_newspreg);
    Bind(wxEVT_MENU, &TraceWindow::newdpreg_menuaction, this, mi_newdpreg);
    Bind(wxEVT_MENU, &TraceWindow::newneonreg_menuaction, this, mi_newneonreg);
    Bind(wxEVT_MENU, &TraceWindow::newmvereg_menuaction, this, mi_newmvereg);
    Bind(wxEVT_MENU, &TraceWindow::recentre_menuaction, this, mi_recentre);
    Bind(wxEVT_MENU, &TraceWindow::calldepth_menuaction, this, mi_calldepth);
    Bind(wxEVT_MENU, &TraceWindow::highlight_menuaction, this, mi_highlight);
    if (mi_branchtarget != wxID_NONE)
        Bind(wxEVT_MENU, &TraceWindow::branchtarget_menuaction, this,
             mi_branchtarget);

    menubar->Check(mi_calldepth, depth_indentation);
    menubar->Check(mi_highlight, syntax_highlight);
    if (mi_branchtarget != wxID_NONE)
        menubar->Check(mi_branchtarget, substitute_branch_targets);

    toolbar->AddControl(new wxStaticText(toolbar, wxID_ANY, "Time: "));
    timeedit =
        new wxTextCtrl(toolbar, wxID_ANY, wxEmptyString, wxDefaultPosition,
                       edit_size("1234567890123"), wxTE_PROCESS_ENTER);
    toolbar->AddControl(timeedit);
    toolbar->AddControl(new wxStaticText(toolbar, wxID_ANY, "PC: "));
    pcedit = new wxTextCtrl(toolbar, wxID_ANY, wxEmptyString, wxDefaultPosition,
                            edit_size("0x12345678 = longish_symbol_name"),
                            wxTE_PROCESS_ENTER);
    toolbar->AddControl(pcedit);
    auto pcprev_button = new wxButton(toolbar, wxID_ANY, "Prev");
    toolbar->AddControl(pcprev_button);
    auto pcnext_button = new wxButton(toolbar, wxID_ANY, "Next");
    toolbar->AddControl(pcnext_button);
    toolbar->Realize();

    timeedit->Bind(wxEVT_TEXT_ENTER, &TraceWindow::timeedit_activated, this);
    timeedit->Bind(wxEVT_KILL_FOCUS, &TraceWindow::timeedit_unfocused, this);
    pcedit->Bind(wxEVT_TEXT_ENTER, &TraceWindow::pcedit_activated, this);
    pcedit->Bind(wxEVT_KILL_FOCUS, &TraceWindow::pcedit_unfocused, this);
    pcprev_button->Bind(wxEVT_BUTTON, &TraceWindow::pc_nextprev<-1>, this);
    pcnext_button->Bind(wxEVT_BUTTON, &TraceWindow::pc_nextprev<+1>, this);

    mi_fold_all = NewControlId();
    mi_unfold_all = NewControlId();
    mi_fold = NewControlId();
    mi_fold_subrs = NewControlId();
    mi_unfold_container = NewControlId();
    mi_unfold = NewControlId();
    mi_unfold_full = NewControlId();
    mi_provenance = NewControlId();
    mi_contextmem = NewControlId();
    mi_provenance = NewControlId();

    Bind(wxEVT_MENU, &TraceWindow::fold_all_menuaction, this, mi_fold_all);
    Bind(wxEVT_MENU, &TraceWindow::unfold_all_menuaction, this, mi_unfold_all);
    Bind(wxEVT_MENU, &TraceWindow::fold_menuaction, this, mi_fold);
    Bind(wxEVT_MENU, &TraceWindow::fold_subrs_menuaction, this, mi_fold_subrs);
    Bind(wxEVT_MENU, &TraceWindow::unfold_container_menuaction, this,
         mi_unfold_container);
    Bind(wxEVT_MENU, &TraceWindow::unfold_menuaction, this, mi_unfold);
    Bind(wxEVT_MENU, &TraceWindow::unfold_full_menuaction, this,
         mi_unfold_full);
    Bind(wxEVT_MENU, &TraceWindow::provenance_menuaction, this, mi_provenance);
    Bind(wxEVT_MENU, &TraceWindow::contextmem_menuaction, this, mi_contextmem);

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

    vu.goto_visline(1);

    if (br.index.isAArch64())
        add_subview(new RegisterWindow64(app, br, this));
    else
        add_subview(new RegisterWindow32(app, br, this));

    set_scrollbar();

    reset_timeedit();
    reset_lineedit();
    reset_pcedit();
    update_subviews();
}

TraceWindow::~TraceWindow()
{
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
    sv->Show(true);
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

unsigned TraceWindow::n_display_lines() { return vu.total_visible_lines(); }

void TraceWindow::redraw_canvas(unsigned line_start, unsigned line_limit)
{
    SeqOrderPayload node;
    vector<string> node_lines;
    unsigned lineofnode = 0;

    controls.clear();

    for (unsigned line = line_start; line < line_limit; line++) {
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

        int x = (display_depth + 2) * char_width;
        int y = line_height * (line - line_start);
        LogicalPos logpos = {0, node.trace_file_firstline, lineofnode - 1, 0};
        drawing_area->add_highlighted_line(x, y, hl, logpos, syntax_highlight);

        if (lineofnode_old == 0) {
            auto foldstate = vu.node_fold_state(node);
            int x = (display_depth + 0.25) * char_width;

            if (foldstate != Browser::TraceView::NodeFoldState::Leaf) {
                bool folded =
                    (foldstate == Browser::TraceView::NodeFoldState::Folded);
                drawing_area->add_fold_control(x, y, folded);

                InWindowControl ctrl = {x, y, char_width, line_height, nullptr};
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
                drawing_area->add_leaf_node_marker(x, y);
            }
        }

        if (lineofnode == node_lines.size() &&
            !node.cmp(vu.curr_visible_node)) {
            drawing_area->add_separator_line(y + line_height - 1);
        }
    }
}

bool TraceWindow::prepare_context_menu(const LogicalPos &logpos)
{
    clear_menu(contextmenu);
    contextmenu->Append(mi_fold_all, "Fold all");
    contextmenu->Append(mi_unfold_all, "Unfold all");

    // Find out as much as we can about the context in question.
    SeqOrderPayload node;
    if (vu.br.get_node_by_physline(logpos.y0, &node, nullptr)) {
        if (container_fnrange.set_to_container(node)) {
            ostringstream oss;
            oss << "Containing call (lines "
                << container_fnrange.callnode.trace_file_firstline
                << "\xe2\x80\x93"
                << container_fnrange.lastnode.trace_file_firstline << " to "
                << br.get_symbolic_address(container_fnrange.firstnode.pc, true)
                << ")";

            contextmenu->AppendSeparator();
            wxMenuItem *label = contextmenu->Append(
                wxID_ANY, wxString::FromUTF8(oss.str().c_str()));
            label->Enable(false);

            contextmenu->Append(mi_fold, "Fold up");
            contextmenu->Append(mi_fold_subrs, "Fold all subroutines");
            contextmenu->Append(mi_unfold_container, "Unfold completely");
        }

        if (callee_fnrange.set_to_callee(node)) {
            ostringstream oss;
            oss << "Folded call (lines "
                << callee_fnrange.callnode.trace_file_firstline
                << "\xe2\x80\x93"
                << callee_fnrange.lastnode.trace_file_firstline << " to "
                << br.get_symbolic_address(callee_fnrange.firstnode.pc, true)
                << ")";

            contextmenu->AppendSeparator();
            wxMenuItem *label = contextmenu->Append(
                wxID_ANY, wxString::FromUTF8(oss.str().c_str()));
            label->Enable(false);

            contextmenu->Append(mi_unfold, "Unfold one level");
            contextmenu->Append(mi_unfold_full, "Unfold completely");
        }

        SeqOrderPayload prev_node;
        if (br.get_previous_node(node, &prev_node))
            context_menu_memroot = prev_node.memory_root;
        else
            context_menu_memroot = 0;
        DecodedTraceLine dtl(br.index.isBigEndian(),
                             br.index.get_trace_line(node, logpos.y1));
        if (dtl.mev) {
            ostringstream oss;
            oss << "Memory access: " << dtl.mev->size << " bytes at 0x" << hex
                << dtl.mev->addr;

            contextmenu->AppendSeparator();
            wxMenuItem *label = contextmenu->Append(wxID_ANY, oss.str());
            label->Enable(false);

            context_menu_memtype = 'm';
            context_menu_start = dtl.mev->addr;
            context_menu_size = dtl.mev->size;

            contextmenu->Append(mi_provenance, "Go to previous write");
            contextmenu->Append(mi_contextmem, "Open a memory window here");
        } else if (dtl.rev) {
            ostringstream oss;
            oss << "Register access: " << reg_name(dtl.rev->reg);

            contextmenu->AppendSeparator();
            wxMenuItem *label = contextmenu->Append(wxID_ANY, oss.str());
            label->Enable(false);

            context_menu_memtype = 'r';
            unsigned iflags = br.get_iflags(context_menu_memroot);
            context_menu_start = reg_offset(dtl.rev->reg, iflags);
            context_menu_size = reg_size(dtl.rev->reg);

            contextmenu->Append(mi_provenance, "Go to previous write");
        }
    }
    return true;
}

void TraceWindow::provenance_menuaction(wxCommandEvent &event)
{
    if (!context_menu_memtype)
        return; // just in case

    unsigned line =
        br.getmem(context_menu_memroot, context_menu_memtype,
                  context_menu_start, context_menu_size, nullptr, nullptr);
    if (line)
        goto_physline(line);
}

void TraceWindow::contextmem_menuaction(wxCommandEvent &event)
{
    if (context_menu_memtype != 'm')
        return; // just in case

    Addr addr = context_menu_start;
    addr ^= (addr & 15);
    addr -= 16 * 8; // centre the address in the new memory view
    add_subview(
        new MemoryWindow(app, addr, 16, br.index.isAArch64(), br, this));
}

void TraceWindow::clipboard_get_paste_data(ostream &os, LogicalPos start,
                                           LogicalPos end)
{
    SeqOrderPayload node;
    vector<string> node_lines;
    LogicalPos pos = start;

    if (!br.get_node_by_physline(pos.y0, &node, nullptr))
        return;
    node_lines = br.index.get_trace_lines(node);

    for (; logpos_cmp(pos, end) <= 0; pos.y1++) {
        if (pos.y1 >= node_lines.size()) {
            if (!vu.next_visible_node(node, &node))
                return;
            node_lines = br.index.get_trace_lines(node);
            pos.y0 = node.trace_file_firstline;
            pos.y1 = 0;
        }

        string &s = node_lines[pos.y1];

        if (pos.y0 == end.y0 && pos.y1 == end.y1) {
            s = s.substr(0, min((size_t)end.char_index + 1, s.size()));
        } else {
            s += "\n";
        }

        if (pos.y0 == start.y0 && pos.y1 == start.y1) {
            s = s.substr(min(s.size(), (size_t)start.char_index));
        }

        os << s;
    }
}

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

    unsigned prev_position = vu.curr_visible_node.trace_file_firstline;

    for (FoldChangeWrapper fcw(this); fcw.progress();)
        vu.set_fold_state(fnrange.firstline, fnrange.lastline, 0, depth);

    vu.goto_physline(prev_position);
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

void TraceWindow::update_location(UpdateLocationType type)
{
    if (type == UpdateLocationType::NewVis)
        vu.update_logical_node();
    else if (type == UpdateLocationType::NewLog)
        vu.update_visible_node();
    reset_timeedit();
    reset_lineedit();
    reset_pcedit();
    update_subviews();
    drawing_area->Refresh();
}

void TraceWindow::keep_visnode_in_view(bool strict_centre)
{
    auto &vis = vu.curr_visible_node;
    unsigned phystop = vis.trace_file_firstline;
    unsigned physbot = phystop + vis.trace_file_lines;

    int screen_lines = drawing_area->height() / line_height;

    if (strict_centre) {
        // Try to precisely centre the separator line in the window.
        wintop = vu.physical_to_visible_line(physbot) - screen_lines / 2;
    } else {
        // Force the visible node to be within the screen bounds. We add a line
        // below it so that the current-position underline is visible.
        wintop =
            min(wintop, static_cast<int>(vu.physical_to_visible_line(phystop)));
        wintop =
            max(wintop, static_cast<int>(vu.physical_to_visible_line(physbot) -
                                         screen_lines + 1));
    }

    // Override that by not going off the bottom or top of the
    // document (but the latter takes priority if the document is
    // shorter than a screenful).
    wintop =
        min(wintop, static_cast<int>(vu.total_visible_lines() - screen_lines));
    wintop = max(wintop, 0);

    set_scrollbar();
}

void TraceWindow::newtrace_menuaction(wxCommandEvent &event)
{
    app->make_trace_window();
}

class MemPromptDialog : public wxDialog {
    wxTextCtrl *addredit;

  public:
    MemPromptDialog(wxWindow *parent)
        : wxDialog(parent, wxID_ANY, wxT("Open memory window"))
    {
        auto *sizer = new wxBoxSizer(wxVERTICAL);

        sizer->Add(
            new wxStaticText(this, wxID_ANY, "Enter memory address to display"),
            wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP));
        addredit =
            new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                           edit_size("1234567890123"));
        sizer->Add(addredit, wxSizerFlags().Expand().Border(
                       wxLEFT | wxRIGHT | wxBOTTOM));

        sizer->Add(CreateButtonSizer(wxOK | wxCANCEL),
                   wxSizerFlags().Expand().Border(wxALL));

        SetSizerAndFit(sizer);

        addredit->SetFocus();
    }

    string addredit_value() { return addredit->GetValue().ToStdString(); }
};

void TraceWindow::newmem_menuaction(wxCommandEvent &event)
{
    if (!mem_prompt_dialog) {
        mem_prompt_dialog = new MemPromptDialog(this);
        mem_prompt_dialog->Show();
        mem_prompt_dialog->Bind(wxEVT_BUTTON,
                                &TraceWindow::mem_prompt_dialog_button, this);
        mem_prompt_dialog->Bind(wxEVT_CLOSE_WINDOW,
                                &TraceWindow::mem_prompt_dialog_closed, this);
    }
}

void TraceWindow::mem_prompt_dialog_ended(bool ok)
{
    if (!mem_prompt_dialog)
        return; // just in case

    if (!ok) { // user pressed Cancel
        delete mem_prompt_dialog;
        mem_prompt_dialog = nullptr;
        return;
    }

    string value = mem_prompt_dialog->addredit_value();

    MemoryWindow::StartAddr addr;
    ostringstream error;
    if (!addr.parse(value, br, error)) {
        wxMessageBox(wxT("Error parsing expression: " + error.str()));
        return;
    }

    delete mem_prompt_dialog;
    mem_prompt_dialog = nullptr;

    add_subview(
        new MemoryWindow(app, addr, 16, br.index.isAArch64(), br, this));
}

void TraceWindow::newstk_menuaction(wxCommandEvent &event)
{
    string expr = "reg::sp";
    MemoryWindow::StartAddr addr;
    ostringstream error;
    bool success = addr.parse(expr, br, error);
    assert(success);
    add_subview(
        new MemoryWindow(app, addr, 16, br.index.isAArch64(), br, this));
}

void TraceWindow::mem_prompt_dialog_button(wxCommandEvent &event)
{
    mem_prompt_dialog_ended(event.GetId() == wxID_OK);
}

void TraceWindow::mem_prompt_dialog_closed(wxCloseEvent &event)
{
    mem_prompt_dialog_ended(false);
}

void TraceWindow::newcorereg_menuaction(wxCommandEvent &event)
{
    if (br.index.isAArch64())
        add_subview(new RegisterWindow64(app, br, this));
    else
        add_subview(new RegisterWindow32(app, br, this));
}

void TraceWindow::newspreg_menuaction(wxCommandEvent &event)
{
    add_subview(new RegisterWindowSP(app, br, this));
}

void TraceWindow::newdpreg_menuaction(wxCommandEvent &event)
{
    add_subview(new RegisterWindowDP(app, br, this));
}

void TraceWindow::newneonreg_menuaction(wxCommandEvent &event)
{
    add_subview(new RegisterWindowNeon(app, br, this, br.index.isAArch64()));
}

void TraceWindow::newmvereg_menuaction(wxCommandEvent &event)
{
    add_subview(new RegisterWindowMVE(app, br, this));
}

void TraceWindow::recentre_menuaction(wxCommandEvent &event)
{
    keep_visnode_in_view(true);
    drawing_area->Refresh();
}

void TraceWindow::calldepth_menuaction(wxCommandEvent &event)
{
    depth_indentation = menubar->IsChecked(mi_calldepth);
    drawing_area->Refresh();
}

void TraceWindow::highlight_menuaction(wxCommandEvent &event)
{
    syntax_highlight = menubar->IsChecked(mi_highlight);
    drawing_area->Refresh();
}

void TraceWindow::branchtarget_menuaction(wxCommandEvent &event)
{
    substitute_branch_targets = menubar->IsChecked(mi_branchtarget);
    drawing_area->Refresh();
}

void TraceWindow::reset_lineedit()
{
    // We report line numbers as the last line of the node, i.e. the
    // line shown just above the separator. This also means you can
    // regard the line number as a Python-style index of a gap
    // _between_ lines - it counts the number of lines above the
    // separator.
    set_lineedit(vu.curr_logical_node.trace_file_firstline +
                 vu.curr_logical_node.trace_file_lines - 1);
}

void TraceWindow::activate_lineedit(unsigned line) { goto_physline(line); }

void TraceWindow::goto_physline(unsigned line)
{
    if (vu.goto_physline(line)) {
        update_location(UpdateLocationType::NewVis);
        keep_visnode_in_view();
    }
}

void TraceWindow::set_timeedit(Time time)
{
    ostringstream oss;
    oss << time;
    timeedit->SetValue(oss.str());
}

void TraceWindow::reset_timeedit()
{
    set_timeedit(vu.curr_logical_node.mod_time);
}

void TraceWindow::timeedit_activated(wxCommandEvent &event)
{
    string value = timeedit->GetValue().ToStdString();
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
    drawing_area->Refresh();
}

void TraceWindow::timeedit_unfocused(wxFocusEvent &event) { reset_timeedit(); }

void TraceWindow::pcedit_activated(wxCommandEvent &event)
{
    string value = pcedit->GetValue().ToStdString();
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
    drawing_area->Refresh();
}

void TraceWindow::pcedit_unfocused(wxFocusEvent &event) { reset_pcedit(); }

void TraceWindow::reset_pcedit()
{
    ostringstream oss;
    unsigned long long pc;
    if (vu.get_current_pc(pc)) {
        oss << "0x" << hex << pc;
        string sym = br.get_symbolic_address(pc, false);
        if (!sym.empty())
            oss << " = " << sym;
    }
    pcedit->SetValue(oss.str());
}

template <int direction> void TraceWindow::pc_nextprev(wxCommandEvent &event)
{
    unsigned long long pc;
    if (vu.get_current_pc(pc) && vu.goto_pc(pc, direction)) {
        update_location(UpdateLocationType::NewVis);
        keep_visnode_in_view();
    }
    drawing_area->Refresh();
}

bool TraceWindow::keypress(wxKeyEvent &event)
{
    switch (event.GetKeyCode()) {
    case WXK_UP:
    case WXK_NUMPAD_UP: {
        off_t prev_memroot = vu.curr_logical_node.memory_root;
        unsigned prev_line = vu.curr_logical_node.trace_file_firstline;
        if (vu.prev_visible_node(&vu.curr_visible_node)) {
            update_location(UpdateLocationType::NewVis);
            tell_subviews_to_diff_against(prev_memroot, prev_line);
            keep_visnode_in_view();
        }
        return true;
    }
    case WXK_DOWN:
    case WXK_NUMPAD_DOWN: {
        off_t prev_memroot = vu.curr_logical_node.memory_root;
        unsigned prev_line = vu.curr_logical_node.trace_file_firstline;
        if (vu.next_visible_node(&vu.curr_visible_node)) {
            update_location(UpdateLocationType::NewVis);
            tell_subviews_to_diff_against(prev_memroot, prev_line);
            keep_visnode_in_view();
        }
        return true;
    }
    case WXK_PAGEUP:
    case WXK_NUMPAD_PAGEUP:
        if (vu.goto_visline(vu.physical_to_visible_line(
                                vu.curr_visible_node.trace_file_firstline) -
                            drawing_area->height() / line_height) ||
            vu.goto_buffer_limit(false)) {
            update_location(UpdateLocationType::NewVis);
            keep_visnode_in_view();
        }
        return true;
    case WXK_PAGEDOWN:
    case WXK_NUMPAD_PAGEDOWN:
        if (vu.goto_visline(vu.physical_to_visible_line(
                                vu.curr_visible_node.trace_file_firstline +
                                vu.curr_visible_node.trace_file_lines - 1) +
                            drawing_area->height() / line_height) ||
            vu.goto_buffer_limit(true)) {
            update_location(UpdateLocationType::NewVis);
            keep_visnode_in_view();
        }
        return true;
    case WXK_HOME:
    case WXK_NUMPAD_HOME:
        if (vu.goto_buffer_limit(false)) {
            update_location(UpdateLocationType::NewVis);
            keep_visnode_in_view();
        }
        return true;
    case WXK_END:
    case WXK_NUMPAD_END:
        if (vu.goto_buffer_limit(true)) {
            update_location(UpdateLocationType::NewVis);
            keep_visnode_in_view();
        }
        return true;
    case WXK_LEFT:
    case WXK_NUMPAD_LEFT: {
        FunctionRange fnrange(vu);
        if (fnrange.set_to_container(vu.curr_visible_node)) {
            if (fnrange.callnode.trace_file_firstline ==
                vu.curr_visible_node.trace_file_firstline) {
                fold_ui_action(fnrange, ContainerFoldType::Fold);
            } else {
                vu.goto_physline(fnrange.callnode.trace_file_firstline);
                update_location(UpdateLocationType::NewLog);
            }
            keep_visnode_in_view();
            drawing_area->Refresh();
        }
        return true;
    }
    case WXK_RIGHT:
    case WXK_NUMPAD_RIGHT: {
        FunctionRange fnrange(vu);
        if (fnrange.set_to_callee(vu.curr_visible_node)) {
            unfold_ui_action(fnrange, false);
            keep_visnode_in_view();
            drawing_area->Refresh();
        }
        return true;
    }
    }
    return false;
}

void TraceWindow::click(const LogicalPos &logpos, const wxMouseEvent &event,
                        const wxPoint &p)
{
    // A left-click updates the current position. We're expecting
    // a click on a line _between_ nodes, so round differently
    // from the way we would in other situations.

    double click_pos = wintop + p.y / line_height;
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

RegisterWindow::RegisterWindow(GuiTarmacBrowserApp *app,
                               const vector<RegisterId> &regs, Browser &br,
                               TraceWindow *tw)
    : SubsidiaryView(app, compute_size(regs), false, br, tw), regs(regs)
{
    mi_ctx_provenance = NewControlId();
    Bind(wxEVT_MENU, &RegisterWindow::provenance_menuaction, this,
         mi_ctx_provenance);
}

bool RegisterWindow::prepare_context_menu(const LogicalPos &logpos)
{
    unsigned reg_index = logpos.y0 + logpos.char_index / colwid * rows;
    if (reg_index >= regs.size())
        return false;

    context_menu_reg = regs[reg_index];

    clear_menu(contextmenu);
    {
        ostringstream oss;
        oss << "Register " << reg_name(context_menu_reg);
        wxMenuItem *label = contextmenu->Append(wxID_ANY, oss.str());
        label->Enable(false);
    }
    contextmenu->Append(mi_ctx_provenance, "Go to last write to this register");

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

void RegisterWindow::redraw_canvas(unsigned line_start, unsigned line_limit)
{
    for (unsigned line = line_start; line < line_limit; line++) {
        for (unsigned col = 0; line + col * rows < regs.size(); col++) {
            unsigned regindex = line + col * rows;
            const RegisterId &r = regs[regindex];

            string dispstr, disptype;
            br.format_reg(dispstr, disptype, r, memroot, diff_memroot,
                          diff_minline);
            size_t valstart = dispstr.size() - format_reg_length(r);
            size_t xoffset = max_name_len + 1 - valstart;

            int x = (colwid * col + xoffset) * char_width;
            int y = line_height * (line - line_start);
            LogicalPos logpos = {col, line, regindex, 0};
            drawing_area->add_regmem_text(x, y, dispstr, disptype, logpos);
        }
    }
}

void RegisterWindow::clipboard_get_paste_data(ostream &os, LogicalPos start,
                                              LogicalPos end)
{
    for (unsigned i = start.y1; i <= end.y1; i++) {
        const RegisterId &r = regs[i];

        string s, type;
        br.format_reg(s, type, r, memroot);

        if (i == end.y1) {
            s = s.substr(0, min((size_t)end.char_index + 1, s.size()));
        } else {
            s += "\n";
        }

        if (i == start.y1) {
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

RegisterWindow32::RegisterWindow32(GuiTarmacBrowserApp *app, Browser &br,
                                   TraceWindow *tw)
    : RegisterWindow(app, core_regs_32(), br, tw)
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

RegisterWindow64::RegisterWindow64(GuiTarmacBrowserApp *app, Browser &br,
                                   TraceWindow *tw)
    : RegisterWindow(app, core_regs_64(), br, tw)
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

RegisterWindowSP::RegisterWindowSP(GuiTarmacBrowserApp *app, Browser &br,
                                   TraceWindow *tw)
    : RegisterWindow(app, float_regs_sp(), br, tw)
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

RegisterWindowDP::RegisterWindowDP(GuiTarmacBrowserApp *app, Browser &br,
                                   TraceWindow *tw)
    : RegisterWindow(app, float_regs_dp(), br, tw)
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

RegisterWindowNeon::RegisterWindowNeon(GuiTarmacBrowserApp *app, Browser &br,
                                       TraceWindow *tw, bool aarch64)
    : RegisterWindow(app, vector_regs_neon(aarch64 ? 32 : 16), br, tw)
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

RegisterWindowMVE::RegisterWindowMVE(GuiTarmacBrowserApp *app, Browser &br,
                                     TraceWindow *tw)
    : RegisterWindow(app, vector_regs_mve(), br, tw)
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

MemoryWindow::MemoryWindow(GuiTarmacBrowserApp *app, StartAddr addr,
                           int bpl, bool sfb, Browser &br, TraceWindow *tw)
    : SubsidiaryView(app, compute_size(bpl, sfb), false, br, tw),
      addr_chars(sfb ? 16 : 8), bytes_per_line(bpl)
{
    set_title("Memory");

    // In MemoryWindow, the 'wintop' field seen by the parent class is
    // ignored; we manage our own start address.
    wintop = 0;

    if (addr.expr) {
        set_start_addr_expr(addr.expr, addr.exprstr);
    } else {
        start_addr_known = true;
        start_addr_expr = nullptr;
        start_addr = addr.constant;
        start_addr -= (start_addr % bytes_per_line);
    }

    mi_ctx_provenance = NewControlId();
    Bind(wxEVT_MENU, &MemoryWindow::provenance_menuaction, this,
         mi_ctx_provenance);

    toolbar->AddControl(new wxStaticText(toolbar, wxID_ANY, "Address: "));
    addredit =
        new wxTextCtrl(toolbar, wxID_ANY, wxEmptyString, wxDefaultPosition,
                       edit_size("0x1234567890123456"), wxTE_PROCESS_ENTER);
    toolbar->AddControl(addredit);
    addredit->Bind(wxEVT_TEXT_ENTER, &MemoryWindow::addredit_activated, this);
    addredit->Bind(wxEVT_KILL_FOCUS, &MemoryWindow::addredit_unfocused, this);

    Bind(wxEVT_MOUSEWHEEL, &MemoryWindow::mousewheel, this);

    reset_addredit();
}

void MemoryWindow::memroot_changed()
{
    if (start_addr_expr)
        compute_start_addr();
}

void MemoryWindow::compute_start_addr()
{
    try {
        if (tw)
            start_addr = tw->vu.evaluate_expression_addr(start_addr_expr);
        else
            start_addr = br.evaluate_expression_addr(start_addr_expr);
        start_addr_known = true;
    } catch (invalid_argument) {
        start_addr_known = false;
    }
}

unsigned MemoryWindow::n_display_lines() { return 16; }

void MemoryWindow::redraw_canvas(unsigned line_start, unsigned line_limit)
{
    for (unsigned line = line_start; line < line_limit; line++) {
        string dispaddr, typeaddr, disphex, typehex, dispchars, typechars;

        Addr addr = start_addr + line * bytes_per_line;
        br.format_memory_split(dispaddr, typeaddr, disphex, typehex, dispchars,
                               typechars, addr, start_addr_known,
                               bytes_per_line, addr_chars, memroot,
                               diff_memroot, diff_minline);

        int y = line_height * (line - line_start);
        drawing_area->add_regmem_text(0, y, dispaddr, typeaddr,
                                      LogicalPos{0, addr, 0, 0});
        int x = char_width * (addr_chars + 2);
        drawing_area->add_regmem_text(x, y, disphex, typehex,
                                      LogicalPos{1, addr, 0, 0});
        x = char_width * (addr_chars + bytes_per_line * 3 + 3);
        drawing_area->add_regmem_text(x, y, dispchars, typechars,
                                      LogicalPos{2, addr, 0, 0});
    }
}

void MemoryWindow::mousewheel(wxMouseEvent &event)
{
    if (event.GetWheelAxis() != wxMOUSE_WHEEL_VERTICAL)
        return;

    mousewheel_accumulator += event.GetWheelRotation();
    double denominator = event.GetWheelDelta();
    if (event.IsPageScroll())
        denominator /= drawing_area->height() / line_height;
    int scaled = mousewheel_accumulator / denominator;
    mousewheel_accumulator -= scaled * denominator;

    if (scaled) {
        start_addr -= scaled * bytes_per_line;
        reset_addredit();
        drawing_area->Refresh();
    }
}

Addr MemoryWindow::addr_from_valid_logpos(const LogicalPos &logpos)
{
    Addr line_addr = logpos.y0;

    switch (logpos.column) {
    case 1: // hex display
        return line_addr + logpos.char_index / 3;
    case 2: // character display
        return line_addr + logpos.char_index;
    default:
        assert(false && "Invalid logpos in addr_from_valid_logpos");
        return 0; // placate compiler warning
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
    Addr line_addr = logpos.y0;
    Addr addr = 0;
    unsigned wordsize = 0;

    switch (logpos.column) {
    case 1: // hex display
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
        assert(logpos.column == 1 || logpos.column == 2);
        unsigned mult = logpos.column == 1 ? 3 : 1;
        unsigned end = logpos.column == 1 ? 1 : 0;

        mouseover_start = mouseover_end = logpos;
        mouseover_start.char_index = (start - logpos.y0) * mult;
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
    if (!get_region_under_pos(logpos, start, size))
        return false;

    context_menu_addr = start;
    context_menu_size = size;
    clear_menu(contextmenu);
    {
        ostringstream oss;
        oss << size << "-byte region at address 0x" << hex << start;
        wxMenuItem *label = contextmenu->Append(wxID_ANY, oss.str());
        label->Enable(false);
    }
    contextmenu->Append(mi_ctx_provenance, "Go to last write to this region");
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

bool MemoryWindow::keypress(wxKeyEvent &event)
{
    switch (event.GetKeyCode()) {
    case WXK_UP:
    case WXK_NUMPAD_UP:
        start_addr -= bytes_per_line;
        reset_addredit();
        drawing_area->Refresh();
        return true;
    case WXK_DOWN:
    case WXK_NUMPAD_DOWN:
        start_addr += bytes_per_line;
        reset_addredit();
        drawing_area->Refresh();
        return true;
    case WXK_PAGEUP:
    case WXK_NUMPAD_PAGEUP:
        start_addr -= drawing_area->height() / line_height * bytes_per_line;
        reset_addredit();
        drawing_area->Refresh();
        return true;
    case WXK_PAGEDOWN:
    case WXK_NUMPAD_PAGEDOWN:
        start_addr += drawing_area->height() / line_height * bytes_per_line;
        reset_addredit();
        drawing_area->Refresh();
        return true;
    }
    return false;
}

void MemoryWindow::reset_addredit()
{
    ostringstream oss;
    if (start_addr_expr) {
        oss << start_addr_exprstr;
    } else {
        Addr addr = start_addr;
        oss << "0x" << hex << addr;
    }
    addredit->SetValue(oss.str());
}

bool MemoryWindow::StartAddr::parse(const string &s, Browser &br,
                                    ostringstream &error)
{
    expr = br.parse_expression(s, error);
    if (!expr)
        return false;

    exprstr = s;
    return true;
}

void MemoryWindow::set_start_addr_expr(ExprPtr expr, const string &exprstr)
{
    start_addr_expr = expr;
    compute_start_addr();

    if (expr->is_constant() && start_addr_known) {
        /*
         * Normalise constant expressions to a plain number. This way,
         * there's no hidden state: when you scroll the window, the
         * new start address you obtained by scrolling will be the
         * official start address, so that the window will stay
         * pointing there even when the trace position changes.
         *
         * We only keep the start address in expression form if it's
         * actually variable.
         */
        start_addr_expr = nullptr;

        /*
         * Also, for constant expressions, round to a multiple of the
         * hex dump width, which I think is generally less confusing.
         */
        start_addr -= (start_addr % bytes_per_line);
    } else {
        /*
         * Keep the string version of the address expression, so that
         * it's obvious from looking at the toolbar that this memory
         * window has a variable start point.
         */
        start_addr_exprstr = exprstr;
    }
}

void MemoryWindow::addredit_activated(wxCommandEvent &event)
{
    string value = addredit->GetValue().ToStdString();

    if (is_empty_expression(value)) {
        // Special case: clearing the address box means 'stop
        // following a variable expression and just become inert at
        // the current address'.
        start_addr_expr = nullptr;
        start_addr_exprstr.clear();
    } else {
        // Otherwise, try to parse the expression.
        ExprPtr expr;
        ostringstream error;
        expr = br.parse_expression(value, error);
        if (!expr) {
            wxMessageBox(wxT("Error parsing expression: " + error.str()));
            return;
        }

        set_start_addr_expr(expr, value);
    }

    reset_addredit();
    drawing_area->Refresh();
    drawing_area->SetFocus();
}

void MemoryWindow::addredit_unfocused(wxFocusEvent &event) { reset_addredit(); }

inline unsigned MemoryWindow::byte_index(const LogicalPos &pos)
{
    switch (pos.column) {
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
    if (anchor.column == 0) {
        // Permit selecting just one address at a time
        if (cursor.column != 0) {
            cursor.column = 0;
            cursor.char_index = UINT_MAX;
        }
        if (cursor.y0 > anchor.y0) {
            cursor.y0 = anchor.y0;
            cursor.char_index = UINT_MAX;
        }
        if (cursor.y0 < anchor.y0) {
            cursor.y0 = anchor.y0;
            cursor.char_index = 0;
        }
        return;
    }

    if (cursor.column != anchor.column) {
        unsigned byte = byte_index(cursor);
        cursor.column = anchor.column;
        cursor.char_index = (cursor.column == 1 ? 3 : 1) * byte;
        if (cursor.column == 1 && logpos_cmp(cursor, anchor) > 0)
            cursor.char_index++; // select 2nd character of end byte
    }
}

void MemoryWindow::clipboard_get_paste_data(ostream &os, LogicalPos start,
                                            LogicalPos end)
{
    unsigned col = start.column;
    assert(col < 3);

    for (Addr addr = start.y0; addr <= end.y0; addr += bytes_per_line) {
        string disp[3], type[3];

        br.format_memory_split(disp[0], type[0], disp[1], type[1], disp[2],
                               type[2], addr, start_addr_known, bytes_per_line,
                               addr_chars, memroot);

        string &s = disp[col];

        if (addr == end.y0) {
            s = s.substr(0, min((size_t)end.char_index + 1, s.size()));
        } else {
            s += "\n";
        }

        if (addr == start.y0) {
            s = s.substr(min(s.size(), (size_t)start.char_index));
        }

        os << s;
    }
}

wxIMPLEMENT_APP(GuiTarmacBrowserApp);

wxFont TextViewWindow::font;
int TextViewWindow::char_width;
int TextViewWindow::baseline, TextViewWindow::line_height;

class WXGUIReporter : public Reporter {
    [[noreturn]] void err(int exitstatus, const char *fmt, ...) override;
    [[noreturn]] void errx(int exitstatus, const char *fmt, ...) override;
    void warn(const char *fmt, ...) override;
    void warnx(const char *fmt, ...) override;
    void indexing_status(const std::string &index_filename,
                         const std::string &trace_filename,
                         IndexUpdateCheck status) override;
    void indexing_warning(const string &trace_filename,
                          unsigned lineno, const string &msg) override;
    void indexing_error(const string &trace_filename,
                        unsigned lineno, const string &msg) override;
    void indexing_start(streampos total) override;
    void indexing_progress(streampos pos) override;
    void indexing_done() override;

    string progress_title;
    int progress_max;
    int prev_progress;
    double progress_scale;
    unique_ptr<wxGenericProgressDialog> progress_dlg;

  public:
    WXGUIReporter() = default;
};

unique_ptr<Reporter> make_wxgui_reporter()
{
    return make_unique<WXGUIReporter>();
}

string vcxxsprintf(const char *fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(nullptr, 0, fmt, ap2);
    va_end(ap2);

    std::unique_ptr<char[]> buf(new char[n+1]);
    vsnprintf(buf.get(), n+1, fmt, ap);

    return buf.get();
}

[[noreturn]] void WXGUIReporter::err(int exitstatus, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    string msg = vcxxsprintf(fmt, ap);
    va_end(ap);
    msg = msg + ": " + get_error_message();
    wxMessageDialog dlg(nullptr, msg, "tarmac-gui-browser fatal error",
                        wxOK | wxCENTRE | wxICON_ERROR);
    dlg.ShowModal();
    exit(exitstatus);
}

[[noreturn]] void WXGUIReporter::errx(int exitstatus, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    string msg = vcxxsprintf(fmt, ap);
    va_end(ap);
    wxMessageDialog dlg(nullptr, msg, "tarmac-gui-browser fatal error",
                        wxOK | wxCENTRE | wxICON_ERROR);
    dlg.ShowModal();
    exit(exitstatus);
}

void WXGUIReporter::warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    string msg = vcxxsprintf(fmt, ap);
    va_end(ap);
    msg = msg + ": " + get_error_message();
    wxMessageDialog dlg(nullptr, msg, "tarmac-gui-browser warning",
                        wxOK | wxCENTRE | wxICON_EXCLAMATION);
    dlg.ShowModal();
}

void WXGUIReporter::warnx(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    string msg = vcxxsprintf(fmt, ap);
    va_end(ap);
    wxMessageDialog dlg(nullptr, msg, "tarmac-gui-browser warning",
                        wxOK | wxCENTRE | wxICON_EXCLAMATION);
    dlg.ShowModal();
}

void WXGUIReporter::indexing_status(const string &index_filename,
                                    const string &trace_filename,
                                    IndexUpdateCheck status)
{
    ostringstream oss;
    oss << "Indexing trace file " << trace_filename << endl
        << "to index file " << index_filename << endl;

    switch (status) {
      case IndexUpdateCheck::Missing:
        oss << "(new index file)";
        break;
      case IndexUpdateCheck::TooOld:
        oss << "(index file was older than trace file)";
        break;
      case IndexUpdateCheck::WrongFormat:
        oss << "(index file was not generated by this version of the tool)";
        break;
      case IndexUpdateCheck::OK:
        oss << "(not actually indexing)";
        break;
    }

    progress_title = oss.str();
}

void WXGUIReporter::indexing_warning(const string &trace_filename,
                                     unsigned lineno, const string &msg)
{
    // Not really sure what we can usefully do with warnings during
    // indexing. I suppose we could try to put them on standard error
    // if we have one, but there's not really a sensible place in the
    // GUI for them.
}

void WXGUIReporter::indexing_error(const string &trace_filename,
                                   unsigned lineno, const string &msg)
{
    ostringstream oss;
    oss << trace_filename << ":" << lineno << ": " << msg;
    errx(1, "%s", msg.c_str());
}

void WXGUIReporter::indexing_start(streampos total)
{
    /*
     * We set an arbitrary dummy value of 10 for the progress bar's
     * range, which we'll change in a moment once we find out the
     * dialog box width.
     *
     * It would be nice to add the wxPD_CAN_ABORT flag here, and use
     * it to allow the user to cancel the operation. But I think we'd
     * want to communicate that back to the indexer, so that it could
     * delete the partial index file on the way out.
     */
    progress_dlg = make_unique<wxProgressDialog>(
        "tarmac-gui-browser indexing", progress_title, 10, nullptr,
        wxPD_APP_MODAL | wxPD_AUTO_HIDE);

    // The obvious thing to do here would be to set progress_max to
    // the input 'total' value, and call progress_dlg->Update every
    // time we progress in the file at all. But if you do that, it
    // turns out that the calls to the dialog update function are slow
    // enough to be the limiting factor on processing a trace file.
    //
    // So instead we limit the number of visible progress update steps
    // to a small number, and only call progress_dlg->Update when we
    // move to the next one of those steps. A reasonable approach is
    // to use the width of the dialog box, which will give us about as
    // many updates as the progress bar has pixels.
    progress_max = min(total, static_cast<streampos>(
                           progress_dlg->GetSize().GetWidth()));
    progress_scale = (double)progress_max / total;
    progress_dlg->SetRange(progress_max);

    prev_progress = 0;
}

void WXGUIReporter::indexing_progress(streampos pos)
{
    int value = progress_scale * pos;
    if (prev_progress != value) {
        prev_progress = value;
        progress_dlg->Update(value);
    }
}

void WXGUIReporter::indexing_done()
{
    progress_dlg = nullptr;
}

std::unique_ptr<Reporter> reporter = make_wxgui_reporter();

class SetupDialog : public wxDialog {
    /*
     * This GUI browser can be invoked with a command line just like
     * the command-line curses browser. But GUI programs are often run
     * from launcher systems like the Start Menu and don't have a
     * command line at all, so they also have to be able to handle
     * that situation.
     *
     * If we're started with a completely empty command line (which
     * would be a fatal error according to the standard TarmacUtility
     * argparse configuration), then we instead put up a GUI dialog
     * box asking for the obvious parameters (in particular, what
     * trace file to load). This class is that dialog.
     */
    wxFilePickerCtrl *trace_file_picker, *image_file_picker;
    wxChoice *reindex_choice;
    wxCheckBox *bigendian_checkbox;

  public:
    SetupDialog(wxWindow *parent = nullptr, wxWindowID id = wxID_ANY) :
        wxDialog(parent, id, wxT("tarmac-gui-browser setup")) {
        auto *sizer = new wxBoxSizer(wxVERTICAL);

        sizer->Add(
            new wxStaticText(this, wxID_ANY, "Trace file to view (required)"),
            wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP));
        trace_file_picker = new wxFilePickerCtrl(
            this, wxID_ANY, wxEmptyString, "Select a trace file to view");
        sizer->Add(trace_file_picker, wxSizerFlags().Expand().Border(
                       wxLEFT | wxRIGHT | wxBOTTOM));

        sizer->Add(
            new wxStaticText(this, wxID_ANY, "ELF image matching the trace"),
            wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP));
        image_file_picker = new wxFilePickerCtrl(
            this, wxID_ANY, wxEmptyString, "Select an ELF image to use");
        sizer->Add(image_file_picker, wxSizerFlags().Expand().Border(
                       wxLEFT | wxRIGHT | wxBOTTOM));

        sizer->Add(
            new wxStaticText(this, wxID_ANY, "Re-index the trace file?"),
            wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP));
        reindex_choice = new wxChoice(
            this, wxID_ANY, wxDefaultPosition,
            edit_size("If necessary"), 0, nullptr);
        sizer->Add(reindex_choice, wxSizerFlags().Expand().Border(
                       wxLEFT | wxRIGHT | wxBOTTOM));
        reindex_choice->Append("If necessary", reinterpret_cast<void *>(0));
        reindex_choice->Append("Always", reinterpret_cast<void *>(1));
        reindex_choice->Append("Never", reinterpret_cast<void *>(2));
        reindex_choice->SetSelection(0);

        bigendian_checkbox = new wxCheckBox(
            this, wxID_ANY, "Trace is from a big-endian system");
        sizer->Add(bigendian_checkbox, wxSizerFlags().Expand().Border());

        sizer->Add(CreateButtonSizer(wxOK | wxCANCEL),
                   wxSizerFlags().Right().Border());
        SetSizerAndFit(sizer);

        // You can't press OK until you've selected a trace file
        FindWindowById(wxID_OK)->Disable();

        trace_file_picker->Bind(wxEVT_FILEPICKER_CHANGED,
                                &SetupDialog::enable_ok_button, this);
    }

    void enable_ok_button(wxFileDirPickerEvent &event) {
        if (trace_file_picker->GetPath().empty())
            FindWindowById(wxID_OK)->Disable();
        else
            FindWindowById(wxID_OK)->Enable();
    }

    std::string trace_file_path() const {
        return trace_file_picker->GetPath().ToStdString();
    }

    std::string image_file_path() const {
        return image_file_picker->GetPath().ToStdString();
    }

    int reindex_policy() const {
        return reindex_choice->GetCurrentSelection();
    }

    bool big_endian() const {
        return bigendian_checkbox->IsChecked();
    }
};

/*
 * On Unix-like platforms, we deal with command-line syntax errors by
 * outputting to standard error, rather than displaying a dialog box,
 * which is more like normal Unix behaviour. This is especially useful
 * when processing --help.
 *
 * On Windows, we don't even _have_ a standard error, so we have to
 * put all that stuff in message boxes because there's no other
 * choice.
 */
#ifndef _WINDOWS
#define ARGUMENT_PARSING_TO_STDERR
#endif

bool GuiTarmacBrowserApp::OnInit()
{
    Argparse ap("tarmac-gui-browser", argc, argv);
    TarmacUtility tu(ap);

    if (argc > 1) {
        /*
         * If we've been given a non-empty command line, parse it
         * similarly to the non-GUI tools.
         */
#ifdef ARGUMENT_PARSING_TO_STDERR
        std::unique_ptr<Reporter> old_reporter = std::move(reporter);
        reporter = make_cli_reporter();
#endif

        ap.parse();

#ifdef ARGUMENT_PARSING_TO_STDERR
        reporter = std::move(old_reporter);
#endif
    } else {
        /*
         * If our command line was completely empty (which would have
         * caused a fatal error from ap.parse), then instead put up a
         * SetupDialog.
         *
         * Having done that, the easiest way to use the results is to
         * inject them back into the TarmacUtility by constructing a
         * fake command line.
         */
        SetupDialog dlg;
        if (dlg.ShowModal() != wxID_OK)
            return false;

        std::string image_path = dlg.image_file_path();
        if (!image_path.empty())
            ap.add_cmdline_word("--image=" + image_path);

        switch (dlg.reindex_policy()) {
          case 1:
            ap.add_cmdline_word("--force-index");
            break;
          case 2:
            ap.add_cmdline_word("--no-index");
            break;
        }

        if (dlg.big_endian())
            ap.add_cmdline_word("--bi");

        ap.add_cmdline_word("--");
        ap.add_cmdline_word(dlg.trace_file_path());

        ap.parse();
    }

    tu.setup();

    config.read();

    br = make_unique<Browser>(tu.trace, tu.image_filename);

    if (config.font.empty()) {
        TextViewWindow::font = wxFont(12, wxFONTFAMILY_TELETYPE,
                                      wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    } else {
        TextViewWindow::font = wxFont(config.font);
        if (!TextViewWindow::font.IsOk()) {
            wxMessageBox(
                wxString("Unable to parse font description:\n" + config.font),
                "Setup error", wxOK | wxICON_ERROR);
            return false;
        }
    }

    {
        wxMemoryDC memdc;
        memdc.SetFont(TextViewWindow::font);
        wxCoord w, h, descent;
        memdc.GetTextExtent(wxS("0123456789AHMIW"), &w, &h, &descent);
        TextViewWindow::char_width = (w + 7) / 15;
        TextViewWindow::line_height = h;
        TextViewWindow::baseline = h - descent;
    }

    make_trace_window();

    return true;
}

void GuiTarmacBrowserApp::make_trace_window()
{
    (new TraceWindow(this, *br))->Show(true);
}
