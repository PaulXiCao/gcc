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
#include <atomic>
#include <type_traits>
#include <utility>

namespace
{
  struct Node : std::hazard_pointer_obj_base<Node> {};
  using HP = std::hazard_pointer;

  static_assert(std::is_nothrow_default_constructible_v<HP>);
  static_assert(std::is_nothrow_move_constructible_v<HP>);
  static_assert(std::is_nothrow_move_assignable_v<HP>);

  static_assert(noexcept(std::declval<const HP&>().empty()));

  static_assert(noexcept(std::declval<HP&>().protect(
      std::declval<std::atomic<Node*>&>())));

  static_assert(noexcept(std::declval<HP&>().try_protect(
      std::declval<Node*&>(),
      std::declval<const std::atomic<Node*>&>())));

  static_assert(noexcept(std::declval<HP&>().reset_protection()));
  static_assert(noexcept(std::declval<HP&>().reset_protection(nullptr)));

  static_assert(noexcept(std::declval<HP&>().reset_protection(
      std::declval<const Node*>())));

  static_assert(noexcept(std::declval<HP&>().swap(std::declval<HP&>())));

  static_assert(noexcept(std::swap(std::declval<HP&>(), std::declval<HP&>())));

  static_assert(noexcept(std::declval<Node&>().retire()));
}
