// Implementation of std::hazard_pointer -*- C++ -*-

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

// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.

// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.

/** @file bits/hazard_ptr.h
 *  This is an internal header file, included by other library headers.
 *  Do not attempt to use it directly. @headername{hazard_pointer}
 */

#ifndef _GLIBCXX_HAZARD_PTR_H
#define _GLIBCXX_HAZARD_PTR_H 1

#ifdef _GLIBCXX_SYSHDR
#pragma GCC system_header
#endif

#include <atomic>             // std::atomic, std::atomic_thread_fence
#include <mutex>              // std::mutex, std::lock_guard
#include <vector>             // std::vector
#include <bits/move.h>        // std::move, std::swap, std::__exchange
#include <bits/stl_algo.h>    // std::sort, std::binary_search
#include <bits/unique_ptr.h>  // std::default_delete
#include <cstddef>            // std::size_t
#include <type_traits>        // std::is_class_v, std::is_base_of_v

namespace std _GLIBCXX_VISIBILITY(default)
{
_GLIBCXX_BEGIN_NAMESPACE_VERSION

  template<typename _Tp, typename _Dp = default_delete<_Tp>>
    class hazard_pointer_obj_base;

  class hazard_pointer;

  [[__nodiscard__]]
  inline hazard_pointer make_hazard_pointer();

  namespace __hazard_pointer
  {
    struct _Hazptr_obj;

    // Type-erased reclamation: invokes the object's deleter on the object.
    using _Reclaim_fn = void (*)(_Hazptr_obj*);

    // Non-template private base of hazard_pointer_obj_base<T, D>.
    //
    // Carries the intrusive retire link and the erased reclaim function, so
    // that retire() is a pointer splice into a per-thread list and cannot
    // allocate.  [saferecl.hp.base] declares retire() noexcept, so an
    // allocating retire turns OOM into terminate(); putting the list storage
    // in the object is the only way out, because a per-thread side table has
    // to be grown by retire() itself.
    //
    // It also carries the space P2530R3 1.5 ("Guidance for ABI-Stability")
    // reserves for the two extensions the paper expects.  That reservation is
    // a one-way door: hazard_pointer_obj_base is a standard-specified type
    // that users *derive from*, so its size is baked into user binaries, and
    // doc/xml/manual/abi.xml lists changing the layout of a standard-specified
    // type as a prohibited change.  The reserved members are initialised here
    // for the same reason: the constructor is inlined into user code, so code
    // compiled before a future extension would otherwise leave them
    // uninitialised.  The corresponding *check* in retire() can be added
    // later, since nothing today can set a non-null cohort.
    //
    // Doubles as the tag the _Protectable concept detects: std::is_base_of
    // ignores access, so one private base does both jobs.
    struct _Hazptr_obj
    {
      // _M_next == this means "not retired"; every constructor re-establishes
      // it.  Backs the [saferecl.hp.base]/6 precondition "x is not retired" in
      // every build rather than only under a debug mode, and costs nothing --
      // the link has to be there anyway.
      _Hazptr_obj* _M_next = this;
      _Reclaim_fn _M_reclaim = nullptr;

      // RESERVED for P2530R3 1.5.  Never read or written by this
      // implementation.  Item 2 is cohort-based synchronous reclamation,
      // item 1 integrated commutative counting; 64 bits because the reference
      // implementation's counting extension uses a 64-bit word, and because
      // with [[__no_unique_address__]] on the deleter it costs the same as 32.
      void* _M_cohort = nullptr;
      __UINT64_TYPE__ _M_count = 0;

      [[__nodiscard__]]
      bool
      _M_not_retired() const noexcept
      { return _M_next == this; }

      _Hazptr_obj() noexcept = default;

      // A copy is a new, unretired object, so the retirement state must not be
      // copied -- otherwise retiring the copy would look like a double retire.
      // This is what costs hazard_pointer_obj_base its trivial copyability,
      // and there is no layout that is both trivially copyable and correct: a
      // trivial copy necessarily copies the state.
      _Hazptr_obj(const _Hazptr_obj&) noexcept { }
      _Hazptr_obj(_Hazptr_obj&&) noexcept { }

      // Assignment leaves the retirement state alone: assigning to an object
      // does not retire or un-retire it.  Deliberately a no-op, so
      // self-assignment needs no special case.
      _Hazptr_obj&
      operator=(const _Hazptr_obj&) noexcept
      { return *this; }

      _Hazptr_obj&
      operator=(_Hazptr_obj&&) noexcept
      { return *this; }

      ~_Hazptr_obj() = default;
    };

    // Intrusive singly-linked list of _Hazptr_obj, with O(1) splice.
    //
    // Head and tail rather than head alone: _M_synchronize() concatenates
    // every thread's list into one chain, and without a tail pointer each
    // concatenation would walk the list it is appending to.
    //
    // This is a correctness change, not a performance one, and measurement
    // says so: against the pre-intrusive implementation, retire() alone is
    // 11.1ns versus 11.2ns -- the per-thread _M_list_mutex costs more than the
    // difference between a vector push_back and a pointer splice.  What it
    // buys is unconditional: [saferecl.hp.base] declares retire() noexcept,
    // and an allocating retire turns OOM into terminate().
    struct _Retire_list
    {
      _Hazptr_obj* _M_head = nullptr;
      _Hazptr_obj* _M_tail = nullptr;
      size_t _M_size = 0;       // O(1); _M_retire_impl()'s threshold needs it

