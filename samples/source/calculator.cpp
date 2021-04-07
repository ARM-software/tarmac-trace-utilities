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

#include "semihosting.h"

static constexpr int EOF = -128; // outside range of a signed char

class FileReader {
    uintptr_t filehandle;
    static constexpr size_t BUFSIZE = 1024;
    char buffer[BUFSIZE];
    size_t bufpos = 0, bufend = 0;
    bool seen_eof = false;

  public:
    FileReader(uintptr_t filehandle) : filehandle(filehandle) {}

    int getch()
    {
        if (bufpos >= bufend) {
            if (seen_eof)
                return EOF;

            uintptr_t n_unread = sys_read(filehandle, buffer, BUFSIZE);
            bufend = BUFSIZE - n_unread;
            if (bufend == 0) {
                seen_eof = true;
                return EOF;
            }

            bufpos = 0;
        }

        return buffer[bufpos++];
    }
};

enum {
    TOK_EOF = 256,
    TOK_NUMBER,
    TOK_NONSENSE,
};

static void print(uint32_t value)
{
    sys_write0("0x");
    for (unsigned i = 8; i-- > 0;)
        sys_writec("0123456789ABCDEF"[0xF & (value >> (4 * i))]);
    sys_writec('\n');
}

class Lexer {
    FileReader &reader;
    int c;
    bool got_a_char = false;

    void nextch()
    {
        c = reader.getch();
        got_a_char = true;
    }

  public:
    int tokentype; // a char, or TOK_something
    uint32_t number;

    Lexer(FileReader &reader) : reader(reader) {}

    void next_inner()
    {
        if (!got_a_char)
            nextch();

        while (c == ' ' || c == '\t' || c == '\r')
            nextch();

        if (c == '#') {
            nextch();
            while (c != '\n' && c != EOF)
                nextch();
        }

        if (c == EOF) {
            tokentype = TOK_EOF;
            return;
        }

        if (c == '+' || c == '-' || c == '*' || c == '(' || c == ')' ||
            c == '^' || c == '\n') {
            tokentype = c;
            got_a_char = false;
            return;
        }

        if (c >= '0' && c <= '9') {
            number = c - '0';
            nextch();

            uint32_t base = 10;
            if (number == 0 && (c == 'x' || c == 'X')) {
                base = 16;
                nextch();
            } else if (number == 0) {
                base = 8;
            }

            while (true) {
                uint32_t digit;
                if (c >= '0' && c <= '9') {
                    digit = c - '0';
                } else if (c >= 'A' && c <= 'F') {
                    digit = c + (10 - 'A');
                } else if (c >= 'a' && c <= 'f') {
                    digit = c + (10 - 'a');
                } else {
                    break;
                }
                if (digit >= base)
                    break;
                number = number * base + digit;
                nextch();
            }

            tokentype = TOK_NUMBER;
            return;
        }

        tokentype = TOK_NONSENSE;
        nextch();
    }

    void next()
    {
#if defined DIAGNOSTICS
        sys_write0("next() entered");
#endif
        next_inner();
#if defined DIAGNOSTICS
        sys_write0("next() got token: ");
        print(tokentype);
        if (tokentype == TOK_NUMBER) {
            sys_write0("  with number = ");
            print(number);
        }
#endif
    }
};

class Parser {
    Lexer &lexer;
    const char *errmsg;

    void error(const char *msg) { errmsg = msg; }

    bool atom(uint32_t &value)
    {
        if (lexer.tokentype == TOK_NUMBER) {
            value = lexer.number;
            lexer.next();
            return true;
        }

        if (lexer.tokentype == '(') {
            lexer.next();
            if (!add(value))
                return false;
            if (lexer.tokentype != ')') {
                error("expected ')' after subexpression");
                return false;
            }
            lexer.next();
            return true;
        }

        if (lexer.tokentype == '+' || lexer.tokentype == '-') {
            uint32_t sign = 1;
            if (lexer.tokentype == '-')
                sign = -sign;

            lexer.next();
            if (!atom(value))
                return false;
            value *= sign;
            return true;
        }

        error("unexpected token");
        return false;
    }

    bool power(uint32_t &value)
    {
        if (!atom(value))
            return false;
        if (lexer.tokentype == '^') {
            lexer.next();
            uint32_t exponent;
            if (!power(exponent))
                return false;
            uint32_t newvalue = 1;
            while (exponent-- > 0)
                newvalue *= value;
            value = newvalue;
        }
        return true;
    }

    bool mult(uint32_t &value)
    {
        if (!power(value))
            return false;
        while (lexer.tokentype == '*') {
            lexer.next();
            uint32_t rhs;
            if (!power(rhs))
                return false;
            value *= rhs;
        }
        return true;
    }

    bool add(uint32_t &value)
    {
        if (!mult(value))
            return false;
        while (lexer.tokentype == '+' || lexer.tokentype == '-') {
            uint32_t sign = 1;
            if (lexer.tokentype == '-')
                sign = -sign;

            lexer.next();
            uint32_t rhs;
            if (!mult(rhs))
                return false;

            value += sign * rhs;
        }
        return true;
    }

    bool expr(uint32_t &value)
    {
        if (!add(value))
            return false;
        if (lexer.tokentype != '\n') {
            error("expected newline after expression");
            return false;
        }
        return true;
    }

  public:
    Parser(Lexer &lexer) : lexer(lexer) { lexer.next(); }

    void try_parse()
    {
        uint32_t value;
        if (expr(value)) {
            print(value);
            lexer.next();
        } else {
            sys_write0(errmsg);
            sys_writec('\n');
            while (lexer.tokentype != TOK_EOF && lexer.tokentype != '\n')
                lexer.next();
            if (lexer.tokentype == '\n')
                lexer.next();
        }
    }
};

extern "C" void c_entry(void)
{
    uintptr_t infh = sys_open(":tt", OPEN_MODE_R);
    FileReader reader(infh);
    Lexer lexer(reader);
    Parser parser(lexer);
    while (lexer.tokentype != TOK_EOF)
        parser.try_parse();
    sys_exit(0);
}
