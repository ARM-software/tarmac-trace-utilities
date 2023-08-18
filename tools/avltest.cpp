/*
 * Copyright 2023 Arm Limited. All rights reserved.
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
#include "libtarmac/disktree.hh"
#include "libtarmac/reporter.hh"

#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

using std::cout;
using std::endl;
using std::function;
using std::map;
using std::set;
using std::string;
using std::vector;

std::unique_ptr<Reporter> reporter = make_cli_reporter();

struct TestPayload {
    int value;
    TestPayload() = default;
    TestPayload(int value) : value(value) {}
    int cmp(const TestPayload &rhs) const {
        if (value < rhs.value) return -1;
        if (value > rhs.value) return +1;
        return 0;
    }
};

enum class Test {
    Single,
    Clone,
};
map<string, Test> testnames = {
    {"single", Test::Single},
    {"clone", Test::Clone},
};

class AVLTest {
    MemArena arena;
    using Tree = AVLDisk<TestPayload>;
    Tree tree;
    bool verbose;

    void dump(OFF_T root);
    void check(vector<OFF_T> roots);

  public:
    AVLTest(bool verbose);
    void test_single();
    void test_clone();
};

AVLTest::AVLTest(bool verbose) : arena(), tree(arena, true), verbose(verbose)
{
    arena.alloc(16);           // so that no node pointer ends up at 0
}

void AVLTest::test_single()
{
    OFF_T root = 0;

    // Moderately sized prime, so that it's easy to use modular
    // multiplication to insert and remove the numbers 1,...,p-1 in
    // different orders
    int p = 1009;

    for (int i = 1; i < p; i++) {
        int j = (i * 123) % p;
        if (verbose)
            cout << "inserting " << j << endl;
        root = tree.insert(root, j);
        dump(root);
        check({root});
    }

    for (int i = 1; i < p; i++) {
        int j = (i * 456) % p;
        if (verbose)
            cout << "removing " << j << endl;
        bool found;
        TestPayload removed;
        root = tree.remove(root, TestPayload(j), &found, &removed);
        assert(found);
        assert(removed.value == j);
    }

    for (int i = 1; i < p; i++) {
        int j = (i * 789) % p;
        if (verbose)
            cout << "inserting " << j << endl;
        root = tree.insert(root, j);
    }
}

void AVLTest::test_clone()
{
    OFF_T rootA = 0, rootB = 0;

    for (int n = 1; n <= 45; n += 2) {
        for (int i = 1; i <= n; i += 2)
            rootA = tree.insert(rootA, i);
        check({rootA});

        for (int i = 0; i <= n+1; i++) {
            if (verbose)
                cout << "before " << i << endl;
            dump(rootA);
            rootB = tree.clone_tree(rootA);
            check({rootA, rootB});
            if (i % 2 == 0) {
                if (verbose)
                    cout << "inserting " << i << endl;
                rootB = tree.insert(rootB, i);
            } else {
                if (verbose)
                    cout << "removing " << i << endl;
                bool found;
                TestPayload removed;
                rootB = tree.remove(rootB, TestPayload(i), &found, &removed);
                assert(found);
                assert(removed.value == i);
            }
            dump(rootA);
            dump(rootB);
            check({rootA, rootB});
            tree.free_tree(rootB);
        }

        tree.free_tree(rootA);
        rootA = 0;
    }
}

void AVLTest::dump(OFF_T root)
{
    if (!verbose)
        return;

    function<void(OFF_T, string, string, string)> dump_node;
    dump_node = [this, &dump_node](OFF_T offset, string prefix_before,
                                   string prefix_at, string prefix_after) {
        Tree::disknode &dn = *tree.arena.getptr<Tree::disknode>(offset);
        if (dn.lc)
            dump_node(dn.lc, prefix_before + "  ", prefix_before + "┌╴",
                      prefix_before + "│ ");
        cout << prefix_at << dn.payload.value << " offset=" << offset
             << " rc=" << dn.refcount << endl;
        if (dn.rc)
            dump_node(dn.rc, prefix_after + "│ ", prefix_after + "└╴",
                      prefix_after + "  ");
    };
    dump_node(root, "", "", "");
}

void AVLTest::check(vector<OFF_T> roots)
{
    /*
     * Iterate over every tree root we're given, finding all the nodes
     * reachable from it, and working out what we _expect_ their
     * reference counts to say.
     */
    map<OFF_T, int> expected_refcounts;

    function<void(OFF_T)> visit_node;
    visit_node = [&, this](OFF_T offset) {
        if (offset == 0)
            return;
        if (++expected_refcounts[offset] == 1) {
            Tree::disknode &dn = *tree.arena.getptr<Tree::disknode>(offset);
            visit_node(dn.lc);
            visit_node(dn.rc);
        }
    };

    for (OFF_T root: roots)
        visit_node(root);

    for (auto kv: expected_refcounts) {
        Tree::disknode &dn = *tree.arena.getptr<Tree::disknode>(kv.first);
        int expected = kv.second, actual = dn.refcount;
        if (expected != actual) {
            cout << "check({";
            const char *sep = "";
            for (OFF_T root: roots) {
                cout << sep << root;
                sep = ", ";
            }
            cout << "}) failed!" << endl;
            cout << "node at " << kv.first << " should have refcount "
                 << expected << ", but instead has " << actual << endl;
            exit(1);
        }
    }
}

int main(int argc, char **argv)
{
    bool verbose = false;
    set<Test> tests_to_run;

    Argparse ap("avltest", argc, argv);
    ap.optnoval({"-v", "--verbose"}, "print verbose diagnostics during tests",
                [&]() { verbose = true; });
    ap.positional("testname", "name of sub-test to run",
                  [&](const std::string &arg) {
                      auto it = testnames.find(arg);
                      if (it == testnames.end())
                          throw ArgparseError("unrecognised test name '" +
                                              arg + "'");
                      tests_to_run.insert(it->second);
                  }, false);
    ap.parse();

    if (tests_to_run.empty())
        for (auto kv: testnames)
            tests_to_run.insert(kv.second);

    AVLTest t(verbose);
    if (tests_to_run.count(Test::Single))
        t.test_single();
    if (tests_to_run.count(Test::Clone))
        t.test_clone();

    return 0;
}
