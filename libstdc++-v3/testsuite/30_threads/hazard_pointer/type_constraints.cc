// { dg-do compile { target c++26 } }
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
#include <memory>
#include <string>
#include <type_traits>

namespace
{
  struct SimpleNode : std::hazard_pointer_obj_base<SimpleNode> {};
  struct CustomDelNode
      : std::hazard_pointer_obj_base<CustomDelNode,
                                     std::default_delete<CustomDelNode>> {};

  // Indirect derivation must still satisfy _Protectable.
  struct FurtherDerived : SimpleNode {};

  // Not hazard-protectable.
  struct PlainClass {};
}

// _Protectable concept (libstdc++ internal name for HazardProtectable).
static_assert(std::__hazard_pointer::_Protectable<SimpleNode>);
static_assert(std::__hazard_pointer::_Protectable<CustomDelNode>);
static_assert(std::__hazard_pointer::_Protectable<FurtherDerived>);

static_assert(!std::__hazard_pointer::_Protectable<int>);
static_assert(!std::__hazard_pointer::_Protectable<void>);
static_assert(!std::__hazard_pointer::_Protectable<int*>);

static_assert(!std::__hazard_pointer::_Protectable<PlainClass>);
static_assert(!std::__hazard_pointer::_Protectable<std::string>);

// retire() deleter constraints.
static_assert(std::is_invocable_v<std::default_delete<SimpleNode>, SimpleNode*>);
static_assert(std::is_default_constructible_v<std::default_delete<SimpleNode>>);
static_assert(std::is_move_assignable_v<std::default_delete<SimpleNode>>);
