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
#include <cstddef>
#include <vector>
#include <testsuite_hooks.h>

namespace hpd = std::__hazard_pointer;

// Returns non-empty handle.
void test01()
{
  auto hp = std::make_hazard_pointer();
  VERIFY( !hp.empty() );
}

// Increments active slot count by 1.
void test02()
{
  const auto before = hpd::_S_default_domain()._M_active_slots();
  auto hp = std::make_hazard_pointer();
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before + 1 );
}

// Multiple calls return distinct slots.
void test03()
{
  const auto before = hpd::_S_default_domain()._M_active_slots();
  auto a = std::make_hazard_pointer();
  auto b = std::make_hazard_pointer();
  auto c = std::make_hazard_pointer();
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before + 3 );
}

// Slot reused after handle destruction.
void test04()
{
  const auto before = hpd::_S_default_domain()._M_active_slots();
  {
    auto hp = std::make_hazard_pointer();
  }
  auto hp2 = std::make_hazard_pointer();
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before + 1 );
}

// Pool grows beyond initial capacity (_S_initial_slots == 8).
void test05()
{
  const auto before = hpd::_S_default_domain()._M_active_slots();
  constexpr std::size_t kExtra = 8 + 4;
  std::vector<std::hazard_pointer> hps;
  hps.reserve(kExtra);
  for (std::size_t i = 0; i < kExtra; ++i)
    hps.emplace_back(std::make_hazard_pointer());
  VERIFY( hpd::_S_default_domain()._M_active_slots() == before + kExtra );
}

int main()
{
  test01();
  test02();
  test03();
  test04();
  test05();
}