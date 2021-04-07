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

#ifndef LIBTARMAC_MEMTREE_HH
#define LIBTARMAC_MEMTREE_HH

#include <algorithm>
#include <cassert>
#include <functional>

// Payload is whatever you want to store directly in the node.

// Annotation must provide a constructor taking a Payload, and a
// constructor taking two other Annotations.
template <class Payload, class Annotation> class AVLMem {
    struct node {
        node *lc, *rc;
        int height;
        Payload payload;
        Annotation annotation;
    };

    static inline int height(node *n) { return n ? n->height : 0; }

    static void rewrite(node *n, node *newlc, node *newrc)
    {
        n->lc = newlc;
        n->rc = newrc;
        n->height = std::max(height(n->lc), height(n->rc)) + 1;
        n->annotation = Annotation(n->payload);
        if (n->lc)
            n->annotation = Annotation(n->lc->annotation, n->annotation);
        if (n->rc)
            n->annotation = Annotation(n->annotation, n->rc->annotation);
    }

    static node *rotate_left(node *n)
    {
        node *rc = n->rc;
        node *t0 = n->lc, *t1 = rc->lc, *t2 = rc->rc;
        rewrite(n, t0, t1);
        rewrite(rc, n, t2);
        return rc;
    }

    static node *rotate_right(node *n)
    {
        node *lc = n->lc;
        node *t0 = lc->lc, *t1 = lc->rc, *t2 = n->rc;
        rewrite(n, t1, t2);
        rewrite(lc, t0, n);
        return lc;
    }

    static void free_node(node *n)
    {
        if (!n)
            return;
        free_node(n->lc);
        free_node(n->rc);
        delete n;
    }

    static node *insert_main(node *root, node *n)
    {
        if (!root) {
            rewrite(n, NULL, NULL);
            return n;
        }

        int cmp = root->payload.cmp(n->payload);
        assert(cmp != 0);

        node *lc = root->lc, *rc = root->rc;
        int k;

        if (cmp > 0) {
            lc = insert_main(lc, n);
            rewrite(root, lc, rc);
            k = height(rc);

            if (height(lc) == k + 2) {
                node *lrc = lc->rc;
                if (height(lrc) == k + 1) {
                    lc = rotate_left(lc);
                    rewrite(root, lc, rc);
                }
                return rotate_right(root);
            }
        } else {
            rc = insert_main(rc, n);
            rewrite(root, lc, rc);
            k = height(lc);

            if (height(rc) == k + 2) {
                node *rlc = rc->lc;
                if (height(rlc) == k + 1) {
                    rc = rotate_right(rc);
                    rewrite(root, lc, rc);
                }
                return rotate_left(root);
            }
        }

        return root;
    }

    template <class PayloadComparable>
    static node *remove_main(node *root, const PayloadComparable *keyfinder,
                             node **removed)
    {

        if (!root) {
            // element to be removed was not found
            *removed = NULL;
            return root;
        }
        node *lc = root->lc, *rc = root->rc;
        int k;

        int cmp;
        if (keyfinder) {
            cmp = keyfinder->cmp(root->payload);
        } else {
            cmp = root->lc ? -1 : 0;
        }

        if (cmp < 0) {
            lc = remove_main(lc, keyfinder, removed);

            rewrite(root, lc, rc);
            k = height(lc);

            if (height(rc) == k + 2) {
                node *rlc = rc->lc;
                if (height(rlc) == k + 1) {
                    rc = rotate_right(rc);
                    rewrite(root, lc, rc);
                }
                return rotate_left(root);
            }
        } else {
            if (cmp > 0) {
                rc = remove_main(rc, keyfinder, removed);
            } else {
                *removed = root;
                if (!root->lc && !root->rc) {
                    return NULL;
                } else if (!root->lc) {
                    return root->rc;
                } else if (!root->rc) {
                    return root->lc;
                } else {
                    rc = remove_main<PayloadComparable>(rc, NULL, &root);
                    rewrite(root, lc, rc);
                }
            }

            rewrite(root, lc, rc);
            k = height(rc);

            if (height(lc) == k + 2) {
                node *lrc = lc->rc;
                if (height(lrc) == k + 1) {
                    lc = rotate_left(lc);
                    rewrite(root, lc, rc);
                }
                return rotate_right(root);
            }
        }

        return root;
    }

    node *root;

  public:
    AVLMem() : root(NULL) {}
    ~AVLMem() { free_node(root); }

    template <class PayloadComparable>
    bool remove(const PayloadComparable &keyfinder, Payload *removed_payload)
    {

        node *removed;
        root = remove_main(root, &keyfinder, &removed);

        if (!removed)
            return false;

        if (removed_payload)
            *removed_payload = removed->payload;
        delete removed;
        return true;
    }

    void insert(const Payload &payload)
    {
        node *n = new node;
        n->lc = n->rc = NULL;
        n->payload = payload;
        n->annotation = Annotation(n->payload);
        n->height = 1;
        root = insert_main(root, n);
    }

    using Searcher =
        std::function<int(Annotation *lhs, Payload &here, Annotation *rhs)>;

    bool search(Searcher searcher, Payload *found_payload)
    {
        node *n = root;
        while (n) {
            int direction =
                searcher(n->lc ? &n->lc->annotation : NULL, n->payload,
                         n->rc ? &n->rc->annotation : NULL);
            if (direction < 0) {
                n = n->lc;
            } else if (direction > 0) {
                n = n->rc;
            } else {
                if (found_payload)
                    *found_payload = n->payload;
                return true;
            }
        }
        return false;
    }
};

#endif // LIBTARMAC_MEMTREE_HH
