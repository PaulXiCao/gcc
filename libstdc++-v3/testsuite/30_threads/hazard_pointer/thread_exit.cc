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
#include <thread>
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

// Retired objects in a worker thread reclaimed on thread exit.
void test01()
{
  int dtor_count = 0;
  std::thread t([&] {
    for (int i = 0; i < 3; ++i)
      (new Tracked(dtor_count))->retire();
  });
  t.join();
  VERIFY( dtor_count == 3 );
}

// Protected survivors offloaded to orphan list, reclaimed by later synchronize.
void test02()
{
  int dtor_count = 0;
  auto* obj = new Tracked(dtor_count);
  const std::atomic<Tracked*> src{obj};

  auto hp = std::make_hazard_pointer();
  (void)hp.protect(src);

  std::thread t([&] {
    obj->retire();
  });
  t.join();

  VERIFY( dtor_count == 0 );

  hp.reset_protection();
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( dtor_count == 1 );
}

// Many threads each drain own retire list on exit.
void test03()
{
  constexpr int kThreads = 4;
  constexpr int kObjectsPerThread = 5;
  int dtor_counts[kThreads] = {};

  std::thread threads[kThreads];
  for (int i = 0; i < kThreads; ++i)
    threads[i] = std::thread([&dtor_counts, i] {
      for (int j = 0; j < kObjectsPerThread; ++j)
        (new Tracked(dtor_counts[i]))->retire();
    });
  for (auto& t : threads)
    t.join();

  for (int i = 0; i < kThreads; ++i)
    VERIFY( dtor_counts[i] == kObjectsPerThread );
}

int main()
{
  test01();
  test02();
  test03();
}