      // Tests _M_head rather than _M_size: _M_head is the structural invariant
      // the traversal in _M_synchronize() relies on, whereas _M_size is a
      // cache maintained alongside it purely for the threshold.  If the two
      // ever disagree, believing _M_head fails safe.
      [[__nodiscard__]]
      bool
      _M_empty() const noexcept
      { return _M_head == nullptr; }

      void
      _M_push(_Hazptr_obj* __obj) noexcept
      {
	__obj->_M_next = _M_head;
	if (_M_empty())
	  _M_tail = __obj;
	_M_head = __obj;
	++_M_size;
      }

      // Move every node of __other to the front of *this; __other becomes
      // empty.  Order is irrelevant here -- the scan visits every node.
      void
      _M_splice(_Retire_list& __other) noexcept
      {
	if (__other._M_empty())
	  return;
	if (_M_empty())
	  {
	    _M_head = __other._M_head;
	    _M_tail = __other._M_tail;
	  }
	else
	  {
	    __other._M_tail->_M_next = _M_head;
	    _M_head = __other._M_head;
	  }
	_M_size += __other._M_size;
	__other._M_clear();
      }

      void
      _M_clear() noexcept
      {
	_M_head = _M_tail = nullptr;
	_M_size = 0;
      }

      // Detach the whole list, leaving *this empty.
      [[__nodiscard__]]
      _Retire_list
      _M_take() noexcept
      { return std::__exchange(*this, _Retire_list{}); }
    };

    // __GCC_DESTRUCTIVE_SIZE is not defined on targets that do not set the
    // interference-size params; 64 is the value all such targets would use.
#ifdef __GCC_DESTRUCTIVE_SIZE
    inline constexpr size_t _S_cacheline_size = __GCC_DESTRUCTIVE_SIZE;
#else
    inline constexpr size_t _S_cacheline_size = 64;
#endif

    // One hazard pointer record, padded to a full cache line so that records
    // owned by different threads do not share one.
    //
    // This is the "internal structure associated with the actual hazard
    // pointers" of P2530R3 1.5 item 3.  The paper is explicit that the domain
    // pointer belongs here rather than in hazard_pointer, and gives the
    // reason: keeping the handle one word wide is what buys the construction
    // and destruction cost of its section 3.1.
    //
    // Records are never destroyed before the domain is, and are never
    // unlinked.  That is what lets _M_synchronize() walk the list with plain
    // atomic loads and no lock, and what keeps the address a live
    // hazard_pointer holds stable.
    struct alignas(_S_cacheline_size) _Hazptr_rec
    {
      // The hazard pointer proper.  Holds a _Hazptr_obj* (see
      // reset_protection) or null.
      atomic<void*> _M_hazard{nullptr};

      // Claimed by a live hazard_pointer?  Released with a plain store,
      // claimed with a CAS, so neither path needs the allocation mutex.
      atomic<bool> _M_active{false};

      // Written once, before the record is published; read by every scan.
      _Hazptr_rec* _M_next = nullptr;

      // RESERVED for P2530R3 1.5 item 3 (custom domains).  Null means the
      // default domain, which is the representation the paper suggests, so a
      // future ~hazard_pointer can test it without changing the layout again.
      void* _M_domain = nullptr;
    };

    // Approximation of "hazard-protectable type" ([saferecl.hp.general]/2).
    // Checks that _Tp is a class deriving from some hazard_pointer_obj_base
    // <_Tp, _Dp> (for any _Dp), detected via the private _Hazptr_obj base
    // (std::is_base_of ignores access).  Not checked here, because _Dp is
    // unknown -- retire(), where _Dp is explicit, checks them: that the base
    // is public, that it is non-virtual, and that there is exactly one.
    template<typename _Tp>
      concept _Protectable = is_class_v<_Tp> && is_base_of_v<_Hazptr_obj, _Tp>;

  } // namespace __hazard_pointer

  // -------------------------------------------------------------------------
  // hazard_pointer_obj_base
  // -------------------------------------------------------------------------

  /// Base class for objects protected by hazard pointers.
  template<typename _Tp, typename _Dp>
    class hazard_pointer_obj_base : private __hazard_pointer::_Hazptr_obj
    {
    public:
      // Splices this object onto the calling thread's retire list in the
      // default domain.  Deletion is deferred until a scan confirms that no
      // hazard pointer holds this address.
      void retire(_Dp __d = _Dp()) noexcept;

    protected:
      hazard_pointer_obj_base() = default;
      hazard_pointer_obj_base(const hazard_pointer_obj_base&) = default;
      hazard_pointer_obj_base(hazard_pointer_obj_base&&) = default;
      hazard_pointer_obj_base&
      operator=(const hazard_pointer_obj_base&) = default;
      hazard_pointer_obj_base&
      operator=(hazard_pointer_obj_base&&) = default;
      ~hazard_pointer_obj_base() = default;

    private:
      // [[__no_unique_address__]] is what pays for the space _Hazptr_obj
      // reserves: with a stateless deleter (the default_delete case) it keeps
      // the empty member out of the object's size instead of costing a full
      // aligned word.
      [[__no_unique_address__]] _Dp _M_deleter;

