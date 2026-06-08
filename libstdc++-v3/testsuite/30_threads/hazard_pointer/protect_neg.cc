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

// Negative tests: protect<T>() must be rejected when T is not _Protectable.

#include <hazard_pointer>
#include <atomic>

struct Plain {};

void test_int()
{
  auto hp = std::make_hazard_pointer();
  std::atomic<int*> src{nullptr};
  (void)hp.protect(src); // int is not _Protectable
}

void test_nonhazard_class()
{
  auto hp = std::make_hazard_pointer();
  std::atomic<Plain*> src{nullptr};
  (void)hp.protect(src); // Plain has no _Obj_base_tag base
}

// { dg-error "hazard-protectable type" "" { target *-*-* } 0 }
