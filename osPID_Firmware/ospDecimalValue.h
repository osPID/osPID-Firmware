#ifndef OSPDECIMALVALUE_H
#define OSPDECIMALVALUE_H

#include "enable_if.h"
#include "ospAssert.h"

/*
 * This is a small template library implementing decimal fixed-point numbers,
 * which are used to avoid floating-point math and its code-space costs.
 */

// these enable compile-time evaluation of 10**N as pow10<N>::value
template<int N> struct ospPow10 { enum { value = 10 * ospPow10<N-1>::value }; };
template<> struct ospPow10<0> { enum { value = 1 }; };

template<int D> class ospDecimalValue;
template<int D> class ospDecimalMultiplyResult;
template<int lDEC, int rDEC> ospDecimalMultiplyResult<lDEC> operator / (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs);

// it is important to keep exact track of when multiply and divide chains are rescaled
// so as to not unexpectedly lose precision
//
// you can only do two things with a multiply result: rescale it back to a normal ospDecimalValue,
// or divide it by an ospDecimalValue which has few enough decimals to not provoke overflow
template<int DECIMALS> class ospDecimalMultiplyResult
{
private:
  long value;

  OSP_STATIC_ASSERT(DECIMALS >= 0, negative_decimals_are_not_supported);
  OSP_STATIC_ASSERT(DECIMALS <= 8, no_more_than_8_decimals_are_supported);

  // instances of this class should not be created by the end-user
  ospDecimalMultiplyResult();
  ospDecimalMultiplyResult(const ospDecimalMultiplyResult& other);

  explicit ospDecimalMultiplyResult(long rawVal)
  {
    value = rawVal;
  }

  template<int D> friend class ospDecimalMultiplyResult;

  static long divide_rounded(long num, int denom)
  {
    long quot = num / denom;
    int rem = num % denom;

    if (abs(rem) >= SCALE / 2)
    {
      if (quot > 0)
        quot += 1;
      else
        quot -= 1;
    }

    return quot;
  }

public:
  enum { SCALE = ospPow10<DECIMALS>::value };

  long rawValue() const { return value; }

  template<int newDEC>
      typename boost::enable_if_c<DECIMALS == newDEC, ospDecimalValue<newDEC> >::type
      rescale() const;
  template<int newDEC>
      typename boost::enable_if_c<DECIMALS < newDEC, ospDecimalValue<newDEC> >::type
      rescale() const;
  template<int newDEC>
      typename boost::enable_if_c<newDEC < DECIMALS, ospDecimalValue<newDEC> >::type
      rescale() const;

  template<int numDEC> ospDecimalMultiplyResult<DECIMALS - numDEC> operator / (const ospDecimalValue<numDEC>& numerator);

  template<int lDEC, int rDEC> friend ospDecimalMultiplyResult<lDEC+rDEC> operator * (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs);
  template<int lDEC, int rDEC> friend ospDecimalMultiplyResult<lDEC> operator / (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs);
};

template<int DECIMALS> struct ospDecimalValue
{
  int value;

private:
  OSP_STATIC_ASSERT(DECIMALS >= 0, negative_decimals_are_not_supported);
  OSP_STATIC_ASSERT(DECIMALS <= 4, no_more_than_4_decimals_are_supported);

  enum { SCALE = ospPow10<DECIMALS>::value };

public:
  // default constructor, copy constructor, and operator= are all trivial, since
  // this must remain a POD class

  operator double() const
  {
    return value / double(SCALE);
  }

  template<int newDEC> typename boost::enable_if_c<DECIMALS < newDEC, ospDecimalValue<newDEC> >::type rescale() const
  {
    return ospDecimalValue<newDEC>(value * ospPow10<newDEC - DECIMALS>::value);
  }

  template<int newDEC> typename boost::enable_if_c<newDEC < DECIMALS, ospDecimalValue<newDEC> >::type rescale() const
  {
    return (*this / ospDecimalValue<0>(ospPow10<DECIMALS - newDEC>::value)).rescale<newDEC>();
  }

  template<int newDEC> typename boost::enable_if_c<newDEC == DECIMALS, ospDecimalValue<newDEC> >::type rescale() const
  {
    return *this;
  }

  int rawValue(void) const { return value; }
  void setRawValue(int newVal) { value = newVal; }

  // basic arithmetic operators

  // negation, addition, and subtraction all have no effect on the precision
  ospDecimalValue operator - () { ospDecimalValue v(*this); v.value = -v.value; return v; }
  ospDecimalValue& operator += (const ospDecimalValue& that) { value += that.value; return *this; }
  ospDecimalValue& operator -= (const ospDecimalValue& that) { value -= that.value; return *this; }

  // but multiplication and division require care to maintain precisions
  template<int lDEC, int rDEC> friend ospDecimalMultiplyResult<lDEC+rDEC> operator * (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs);
  template<int lDEC, int rDEC> friend ospDecimalMultiplyResult<lDEC> operator / (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs);

  // basic logical operators

  // always multiply up the lower-precision value
  template<int tDEC> typename boost::enable_if_c<DECIMALS == tDEC, bool>::type operator == (const ospDecimalValue<tDEC>& that) const
  {
    return value == that.value;
  }

  template<int lDEC, int rDEC> friend
  typename boost::enable_if_c<lDEC < rDEC, bool>::type
  operator == (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs);

  template<int lDEC, int rDEC> friend
  typename boost::enable_if_c<rDEC < lDEC, bool>::type
  operator == (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs);

  template<int tDEC> typename boost::enable_if_c<DECIMALS == tDEC, bool>::type operator < (const ospDecimalValue<tDEC>& that) const
  {
    return value < that.value;
  }

  template<int lDEC, int rDEC> friend
  typename boost::enable_if_c<lDEC < rDEC, bool>::type
  operator < (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs);

  template<int lDEC, int rDEC> friend
  typename boost::enable_if_c<rDEC < lDEC, bool>::type
  operator < (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs);

  // derived arithmetic operators
  ospDecimalValue& operator *= (const ospDecimalValue& that) { *this = ((*this) * that).rescale<DECIMALS>(); return *this; }
  ospDecimalValue& operator /= (const ospDecimalValue& that) { *this = ((*this) / that).rescale<DECIMALS>(); return *this; }
  friend ospDecimalValue operator + (const ospDecimalValue& lhs, const ospDecimalValue& rhs)
  { ospDecimalValue res(lhs); res += rhs; return res; }
  friend ospDecimalValue operator - (const ospDecimalValue& lhs, const ospDecimalValue& rhs)
  { ospDecimalValue res(lhs); res -= rhs; return res; }

  // derived logical operators
  template<int lDEC, int rDEC> friend bool operator != (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs);
  template<int lDEC, int rDEC> friend bool operator > (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs);
  template<int lDEC, int rDEC> friend bool operator <= (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs);
  template<int lDEC, int rDEC> friend bool operator >= (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs);
};

