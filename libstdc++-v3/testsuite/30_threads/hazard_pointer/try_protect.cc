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

namespace hpd = std::__hazard_pointer;

namespace
{
  struct Node : std::hazard_pointer_obj_base<Node>
  {
    int value = 0;
  };

  struct Tracked : std::hazard_pointer_obj_base<Tracked>
  {
    explicit Tracked(int& c) : counter(c) {}
    ~Tracked() { ++counter; }
    int& counter;
  };
}

// Returns true when pointer is stable.
void test01()
{
  auto hp = std::make_hazard_pointer();
  Node x;
  const std::atomic<Node*> src{&x};
  Node* ptr = src.load(std::memory_order::relaxed);
  VERIFY( hp.try_protect(ptr, src) );
}

// Sets ptr on success.
void test02()
{
  auto hp = std::make_hazard_pointer();
  Node x;
  x.value = 7;
  const std::atomic<Node*> src{&x};
  Node* ptr = src.load(std::memory_order::relaxed);
  const bool ok = hp.try_protect(ptr, src);
  VERIFY( ok );
  VERIFY( ptr == &x );
}

// Returns true for null source.
void test03()
{
  auto hp = std::make_hazard_pointer();
  const std::atomic<Node*> src{nullptr};
  Node* ptr = nullptr;
  VERIFY( hp.try_protect(ptr, src) );
  VERIFY( ptr == nullptr );
}

// Slot still owned after success.
void test04()
{
  auto hp = std::make_hazard_pointer();
  Node x;
  const std::atomic<Node*> src{&x};
  Node* ptr = &x;
  (void)hp.try_protect(ptr, src);
  VERIFY( !hp.empty() );
}

// Protected object not reclaimed after success.
void test05()
{
  auto hp = std::make_hazard_pointer();
  int dtor_count = 0;
  auto* obj = new Tracked(dtor_count);
  const std::atomic<Tracked*> src{obj};
  Tracked* ptr = src.load(std::memory_order::relaxed);
  VERIFY( hp.try_protect(ptr, src) );

  obj->retire();
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( dtor_count == 0 );

  hp.reset_protection();
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( dtor_count == 1 );
}

// try_protect and protect agree on a stable pointer.
void test06()
{
  Node x;
  const std::atomic<Node*> src{&x};

  auto hp1 = std::make_hazard_pointer();
  auto hp2 = std::make_hazard_pointer();

  Node* ptr = src.load(std::memory_order::relaxed);
  const bool ok = hp1.try_protect(ptr, src);
  const Node* const via_protect = hp2.protect(src);

  VERIFY( ok );
  VERIFY( ptr == via_protect );
  VERIFY( ptr == &x );
}

int main()
{
  test01();
  test02();
  test03();
  test04();
  test05();
  test06();
}
