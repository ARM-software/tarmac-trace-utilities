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

char array[] = "thequickbrownfoxjumpsoverthelazydog\n";

static inline void swap(char *a, char *b)
{
    char tmp = *a;
    *a = *b;
    *b = tmp;
}

void quicksort(char *array, size_t size)
{
    /* Base case: arrays of size 0 or 1 are already sorted */
    if (size <= 1)
        return;

    /* Pick pivot in the simplest possible way */
    char pivot = array[0];

    size_t i = 1, j = 1;
    /* Invariant: array[0,...,i-1] <= pivot, array[i,...,j-1] > pivot */
    while (j < size) {
        if (array[j] > pivot) {
            /* add array[j] to the right part */
            j++;
        } else {
            /* add array[j] to the left part */
            swap(&array[i], &array[j]);
            i++;
            j++;
        }
    }

    /* Swap the pivot into its midpoint position, namely i-1 */
    swap(&array[0], &array[i - 1]);

    /* And recurse into two arrays guaranteed smaller */
    quicksort(array, i - 1);
    quicksort(array + i, j - i);
}

void c_entry(void)
{
    quicksort(array, sizeof(array) - 2);
    sys_write0(array);
    sys_exit(0);
}
