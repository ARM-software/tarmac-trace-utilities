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

#include "libtarmac/expr.hh"
#include "libtarmac/registers.hh"

#include <ctype.h>
#include <exception>
#include <istream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>

using std::exception;
using std::istream;
using std::make_shared;
using std::ostream;
using std::ostringstream;
using std::string;

struct ParseError : exception {
    string msg;
    ParseError(const string &msg_) : msg(msg_) {}
};

struct ConstantExpression : Expression {
    uint64_t value;
    ConstantExpression(uint64_t value_) : value(value_) {}
    uint64_t evaluate(const ExecutionContext &) { return value; }
    virtual void dump(ostream &os) { os << "(const " << value << ")"; }
    virtual bool is_constant() { return true; }
};

ExprPtr constant_expression(uint64_t value)
{
    return make_shared<ConstantExpression>(value);
}

struct OperatorExpression : Expression {
    ExprPtr lhexpr, rhexpr;
    OperatorExpression(ExprPtr lhexpr_, ExprPtr rhexpr_)
        : lhexpr(lhexpr_), rhexpr(rhexpr_)
    {
    }
    virtual uint64_t op(uint64_t lhval, uint64_t rhval) = 0;
    uint64_t evaluate(const ExecutionContext &ec)
    {
        uint64_t lhval = lhexpr ? lhexpr->evaluate(ec) : 0;
        uint64_t rhval = rhexpr ? rhexpr->evaluate(ec) : 0;
        return op(lhval, rhval);
    }
    virtual const char *opname() const = 0;
    virtual void dump(ostream &os)
    {
        os << "(" << opname() << " ";
        lhexpr->dump(os);
        if (rhexpr) {
            os << " ";
            rhexpr->dump(os);
        }
        os << ")";
    }
    virtual bool is_constant() {
        return (lhexpr ? lhexpr->is_constant() : true) &&
               (rhexpr ? rhexpr->is_constant() : true);
    }
};

struct AddExpression : OperatorExpression {
    using OperatorExpression::OperatorExpression;
    const char *opname() const { return "+"; }
    uint64_t op(uint64_t lhval, uint64_t rhval) { return lhval + rhval; }
};
struct SubExpression : OperatorExpression {
    using OperatorExpression::OperatorExpression;
    const char *opname() const { return "-"; }
    uint64_t op(uint64_t lhval, uint64_t rhval) { return lhval - rhval; }
};
struct MulExpression : OperatorExpression {
    using OperatorExpression::OperatorExpression;
    const char *opname() const { return "*"; }
    uint64_t op(uint64_t lhval, uint64_t rhval) { return lhval * rhval; }
};
struct ShlExpression : OperatorExpression {
    using OperatorExpression::OperatorExpression;
    const char *opname() const { return "<<"; }
    uint64_t op(uint64_t lhval, uint64_t rhval)
    {
        return rhval >= 64 ? 0 : lhval << rhval;
    }
};
struct ShrExpression : OperatorExpression {
    using OperatorExpression::OperatorExpression;
    const char *opname() const { return ">>"; }
    uint64_t op(uint64_t lhval, uint64_t rhval)
    {
        return rhval >= 64 ? 0 : lhval >> rhval;
    }
};
struct NegExpression : OperatorExpression {
    NegExpression(ExprPtr lhexpr_) : OperatorExpression(lhexpr_, nullptr) {}
    const char *opname() const { return "-"; }
    uint64_t op(uint64_t lhval, uint64_t /*rhval*/) { return -lhval; }
};

struct RegisterExpression : Expression {
    RegisterId reg;
    string name; // for error messages
    RegisterExpression(const RegisterId &reg, const string &name) :
        reg(reg), name(name) {}
    uint64_t evaluate(const ExecutionContext &ec)
    {
        uint64_t toret;

        if (ec.lookup_register(reg, toret))
            return toret;

        throw EvaluationError("register name '" + name + "'");
    }
    virtual void dump(ostream &os) { os << "(register " << name << ")"; }
};

enum {
    ATOM = 256,
    ID,
    LEFTSHIFT,
    RIGHTSHIFT,
    SCOPE,
    BADTOKEN,
    TOK_EOF,
};

struct Lexer {
    string::const_iterator pos, end;

    unsigned token;
    ExprPtr exprvalue;
    string idvalue;

    Lexer(const string &input) : pos(input.begin()), end(input.end())
    {
        advance();
    }

    void advance();
};