      // reset_protection() has to map a _Tp* to the _Hazptr_obj subobject that
      // the scan compares against, and that base is private.  Private plus a
      // friend keeps the implicit _Tp* -> _Hazptr_obj* conversion out of user
      // overload resolution.
      friend class hazard_pointer;
    };

  namespace __hazard_pointer
  {
    // Layout tripwire.  hazard_pointer_obj_base is a standard-specified type
    // that users derive from, so its size is baked into user binaries and
    // cannot be changed once shipped -- doc/xml/manual/abi.xml lists that as a
    // prohibited change.  The reserved members exist precisely so the size
    // does not have to move later; pin it here so an accidental change fails
    // the build rather than the field.  Guarded on 8-byte pointers so 32-bit
    // targets are not held to a 64-bit number.
    struct _Abi_probe : hazard_pointer_obj_base<_Abi_probe> { };

    // Self-reporting, deliberately.  A bare static_assert on sizeof() says
    // only that the number moved, never what it moved to.  Encoding both
    // numbers as template arguments puts them in the diagnostic: the failure
    // reads _Abi_pin<40, 32>, actual first.
    template<size_t _Actual, size_t _Expected>
      struct _Abi_pin
      {
	static_assert(sizeof(void*) != 8 || _Actual == _Expected,
		      "layout changed -- this is an ABI break, not a "
		      "refactor.  The template arguments in this "
		      "diagnostic are <actual, expected>.");
	static constexpr bool _S_ok = true;
      };

    static_assert(_Abi_pin<sizeof(hazard_pointer_obj_base<_Abi_probe>),
			   32>::_S_ok);
    static_assert(_Abi_pin<alignof(hazard_pointer_obj_base<_Abi_probe>),
			   8>::_S_ok);
  } // namespace __hazard_pointer

  // -------------------------------------------------------------------------
  // hazard_pointer
  // -------------------------------------------------------------------------

  /// RAII handle owning one hazard pointer in the default domain.
  class hazard_pointer
  {
  public:
    hazard_pointer() noexcept = default;

    hazard_pointer(hazard_pointer&& __other) noexcept;

    hazard_pointer&
    operator=(hazard_pointer&& __other) noexcept;

    ~hazard_pointer();

    hazard_pointer(const hazard_pointer&) = delete;
    hazard_pointer& operator=(const hazard_pointer&) = delete;

    // True if this handle owns no hazard pointer.  A handle that owns an
    // unassociated hazard pointer (one holding nullptr) is NOT empty.
    [[__nodiscard__]]
    bool
    empty() const noexcept;

    template<typename _Tp>
      [[__nodiscard__]]
      _Tp*
      protect(const atomic<_Tp*>& __src) noexcept;

    template<typename _Tp>
      [[__nodiscard__]]
      bool
      try_protect(_Tp*& __ptr, const atomic<_Tp*>& __src) noexcept;

    template<typename _Tp>
      void
      reset_protection(const _Tp* __ptr) noexcept;

    void
    reset_protection(nullptr_t = nullptr) noexcept;

    void
    swap(hazard_pointer& __other) noexcept;

  private:
    // One word, and deliberately so: P2530R3 1.5 item 3 puts the reserved
    // domain pointer in the record rather than here, because a future
    // custom-domain extension must not have to grow hazard_pointer -- that
    // would be another prohibited layout change on a standard-specified type.
    // An index would have been the cheaper way to delete _M_release_rec()'s
    // pool scan, but an index cannot name a domain, so it would have bought
    // the scan back at the price of the extension.
    __hazard_pointer::_Hazptr_rec* _M_rec = nullptr;    // null = empty handle

    explicit hazard_pointer(__hazard_pointer::_Hazptr_rec* __rec) noexcept;

    friend hazard_pointer make_hazard_pointer();
  };

  namespace __hazard_pointer
  {
    // hazard_pointer is frozen for the same reason as
    // hazard_pointer_obj_base, and pinned the same way.  Staying one word is
    // the whole point of P2530R3 1.5 item 3.
    static_assert(_Abi_pin<sizeof(hazard_pointer), 8>::_S_ok,
		  "hazard_pointer must stay one word -- P2530R3 1.5 item 3");
  } // namespace __hazard_pointer

  // -------------------------------------------------------------------------
  // __hazard_pointer::_Domain
  // -------------------------------------------------------------------------

  namespace __hazard_pointer
  {
    struct _RetireListNode;

    // Owns the hazard pointer records and the pending retire lists.
    // Process-global singleton; construction and destruction are restricted
    // to _S_default_domain().
    class _Domain
    {
    public:
      // Claim an unused record, appending a new one if every existing record
      // is taken.  Throws std::bad_alloc only on OOM -- this is the one
      // function on the path from make_hazard_pointer(), which
      // [saferecl.hp.holder.ctor]/3 explicitly allows to throw.  Every
      // allocation this implementation performs on a live domain happens here.
      [[__nodiscard__]]
      _Hazptr_rec*
      _M_acquire_rec();

      // Return a record for reuse.  Never allocates, never locks, never fails.
      void
      _M_release_rec(_Hazptr_rec* __rec) noexcept;

