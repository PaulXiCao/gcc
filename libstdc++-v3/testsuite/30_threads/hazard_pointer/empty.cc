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
#include <utility>
#include <testsuite_hooks.h>

namespace
{
  struct Node : std::hazard_pointer_obj_base<Node> {};
}

void test01()
{
  const std::hazard_pointer hp;
  VERIFY( hp.empty() );
}

void test02()
{
  auto hp = std::make_hazard_pointer();
  VERIFY( !hp.empty() );
}

// After move ctor: source empty, dest non-empty.
void test03()
{
  auto a = std::make_hazard_pointer();
  const std::hazard_pointer b = std::move(a);
  VERIFY( a.empty() );
  VERIFY( !b.empty() );
}

// After move-assign: source empty.
void test04()
{
  auto a = std::make_hazard_pointer();
  std::hazard_pointer b;
  b = std::move(a);
  VERIFY( a.empty() );
  VERIFY( !b.empty() );
}

// After protect(): still owns slot.
void test05()
{
  auto hp = std::make_hazard_pointer();
  Node x;
  const std::atomic<Node*> src{&x};
  (void)hp.protect(src);
  VERIFY( !hp.empty() );
}

// After reset_protection(): hazard cleared, slot kept.
void test06()
{
  auto hp = std::make_hazard_pointer();
  Node x;
  const std::atomic<Node*> src{&x};
  (void)hp.protect(src);
  hp.reset_protection();
  VERIFY( !hp.empty() );
}

// After swap: ownership exchanged.
void test07()
{
  auto a = std::make_hazard_pointer();
  std::hazard_pointer b;
  VERIFY( !a.empty() );
  VERIFY( b.empty() );
  a.swap(b);
  VERIFY( a.empty() );
  VERIFY( !b.empty() );
}

int main()
{
  test01();
  test02();
  test03();
  test04();
  test05();
  test06();
  test07();
}
