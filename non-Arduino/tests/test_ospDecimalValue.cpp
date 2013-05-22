/*
 * This file is meant to provide some basic functionality tests of ospDecimalValue<N>.
 * It should simply be compiled on a host machine and the resulting executable should
 * be run. THIS IS NOT ARDUINO CODE!
 */

#undef NDEBUG
#include <cassert>
#include <iostream>
#include <boost/type_traits.hpp>
#include <inttypes.h>

using std::cout;
using std::endl;

#define HOSTED_TEST
// force type sizes to match those of the Arduino
#define int int16_t
#define long int32_t
#include "../../osPID_Firmware/ospDecimalValue.h"

// This will only work with a compiler that provides is_pod<> support for structs and classes!
//OSP_STATIC_ASSERT(boost::is_pod< ospDecimalValue<1> >::value, ospDecimalValue_must_be_POD);

void test_equality(void)
{
  ospDecimalValue<0> one0 = { 1 };
  ospDecimalValue<1> one1 = { 10 };
  ospDecimalValue<2> one2 = { 100 };
  ospDecimalValue<3> one3 = { 1000 };
  ospDecimalValue<4> one4 = { 10000 };

  assert(one0 == one0);
  assert(one0 == one1);
  assert(one0 == one2);
  assert(one0 == one3);
  assert(one0 == one4);

  assert(one1 == one0);
  assert(one1 == one1);
  assert(one1 == one2);
  assert(one1 == one3);
  assert(one1 == one4);

  assert(one2 == one0);
  assert(one2 == one1);
  assert(one2 == one2);
  assert(one2 == one3);
  assert(one2 == one4);

  assert(one2 == one0);
  assert(one2 == one1);
  assert(one2 == one2);
  assert(one2 == one3);
  assert(one2 == one4);

  assert(one3 == one0);
  assert(one3 == one1);
  assert(one3 == one2);
  assert(one3 == one3);
  assert(one3 == one4);

  assert(one4 == one0);
  assert(one4 == one1);
  assert(one4 == one2);
  assert(one4 == one3);
  assert(one4 == one4);
}

void test_comparisons(void)
{
  ospDecimalValue<1> one1 = { 10 };
  ospDecimalValue<2> ten2 = { 1000 };
  ospDecimalValue<3> mTen3 = { -10000 };
  ospDecimalValue<0> mTen0 = { -10 };

  assert(one1 < ten2);
  assert(one1 > mTen0);
  assert(ten2 >= ten2);
  assert(mTen0 <= mTen3);
  assert(mTen0 != ten2);
  assert(mTen0 == mTen3);
  assert(mTen0 < one1);
  assert(ten2 > mTen3);
  assert(mTen3 == mTen3.rescale<1>());
  assert(ten2.rescale<3>() == -mTen3.rescale<1>());
}

void test_basic_arithmetic(void)
{
  ospDecimalValue<2> one = { 100 };
  ospDecimalValue<2> ten = { 1000 };
  ospDecimalValue<1> hundred = { 1000 };

  assert((ten * ten).rescale<1>() == hundred);
  assert(one + one == makeDecimal<2>(200));
  assert(one - one == makeDecimal<4>(0));
  assert(ten - ten - ten == -ten);
  assert(one + ten == makeDecimal<2>(1100));
  assert((hundred / hundred).rescale<0>() == one);
  assert((ten * ten / hundred).rescale<3>() == makeDecimal<0>(1));
}

void test_modifying_arithmetic(void)
{
  ospDecimalValue<2> t1 = { 1000 };

  t1 *= makeDecimal<2>(200);
  assert(t1 == makeDecimal<0>(20));
  t1 /= makeDecimal<2>(400);
  assert(t1 == makeDecimal<3>(5000));
}

#undef int
int main(int, char **)
{
  // the following should fail to compile
  //ospDecimalValue<5> one5 = { 100000 };

  test_equality();
  test_comparisons();
  test_basic_arithmetic();
  test_modifying_arithmetic();

  return 0;
}