      // Splice __obj onto the calling thread's retire list.
      void
      _M_retire_impl(_Hazptr_obj* __obj) noexcept;

      // Reclaim every retired object whose address is not held by any hazard
      // pointer.  noexcept: reached from retire(), which
      // [saferecl.hp.base] declares noexcept, and
      // [saferecl.hp.general]/5 makes a throwing deleter undefined behaviour.
      void
      _M_synchronize() noexcept;

      // Number of currently claimed records.  Exposed for testing.
      [[__nodiscard__]]
      size_t
      _M_active_slots() const noexcept;

      // Number of entries in the calling thread's retire list; exposed for
      // single-threaded testing.
      [[__nodiscard__]]
      size_t
      _M_retire_list_size() const noexcept;

    private:
      // Head of the append-only record list.  Published with release,
      // traversed with acquire; records are never unlinked, so a traversal
      // needs no lock and no snapshot array.  The free flag lives in the
      // record, which costs nothing because records are cache-line padded
      // anyway, and it is what makes _M_release_rec() O(1).
      atomic<_Hazptr_rec*> _M_recs_head{nullptr};

      // Serialises appends only.  The claim path is a CAS on
      // _Hazptr_rec::_M_active and does not take it; the scan does not take
      // it either.
      mutex _M_rec_alloc_mutex;

      atomic_size_t _M_active_count = 0;        // records currently claimed
      atomic_size_t _M_rec_count = 0;   // records ever created

      // Retired objects from threads that exited with still-protected
      // survivors.  Collected by _M_synchronize() alongside live-thread lists.
      _Retire_list _M_orphan_list;      // guarded by _M_orphan_mutex
      mutex _M_orphan_mutex;

      _RetireListNode* _M_retire_lists_head = nullptr;
      size_t _M_retire_list_node_count = 0;
      mutex _M_retire_lists_mutex;      // guards the two members above

      void
      _M_ensure_node_registered();

      void
      _M_unregister_node(const _RetireListNode& __node) noexcept;

      friend struct _RetireListNode;

      // Private ctor/dtor plus a friend declaration enforces at compile time
      // that the only _Domain instance is the static local in
      // _S_default_domain(), so static storage duration is guaranteed by
      // construction rather than by a runtime check.  The friend declaration
      // also covers the static-local destructor: the compiler registers the
      // atexit handler from within the friend function's scope.
      _Domain();
      ~_Domain();
      friend _Domain& _S_default_domain() noexcept;
    };

    // Process-wide default domain.  Lazily initialised on first use.
    inline _Domain&
    _S_default_domain() noexcept
    {
      static _Domain __domain;
      return __domain;
    }

    // Per-thread node in the retire-list registry.  Each thread that calls
    // _M_retire_impl() or _M_synchronize() has exactly one node, registered
    // lazily on first use and removed when the thread exits.
    //
    // _M_list_mutex guards _M_list against concurrent access between
    // _M_synchronize() (any thread) and _M_retire_impl() (the owning thread).
    // _M_retire_lists_mutex guards the registry structure (the _M_next
    // pointers and the head) but NOT the contents of _M_list.
    struct _RetireListNode
    {
      _Retire_list _M_list;             // guarded by _M_list_mutex
      mutex _M_list_mutex;
      _RetireListNode* _M_next = nullptr; // guarded by _M_retire_lists_mutex
      bool _M_registered = false;       // written once, by the owning thread

      // Scratch for _M_synchronize()'s protected set, owned by and reused on
      // this thread.  Its capacity only has to grow when the record pool does,
      // so after the first scan a steady-state reclamation allocates nothing.
      //
      // It cannot be sized from _M_acquire_rec() the way every other
      // allocation is: a thread that only ever retires never calls
      // make_hazard_pointer(), so it would never reach that path.  Growth
      // therefore happens inside a noexcept function and has to be able to
      // fail -- see _M_synchronize().
      vector<void*> _M_scan_buf;

      // Called on thread exit.  Drains the retire list, offloads survivors,
      // then unregisters.
      ~_RetireListNode();
    };

    inline thread_local _RetireListNode _S_tl_node;

  } // namespace __hazard_pointer

  // -------------------------------------------------------------------------
  // Free functions
  // -------------------------------------------------------------------------

  inline void
  swap(hazard_pointer& __a, hazard_pointer& __b) noexcept
  { __a.swap(__b); }

  // The only function in the public interface allowed to allocate, and the
  // only one that does -- [saferecl.hp.holder.ctor]/3, "Throws: May throw
  // bad_alloc".
  [[__nodiscard__]]
  inline hazard_pointer
  make_hazard_pointer()
  {
    return hazard_pointer(__hazard_pointer::_S_default_domain()
			    ._M_acquire_rec());
  }

  // -------------------------------------------------------------------------
  // hazard_pointer_obj_base implementation
  // -------------------------------------------------------------------------

