// { dg-do compile { target c++26 } }
// { dg-additional-options "-pthread -Werror=unused-result" { target pthread } }
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

// Negative tests: each [[nodiscard]] return discard must trip
// -Werror=unused-result.

#include <hazard_pointer>
#include <atomic>

struct Node : std::hazard_pointer_obj_base<Node> {};

void test_make_hazard_pointer()
{
  std::make_hazard_pointer(); // { dg-error "ignoring return value" }
}

void test_empty()
{
  auto hp = std::make_hazard_pointer();
  hp.empty(); // { dg-error "ignoring return value" }
}

void test_protect()
{
  auto hp = std::make_hazard_pointer();
  Node x;
  std::atomic<Node*> src{&x};
  hp.protect(src); // { dg-error "ignoring return value" }
}

void test_try_protect()
{
  auto hp = std::make_hazard_pointer();
  Node x;
  std::atomic<Node*> src{&x};
  Node* ptr = &x;
  hp.try_protect(ptr, src); // { dg-error "ignoring return value" }
}

// { dg-prune-output "some warnings being treated as errors" }
