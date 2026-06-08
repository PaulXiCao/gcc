// Copyright (C) 2026 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

// { dg-do compile { target c++26 } }
// { dg-require-effective-target hosted }
// { dg-add-options no_pch }

#include <hazard_pointer>

#ifndef __cpp_lib_hazard_pointer
# error "Feature-test macro for hazard_pointer missing in <hazard_pointer>"
#elif __cpp_lib_hazard_pointer != 202306L
# error "Feature-test macro for hazard_pointer has wrong value in <hazard_pointer>"
#endif