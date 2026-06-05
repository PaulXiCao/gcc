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

// Transfers slot ownership.
void test01()
{
  const auto before = hpd::_S_default_domain()._M_active_slots();
  auto a = std::make_hazard_pointer();
  std::hazard_pointer b;
  VERIFY( !a.empty() );
  VERIFY( b.empty() );
  b = std::move(a);
  VERIFY( a.empty() );
  VERIFY( !b.empty() );
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before + 1 );
}

// Source becomes empty after move-assign.
void test02()
{
  auto a = std::make_hazard_pointer();
  std::hazard_pointer b;
  b = std::move(a);
  VERIFY( a.empty() );
}

// Destination releases prior slot.
void test03()
{
  const auto before = hpd::_S_default_domain()._M_active_slots();
  auto a = std::make_hazard_pointer();
  auto b = std::make_hazard_pointer();
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before + 2 );
  b = std::move(a);
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before + 1 );
}

// Self-assignment is a no-op.
void test04()
{
  auto hp = std::make_hazard_pointer();
  const auto before = hpd::_S_default_domain()._M_active_slots();
  auto& alias = hp;
  hp = std::move(alias);
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before );
  VERIFY( !hp.empty() );
}

// Move-assign empty to empty: both stay empty, no slot change.
void test05()
{
  const auto before = hpd::_S_default_domain()._M_active_slots();
  std::hazard_pointer a;
  std::hazard_pointer b;
  b = std::move(a);
  VERIFY( b.empty() );
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before );
}

int main()
{
  test01();
  test02();
  test03();
  test04();
  test05();
}