  template<typename _Tp, typename _Dp>
    inline void
    hazard_pointer_obj_base<_Tp, _Dp>::retire(_Dp __d) noexcept
    {
      // noexcept per the standard API, and honoured on the allocation side:
      // _M_retire_impl() splices this object onto an intrusive list, so there
      // is no vector to grow and no way for OOM to reach std::terminate().
      // The only operation left that can throw is mutex::lock (system_error),
      // which is not a memory-pressure failure.
      static_assert(is_invocable_v<_Dp, _Tp*>,
		    "D must be invocable with T* -- [saferecl.hp.base]/1");
      static_assert(is_default_constructible_v<_Dp>,
		    "D must be default-constructible -- "
		    "[saferecl.hp.base]/3");
      static_assert(is_move_assignable_v<_Dp>,
		    "D must be move-assignable -- [saferecl.hp.base]/3");
      static_assert(is_base_of_v<hazard_pointer_obj_base<_Tp, _Dp>, _Tp>,
		    "T must derive from hazard_pointer_obj_base<T, D>"
		    " -- [saferecl.hp.base]/5");
      static_assert(is_convertible_v<_Tp*,
				     hazard_pointer_obj_base<_Tp, _Dp>*>,
		    "hazard_pointer_obj_base<T, D> must be a public base of T"
		    " -- [saferecl.hp.base]/5");
      static_assert(!is_virtual_base_of_v<hazard_pointer_obj_base<_Tp, _Dp>,
					  _Tp>,
		    "hazard_pointer_obj_base<T, D> must be a non-virtual base"
		    " of T -- [saferecl.hp.base]/5");

      // [saferecl.hp.base]/6 precondition: x is not retired.  The
      // _M_next == this sentinel backs it in every build, unlike a bool member
      // which would make sizeof(hazard_pointer_obj_base) depend on whether the
      // translation unit was compiled with assertions enabled.
      __glibcxx_assert(this->_M_not_retired());

      _M_deleter = std::move(__d);

      // Type erasure: the domain holds _Hazptr_obj*, and only this
      // instantiation knows _Tp and _Dp.  The cast to
      // hazard_pointer_obj_base is a real base-to-derived cast, so it works at
      // any offset -- no layout assumption is made.
      this->_M_reclaim = [](__hazard_pointer::_Hazptr_obj* __p)
	{
	  auto* const __base = static_cast<hazard_pointer_obj_base*>(__p);
	  _Tp* const __self = static_cast<_Tp*>(__base);
	  _Dp __d2 = std::move(__base->_M_deleter);
	  __d2(__self);
	};

      // Pass the _Hazptr_obj subobject, not the _Tp*: that is the address
      // reset_protection() publishes and the scan compares against.
      __hazard_pointer::_S_default_domain()._M_retire_impl(this);
    }

  // -------------------------------------------------------------------------
  // hazard_pointer implementation
  // -------------------------------------------------------------------------

  inline
  hazard_pointer::hazard_pointer(__hazard_pointer::_Hazptr_rec* __rec) noexcept
  : _M_rec(__rec)
  { }

  // Two stores and no lock, which is what P2530R3 1.5 item 3 asks of this
  // destructor: it calls it out by name as inlined and latency-critical.
  inline
  hazard_pointer::~hazard_pointer()
  {
    if (_M_rec)
      {
	reset_protection();
	__hazard_pointer::_S_default_domain()._M_release_rec(_M_rec);
      }
  }

  inline
  hazard_pointer::hazard_pointer(hazard_pointer&& __other) noexcept
  : _M_rec(__other._M_rec)
  { __other._M_rec = nullptr; }

  inline hazard_pointer&
  hazard_pointer::operator=(hazard_pointer&& __other) noexcept
  {
    if (this == &__other)
      return *this;
    if (!empty())
      {
	reset_protection();
	__hazard_pointer::_S_default_domain()._M_release_rec(_M_rec);
      }
    _M_rec = __other._M_rec;
    __other._M_rec = nullptr;
    return *this;
  }

  inline bool
  hazard_pointer::empty() const noexcept
  { return _M_rec == nullptr; }

  template<typename _Tp>
    inline bool
    hazard_pointer::try_protect(_Tp*& __ptr,
				const atomic<_Tp*>& __src) noexcept
    {
      static_assert(__hazard_pointer::_Protectable<_Tp>,
		    "T must be a hazard-protectable type"
		    " -- [saferecl.hp.holder.mem]/3");
      __glibcxx_assert(!empty());
      const _Tp* const __old = __ptr;
      reset_protection(__old);
      // seq_cst pairs with the seq_cst store in reset_protection(const T*) to
      // form an SC pair: either the scan sees the hazard or this reload sees
      // the new pointer.  acquire alone is insufficient on weak-memory
      // targets.
      __ptr = __src.load(memory_order::seq_cst);
      if (!(__old == __ptr))    // same expression as in the standard
	reset_protection();
      return __old == __ptr;
    }

  template<typename _Tp>
    inline _Tp*
    hazard_pointer::protect(const atomic<_Tp*>& __src) noexcept
    {
      static_assert(__hazard_pointer::_Protectable<_Tp>,
		    "T must be a hazard-protectable type"
		    " -- [saferecl.hp.holder.mem]/3");
      // ABA safety comes from try_protect()'s seq_cst store and seq_cst
      // reload, not from this loop; the loop only ensures convergence when
      // __src changes.
      _Tp* __ptr = __src.load(memory_order::relaxed);
      while (!try_protect(__ptr, __src))
	;
      return __ptr;
    }

