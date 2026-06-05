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
    explicit Tracked(int& counter) : counter_(counter) {}
    ~Tracked() { ++counter_; }
    int& counter_;
  };
}

// Retired object deleted after synchronize.
void test01()
{
  // Hold one active slot so the auto-sync threshold (2 * active_count) >= 2,
  // ensuring the single retire below does NOT auto-sync.
  auto hp = std::make_hazard_pointer();
  int dtor_count = 0;
  (new Tracked(dtor_count))->retire();
  VERIFY( dtor_count == 0 );
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( dtor_count == 1 );
}

// Multiple retired objects all deleted on synchronize.
void test02()
{
  int dtor_count = 0;
  for (int i = 0; i < 5; ++i)
    (new Tracked(dtor_count))->retire();
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( dtor_count == 5 );
}

// Protected object survives synchronize.
void test03()
{
  int dtor_count = 0;
  auto* obj = new Tracked(dtor_count);
  const std::atomic<Tracked*> src{obj};

  auto hp = std::make_hazard_pointer();
  const Tracked* const protected_obj = hp.protect(src);
  VERIFY( protected_obj == obj );

  obj->retire();
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( dtor_count == 0 );

  hp.reset_protection();
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( dtor_count == 1 );
}

// Retire list grows before synchronize.
void test04()
{
  auto hp = std::make_hazard_pointer();
  const auto before = hpd::_S_default_domain()._M_retire_list_size();
  int dtor_count = 0;
  (new Tracked(dtor_count))->retire();
  (new Tracked(dtor_count))->retire();
  VERIFY( hpd::_S_default_domain()._M_retire_list_size() == before + 2 );
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( hpd::_S_default_domain()._M_retire_list_size() == 0u );
  VERIFY( dtor_count == 2 );
}

// Threshold auto-triggers synchronize: 1 active slot → threshold 2 → 3 retires trip it.
void test05()
{
  auto hp = std::make_hazard_pointer();
  int dummy = 0;
  Tracked x{dummy};
  const std::atomic<Tracked*> src{&x};
  const Tracked* const protected_x = hp.protect(src);
  VERIFY( protected_x == &x );

  int dtor_count = 0;
  for (int i = 0; i < 3; ++i)
    (new Tracked(dtor_count))->retire();

  VERIFY( dtor_count > 0 );
}

int main()
{
  test01();
  test02();
  test03();
  test04();
  test05();
}
