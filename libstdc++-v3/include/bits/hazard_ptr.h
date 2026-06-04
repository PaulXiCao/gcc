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

#include <algorithm>         // std::find, std::find_if, std::sort,
#include <atomic>            // std::atomic, std::atomic_size_t
#include <bits/move.h>       // std::move, std::swap
#include <bits/unique_ptr.h> // std::default_delete
#include <deque>             // std::deque
#include <mutex>             // std::mutex, std::lock_guard
#include <vector>            // std::vector
                             //   std::binary_search
#include <cstddef>           // std::size_t, offsetof
#include <type_traits>       // std::is_class_v, std::is_base_of_v, etc.

namespace std _GLIBCXX_VISIBILITY(default) {
_GLIBCXX_BEGIN_NAMESPACE_VERSION

template <typename _Tp, typename _Dp = default_delete<_Tp>>
class hazard_pointer_obj_base;

class hazard_pointer;

[[nodiscard]] inline hazard_pointer make_hazard_pointer();

namespace __hazard_pointer {
// Empty tag privately inherited by hazard_pointer_obj_base<T,D>.
// Lets _Protectable detect T derives from some instantiation without
// knowing D; std::is_base_of ignores access specifiers.
struct _Obj_base_tag {};

template <typename _Tp>
concept _Protectable = is_class_v<_Tp> && is_base_of_v<_Obj_base_tag, _Tp>;

using _Deleter_fn = void (*)(void *);

struct _RetireRecord {
  void *_M_ptr = nullptr;
  _Deleter_fn _M_deleter = nullptr;
};

struct _RetireListNode;

// One hazard slot padded to a full cache line to prevent false sharing
// between threads using adjacent slots.
struct alignas(__GCC_DESTRUCTIVE_SIZE) _Slot {
  atomic<void *> _M_value{nullptr};
};

// Owns the hazard slot pool and the per-thread retire lists.
// Process-global singleton; construction restricted to _S_default_domain().
//
// Lock ordering (always acquire outer before inner to prevent deadlock):
//   outer: _M_slot_free_mutex — guards slot pool structure
//   inner: _RetireListNode::_M_list_mutex — guards one thread's list
// _M_retire_lists_mutex is never held concurrently with _M_slot_free_mutex.
class _Domain {
public:
  [[nodiscard]] atomic<void *> *_M_acquire_slot();
  void _M_release_slot(atomic<void *> *__slot);
  void _M_retire_impl(void *__ptr, _Deleter_fn __deleter);
  void _M_synchronize();
  [[nodiscard]] size_t _M_active_slots() const noexcept;
  [[nodiscard]] size_t _M_retire_list_size() const noexcept;

private:
  static constexpr size_t _S_initial_slots = 8;

  // _M_slot_free and _M_slots are kept separate so _M_acquire_slot scans
  // the compact bool vector cheaply without touching padded slot memory.
  // _M_slots uses deque so that _Slot addresses (held by live
  // hazard_pointer objects as _M_slot) remain stable across push_back.
  vector<bool> _M_slot_free; // guarded by _M_slot_free_mutex
  mutex _M_slot_free_mutex;
  deque<_Slot> _M_slots;             // structure guarded by _M_slot_free_mutex
  atomic_size_t _M_active_count = 0; // mirrors slot occupancy; relaxed ok

  vector<_RetireRecord> _M_orphan_list; // guarded by _M_orphan_mutex
  mutex _M_orphan_mutex;

  _RetireListNode *_M_retire_lists_head =
      nullptr;                          // guarded by _M_retire_lists_mutex
  size_t _M_retire_list_node_count = 0; // guarded by _M_retire_lists_mutex
  mutex _M_retire_lists_mutex;

  void _M_ensure_node_registered();
  void _M_unregister_node(const _RetireListNode &__node) noexcept;

  friend struct _RetireListNode;

  _Domain();
  ~_Domain();
  friend _Domain &_S_default_domain() noexcept;
};

// Per-thread node in the retire_lists linked list.  Registered lazily on
// first _M_retire_impl()/_M_synchronize() call; removed on thread exit.
// _M_list_mutex guards *_M_list against concurrent access between
// _M_synchronize() (any thread) and _M_retire_impl() (owning thread).
struct _RetireListNode {
  vector<_RetireRecord> *_M_list = nullptr;
  mutex _M_list_mutex;
  _RetireListNode *_M_next = nullptr;

