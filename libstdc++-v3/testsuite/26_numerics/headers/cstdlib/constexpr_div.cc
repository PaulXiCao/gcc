// Copyright (C) 2024-2024 Free Software Foundation, Inc.
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

// Math-related cstdlib bits are not freestanding.
// { dg-require-effective-target hosted }

// 17.4.1.2 Headers, cstdlib

#include <cstdlib>
#include <testsuite_hooks.h>

void test_div_int_int()
{
  const std::div_t a = std::div(42, 5);
  VERIFY( a.quot == 8 );
  VERIFY( a.rem  == 2 );

#if __cplusplus >= 202302L
  static_assert(std::div(42, 5) == std::div_t{.quot = 8, .rem = 2});
#endif
}

int main()
{
  test_div_int_int();
}
