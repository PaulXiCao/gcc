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

// [saferecl.hp.base] declares retire() noexcept, so it must not be able to
// fail.  An implementation that pushes onto a std::vector makes OOM inside a
// noexcept function -- i.e. std::terminate().  This test replaces the global
// allocation functions and asserts that the retire path performs no
// allocation at all.
//
// Only the four unsized/sized plain overloads are replaced; the aligned forms
// are left alone, so the cache-line-aligned hazard pointer records are not
// counted.  That is deliberate and does not weaken the claim: records are
// created only by make_hazard_pointer(), which [saferecl.hp.holder.ctor]/3
// explicitly allows to throw bad_alloc, and never by retire().

#include <hazard_pointer>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <vector>
#include <testsuite_hooks.h>

namespace hpd = std::__hazard_pointer;

namespace
{
  std::atomic<bool> armed{false};
  std::atomic<int> allocations{0};

  void note_allocation() noexcept
  {
    if (armed.load(std::memory_order_relaxed))
      allocations.fetch_add(1, std::memory_order_relaxed);
  }

  struct Node : std::hazard_pointer_obj_base<Node>
  {
    int value = 0;
  };
}

// Replaced globals.  malloc/free are paired consistently across all four, so
// mixing with the untouched aligned overloads is safe.
//
// __gnu_test::counter from testsuite/util/replacement_memory_operators.h was
// considered and does not fit: its destructor throws when the live count is
// non-zero at exit, and the hazard pointer records are freed in ~_Domain
// during static-storage destruction, which need not run before it.
void* operator new(std::size_t n)
{
  note_allocation();
  void* const p = std::malloc(n == 0 ? 1 : n);
  if (p == nullptr)
    throw std::bad_alloc();
  return p;
}

void* operator new[](std::size_t n)
{ return ::operator new(n); }

void operator delete(void* p) noexcept
{ std::free(p); }

void operator delete[](void* p) noexcept
{ std::free(p); }

void operator delete(void* p, std::size_t) noexcept
{ std::free(p); }

void operator delete[](void* p, std::size_t) noexcept
{ std::free(p); }

// The counter is only evidence if it can actually count.  A green run of the
// tests below proves nothing unless the harness is known to fire.  Note that
// the pair below is ::operator new / ::operator delete rather than a
// new-expression: the compiler is allowed to elide the latter, and an elided
// control proves nothing.
void test01()
{
  static std::atomic<void*> sink{nullptr};

  allocations.store(0, std::memory_order_relaxed);
  armed.store(true, std::memory_order_relaxed);
  sink.store(::operator new(64), std::memory_order_relaxed);
  armed.store(false, std::memory_order_relaxed);
  ::operator delete(sink.exchange(nullptr, std::memory_order_relaxed));

  VERIFY( allocations.load(std::memory_order_relaxed) == 1 );
}

// retire() is a pointer splice and allocates nothing.
void test02()
{
  // Hold hazard pointers so the auto-synchronize threshold (> 2 * active
  // count) is not crossed by the retires below.  Acquiring them may allocate;
  // that happens before arming.
  constexpr int kHandles = 4;
  std::vector<std::hazard_pointer> hps;
  hps.reserve(kHandles);
  for (int i = 0; i < kHandles; ++i)
    hps.push_back(std::make_hazard_pointer());

  // Drain leftovers and force this thread's retire-list node to register, so
  // that neither happens inside the measured region.
  hpd::_S_default_domain()._M_synchronize();
  VERIFY( hpd::_S_default_domain()._M_retire_list_size() == 0 );

  constexpr int kRetires = 2 * kHandles; // threshold is >, so this stays under
  std::vector<Node*> nodes;
  nodes.reserve(kRetires);
  for (int i = 0; i < kRetires; ++i)
    nodes.push_back(new Node());

  allocations.store(0, std::memory_order_relaxed);
  armed.store(true, std::memory_order_relaxed);
  for (Node* n : nodes)
    n->retire();
  armed.store(false, std::memory_order_relaxed);

  // retire() allocated; the retire list is not purely intrusive.
  VERIFY( allocations.load(std::memory_order_relaxed) == 0 );
  // Proves the retires actually landed, rather than the count being zero
  // because nothing happened.
  VERIFY( hpd::_S_default_domain()._M_retire_list_size()
	    == std::size_t(kRetires) );

  hps.clear();
  hpd::_S_default_domain()._M_synchronize();
}

// The other half of the guarantee: retire() may auto-synchronize, so a
// reclamation that allocates puts the allocation back on retire()'s path.
// Records live in a list the scan walks directly, and the protected-set
// buffer is owned by the thread and reused, so once it has been sized a
// reclamation allocates nothing at all.
void test03()
{
  constexpr int kHandles = 4;
  std::vector<std::hazard_pointer> hps;
  hps.reserve(kHandles);
  for (int i = 0; i < kHandles; ++i)
    hps.push_back(std::make_hazard_pointer());

  // Warm-up: the first scan on this thread sizes the buffer, and the records
  // themselves are created on demand.  Both are allowed to allocate -- the
  // claim is about the steady state, so reach it before measuring.
  for (int i = 0; i < 4; ++i)
    (new Node())->retire();
  hpd::_S_default_domain()._M_synchronize();
  hpd::_S_default_domain()._M_synchronize();

  std::vector<Node*> nodes;
  nodes.reserve(8);
  for (int i = 0; i < 8; ++i)
    nodes.push_back(new Node());

  allocations.store(0, std::memory_order_relaxed);
  armed.store(true, std::memory_order_relaxed);
  for (Node* n : nodes)
    n->retire();
  hpd::_S_default_domain()._M_synchronize();
  armed.store(false, std::memory_order_relaxed);

  // Reclamation allocated in the steady state.
  VERIFY( allocations.load(std::memory_order_relaxed) == 0 );
  // Nothing was actually reclaimed.
  VERIFY( hpd::_S_default_domain()._M_retire_list_size() == 0 );
}

// And reclamation still happens at all.
void test04()
{
  int destroyed = 0;
  struct Counted : std::hazard_pointer_obj_base<Counted>
  {
    int* sink;
    explicit Counted(int* s) : sink(s) {}
    ~Counted() { ++*sink; }
  };

  for (int i = 0; i < 3; ++i)
    (new Counted(&destroyed))->retire();
  hpd::_S_default_domain()._M_synchronize();

  VERIFY( destroyed == 3 );
}

int main()
{
  test01();
  test02();
  test03();
  test04();
}