  ~_RetireListNode();
};

// Bundles the retire list and its registry node so that member
// destruction order (reverse of declaration) guarantees _M_retire_list
// outlives _M_node — ~_RetireListNode() always sees a live list.
struct _ThreadState {
  vector<_RetireRecord> _M_retire_list; // destroyed second
  _RetireListNode _M_node;              // destroyed first
};

inline thread_local _ThreadState _S_tl_state;

inline _Domain &_S_default_domain() noexcept {
  static _Domain __domain;
  return __domain;
}

inline void __append_to(vector<_RetireRecord> &__dst,
                        const vector<_RetireRecord> &__src) {
  __dst.insert(__dst.end(), __src.begin(), __src.end());
}

} // namespace __hazard_pointer

// -------------------------------------------------------------------------
// hazard_pointer_obj_base
// -------------------------------------------------------------------------

template <typename _Tp, typename _Dp>
class hazard_pointer_obj_base : private __hazard_pointer::_Obj_base_tag {
public:
  void retire(_Dp __d = _Dp()) noexcept;

protected:
  hazard_pointer_obj_base() = default;
  hazard_pointer_obj_base(const hazard_pointer_obj_base &) = default;
  hazard_pointer_obj_base(hazard_pointer_obj_base &&) = default;
  hazard_pointer_obj_base &operator=(const hazard_pointer_obj_base &) = default;
  hazard_pointer_obj_base &operator=(hazard_pointer_obj_base &&) = default;
  ~hazard_pointer_obj_base() = default;

private:
  _Dp _M_deleter;
};

// -------------------------------------------------------------------------
// hazard_pointer
// -------------------------------------------------------------------------

class hazard_pointer {
public:
  hazard_pointer() noexcept = default;

  hazard_pointer(hazard_pointer &&__other) noexcept;

  hazard_pointer &operator=(hazard_pointer &&__other) noexcept;

  ~hazard_pointer();

  hazard_pointer(const hazard_pointer &) = delete;
  hazard_pointer &operator=(const hazard_pointer &) = delete;

  [[nodiscard]] bool empty() const noexcept;

  template <typename _Tp>
  [[nodiscard]] _Tp *protect(const atomic<_Tp *> &__src) noexcept;

  template <typename _Tp>
  [[nodiscard]] bool try_protect(_Tp *&__ptr,
                                 const atomic<_Tp *> &__src) noexcept;

  template <typename _Tp> void reset_protection(const _Tp *__ptr) noexcept;

  void reset_protection(nullptr_t = nullptr) noexcept;

  void swap(hazard_pointer &__other) noexcept;

private:
  atomic<void *> *_M_slot = nullptr;

  explicit hazard_pointer(atomic<void *> *__slot) noexcept;

