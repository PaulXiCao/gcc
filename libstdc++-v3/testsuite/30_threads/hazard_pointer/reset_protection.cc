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
  struct Node : std::hazard_pointer_obj_base<Node> {};

  struct Tracked : std::hazard_pointer_obj_base<Tracked>
  {
    explicit Tracked(int& c) : counter(c) {}
    ~Tracked() { ++counter; }
    int& counter;
  };
}

// reset_protection clears hazard, slot still owned.
void test01()
{
  auto hp = std::make_hazard_pointer();
  Node x;
  const std::atomic<Node*> src{&x};
  (void)hp.protect(src);
  hp.reset_protection();
  VERIFY( !hp.empty() );
}

// nullptr overload clears hazard.
void test02()
{
  auto hp = std::make_hazard_pointer();
  Node x;
  const std::atomic<Node*> src{&x};
  (void)hp.protect(src);
  hp.reset_protection(nullptr);
  VERIFY( !hp.empty() );
}

// After reset, object can be reclaimed.
void test03()
{
  auto hp = std::make_hazard_pointer();
  int dtor_count = 0;
  auto* obj = new Tracked(dtor_count);
  const std::atomic<Tracked*> src{obj};
  (void)hp.protect(src);

  obj->retire();
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( dtor_count == 0 );

  hp.reset_protection();
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( dtor_count == 1 );
}

// Typed-pointer overload publishes a hazard.
void test04()
{
  auto hp = std::make_hazard_pointer();
  int dtor = 0;
  auto* obj = new Tracked(dtor);
  hp.reset_protection(obj);

  obj->retire();
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( dtor == 0 );

  hp.reset_protection();
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( dtor == 1 );
}

// Double reset is safe (idempotent).
void test05()
{
  auto hp = std::make_hazard_pointer();
  Node x;
  const std::atomic<Node*> src{&x};
  (void)hp.protect(src);
  hp.reset_protection();
  hp.reset_protection();
  VERIFY( !hp.empty() );
}

int main()
{
  test01();
  test02();
  test03();
  test04();
  test05();
}