  template<typename _Tp>
    inline void
    hazard_pointer::reset_protection(const _Tp* __ptr) noexcept
    {
      static_assert(__hazard_pointer::_Protectable<_Tp>,
		    "T must be a hazard-protectable type"
		    " -- [saferecl.hp.holder.mem]/7");
      __glibcxx_assert(!empty());
      if (__ptr == nullptr)
	reset_protection();
      else if (_M_rec)
	{
	  // Publish the _Hazptr_obj subobject, not the _Tp*.  That is what the
	  // scan compares against, and the two differ whenever the base is not
	  // at offset 0 -- e.g. struct T : Other, hazard_pointer_obj_base<T>,
	  // which [saferecl.hp.general]/2 permits.  The offset is a
	  // compile-time constant, so the reader path pays an add at most.
	  const __hazard_pointer::_Hazptr_obj* const __obj = __ptr;
	  void* const __key
	    = const_cast<__hazard_pointer::_Hazptr_obj*>(__obj);
	  // seq_cst drains the store buffer so that the scan cannot miss the
	  // hazard; release is insufficient.
	  _M_rec->_M_hazard.store(__key, memory_order::seq_cst);
	}
    }

  inline void
  hazard_pointer::reset_protection(nullptr_t) noexcept
  {
    __glibcxx_assert(!empty());
    if (_M_rec)
      _M_rec->_M_hazard.store(nullptr, memory_order::release);
  }

  inline void
  hazard_pointer::swap(hazard_pointer& __other) noexcept
  { std::swap(_M_rec, __other._M_rec); }

  // -------------------------------------------------------------------------
  // __hazard_pointer::_Domain implementation
  // -------------------------------------------------------------------------

  namespace __hazard_pointer
  {
    // No pre-allocated pool: records are created on demand and never destroyed
    // until the domain is, so a process that never uses hazard pointers pays
    // nothing and the constructor cannot fail.
    inline _Domain::_Domain() = default;

    inline _Domain::~_Domain()
    {
      // Called during static-storage destruction.  Thread-local storage is
      // destroyed before static storage, so _S_tl_node is already gone and
      // _M_synchronize() cannot be called.  All threads have exited, so no
      // hazard pointers are held and every orphan can be reclaimed
      // unconditionally.
      for (_Hazptr_obj* __obj = _M_orphan_list._M_head; __obj != nullptr;)
	{
	  // Read before _M_reclaim(): it frees __obj.
	  _Hazptr_obj* const __next = __obj->_M_next;
	  __obj->_M_reclaim(__obj);
	  __obj = __next;
	}
      _M_orphan_list._M_clear();

      // Records outlive every hazard_pointer by construction --
      // _M_release_rec() only clears a flag -- so they are freed here, once
      // all threads are gone.
      for (const _Hazptr_rec* __rec = _M_recs_head.load(memory_order::relaxed);
	   __rec != nullptr;)
	{
	  const _Hazptr_rec* const __next = __rec->_M_next;
	  delete __rec;
	  __rec = __next;
	}
    }

    inline _Hazptr_rec*
    _Domain::_M_acquire_rec()
    {
      // Fast path: claim an existing record with a CAS.  No lock, so
      // concurrent make_hazard_pointer() calls only contend when they race for
      // the same record.
      for (_Hazptr_rec* __rec = _M_recs_head.load(memory_order::acquire);
	   __rec != nullptr; __rec = __rec->_M_next)
	{
	  // Test-and-test-and-set.  The relaxed load is not an optimisation
	  // for the contended case, it is what stops the walk from being
	  // O(pool) *locked* RMWs: without it every occupied record on the way
	  // to a free one costs a failed compare_exchange, and a failed CAS
	  // still acquires the cache line exclusively and dirties it.
	  // Measured on x86_64, acquiring the 256th handle while 255 are held:
	  // 684ns with the bare CAS walk against 111ns without.  A stale true
	  // only costs a skipped record, and a stale false is resolved by the
	  // CAS below.
	  if (__rec->_M_active.load(memory_order::relaxed))
	    continue;
	  bool __expected = false;
	  if (__rec->_M_active.compare_exchange_strong(__expected, true,
						       memory_order::acquire,
						       memory_order::relaxed))
	    {
	      // Relaxed: this counter feeds _M_retire_impl()'s heuristic
	      // threshold and _M_active_slots().  Nothing is ordered through
	      // it, and as a seq_cst RMW it was a second contended line on
	      // every acquire and release.
	      _M_active_count.fetch_add(1, memory_order::relaxed);
	      return __rec;
	    }
	}

      // Every record is taken -- append one.  Serialised, but this is the rare
      // path: it runs at most once per concurrently-live hazard_pointer, ever.
      auto* const __rec = new _Hazptr_rec();    // throws bad_alloc on OOM
      __rec->_M_active.store(true, memory_order::relaxed);
      {
	const lock_guard<mutex> __lk(_M_rec_alloc_mutex);
	__rec->_M_next = _M_recs_head.load(memory_order::relaxed);
	// Release, paired with the acquire loads above and in
	// _M_synchronize().  A scan that does not observe this store cannot
	// observe the hazard store that follows it either, and the seq_cst
	// fence in _M_synchronize() already forces the scan to observe any
	// hazard whose reader went on to validate.  So a record published
	// after a scan started is one whose reader has not yet committed to
	// protecting anything.
	_M_recs_head.store(__rec, memory_order::release);
      }
      _M_active_count.fetch_add(1, memory_order::relaxed);
      _M_rec_count.fetch_add(1, memory_order::relaxed);
      return __rec;
    }

