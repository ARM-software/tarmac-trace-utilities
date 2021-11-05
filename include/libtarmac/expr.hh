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

#include <memory>
#include <ostream>
#include <string>

struct EvaluationError {
    std::string msg;
    EvaluationError(const std::string &msg) : msg(msg) {}
};

struct ExecutionContext {
    enum class Context { Register, Symbol };
    virtual ~ExecutionContext() {}
    virtual bool lookup(const std::string &name, Context context,
                        uint64_t &out) const = 0;
};

struct TrivialExecutionContext : ExecutionContext {
    bool lookup(const std::string & /*name*/, Context /*context*/,
                uint64_t & /*out*/) const
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

ExprPtr parse_expression(const std::string &input, std::ostream &error);
ExprPtr constant_expression(uint64_t value);

#endif // LIBTARMAC_EXPR_HH
