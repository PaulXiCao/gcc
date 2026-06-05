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
  struct Tracked : std::hazard_pointer_obj_base<Tracked>
  {
    explicit Tracked(int& c) : counter(c) {}
    ~Tracked() { ++counter; }
    int& counter;
  };
}

// Member swap exchanges non-empty and empty.
void test01()
{
  auto a = std::make_hazard_pointer();
  std::hazard_pointer b;
  VERIFY( !a.empty() );
  VERIFY( b.empty() );
  a.swap(b);
  VERIFY( a.empty() );
  VERIFY( !b.empty() );
}

// Member swap both non-empty: slot count unchanged.
void test02()
{
  const auto before = hpd::_S_default_domain()._M_active_slots();
  auto a = std::make_hazard_pointer();
  auto b = std::make_hazard_pointer();
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before + 2 );
  a.swap(b);
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before + 2 );
}

// After swap, the protecting slot moves to the other handle.
void test03()
{
  auto hp1 = std::make_hazard_pointer();
  auto hp2 = std::make_hazard_pointer();

  int dtor = 0;
  auto* obj = new Tracked(dtor);
  const std::atomic<Tracked*> src{obj};
  (void)hp1.protect(src);

  hp1.swap(hp2);

  obj->retire();
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( dtor == 0 );

  hp2.reset_protection();
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( dtor == 1 );
}

// Free swap exchanges slots.
void test04()
{
  auto a = std::make_hazard_pointer();
  std::hazard_pointer b;
  VERIFY( !a.empty() );
  VERIFY( b.empty() );
  std::swap(a, b);
  VERIFY( a.empty() );
  VERIFY( !b.empty() );
}

// Free swap both non-empty: count unchanged.
void test05()
{
  const auto before = hpd::_S_default_domain()._M_active_slots();
  auto a = std::make_hazard_pointer();
  auto b = std::make_hazard_pointer();
  std::swap(a, b);
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before + 2 );
}

// Member self-swap is a no-op.
void test06()
{
  auto hp = std::make_hazard_pointer();
  VERIFY( !hp.empty() );
  hp.swap(hp);
  VERIFY( !hp.empty() );
}

// Free swap both empty: remains empty.
void test07()
{
  std::hazard_pointer a;
  std::hazard_pointer b;
  std::swap(a, b);
  VERIFY( a.empty() );
  VERIFY( b.empty() );
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