    inline void
    _Domain::_M_release_rec(_Hazptr_rec* __rec) noexcept
    {
      // Clear the hazard before offering the record for reuse.
      __rec->_M_hazard.store(nullptr, memory_order::release);
      _M_active_count.fetch_sub(1, memory_order::relaxed);
      // Release, so that a thread which later claims this record via the
      // acquiring CAS sees the cleared hazard.
      __rec->_M_active.store(false, memory_order::release);
    }

    inline size_t
    _Domain::_M_active_slots() const noexcept
    { return _M_active_count.load(memory_order::relaxed); }

    inline size_t
    _Domain::_M_retire_list_size() const noexcept
    { return _S_tl_node._M_list._M_size; }

    inline void
    _Domain::_M_retire_impl(_Hazptr_obj* __obj) noexcept
    {
      // Lazy registration: on the first call per thread, insert _S_tl_node
      // into the registry.  _M_registered is safe to check without a lock
      // because only the owning thread ever writes it, and only once.
      _M_ensure_node_registered();

      // Splice under _M_list_mutex so that _M_synchronize() cannot observe a
      // half-linked list.  This is the operation that used to be a vector
      // push_back, i.e. the reason retire() could turn OOM into terminate().
      size_t __sz;
      {
	const lock_guard<mutex> __lk(_S_tl_node._M_list_mutex);
	_S_tl_node._M_list._M_push(__obj);
	__sz = _S_tl_node._M_list._M_size;
      }

      // Heuristic threshold: scan when the retire list grows to more than
      // twice the number of active hazard pointers.  Called with no lock held.
      if (__sz > 2 * _M_active_count.load(memory_order::relaxed))
	_M_synchronize();
    }

    // Called from ~_RetireListNode() when a thread exits.  Splices the node
    // out of the registry so _M_synchronize() never dereferences its
    // soon-to-be-invalid list.
    inline void
    _Domain::_M_unregister_node(const _RetireListNode& __node) noexcept
    {
      const lock_guard<mutex> __lk(_M_retire_lists_mutex);
      _RetireListNode* __curr = _M_retire_lists_head;
      _RetireListNode* __prev = nullptr;
      while (__curr && __curr != &__node)
	{
	  __prev = __curr;
	  __curr = __curr->_M_next;
	}
      if (!__curr)
	return; // node was never registered
      if (__prev)
	__prev->_M_next = __curr->_M_next;
      else
	_M_retire_lists_head = __curr->_M_next;
      --_M_retire_list_node_count;
    }

    // Splice a collected-but-unscanned list onto the calling thread's own
    // list, leaving it empty.  Used both for survivors and for the bail-out
    // path when the scan cannot be sized; in either case the objects stay
    // retired and reachable, so a later scan reclaims them.
    //
    // "local" means the calling thread's, which is deliberately not the thread
    // the objects were retired on -- that one may have exited since.
    inline void
    __splice_to_local_list(_Retire_list& __list) noexcept
    {
      if (__list._M_empty())
	return;
      const lock_guard<mutex> __lk(_S_tl_node._M_list_mutex);
      _S_tl_node._M_list._M_splice(__list);
    }

