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

template <class Callable>
void check()
{
  typedef typename Callable::Int Int;
  typedef typename Callable::Div_t Div_t;

  _GLIBCXX_CONSTEXPR Int n = 42;
  _GLIBCXX_CONSTEXPR Int d = 5;
  _GLIBCXX_CONSTEXPR Int quot = 8;
  _GLIBCXX_CONSTEXPR Int rem = 2;

  const Div_t a = Callable()(n, d);
  VERIFY(a.quot == quot);
  VERIFY(a.rem == rem);

#if __cplusplus >= 202302L
  constexpr Div_t b = std::div(n, d);
  static_assert(b.quot == quot);
  static_assert(b.rem == rem);
#endif
}

template <class Int_, class Div_t_>
struct Call_div {
  typedef Int_ Int;
  typedef Div_t_ Div_t;
  _GLIBCXX_CONSTEXPR Div_t operator()(Int n, Int d) const { return std::div(n, d); }
};

struct Call_ldiv {
  typedef long Int;
  typedef std::ldiv_t Div_t;
  _GLIBCXX_CONSTEXPR Div_t operator()(Int n, Int d) const { return std::ldiv(n, d); }
};

#ifndef _GLIBCXX_USE_C99_LONG_LONG_DYNAMIC
struct Call_lldiv {
  typedef long long Int;
  typedef std::lldiv_t Div_t;
  _GLIBCXX_CONSTEXPR Div_t operator()(Int n, Int d) const { return std::lldiv(n, d); }
};
#endif

int main()
{
  check<Call_div<int, std::div_t> >();
  check<Call_div<long, std::ldiv_t> >();
  check<Call_ldiv>();
#ifndef _GLIBCXX_USE_C99_LONG_LONG_DYNAMIC
  check<Call_div<long long, std::lldiv_t> >();
  check<Call_lldiv>();
#endif
}
