// { dg-do compile { target c++26 } }
// { dg-additional-options "-pthread" { target pthread } }
// { dg-require-gthreads "" }

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

// Negative test: retire() must be rejected when hazard_pointer_obj_base
// is a private base of T (is_convertible_v<T*, base*> fails).

#include <hazard_pointer>

struct Node : private std::hazard_pointer_obj_base<Node>
{
  using hazard_pointer_obj_base::retire;
};

void test()
{
  auto* p = new Node();
  p->retire();
}

// { dg-error "must be a public base of T" "" { target *-*-* } 0 }
// { dg-prune-output "inaccessible base of" }
