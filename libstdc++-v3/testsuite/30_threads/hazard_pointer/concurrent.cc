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
#include <chrono>
#include <functional>
#include <thread>
#include <vector>
#include <testsuite_hooks.h>

namespace hpd = std::__hazard_pointer;

namespace
{
  struct Node : std::hazard_pointer_obj_base<Node>
  {
    int value;
    explicit Node(int v) : value(v) {}
  };

  void reader_fn(std::atomic<Node*>& shared, std::atomic<bool>& stop,
                 std::atomic<int>& errors)
  {
    auto hp = std::make_hazard_pointer();
    while (!stop.load(std::memory_order_acquire))
    {
      const Node* const p = hp.protect(shared);
      if (p && p->value < 0)
        errors.fetch_add(1, std::memory_order_relaxed);
      hp.reset_protection();
    }
  }

  void writer_fn(std::atomic<Node*>& shared, std::atomic<bool>& stop)
  {
    int counter = 0;
    while (!stop.load(std::memory_order_acquire))
    {
      Node* next = new Node(++counter);
      Node* const old = shared.exchange(next, std::memory_order_seq_cst);
      if (old)
        old->retire();
    }
    Node* final_ = shared.exchange(nullptr, std::memory_order_seq_cst);
    if (final_)
      final_->retire();
  }
}

// Readers and one writer: no data race, no error from reading freed memory.
void test01()
{
  std::atomic<Node*> shared{new Node(1)};
  std::atomic<bool> stop{false};
  std::atomic<int> errors{0};

  constexpr int kReaders = 4;
  std::vector<std::thread> threads;
  threads.reserve(kReaders + 1);

  for (int i = 0; i < kReaders; ++i)
    threads.emplace_back(reader_fn, std::ref(shared), std::ref(stop),
                         std::ref(errors));
  threads.emplace_back(writer_fn, std::ref(shared), std::ref(stop));

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  stop.store(true, std::memory_order_release);
  for (auto& t : threads)
    t.join();

  hpd::_S_default_domain()._M_synchronize();
  VERIFY( errors.load() == 0 );
}

// protect() sees the latest value across stores.
void test02()
{
  auto hp = std::make_hazard_pointer();
  Node a{10}, b{20};
  std::atomic<Node*> src{&a};

  const Node* const p1 = hp.protect(src);
  VERIFY( p1 == &a );

  src.store(&b, std::memory_order_release);
  const Node* const p2 = hp.protect(src);
  VERIFY( p2 == &b );
}

// Multiple writers, multiple readers: no data race.
void test03()
{
  constexpr int kReaders = 4;
  constexpr int kWriters = 4;

  std::vector<std::atomic<Node*>> srcs(kWriters);
  for (auto& s : srcs)
    s.store(new Node(1), std::memory_order_relaxed);

  std::atomic<bool> stop{false};
  std::atomic<int> errors{0};

  auto multi_reader = [&] {
    std::vector<std::hazard_pointer> hps;
    hps.reserve(kWriters);
    for (int i = 0; i < kWriters; ++i)
      hps.emplace_back(std::make_hazard_pointer());
    while (!stop.load(std::memory_order_acquire))
      for (int i = 0; i < kWriters; ++i)
      {
        const Node* const p = hps[i].protect(srcs[i]);
        if (p && p->value < 0)
          errors.fetch_add(1, std::memory_order_relaxed);
        hps[i].reset_protection();
      }
  };

  auto single_writer = [&](int idx) {
    int counter = 1;
    while (!stop.load(std::memory_order_acquire))
    {
      Node* next = new Node(++counter);
      Node* old = srcs[idx].exchange(next, std::memory_order_seq_cst);
      if (old)
        old->retire();
    }
    Node* final_ = srcs[idx].exchange(nullptr, std::memory_order_seq_cst);
    if (final_)
      final_->retire();
  };

  std::vector<std::thread> threads;
  threads.reserve(kReaders + kWriters);
  for (int i = 0; i < kReaders; ++i)
    threads.emplace_back(multi_reader);
  for (int i = 0; i < kWriters; ++i)
    threads.emplace_back(single_writer, i);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  stop.store(true, std::memory_order_release);
  for (auto& t : threads)
    t.join();

  hpd::_S_default_domain()._M_synchronize();
  VERIFY( errors.load() == 0 );
}

// Retire from many threads: all reclaimed by thread-exit drain.
void test04()
{
  constexpr int kThreads = 8;
  constexpr int kObjectsPerThread = 10;
  std::atomic<int> total_deleted{0};

  struct CountedNode : std::hazard_pointer_obj_base<CountedNode>
  {
    std::atomic<int>& counter;
    explicit CountedNode(std::atomic<int>& c) : counter(c) {}
    ~CountedNode() { counter.fetch_add(1, std::memory_order_relaxed); }
  };

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int i = 0; i < kThreads; ++i)
    threads.emplace_back([&] {
      for (int j = 0; j < kObjectsPerThread; ++j)
        (new CountedNode(total_deleted))->retire();
    });
  for (auto& t : threads)
    t.join();

  VERIFY( total_deleted.load() == kThreads * kObjectsPerThread );
}

// High contention: readers + writer over longer duration.
void test05()
{
  constexpr int kReaders = 6;
  std::atomic<Node*> shared{new Node(42)};
  std::atomic<bool> stop{false};
  std::atomic<int> errors{0};

  std::vector<std::thread> threads;
  threads.reserve(kReaders + 1);

  for (int i = 0; i < kReaders; ++i)
    threads.emplace_back(reader_fn, std::ref(shared), std::ref(stop),
                         std::ref(errors));
  threads.emplace_back(writer_fn, std::ref(shared), std::ref(stop));

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  stop.store(true, std::memory_order_release);
  for (auto& t : threads)
    t.join();

  hpd::_S_default_domain()._M_synchronize();
  VERIFY( errors.load() == 0 );
}

int main()
{
  test01();
  test02();
  test03();
  test04();
  test05();
}
