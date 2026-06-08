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
#include <utility>
#include <testsuite_hooks.h>

namespace hpd = std::__hazard_pointer;

// Default-constructed handle is empty.
void test01()
{
  const std::hazard_pointer hp;
  VERIFY( hp.empty() );
}

// Default-constructed handle does not increment active slot count.
void test02()
{
  const auto before = hpd::_S_default_domain()._M_active_slots();
  const std::hazard_pointer hp;
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before );
}

// Move ctor transfers slot ownership: count unchanged.
void test03()
{
  const auto before = hpd::_S_default_domain()._M_active_slots();
  auto a = std::make_hazard_pointer();
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before + 1 );
  const std::hazard_pointer b = std::move(a);
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before + 1 );
}

// Move ctor: source becomes empty.
void test04()
{
  auto a = std::make_hazard_pointer();
  const std::hazard_pointer b = std::move(a);
  VERIFY( a.empty() );
  VERIFY( !b.empty() );
}

// Destructor releases slot.
void test05()
{
  const auto before = hpd::_S_default_domain()._M_active_slots();
  {
    auto hp = std::make_hazard_pointer();
    VERIFY( hpd::_S_default_domain()._M_active_slots() == before + 1 );
  }
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before );
}

// Destructor on default-constructed handle is a no-op.
void test06()
{
  const auto before = hpd::_S_default_domain()._M_active_slots();
  {
    const std::hazard_pointer hp;
  }
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before );
}

// Multiple hazard_pointers are distinct slots.
void test07()
{
  const auto before = hpd::_S_default_domain()._M_active_slots();
  auto a = std::make_hazard_pointer();
  auto b = std::make_hazard_pointer();
  auto c = std::make_hazard_pointer();
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before + 3 );
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
