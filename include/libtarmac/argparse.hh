/*
 * Copyright 2016-2021, 2023 Arm Limited. All rights reserved.
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

#ifndef TARMAC_ARGPARSE_HH
#define TARMAC_ARGPARSE_HH

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

class ArgparseError : public std::exception {
  private:
    const std::string Msg;

  public:
    ArgparseError(const std::string &msg) : Msg(msg) {}
    const std::string &msg() const { return Msg; }
};

class ArgparseSpecialAction : public std::exception {
};
class ArgparseHelpAction : public ArgparseSpecialAction {
};

class Argparse {
  public:
    using OptNoValResponder = std::function<void()>;
    using OptValResponder = std::function<void(const std::string &)>;

    Argparse(const std::string &programname) : programname(programname) {}
    Argparse(const std::string &programname, int argc, char **argv);
    template <template <class> class Container>
    Argparse(const std::string &programname,
             const Container<std::string> &words)
        : Argparse(programname)
    {
        for (const std::string &word : words)
            append_cmdline_word(word);
    }

    void append_cmdline_word(const std::string &arg)
    {
        arguments.push_back(arg);
    }

    void prepend_cmdline_word(const std::string &arg)
    {
        arguments.push_front(arg);
    }

    void optnoval(const std::vector<std::string> &optnames,
                  const std::string &help, OptNoValResponder responder);
    void optval(const std::vector<std::string> &optnames,
                const std::string &metavar, const std::string &help,
                OptValResponder responder);
    void positional(const std::string &metavar, const std::string &help,
                    OptValResponder responder, bool required = true);
    void positional_multiple(const std::string &metavar,
                             const std::string &help,
                             OptValResponder responder);

    void help(std::ostream &os);

    // Parse the provided command line words against the provided
    // option and argument specifications. In case of error, throw
    // ArgparseError containing an error message; if the user provides
    // a special-action option like --help, throw an
    // ArgparseSpecialAction derivative indicating what action to
    // take.
    void parse_or_throw();

    // Call parse_or_throw, but respond to special actions like --help
    // by actually doing the action and then exit(0), and respond to
    // ArgparseError by formatting the error message on standard error
    // and calling exit(1). The idea is that you call this near the
    // start of main(), and if it returns at all, you have a valid
    // command line and can proceed with your main activity.
    void parse();

    // Like the nullary parse(), but you provide a last-minute
    // validation function that can check that the combination of
    // provided options makes sense (e.g. perhaps two of your options
    // are mutually exclusive, or one requires another). The idea is
    // that if the validator has a problem, it can throw ArgparseError
    // with a bare error message, and the catch clause inside parse()
    // will handle formatting it nicely for stderr the same way as it
    // formats its own complaints.
    void parse(std::function<void(void)> final_validator);

  private:
    struct Opt {
        std::vector<char> shortnames;
        std::vector<std::string> longnames;
        bool has_val, positional, multiple, required;
        std::string metavar;
        std::string help;
        OptNoValResponder novalresponder;
        OptValResponder valresponder;

        Opt(bool has_val, const std::vector<std::string> &optnames,
            const std::string &help);
    };

    void add_opt(std::unique_ptr<Opt> opt);

    std::string programname;
    std::deque<std::string> arguments;
    std::vector<std::unique_ptr<Opt>> options;
    std::map<std::string, const Opt *> longopts;
    std::map<char, const Opt *> shortopts;
    std::vector<const Opt *> single_positionals;
    const Opt *multiple_positional = nullptr;
};

#endif // TARMAC_ARGPARSE_HH
