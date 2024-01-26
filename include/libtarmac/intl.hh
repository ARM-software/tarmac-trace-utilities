/*
 * Copyright 2024 Arm Limited. All rights reserved.
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

#ifndef LIBTARMAC_INTL_HH
#define LIBTARMAC_INTL_HH

/*
 * Wrapper to cope with the gettext library only being available in
 * some builds.
 */

#include "cmake.h"

#if HAVE_LIBINTL
#include <libintl.h>
#define _(str) gettext(str)
#elif defined _WINDOWS
const char *fake_gettext(const char *text);
#define _(str) fake_gettext(str)
#else
#define _(str) str
#endif

void gettext_setup();

#endif // LIBTARMAC_INTL_HH
