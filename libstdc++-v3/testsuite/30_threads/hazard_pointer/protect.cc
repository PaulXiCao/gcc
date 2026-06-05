// { dg-do run { target c++26 } }
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

#include <hazard_pointer>
#include <atomic>
#include <testsuite_hooks.h>

namespace
{
  struct Node : std::hazard_pointer_obj_base<Node>
  {
    int value = 0;
  };
}

// Returns current value of the atomic.
void test01()
{
  auto hp = std::make_hazard_pointer();
  Node x;
  x.value = 42;
  const std::atomic<Node*> src{&x};
  const Node* const p = hp.protect(src);
  VERIFY( p == &x );
  VERIFY( p->value == 42 );
}

// Returns nullptr for null atomic.
void test02()
{
  auto hp = std::make_hazard_pointer();
  const std::atomic<Node*> src{nullptr};
  VERIFY( hp.protect(src) == nullptr );
}

// Slot not empty after protect.
void test03()
{
  auto hp = std::make_hazard_pointer();
  Node x;
  const std::atomic<Node*> src{&x};
  (void)hp.protect(src);
  VERIFY( !hp.empty() );
}

// protect() matches atomic load.
void test04()
{
  auto hp = std::make_hazard_pointer();
  Node vals[3];
  vals[0].value = 1;
  vals[1].value = 2;
  vals[2].value = 3;
  const std::atomic<Node*> src{&vals[0]};
  const Node* const p = hp.protect(src);
  VERIFY( p == src.load() );
}

// Re-protect on the same handle updates the slot.
void test05()
{
  auto hp = std::make_hazard_pointer();
  Node a, b;
  std::atomic<Node*> src{&a};
  (void)hp.protect(src);
  src.store(&b, std::memory_order_relaxed);
  const Node* const p = hp.protect(src);
  VERIFY( p == &b );
}

int main()
{
  test01();
  test02();
  test03();
  test04();
  test05();
}