void Lexer::advance()
{
    while (pos != end && (*pos == '\n' || *pos == '\t' || *pos == ' '))
        pos++;

    if (pos == end) {
        token = TOK_EOF;
        return;
    }

    if (*pos == '0' && pos + 1 != end &&
        (*(pos + 1) == 'x' || *(pos + 1) == 'X')) {
        // Hex number.
        pos += 2;
        ostringstream oss;
        while (pos != end && isxdigit((unsigned char)*pos))
            oss << *pos++;
        token = ATOM;
        exprvalue =
            ExprPtr(new ConstantExpression(stoull(oss.str(), nullptr, 16)));
        return;
    }

    if (isdigit((unsigned char)*pos)) {
        // Decimal number.
        ostringstream oss;
        while (pos != end && isdigit((unsigned char)*pos))
            oss << *pos++;
        token = ATOM;
        exprvalue =
            ExprPtr(new ConstantExpression(stoull(oss.str(), nullptr, 10)));
        return;
    }

    if ((*pos >= 'A' && *pos <= 'Z') || (*pos >= 'a' && *pos <= 'z') ||
        *pos == '_' || *pos == '$') {
        ostringstream oss;
        while (pos != end &&
               ((*pos >= 'A' && *pos <= 'Z') || (*pos >= 'a' && *pos <= 'z') ||
                (*pos >= '0' && *pos <= '9') || *pos == '_' || *pos == '$'))
            oss << *pos++;
        token = ID;
        idvalue = oss.str();
        return;
    }

    if (*pos == '<' && pos + 1 != end && *(pos + 1) == '<') {
        token = LEFTSHIFT;
        pos += 2;
        return;
    }

    if (*pos == '>' && pos + 1 != end && *(pos + 1) == '>') {
        token = RIGHTSHIFT;
        pos += 2;
        return;
    }

    if (*pos == ':' && pos + 1 != end && *(pos + 1) == ':') {
        token = SCOPE;
        pos += 2;
        return;
    }

    if (*pos == '+' || *pos == '-' || *pos == '*' || *pos == '(' ||
        *pos == ')') {
        token = *pos++;
        return;
    }

    token = BADTOKEN;
    pos++;
}

struct Parser {
    Lexer &lexer;
    const ParseContext &pc;

    Parser(Lexer &lexer, const ParseContext &pc) : lexer(lexer), pc(pc) {}

    ExprPtr parse_unary();
    ExprPtr parse_mul();
    ExprPtr parse_add();
    ExprPtr parse_expr();

    ExprPtr parse_register_name(const string &);
    ExprPtr parse_symbol_name(const string &);
};

ExprPtr Parser::parse_unary()
{
    ExprPtr toret;

    while (lexer.token == '+')
        lexer.advance();

    switch (lexer.token) {
    case ATOM:
        toret = lexer.exprvalue;
        lexer.advance();
        break;

    case ID: {
        string id1 = lexer.idvalue;
        lexer.advance();
        if (lexer.token == SCOPE) {
            lexer.advance();
            if (lexer.token != ID)
                throw ParseError("expected an identifier after '::'");

            if (id1 == "reg") {
                toret = parse_register_name(lexer.idvalue);
                if (!toret)
                    throw ParseError("unrecognised register name '" +
                                     lexer.idvalue + "'");
            } else if (id1 == "sym") {
                toret = parse_symbol_name(lexer.idvalue);
                if (!toret)
                    throw ParseError("unrecognised symbol name '" +
                                     lexer.idvalue + "'");
            } else
                throw ParseError("unrecognised identifier scope '" + id1 + "'");

            lexer.advance();
        } else {
            toret = parse_register_name(id1);
            if (!toret)
                toret = parse_symbol_name(lexer.idvalue);
            if (!toret)
                throw ParseError("unrecognised identifier name '" +
                                 lexer.idvalue + "'");
        }
        break;
    }

    case '(':
        lexer.advance();
        toret = parse_expr();
        if (lexer.token != ')')
            throw ParseError("expected closing ')'");
        lexer.advance();
        break;

    case '-':
        lexer.advance();
        toret = ExprPtr(new NegExpression(parse_unary()));
        break;

    case TOK_EOF:
        throw ParseError("unexpected end of expression");

    default:
        throw ParseError("unexpected token");
    }

    return toret;
}

ExprPtr Parser::parse_register_name(const string &name)
{
    RegisterId reg;
    if (pc.lookup_register(name, reg))
        return ExprPtr(new RegisterExpression(reg, name));
    return nullptr;
}

ExprPtr Parser::parse_symbol_name(const string &name)
{
    uint64_t value;
    if (pc.lookup_symbol(name, value))
        return ExprPtr(new ConstantExpression(value));
    return nullptr;
}

ExprPtr Parser::parse_mul()
{
    ExprPtr toret = parse_unary();

    while (lexer.token == '*') {
        lexer.advance();
        toret = ExprPtr(new MulExpression(toret, parse_unary()));
    }

    return toret;
}

ExprPtr Parser::parse_add()
{
    ExprPtr toret = parse_mul();

    while (lexer.token == '+' || lexer.token == '-') {
        auto op = lexer.token;
        lexer.advance();
        ExprPtr rhs = parse_mul();
        switch (op) {
        case '+':
            toret = ExprPtr(new AddExpression(toret, rhs));
            break;
        case '-':
            toret = ExprPtr(new SubExpression(toret, rhs));
            break;
        }
    }

    return toret;
}

ExprPtr Parser::parse_expr()
{
    ExprPtr toret = parse_add();

    while (lexer.token == LEFTSHIFT || lexer.token == RIGHTSHIFT) {
        auto op = lexer.token;
        lexer.advance();
        ExprPtr rhs = parse_add();
        switch (op) {
        case LEFTSHIFT:
            toret = ExprPtr(new ShlExpression(toret, rhs));
            break;
        case RIGHTSHIFT:
            toret = ExprPtr(new ShrExpression(toret, rhs));
            break;
        }
    }

    return toret;
}

ExprPtr parse_expression(const std::string &input, const ParseContext &pc,
                         std::ostream &error)
{
    Lexer lexer(input);
    Parser parser(lexer, pc);

    try {
        ExprPtr toret = parser.parse_expr();
        if (lexer.token != TOK_EOF)
            throw ParseError("unexpected tokens after expression");
        return toret;
    } catch (ParseError err) {
        error << err.msg;
        return nullptr;
    }
}
