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
  // --- Quarantined nodes: making reclamation observable -------------------
  //
  // The reader check here used to be `p->value < 0` while writers only ever
  // published `++counter`, so it could never fire: these tests could not
  // detect a use-after-free at all.
  //
  // The obvious repair, having ~Node() write a negative sentinel, does not
  // work either.  `delete p` runs the destructor and then operator delete,
  // and glibc immediately writes its tcache next/key over the first 16 bytes
  // of the chunk.  Node::value sits at offset 4, so the sentinel is replaced
  // by allocator bookkeeping -- which reads back as a small *positive* int --
  // nanoseconds after it is written.  The writer's next allocation then hands
  // the same chunk straight back, tcache being LIFO.  Measured on
  // x86_64/glibc: the sentinel was not visible once in five trials.
  //
  // So reclaimed nodes must not go back to the allocator.  The deleter
  // poisons the node and parks it here; the quarantine is drained only at
  // process exit.  The memory stays valid, so a reader that dereferences a
  // node the domain has already reclaimed reliably observes kPoisoned -- on
  // every target, with no dependence on a sanitizer, and without reading
  // freed memory (which would be UB the compiler is entitled to exploit).
  constexpr int kPoisoned = -1;

  // Quarantined nodes are never freed during a run, so the writers below are
  // bounded by a node budget as well as by the stop flag.
  constexpr int kMaxNodesPerWriter = 50000;

  struct QuarantinedNode;

  // An intrusive lock-free stack, not a mutex and a vector.  The deleter runs
  // from _M_synchronize(), which is noexcept, so a vector push_back that
  // threw bad_alloc would turn an OOM inside the test's own quarantine into
  // std::terminate() -- the exact failure mode the intrusive retire list
  // exists to remove.  Pushing onto a link the node already carries cannot
  // fail, and needs no mutex, so there is no lock-ordering argument to make
  // against the domain's own locks.
  struct Quarantine
  {
    std::atomic<QuarantinedNode*> head{nullptr};

    void reclaim(QuarantinedNode* ptr) noexcept;
    ~Quarantine();
  };

  // constinit is load-bearing, not decoration.  ~_Domain() drains the orphan
  // list through this deleter during static-storage destruction, so the
  // quarantine has to still be alive at that point.  Constant initialization
  // puts its construction before any dynamic initialization, and therefore
  // its destruction after the function-local static domain in
  // _S_default_domain().  Spelling it constinit makes the compiler enforce
  // that: a member without a constexpr default constructor breaks the build
  // here rather than the test at exit.
  constinit Quarantine quarantine;

  // A separate stateless type is required: retire() needs D to be default
  // constructible and move assignable, and Quarantine is neither invocable
  // nor movable.
  struct QuarantineDeleter
  {
    void operator()(QuarantinedNode* ptr) const { quarantine.reclaim(ptr); }
  };

  struct QuarantinedNode
    : std::hazard_pointer_obj_base<QuarantinedNode, QuarantineDeleter>
  {
    int value;
    // Quarantine link.  Only ever touched after the domain has handed the
    // node to the deleter, i.e. once the domain is done with it, so it cannot
    // alias the intrusive retire link inside the protected base.
    QuarantinedNode* qnext = nullptr;
    explicit QuarantinedNode(int v) : value(v) {}
  };

  // `value` is a plain int on purpose.  If the reclaim-side ordering is ever
  // wrong, this store races the reader's load of the same int, so a run under
  // -fsanitize=thread reports it and one bug has two independent detectors.
  //
  // The poison is written before the node is published to the stack, and the
  // release/acquire pair on head carries it to the destructor.  Nothing here
  // can throw, which is the point -- see the note on Quarantine above.
  void Quarantine::reclaim(QuarantinedNode* ptr) noexcept
  {
    ptr->value = kPoisoned;
    QuarantinedNode* next = head.load(std::memory_order_relaxed);
    do
      ptr->qnext = next;
    while (!head.compare_exchange_weak(next, ptr, std::memory_order_release,
				       std::memory_order_relaxed));
  }

  Quarantine::~Quarantine()
  {
    for (const QuarantinedNode* ptr = head.load(std::memory_order_acquire);
	 ptr != nullptr;)
      {
	const QuarantinedNode* const next = ptr->qnext; // read before delete
	delete ptr;
	ptr = next;
      }
  }

  // reads counts successful protects, so a test cannot pass by never
  // observing a node at all -- that vacuity is exactly what the original
  // version of this test got wrong.
  void quarantined_reader_fn(std::atomic<QuarantinedNode*>& shared,
			     std::atomic<bool>& stop,
			     std::atomic<int>& errors,
			     std::atomic<int>& reads)
  {
    auto hp = std::make_hazard_pointer();
    while (!stop.load(std::memory_order_acquire))
      {
	const QuarantinedNode* const p = hp.protect(shared);
	if (p)
	  {
	    reads.fetch_add(1, std::memory_order_relaxed);
	    if (p->value == kPoisoned)
	      errors.fetch_add(1, std::memory_order_relaxed);
	  }
	hp.reset_protection();
      }
  }

  void quarantined_writer_fn(std::atomic<QuarantinedNode*>& shared,
			     std::atomic<bool>& stop)
  {
    int counter = 0;
    while (!stop.load(std::memory_order_acquire)
	   && counter < kMaxNodesPerWriter)
      {
	QuarantinedNode* next = new QuarantinedNode(++counter);
	QuarantinedNode* const old
	  = shared.exchange(next, std::memory_order_seq_cst);
	if (old)
	  old->retire();
      }
    QuarantinedNode* final_
      = shared.exchange(nullptr, std::memory_order_seq_cst);
    if (final_)
      final_->retire();
  }

  // --- Plain nodes: the real operator delete path -------------------------
  //
  // Kept on the default deleter so that one test still frees for real while
  // readers are running.  Its detector is a sanitizer, not a value check: per
  // the note above, no sentinel can survive the free, so the dereference
  // itself is the point -- a failed protection surfaces as
  // heap-use-after-free under -fsanitize=address.
  struct Node : std::hazard_pointer_obj_base<Node>
  {
    int value;
    explicit Node(int v) : value(v) {}
  };

  // observed is a store, not an accumulator: summing values would overflow
  // int over millions of iterations.  It starts at kNothingObserved, which
  // writers never publish, so the test can still tell "read nothing" from
  // "read something".
  constexpr int kNothingObserved = -1;

  void reader_fn(std::atomic<Node*>& shared, std::atomic<bool>& stop,
		 std::atomic<int>& observed)
  {
    auto hp = std::make_hazard_pointer();
    while (!stop.load(std::memory_order_acquire))
      {
	const Node* const p = hp.protect(shared);
	if (p)
	  observed.store(p->value, std::memory_order_relaxed);
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

// Readers and one writer: no reader ever sees a reclaimed node.
void test01()
{
  std::atomic<QuarantinedNode*> shared{new QuarantinedNode(1)};
  std::atomic<bool> stop{false};
  std::atomic<int> errors{0};
  std::atomic<int> reads{0};

  constexpr int kReaders = 4;
  std::vector<std::thread> threads;
  threads.reserve(kReaders + 1);

  for (int i = 0; i < kReaders; ++i)
    threads.emplace_back(quarantined_reader_fn, std::ref(shared),
			 std::ref(stop), std::ref(errors), std::ref(reads));
  threads.emplace_back(quarantined_writer_fn, std::ref(shared),
		       std::ref(stop));

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  stop.store(true, std::memory_order_release);
  for (auto& t : threads)
    t.join();

  hpd::_S_default_domain()._M_synchronize();
  // A reader dereferenced a node the domain had already reclaimed.
  VERIFY( errors.load() == 0 );
  // No reader ever observed a node; the test proved nothing.
  VERIFY( reads.load() > 0 );
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

// Multiple writers, multiple readers.
void test03()
{
  constexpr int kReaders = 4;
  constexpr int kWriters = 4;

  std::vector<std::atomic<QuarantinedNode*>> srcs(kWriters);
  for (auto& s : srcs)
    s.store(new QuarantinedNode(1), std::memory_order_relaxed);

  std::atomic<bool> stop{false};
  std::atomic<int> errors{0};
  std::atomic<int> reads{0};

  auto multi_reader = [&] {
    std::vector<std::hazard_pointer> hps;
    hps.reserve(kWriters);
    for (int i = 0; i < kWriters; ++i)
      hps.emplace_back(std::make_hazard_pointer());
    while (!stop.load(std::memory_order_acquire))
      for (int i = 0; i < kWriters; ++i)
	{
	  const QuarantinedNode* const p = hps[i].protect(srcs[i]);
	  if (p)
	    {
	      reads.fetch_add(1, std::memory_order_relaxed);
	      if (p->value == kPoisoned)
		errors.fetch_add(1, std::memory_order_relaxed);
	    }
	  hps[i].reset_protection();
	}
  };

  auto single_writer = [&](int idx) {
    int counter = 1;
    while (!stop.load(std::memory_order_acquire)
	   && counter < kMaxNodesPerWriter)
      {
	QuarantinedNode* next = new QuarantinedNode(++counter);
	QuarantinedNode* old
	  = srcs[idx].exchange(next, std::memory_order_seq_cst);
	if (old)
	  old->retire();
      }
    QuarantinedNode* final_
      = srcs[idx].exchange(nullptr, std::memory_order_seq_cst);
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
  VERIFY( reads.load() > 0 );
}

// Retire from many threads: all reclaimed by the thread-exit drain.
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

// High contention, on the real operator delete path.
//
// What this can and cannot prove on x86, since a green run here is easy to
// over-read: it is a sampling test, so it can only ever produce weak positive
// evidence.  The reason it is weak is *not* "x86 is TSO, so the reordering
// cannot happen" -- that is false.  TSO permits exactly the StoreLoad
// reordering the bug needs, and the litmus shape does reproduce on x86_64
// (measured 1 and 127 positives per 10^6 with litmus7).  What plausibly hides
// it in the *real* code is narrower: the reclaim path's mutex performs locked
// RMWs between the removal store and the record scan.  So the honest claim is
// "unlikely to reproduce on x86, for a reason specific to the lock".
void test05()
{
  constexpr int kReaders = 6;
  std::atomic<Node*> shared{new Node(42)};
  std::atomic<bool> stop{false};
  std::atomic<int> observed{kNothingObserved};

  std::vector<std::thread> threads;
  threads.reserve(kReaders + 1);

  for (int i = 0; i < kReaders; ++i)
    threads.emplace_back(reader_fn, std::ref(shared), std::ref(stop),
			 std::ref(observed));
  threads.emplace_back(writer_fn, std::ref(shared), std::ref(stop));

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  stop.store(true, std::memory_order_release);
  for (auto& t : threads)
    t.join();

  hpd::_S_default_domain()._M_synchronize();
  // No reader ever observed a node; the test proved nothing.
  VERIFY( observed.load() != kNothingObserved );
}

int main()
{
  test01();
  test02();
  test03();
  test04();
  test05();
}