// ospDecimalMultiplyResult<D1>::rescale<D2>()
template<int oldDEC> template<int newDEC> inline
typename boost::enable_if_c<oldDEC == newDEC, ospDecimalValue<newDEC> >::type
ospDecimalMultiplyResult<oldDEC>::rescale() const
{
  return (ospDecimalValue<newDEC>){int(value)};
}

template<int oldDEC> template<int newDEC> inline
typename boost::enable_if_c<oldDEC < newDEC, ospDecimalValue<newDEC> >::type
ospDecimalMultiplyResult<oldDEC>::rescale() const
{
  return (ospDecimalValue<newDEC>){int(value * ospPow10<newDEC - oldDEC>::value)};
}

template<int oldDEC> template<int newDEC> inline
typename boost::enable_if_c<newDEC < oldDEC, ospDecimalValue<newDEC> >::type
ospDecimalMultiplyResult<oldDEC>::rescale() const
{
  return (ospDecimalValue<newDEC>){divide_rounded(value, ospPow10<oldDEC - newDEC>::value)};
}

// ospDecimalMultiplyResult division
template<int DECIMALS> template<int numDEC> inline ospDecimalMultiplyResult<DECIMALS - numDEC>
ospDecimalMultiplyResult<DECIMALS>::operator / (const ospDecimalValue<numDEC>& numerator)
{
  return ospDecimalMultiplyResult<DECIMALS - numDEC>(divide_rounded(value, numerator.rawValue()));
}

// ospDecimalValue multiplication
template<int lDEC, int rDEC> inline ospDecimalMultiplyResult<lDEC+rDEC> operator * (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs)
{
  return ospDecimalMultiplyResult<lDEC+rDEC>(long(lhs.value) * long(rhs.value));
}

template<int lDEC, int rDEC> inline ospDecimalMultiplyResult<lDEC> operator / (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs)
{
  return ospDecimalMultiplyResult<lDEC>(ospDecimalMultiplyResult<lDEC+rDEC>::divide_rounded(long(lhs.value) * ospPow10<rDEC>::value, rhs.value));
}

// ospDecimalValue equality
template<int lDEC, int rDEC>
typename boost::enable_if_c<rDEC < lDEC, bool>::type
operator == (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs)
{
  return long(lhs.value) == long(rhs.value) * ospPow10<lDEC - rDEC>::value;
}

template<int lDEC, int rDEC>
typename boost::enable_if_c<lDEC < rDEC, bool>::type
operator == (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs)
{
  return long(lhs.value) * ospPow10<rDEC - lDEC>::value == long(rhs.value);
}

// ospDecimalValue less-than
template<int lDEC, int rDEC>
typename boost::enable_if_c<rDEC < lDEC, bool>::type
operator < (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs)
{
  return long(lhs.value) < long(rhs.value) * ospPow10<lDEC - rDEC>::value;
}

template<int lDEC, int rDEC>
typename boost::enable_if_c<lDEC < rDEC, bool>::type
operator < (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs)
{
  return long(lhs.value) * ospPow10<rDEC - lDEC>::value < long(rhs.value);
}

// derived ospDecimalValue comparisons
template<int lDEC, int rDEC> inline bool operator != (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs)
{
  return !(lhs == rhs);
}

template<int lDEC, int rDEC> inline bool operator > (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs)
{
  return  !(lhs < rhs) && !(lhs == rhs);
}

template<int lDEC, int rDEC> inline bool operator <= (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs)
{
  return (lhs < rhs || lhs == rhs);
}

template<int lDEC, int rDEC> inline bool operator >= (const ospDecimalValue<lDEC>& lhs, const ospDecimalValue<rDEC>& rhs)
{
  return !(lhs < rhs);
}

template<int D> inline ospDecimalValue<D> makeDecimal(int val)
{
  return (ospDecimalValue<D>) { val };
}

#endif

