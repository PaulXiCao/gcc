// { dg-do compile { target c++26 } }
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

// hazard_pointer_obj_base is a standard-specified type that users derive
// from, so its size is baked into user binaries rather than only into
// libstdc++.so, and doc/xml/manual/abi.xml lists changing the layout of a
// standard-specified type as a prohibited change.  hazard_pointer is frozen
// for the same reason, and must stay one word: the reserved domain pointer
// deliberately lives in the record rather than in the handle, so that adding
// custom domains later never has to widen it (P2530R3 sec. 1.5 item 3).
//
// The header carries the same assertions, so this file is not what stops an
// accidental change -- it is what makes the freeze visible in the testsuite
// rather than only in a header a reader may not open.
//
// The numbers are per-ABI, so they are guarded on 8-byte pointers rather than
// generalised: any formula portable enough to hold everywhere would have to
// be derived from the members, which would make the assertion follow the code
// instead of pinning it.

#include <hazard_pointer>
#include <memory>
#include <type_traits>

namespace
{
  struct Probe : std::hazard_pointer_obj_base<Probe> { };

  struct FunctorProbe;

  struct Deleting
  {
    void operator()(FunctorProbe* p) const;
  };

  // A stateless deleter must not cost a word: [[__no_unique_address__]] on
  // the deleter member is what pays for the space P2530R3 sec. 1.5 reserves.
  struct FunctorProbe
    : std::hazard_pointer_obj_base<FunctorProbe, Deleting> { };

  using Base = std::hazard_pointer_obj_base<Probe>;
  using FunctorBase = std::hazard_pointer_obj_base<FunctorProbe, Deleting>;
}

#if __SIZEOF_POINTER__ == 8
static_assert(sizeof(Base) == 32);
static_assert(alignof(Base) == 8);
static_assert(sizeof(FunctorBase) == sizeof(Base));
static_assert(sizeof(std::hazard_pointer) == 8);
#endif

// Not trivially copyable, and deliberately so.  Two independent causes:
// reserving anything makes the default constructor non-trivial, and the
// not-retired sentinel needs user-provided copy and move, or retiring a copy
// would look like a double retire.  No layout is both trivially copyable and
// correct: a trivial copy necessarily copies the retirement state.
static_assert(!std::is_trivially_copyable_v<Base>);

// Still nothrow default-constructible and nothrow destructible, which the
// synopsis requires of the protected base.
static_assert(std::is_nothrow_destructible_v<Probe>);
static_assert(std::is_nothrow_default_constructible_v<Probe>);

// The handle is movable but not copyable.
static_assert(std::is_nothrow_move_constructible_v<std::hazard_pointer>);
static_assert(std::is_nothrow_move_assignable_v<std::hazard_pointer>);
static_assert(!std::is_copy_constructible_v<std::hazard_pointer>);
static_assert(!std::is_copy_assignable_v<std::hazard_pointer>);