  friend hazard_pointer make_hazard_pointer();
};

// -------------------------------------------------------------------------
// Free functions
// -------------------------------------------------------------------------

inline void swap(hazard_pointer &__a, hazard_pointer &__b) noexcept {
  __a.swap(__b);
}

[[nodiscard]] inline hazard_pointer make_hazard_pointer() {
  return hazard_pointer(
      __hazard_pointer::_S_default_domain()._M_acquire_slot());
}

// -------------------------------------------------------------------------
// hazard_pointer_obj_base implementation
// -------------------------------------------------------------------------

template <typename _Tp, typename _Dp>
inline void hazard_pointer_obj_base<_Tp, _Dp>::retire(_Dp __d) noexcept {
  static_assert(is_invocable_v<_Dp, _Tp *>,
                "D must be invocable with T* -- [32.11.3.3]/1");
  static_assert(is_default_constructible_v<_Dp>,
                "D must be default-constructible -- [32.11.3.3]/3");
  static_assert(is_move_assignable_v<_Dp>,
                "D must be move-assignable -- [32.11.3.3]/3");
  static_assert(is_base_of_v<hazard_pointer_obj_base<_Tp, _Dp>, _Tp>,
                "T must derive from hazard_pointer_obj_base<T,D>"
                " -- [32.11.3.3]/5");
  static_assert(is_convertible_v<_Tp *, hazard_pointer_obj_base<_Tp, _Dp> *>,
                "hazard_pointer_obj_base<T,D> must be a public base of T"
                " -- [32.11.3.3]/5");
  static_assert(!is_virtual_base_of_v<hazard_pointer_obj_base<_Tp, _Dp>, _Tp>,
                "hazard_pointer_obj_base<T,D> must be a non-virtual base"
                " of T -- [32.11.3.3]/5");

  // _M_deleter sits at offset 0: hazard_pointer_obj_base has one empty
  // private base (_Obj_base_tag, EBO) contributing zero bytes, so
  // _M_deleter lands at offset 0 of the base subobject, which itself is
  // at offset 0 of T (single non-virtual inheritance).  The retire lambda
  // recovers the deleter via a cast to hazard_pointer_obj_base*.
  static_assert(offsetof(hazard_pointer_obj_base, _M_deleter) == 0,
                "_M_deleter must be at offset 0 for the retire cast");

  _M_deleter = std::move(__d);

  _Tp *__self = static_cast<_Tp *>(this);
  __hazard_pointer::_S_default_domain()._M_retire_impl(
      static_cast<void *>(__self), [](void *__p) {
        _Tp *__self_ = static_cast<_Tp *>(__p);
        auto *__base = static_cast<hazard_pointer_obj_base *>(__self_);
        _Dp __d2 = std::move(__base->_M_deleter);
        __d2(__self_);
      });
}

// -------------------------------------------------------------------------
// hazard_pointer implementation
// -------------------------------------------------------------------------

inline hazard_pointer::hazard_pointer(atomic<void *> *__slot) noexcept
    : _M_slot(__slot) {}

inline hazard_pointer::~hazard_pointer() {
  if (_M_slot) {
    reset_protection();
    __hazard_pointer::_S_default_domain()._M_release_slot(_M_slot);
  }
}

inline hazard_pointer::hazard_pointer(hazard_pointer &&__other) noexcept
    : _M_slot(__other._M_slot) {
  __other._M_slot = nullptr;
}

inline hazard_pointer &
hazard_pointer::operator=(hazard_pointer &&__other) noexcept {
  if (this == &__other)
    return *this;
  if (!empty()) {
    reset_protection();
    __hazard_pointer::_S_default_domain()._M_release_slot(_M_slot);
  }
  _M_slot = __other._M_slot;
  __other._M_slot = nullptr;
  return *this;
}

inline bool hazard_pointer::empty() const noexcept {
  return _M_slot == nullptr;
}

template <typename _Tp>
inline bool hazard_pointer::try_protect(_Tp *&__ptr,
                                        const atomic<_Tp *> &__src) noexcept {
  static_assert(__hazard_pointer::_Protectable<_Tp>,
                "T must be a hazard-protectable type -- [32.11.3.4.3]/3");
  __glibcxx_assert(!empty());
  const _Tp *const __old = __ptr;
  reset_protection(__old);
  // seq_cst pairs with the seq_cst store in reset_protection(T*) to form
  // an SC pair: either the snapshot sees the hazard or this reload sees
  // the new pointer.  acquire alone is insufficient on weak-memory arches.
  __ptr = __src.load(memory_order::seq_cst);
  if (!(__old == __ptr))
    reset_protection();
  return __old == __ptr;
}

template <typename _Tp>
inline _Tp *hazard_pointer::protect(const atomic<_Tp *> &__src) noexcept {
  static_assert(__hazard_pointer::_Protectable<_Tp>,
                "T must be a hazard-protectable type -- [32.11.3.4.3]/3");
  _Tp *__ptr = __src.load(memory_order::relaxed);
  while (!try_protect(__ptr, __src))
    ;
  return __ptr;
}

template <typename _Tp>
inline void hazard_pointer::reset_protection(const _Tp *__ptr) noexcept {
  static_assert(__hazard_pointer::_Protectable<_Tp>,
                "T must be a hazard-protectable type -- [32.11.3.4.3]/7");
  __glibcxx_assert(!empty());
  if (__ptr == nullptr)
    reset_protection();
  else if (_M_slot)
    // seq_cst drains the store buffer so synchronize()'s acquire-load
    // snapshot cannot miss the hazard.  release is insufficient: the
    // store can sit in the buffer while the scan reads null.
    _M_slot->store(const_cast<void *>(static_cast<const void *>(__ptr)),
                   memory_order::seq_cst);
}

inline void hazard_pointer::reset_protection(nullptr_t) noexcept {
  __glibcxx_assert(!empty());
  if (_M_slot)
    _M_slot->store(nullptr, memory_order::release);
}

inline void hazard_pointer::swap(hazard_pointer &__other) noexcept {
  std::swap(_M_slot, __other._M_slot);
}

// -------------------------------------------------------------------------
// __hazard_pointer::_Domain implementation
// -------------------------------------------------------------------------

namespace __hazard_pointer {
inline _Domain::_Domain()
    : _M_slot_free(_S_initial_slots, true), _M_slots(_S_initial_slots) {}

inline _Domain::~_Domain() {
  // Thread-local storage is destroyed before static storage; all threads
  // have exited, so no hazard pointers are held.  Every orphan record can
  // be deleted unconditionally without a protected-set check.
  for (const _RetireRecord &__r : _M_orphan_list)
    __r._M_deleter(__r._M_ptr);
}

inline atomic<void *> *_Domain::_M_acquire_slot() {
  const lock_guard<mutex> __lk(_M_slot_free_mutex);
  const auto __it = std::find(_M_slot_free.begin(), _M_slot_free.end(), true);
  if (__it != _M_slot_free.end()) {
    *__it = false;
    ++_M_active_count;
    return &_M_slots[size_t(std::distance(_M_slot_free.begin(), __it))]
                ._M_value;
  }
  // All slots occupied — grow pool by one.
  _M_slot_free.push_back(false);
  _M_slots.emplace_back();
  ++_M_active_count;
  return &_M_slots.back()._M_value;
}

inline void _Domain::_M_release_slot(atomic<void *> *__slot) {
  __slot->store(nullptr, memory_order::release);
  const lock_guard<mutex> __lk(_M_slot_free_mutex);
  const auto __it = std::find_if(
      _M_slots.begin(), _M_slots.end(),
      [__slot](const _Slot &__s) { return &__s._M_value == __slot; });
  if (__it != _M_slots.end()) {
    _M_slot_free[size_t(std::distance(_M_slots.begin(), __it))] = true;
    --_M_active_count;
    return;
  }
  __glibcxx_assert(false); // slot not found — unreachable in correct use
}

inline size_t _Domain::_M_active_slots() const noexcept {
  return _M_active_count.load(memory_order::relaxed);
}

inline size_t _Domain::_M_retire_list_size() const noexcept {
  return _S_tl_state._M_retire_list.size();
}

inline void _Domain::_M_retire_impl(void *__ptr, _Deleter_fn __deleter) {
  _M_ensure_node_registered();

  const size_t __sz = [&] {
    const lock_guard<mutex> __lk(_S_tl_state._M_node._M_list_mutex);
    _S_tl_state._M_retire_list.push_back({__ptr, __deleter});
    return _S_tl_state._M_retire_list.size();
  }();

  // Heuristic threshold: scan when retire list > 2 * active hazard slots.
  if (__sz > 2 * _M_active_count.load(memory_order::relaxed))
    _M_synchronize();
}

inline void
_Domain::_M_unregister_node(const _RetireListNode &__node) noexcept {
  const lock_guard<mutex> __lk(_M_retire_lists_mutex);
  _RetireListNode *__curr = _M_retire_lists_head;
  _RetireListNode *__prev = nullptr;
  while (__curr && __curr != &__node) {
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

inline void _Domain::_M_synchronize() {
  _M_ensure_node_registered();

  // Step 1: collect — atomically swap every thread's retire list with an
  // empty one, then drain the orphan list.  Collect BEFORE snapshot so
  // that any writer publishing a hazard concurrently either has the hazard
  // visible in the snapshot, or sees its object still in the source list
  // and retries.  The collect step's _M_list_mutex acquires happen-before
  // every writer's retirement push, giving the snapshot's acquire-loads a
  // consistent view without relying on cross-atomic SC reasoning that is
  // beyond TSan's vector-clock model.
  vector<_RetireRecord> __all;
  {
    vector<vector<_RetireRecord>> __tmp;
    {
      const lock_guard<mutex> __lk(_M_retire_lists_mutex);
      __tmp.reserve(_M_retire_list_node_count);
      for (_RetireListNode *__n = _M_retire_lists_head; __n;
           __n = __n->_M_next) {
        __tmp.emplace_back();
        const lock_guard<mutex> __lk2(__n->_M_list_mutex);
        std::swap(__tmp.back(), *__n->_M_list);
      }
    }
    size_t __sz = 0;
    for (const auto &__t : __tmp)
      __sz += __t.size();
    __all.reserve(__sz);
    for (const auto &__t : __tmp)
      __append_to(__all, __t);
  }
  {
    const lock_guard<mutex> __lk(_M_orphan_mutex);
    __append_to(__all, _M_orphan_list);
    _M_orphan_list.clear();
  }

  // Step 2: snapshot slot addresses under the lock for a consistent view
  // of the deque structure, then do the acquire-loads after releasing it.
  vector<const atomic<void *> *> __slot_ptrs;
  {
    const lock_guard<mutex> __lk(_M_slot_free_mutex);
    __slot_ptrs.reserve(_M_slots.size());
    for (const _Slot &__s : _M_slots)
      __slot_ptrs.push_back(&__s._M_value);
  }
  vector<void *> __snapshot;
  __snapshot.reserve(__slot_ptrs.size());
  for (const atomic<void *> *__sp : __slot_ptrs)
    if (void *__p = __sp->load(memory_order::acquire))
      __snapshot.push_back(__p);
  std::sort(__snapshot.begin(), __snapshot.end());

  // Step 3: reclaim every record not in the protected set.
  // No lock held; deleters may safely call retire() or synchronize().
  vector<_RetireRecord> __survivors;
  __survivors.reserve(__all.size());
  for (const _RetireRecord &__r : __all) {
    if (std::binary_search(__snapshot.begin(), __snapshot.end(), __r._M_ptr))
      __survivors.push_back(__r);
    else
      __r._M_deleter(__r._M_ptr);
  }

  // Step 4: return survivors to calling thread's own retire list.
  // Survivors cannot go back to their original threads — those may have
  // exited.  The calling thread's list is guaranteed live here.
  if (!__survivors.empty()) {
    const lock_guard<mutex> __lk(_S_tl_state._M_node._M_list_mutex);
    __append_to(_S_tl_state._M_retire_list, __survivors);
  }
}

inline void _Domain::_M_ensure_node_registered() {
  thread_local bool __registered = false;
  if (__registered)
    return;
  _S_tl_state._M_node._M_list = &_S_tl_state._M_retire_list;
  {
    const lock_guard<mutex> __lk(_M_retire_lists_mutex);
    _S_tl_state._M_node._M_next = _M_retire_lists_head;
    _M_retire_lists_head = &_S_tl_state._M_node;
    ++_M_retire_list_node_count;
  }
  __registered = true;
}

inline _RetireListNode::~_RetireListNode() {
  if (!_M_list)
    return; // _M_ensure_node_registered never ran — nothing to do

  _Domain &__domain = _S_default_domain();

  // Reclaim as much as possible before this thread's storage goes away.
  // _M_synchronize puts survivors back into _S_tl_state._M_retire_list,
  // which is still live here because _ThreadState destroys _M_node (this)
  // before _M_retire_list.
  __domain._M_synchronize();

  // Unregister before touching *_M_list without _M_list_mutex.
  // _M_unregister_node blocks until any concurrent _M_synchronize that
  // has already seen this node has fully released both mutexes; after it
  // returns no future _M_synchronize can see this node, so *_M_list is
  // exclusively owned by this thread.
  __domain._M_unregister_node(*this);

  if (!_M_list->empty()) {
    const lock_guard<mutex> __lk(__domain._M_orphan_mutex);
    __append_to(__domain._M_orphan_list, *_M_list);
    _M_list->clear();
  }
}

} // namespace __hazard_pointer

_GLIBCXX_END_NAMESPACE_VERSION
} // namespace std _GLIBCXX_VISIBILITY(default)

#endif // _GLIBCXX_HAZARD_PTR_H
