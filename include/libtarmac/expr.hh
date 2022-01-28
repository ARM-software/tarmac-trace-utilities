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

#ifndef LIBTARMAC_EXPR_HH
#define LIBTARMAC_EXPR_HH

#include <exception>
#include <memory>
#include <ostream>
#include <string>

struct RegisterId;

struct EvaluationError : std::exception {
    std::string msg;
    EvaluationError(const std::string &msg) : msg(msg) {}
};

// Class passed to parse_expression() which knows how to look up a
// symbol name and turn it into that symbol's constant value, or how
// to look up a register name and turn it into a RegisterId. In both
// cases, returns false if the name doesn't exist.
struct ParseContext {
    virtual ~ParseContext() {}
    virtual bool lookup_symbol(const std::string &name,
                               uint64_t &out) const = 0;
    virtual bool lookup_register(const std::string &name,
                                 RegisterId &out) const = 0;
};

// Class passed to Expression::evaluate() which knows how to retrieve
// the value of a register given a RegisterId, or return false if the
// register's value is unavailable in this particular execution
// context (e.g. a time in the trace before it was first written).
struct ExecutionContext {
    virtual ~ExecutionContext() {}
    virtual bool lookup_register(const RegisterId &reg,
                                 uint64_t &out) const = 0;
};

struct TrivialParseContext : ParseContext {
    bool lookup_symbol(const std::string &name, uint64_t &out) const
    {
        return false;
    }

    bool lookup_register(const std::string &name, RegisterId &out) const
    {
        return false;
    }
};

struct TrivialExecutionContext : ExecutionContext {
    bool lookup_register(const RegisterId & /*reg*/, uint64_t & /*out*/) const
    {
        return false;
    }
};

struct Expression {
    virtual ~Expression() {}
    virtual uint64_t evaluate(const ExecutionContext &) = 0;
    virtual void dump(std::ostream &) = 0;
    virtual bool is_constant() { return false; }
};

using ExprPtr = std::shared_ptr<Expression>;

ExprPtr parse_expression(const std::string &input, const ParseContext &,
                         std::ostream &error);
ExprPtr constant_expression(uint64_t value);

#endif // LIBTARMAC_EXPR_HH
