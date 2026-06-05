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
  struct LoggingNode;

  struct LoggingDeleter
  {
    int* delete_count = nullptr;
    LoggingDeleter() = default;
    explicit LoggingDeleter(int* c) : delete_count(c) {}
    void operator()(LoggingNode* p) const;
  };

  struct LoggingNode : std::hazard_pointer_obj_base<LoggingNode, LoggingDeleter> {};

  inline void LoggingDeleter::operator()(LoggingNode* p) const
  {
    if (delete_count)
      ++(*delete_count);
    delete p;
  }

  struct TrackedNode : std::hazard_pointer_obj_base<TrackedNode>
  {
    explicit TrackedNode(int& c) : counter(c) {}
    ~TrackedNode() { ++counter; }
    int& counter;
  };
}

// Default deleter calls delete.
void test01()
{
  int dtor_count = 0;
  auto* node = new TrackedNode(dtor_count);
  node->retire();
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( dtor_count == 1 );
}

// Functor deleter is invoked.
void test02()
{
  int delete_count = 0;
  auto* node = new LoggingNode();
  node->retire(LoggingDeleter{&delete_count});
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( delete_count == 1 );
}

// Functor deleter not called while protected.
void test03()
{
  int delete_count = 0;
  auto* node = new LoggingNode();
  const std::atomic<LoggingNode*> src{node};

  auto hp = std::make_hazard_pointer();
  (void)hp.protect(src);
  node->retire(LoggingDeleter{&delete_count});
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( delete_count == 0 );

  hp.reset_protection();
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( delete_count == 1 );
}

// retire() with no arg uses default-constructed D.
void test04()
{
  int dtor_count = 0;
  auto* node = new TrackedNode(dtor_count);
  node->retire();
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( dtor_count == 1 );
}

// Multiple nodes with custom deleter all invoked.
void test05()
{
  constexpr int kCount = 5;
  int delete_count = 0;
  for (int i = 0; i < kCount; ++i)
  {
    auto* node = new LoggingNode();
    node->retire(LoggingDeleter{&delete_count});
  }
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( delete_count == kCount );
}

int main()
{
  test01();
  test02();
  test03();
  test04();
  test05();
}
