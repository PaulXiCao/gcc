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

template <class Int, class Div_t>
void check_div()
{
  constexpr Int n = 42;
  constexpr Int d = 5;
  constexpr Int quot = 8;
  constexpr Int rem = 2;

  const Div_t a = std::div(n, d);
  VERIFY(a.quot == quot);
  VERIFY(a.rem == rem);

#if __cplusplus >= 202302L
  constexpr Div_t b = std::div(n, d);
  static_assert(b.quot == quot);
  static_assert(b.rem == rem);
#endif
}

int main()
{
  check_div<int, std::div_t>();
  check_div<long, std::ldiv_t>();
}