    inline void
    _Domain::_M_synchronize() noexcept
    {
      // Ensure this thread is registered so survivors have a valid home to
      // return to.
      _M_ensure_node_registered();

      // Step 1: collect -- detach every thread's retire list, then the orphan
      // list, and concatenate them.  Done BEFORE the scan so that a reader
      // publishing a hazard concurrently with the collect either sees the
      // object still in its source (and thus retries) or has the hazard
      // visible to the scan below.  Every step here is a pointer splice: the
      // collect phase does not allocate.
      _Retire_list __pending;
      {
	const lock_guard<mutex> __lk(_M_retire_lists_mutex);
	for (_RetireListNode* __n = _M_retire_lists_head; __n;
	     __n = __n->_M_next)
	  {
	    // The splice touches only the local __pending, so it does not
	    // belong inside _M_list_mutex -- which is the lock
	    // _M_retire_impl() takes on the owning thread's hot path.
	    _Retire_list __taken;
	    {
	      const lock_guard<mutex> __lk2(__n->_M_list_mutex);
	      __taken = __n->_M_list._M_take();
	    }
	    __pending._M_splice(__taken);
	  }
      }
      {
	_Retire_list __taken;
	{
	  const lock_guard<mutex> __lk(_M_orphan_mutex);
	  __taken = _M_orphan_list._M_take();
	}
	__pending._M_splice(__taken);
      }
      if (__pending._M_empty())
	return;

      // Step 2: size the protected-set buffer.  The record list needs no
      // snapshot of its own: records are never unlinked, so the scan below
      // walks it directly, with no lock and no intermediate array.
      //
      // The buffer belongs to this thread and is reused, so it only has to
      // grow when the record pool does.  Growth still has to be able to fail,
      // because a thread that only retires never calls make_hazard_pointer()
      // and so can first reach this point inside a noexcept function.
      //
      // On failure the scan falls back to a linear membership test instead of
      // giving up: reclamation always completes, it is just slower.  Each
      // hazard is then re-loaded once per candidate rather than once in total,
      // which is sound -- every load still happens after the fence below, and
      // correctness needs each load to be ordered after it, not to be part of
      // one instant.
      vector<void*>& __snapshot = _S_tl_node._M_scan_buf;
      __snapshot.clear();
      bool __have_buffer = true;
      __try
	{
	  __snapshot.reserve(_M_rec_count.load(memory_order::relaxed));
	}
      __catch(...)
	{
	  __have_buffer = false;
	}

      // Reclaim-side fence -- MANDATORY, not an optimisation barrier.
      //
      // The acquire loads below do not join the seq_cst total order that the
      // reader side relies on, and the collect step's lock chain only orders a
      // *writer's* retirement against the collect: it adds no edge between an
      // independent reader's hazard store and this scan.  Without this fence a
      // reader can re-validate its source, still see the object (so it keeps
      // dereferencing it), while this scan reads a stale empty record and
      // frees it.  [saferecl.hp.general]/6 requires the end of the protection
      // epoch to strongly happen before the reclamation, which an
      // acquire-only scan does not provide.
      //
      // Upgrading the loads below to seq_cst instead of fencing does NOT fix
      // it: the removal store on the source is user code, and P2530R3 does not
      // require it to be seq_cst, so the StoreLoad reordering survives.
      //
      // Found in review by Thomas Rodgers, confirmed with Maged Michael, and
      // observed on POWER9/POWER10 hardware.
      atomic_thread_fence(memory_order::seq_cst);

      _Hazptr_rec* const __recs = _M_recs_head.load(memory_order::acquire);

      if (__have_buffer)
	{
	  for (const _Hazptr_rec* __rec = __recs; __rec != nullptr;
	       __rec = __rec->_M_next)
	    if (void* const __p = __rec->_M_hazard.load(memory_order::acquire))
	      __snapshot.push_back(__p);        // within the reserved capacity
	  std::sort(__snapshot.begin(), __snapshot.end());
	}

      // Step 3: reclaim every object not in the protected set.  No lock is
      // held here, so deleters may safely call retire() or synchronize().
      //
      // Both sides of the comparison are _Hazptr_obj subobject addresses:
      // retire() passes one, and reset_protection() publishes one.
      const auto __protected = [&](_Hazptr_obj* __obj) noexcept
	{
	  void* const __key = __obj;
	  if (__have_buffer)
	    return std::binary_search(__snapshot.begin(), __snapshot.end(),
				      __key);
	  for (const _Hazptr_rec* __rec = __recs; __rec != nullptr;
	       __rec = __rec->_M_next)
	    if (__rec->_M_hazard.load(memory_order::acquire) == __key)
	      return true;
	  return false;
	};

      _Retire_list __survivors;
      for (_Hazptr_obj* __obj = __pending._M_head; __obj != nullptr;)
	{
	  // Read before the object is spliced or freed.
	  _Hazptr_obj* const __next = __obj->_M_next;
	  if (__protected(__obj))
	    __survivors._M_push(__obj);
	  else
	    __obj->_M_reclaim(__obj);
	  __obj = __next;
	}

      // Step 4: put survivors back into the calling thread's own list.  They
      // cannot be returned to their original threads, which may have exited
      // between the collect and here; the calling thread's list is guaranteed
      // alive for the duration of this call.
      __splice_to_local_list(__survivors);
    }

    inline void
    _Domain::_M_ensure_node_registered()
    {
      if (_S_tl_node._M_registered)
	return;
      {
	const lock_guard<mutex> __lk(_M_retire_lists_mutex);
	_S_tl_node._M_next = _M_retire_lists_head;
	_M_retire_lists_head = &_S_tl_node;
	++_M_retire_list_node_count;
      }
      _S_tl_node._M_registered = true;  // only after successful registration
    }

    inline
    _RetireListNode::~_RetireListNode()
    {
      if (!_M_registered)
	return; // _M_ensure_node_registered() never ran -- nothing to do

      _Domain& __domain = _S_default_domain();

      // Reclaim as much as possible before this thread's retire list goes
      // away.  _M_synchronize() puts survivors back into _M_list, which is a
      // member of *this and therefore still alive throughout this destructor.
      __domain._M_synchronize();

      // Unregister before touching _M_list without _M_list_mutex.
      // _M_unregister_node() acquires _M_retire_lists_mutex, which
      // _M_synchronize() holds for its entire collect loop -- so this call
      // blocks until any concurrent _M_synchronize() that has already seen
      // this node has fully released both mutexes.  After it returns, no
      // future _M_synchronize() can find this node.
      __domain._M_unregister_node(*this);

      // Move any remaining survivors (still actively protected) to the
      // domain's orphan list so a future scan from any thread can reclaim
      // them.
      if (!_M_list._M_empty())
	{
	  const lock_guard<mutex> __lk(__domain._M_orphan_mutex);
	  __domain._M_orphan_list._M_splice(_M_list);
	}
    }

  } // namespace __hazard_pointer

_GLIBCXX_END_NAMESPACE_VERSION
} // namespace std

#endif // _GLIBCXX_HAZARD_PTR_H
