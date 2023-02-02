/*
 * Copyright 2016-2021,2023 Arm Limited. All rights reserved.
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

#include "libtarmac/argparse.hh"
#include "libtarmac/reporter.hh"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <queue>
#include <sstream>

using std::cout;
using std::deque;
using std::endl;
using std::exit;
using std::make_unique;
using std::move;
using std::ostream;
using std::ostringstream;
using std::queue;
using std::string;
using std::unique_ptr;
using std::vector;

Argparse::Opt::Opt(bool has_val, const vector<string> &optnames,
                   const string &help)
    : has_val(has_val), multiple(false), help(help)
{
    positional = true; // assumed if no names given

    for (const string &name : optnames) {
        positional = false;
        size_t ndashes = name.find_first_not_of("-");
        if (ndashes == 2 && name.size() > 2)
            longnames.push_back(name.substr(2));
        else if (ndashes == 1 && name.size() == 2)
            shortnames.push_back(name[1]);
        else
            throw ArgparseError(
                "Option name should be of the form '--foo' or '-f'");
    }
}

Argparse::Argparse(const string &programname, int argc, char **argv)
    : Argparse(programname)
{
    for (int i = 1; i < argc; i++)
        append_cmdline_word(argv[i]);
}

void Argparse::add_opt(unique_ptr<Opt> opt)
{
    auto optraw = opt.get();
    options.push_back(move(opt));
    for (char shortname : optraw->shortnames)
        shortopts[shortname] = optraw;
    for (const string &longname : optraw->longnames)
        longopts[longname] = optraw;
}

void Argparse::optnoval(const vector<string> &optnames, const string &help,
                        OptNoValResponder responder)
{
    assert(optnames.size() > 0);
    auto opt = make_unique<Opt>(false, optnames, help);
    opt->novalresponder = responder;
    add_opt(move(opt));
}

void Argparse::optval(const vector<string> &optnames, const string &metavar,
                      const string &help, OptValResponder responder)
{
    assert(optnames.size() > 0);
    auto opt = make_unique<Opt>(true, optnames, help);
    opt->metavar = metavar;
    opt->valresponder = responder;
    add_opt(move(opt));
}

void Argparse::positional(const string &metavar, const string &help,
                          OptValResponder responder, bool required)
{
    assert(!multiple_positional && "Can't add a single positional argument"
                                   " after a multiple one");
    auto opt = make_unique<Opt>(true, vector<string>{}, help);
    opt->metavar = metavar;
    opt->valresponder = responder;
    opt->required = required;
    single_positionals.push_back(opt.get());
    add_opt(move(opt));
}

void Argparse::positional_multiple(const string &metavar, const string &help,
                                   OptValResponder responder)
{
    assert(!multiple_positional && "Can't have two multiple positional"
                                   " arguments");
    auto opt = make_unique<Opt>(true, vector<string>{}, help);
    opt->metavar = metavar;
    opt->valresponder = responder;
    opt->multiple = true;
    multiple_positional = opt.get();
    add_opt(move(opt));
}

void Argparse::parse_or_throw()
{
    bool doing_opts = true; // becomes false if we see "--" terminator
    auto posit = single_positionals.begin();
    auto posend = single_positionals.end();

    while (!arguments.empty()) {
        const string arg = arguments.front();
        arguments.pop_front();

        if (doing_opts && arg.size() > 0 && arg[0] == '-') {
            // This is an option of some kind.

            if (arg == "--") {
                // Terminate option parsing. Everything from here on
                // is a positional argument.
                doing_opts = false;
                continue;
            }

            size_t ndashes = arg.find_first_not_of("-");
            if (ndashes == 2) {
                size_t equals = arg.find('=');
                size_t nameend = equals != string::npos ? equals : arg.size();
                const string name = arg.substr(ndashes, nameend - ndashes);

                if (name == "help")
                    throw ArgparseHelpAction();

                auto opt_it = longopts.find(name);
                if (opt_it == longopts.end())
                    throw ArgparseError("'--" + name +
                                        "': "
                                        "unrecognised option name");
                const Opt *opt = opt_it->second;

                if (opt->has_val) {
                    string val;

                    if (equals != string::npos) {
                        val = arg.substr(equals + 1);
                    } else if (!arguments.empty()) {
                        val = arguments.front();
                        arguments.pop_front();
                    } else {
                        throw ArgparseError("'--" + name +
                                            "': "
                                            "option expects a value");
                    }

                    opt->valresponder(val);
                } else {
                    if (equals != string::npos)
                        throw ArgparseError("'--" + name +
                                            "': "
                                            "option expects no value");

                    opt->novalresponder();
                }
            } else if (ndashes == 1) {
                size_t pos = ndashes, end = arg.size();
                while (pos < end) {
                    char chr = arg[pos++];

                    auto opt_it = shortopts.find(chr);
                    if (opt_it == shortopts.end())
                        throw ArgparseError("'-" + string(1, chr) +
                                            "': "
                                            "unrecognised option name");
                    const Opt *opt = opt_it->second;

                    if (opt->has_val) {
                        string val;

                        if (pos < end) {
                            val = arg.substr(pos);
                            pos = end;
                        } else if (!arguments.empty()) {
                            val = arguments.front();
                            arguments.pop_front();
                        } else {
                            throw ArgparseError("'-" + string(1, chr) +
                                                "': "
                                                "option expects a value");
                        }

                        opt->valresponder(val);
                    } else {
                        opt->novalresponder();
                    }
                }
            } else {
                throw ArgparseError("'" + arg +
                                    "': "
                                    "badly formatted option");
            }
        } else {
            // Treat this as a positional argument.
            if (posit != posend) {
                const Opt *opt = *posit++;
                opt->valresponder(arg);
            } else if (multiple_positional) {
                multiple_positional->valresponder(arg);
            } else {
                throw ArgparseError("'" + arg +
                                    "': "
                                    "unexpected positional argument");
            }
        }
    }

    for (; posit != posend; ++posit) {
        const Opt *opt = *posit;
        if (opt->required)
            throw ArgparseError("expected additional arguments"
                                " (starting with '" +
                                opt->metavar + "')");
    }
}

void Argparse::parse(std::function<void(void)> final_validator)
{
    try {
        parse_or_throw();
        final_validator();
    } catch (ArgparseError e) {
        ostringstream oss;
        oss << programname << ": " << e.msg() << endl
            << string(programname.size() + 2, ' ') << "try '" << programname
            << " --help' for help";
        reporter->errx(1, "%s", oss.str().c_str());
        exit(1);
    } catch (ArgparseHelpAction) {
        help(cout);
        exit(0);
    }
}

void Argparse::parse()
{
    parse([]() {});
}

static vector<string> textwrap(const string &input, unsigned indent1,
                               unsigned width1, unsigned indent, unsigned width)
{
    queue<string> words;

    for (size_t pos = 0, size = input.size(); pos < size; pos++) {
        size_t wordstart = input.find_first_not_of(" ", pos);
        if (wordstart == string::npos)
            break;
        size_t wordend = input.find_first_of(" ", wordstart);
        if (wordend == string::npos)
            wordend = input.size();
        words.push(input.substr(wordstart, wordend - wordstart));
        pos = wordend;
    }

    vector<string> lines;

    ostringstream line;
    bool pushed_word = false;

    unsigned thiswidth = width1;
    line << string(indent1, ' ');

    while (!words.empty()) {
        const string &word = words.front();

        unsigned candidate_len = line.tellp();
        candidate_len += word.size();
        if (pushed_word)
            candidate_len++; // count the separating space

        if (candidate_len > thiswidth) {
            lines.push_back(line.str().substr(0, line.tellp()));
            line.seekp(0);
            pushed_word = false;

            thiswidth = width;
            line << string(indent, ' ');
        }

        if (pushed_word)
            line << " ";
        line << word;
        pushed_word = true;
        words.pop();
    }

    if (pushed_word)
        lines.push_back(line.str().substr(0, line.tellp()));

    return lines;
}

void Argparse::help(ostream &os)
{
    constexpr unsigned WIDTH = 79;
    constexpr unsigned FULLINDENT = 8;
    constexpr unsigned OPTWIDTH = 60;
    constexpr unsigned OPTINDENT1 = 4;
    constexpr unsigned OPTINDENT = 8;
    constexpr unsigned HELPINDENT1 = 24;
    constexpr unsigned HELPINDENT = HELPINDENT1 + 2;
    constexpr unsigned HELPSPACE = 2;
    constexpr unsigned SPECIALHELPINDENT1 = 32;

    {
        ostringstream hdr;
        hdr << "usage: " << programname;
        if (!longopts.empty() || !shortopts.empty())
            hdr << " [options]";
        for (const Opt *opt : single_positionals)
            hdr << " " << (opt->required ? "" : "[") << opt->metavar
                << (opt->required ? "" : "]");
        if (multiple_positional)
            hdr << " " << multiple_positional->metavar << "...";
        for (const string &line :
             textwrap(hdr.str(), 0, WIDTH, FULLINDENT, WIDTH))
            os << line << endl;
    }

    auto showopt = [&](const string &desc, const string &help,
                       size_t helpindent1) {
        vector<string> desclines =
            textwrap(desc, OPTINDENT1, OPTWIDTH, OPTINDENT, OPTWIDTH);

        // Omit the physical indent from helplines[0] so we can choose to
        // print it to the right of desclines[n-1]
        vector<string> helplines =
            textwrap(help, 0, WIDTH - helpindent1, HELPINDENT, WIDTH);

        size_t ndesc = desclines.size();

        for (size_t i = 0; i < ndesc; i++) {
            os << desclines[i];
            if (i + 1 < ndesc)
                os << endl;
        }

        if (ndesc && desclines[ndesc - 1].size() < helpindent1 - HELPSPACE)
            os << string(helpindent1 - desclines[ndesc - 1].size(), ' ');
        else
            os << endl << string(helpindent1, ' ');

        for (const string &helpline : helplines)
            os << helpline << endl;
    };

    if (!longopts.empty() || !shortopts.empty()) {
        os << "options:" << endl;

        for (auto &opt : options) {
            if (opt->positional)
                continue;

            ostringstream desc;
            desc << "  ";
            const char *sep = "";
            for (char c : opt->shortnames) {
                desc << sep << '-' << c;
                sep = ", ";
            }
            for (const string &s : opt->longnames) {
                desc << sep << "--" << s;
                sep = ", ";
            }

            if (opt->has_val)
                desc << (opt->longnames.empty() ? " " : "=") << opt->metavar;

            showopt(desc.str(), opt->help, HELPINDENT1);
        }
    }

    if (!single_positionals.empty() || multiple_positional) {
        os << "positional arguments:" << endl;

        for (auto &opt : options) {
            if (!opt->positional)
                continue;

            ostringstream desc;
            desc << "  " << opt->metavar;
            showopt(desc.str(), opt->help, HELPINDENT1);
        }
    }

    os << "also:" << endl;
    showopt(programname + " --help", "display this text", SPECIALHELPINDENT1);
}