// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <gtest/gtest.h>

#include <locale>
#include <string>

#include "arrow/status.h"
#include "arrow/test-util.h"
#include "arrow/util/parsing.h"

namespace arrow {

using internal::StringConverter;

template <typename ConverterType, typename C_TYPE>
void AssertConversion(ConverterType& converter, const std::string& s, C_TYPE expected) {
  typename ConverterType::value_type out;
  ASSERT_TRUE(converter(s.data(), s.length(), &out))
      << "Conversion failed for '" << s << "' (expected to return " << expected << ")";
  ASSERT_EQ(out, expected);
}

template <typename ConverterType>
void AssertConversionFails(ConverterType& converter, const std::string& s) {
  typename ConverterType::value_type out;
  ASSERT_FALSE(converter(s.data(), s.length(), &out))
      << "Conversion should have failed for '" << s << "' (returned " << out << ")";
}

class LocaleGuard {
 public:
  explicit LocaleGuard(const char* new_locale) : global_locale_(std::locale()) {
    try {
      std::locale::global(std::locale(new_locale));
    } catch (std::runtime_error) {
      // Locale unavailable, ignore
    }
  }

  ~LocaleGuard() { std::locale::global(global_locale_); }

 protected:
  std::locale global_locale_;
};

TEST(StringConversion, ToBoolean) {
  StringConverter<BooleanType> converter;

  AssertConversion(converter, "true", true);
  AssertConversion(converter, "tRuE", true);
  AssertConversion(converter, "FAlse", false);
  AssertConversion(converter, "false", false);
  AssertConversion(converter, "1", true);
  AssertConversion(converter, "0", false);

  AssertConversionFails(converter, "");
}

TEST(StringConversion, ToFloat) {
  StringConverter<FloatType> converter;

  AssertConversion(converter, "1.5", 1.5f);
  AssertConversion(converter, "0", 0.0f);
  // XXX ASSERT_EQ doesn't distinguish signed zeros
  AssertConversion(converter, "-0.0", -0.0f);
  AssertConversion(converter, "-1e20", -1e20f);

  AssertConversionFails(converter, "");
  AssertConversionFails(converter, "e");
}

TEST(StringConversion, ToDouble) {
  StringConverter<DoubleType> converter;

  AssertConversion(converter, "1.5", 1.5);
  AssertConversion(converter, "0", 0);
  // XXX ASSERT_EQ doesn't distinguish signed zeros
  AssertConversion(converter, "-0.0", -0.0);
  AssertConversion(converter, "-1e100", -1e100);

  AssertConversionFails(converter, "");
  AssertConversionFails(converter, "e");
}

TEST(StringConversion, ToFloatLocale) {
  // French locale uses the comma as decimal point
  LocaleGuard locale_guard("fr_FR.UTF-8");

  StringConverter<FloatType> converter;
  AssertConversion(converter, "1.5", 1.5f);
}

TEST(StringConversion, ToDoubleLocale) {
  // French locale uses the comma as decimal point
  LocaleGuard locale_guard("fr_FR.UTF-8");

  StringConverter<DoubleType> converter;
  AssertConversion(converter, "1.5", 1.5f);
}

TEST(StringConversion, ToInt8) {
  StringConverter<Int8Type> converter;

  AssertConversion(converter, "0", 0);
  AssertConversion(converter, "127", 127);
  AssertConversion(converter, "-128", -128);

  // Non-representable values
  AssertConversionFails(converter, "128");
  AssertConversionFails(converter, "-129");

  AssertConversionFails(converter, "");
  AssertConversionFails(converter, "0.0");
  AssertConversionFails(converter, "e");
}

TEST(StringConversion, ToUInt8) {
  StringConverter<UInt8Type> converter;

  AssertConversion(converter, "0", 0);
  AssertConversion(converter, "255", 255);

  // Non-representable values
  //   AssertConversionFails(converter, "-1");
  AssertConversionFails(converter, "256");

  AssertConversionFails(converter, "");
  AssertConversionFails(converter, "0.0");
  AssertConversionFails(converter, "e");
}

TEST(StringConversion, ToInt16) {
  StringConverter<Int16Type> converter;

  AssertConversion(converter, "0", 0);
  AssertConversion(converter, "32767", 32767);
  AssertConversion(converter, "-32768", -32768);

  // Non-representable values
  AssertConversionFails(converter, "32768");
  AssertConversionFails(converter, "-32769");

  AssertConversionFails(converter, "");
  AssertConversionFails(converter, "0.0");
  AssertConversionFails(converter, "e");
}

TEST(StringConversion, ToUInt16) {
  StringConverter<UInt16Type> converter;

  AssertConversion(converter, "0", 0);
  AssertConversion(converter, "65535", 65535);

  // Non-representable values
  //   AssertConversionFails(converter, "-1");
  AssertConversionFails(converter, "65536");

  AssertConversionFails(converter, "");
  AssertConversionFails(converter, "0.0");
  AssertConversionFails(converter, "e");
}

TEST(StringConversion, ToInt32) {
  StringConverter<Int32Type> converter;

  AssertConversion(converter, "0", 0);
  AssertConversion(converter, "2147483647", 2147483647);
  AssertConversion(converter, "-2147483648", -2147483648LL);

  // Non-representable values
  AssertConversionFails(converter, "2147483648");
  AssertConversionFails(converter, "-2147483649");

  AssertConversionFails(converter, "");
  AssertConversionFails(converter, "0.0");
  AssertConversionFails(converter, "e");
}

TEST(StringConversion, ToUInt32) {
  StringConverter<UInt32Type> converter;

  AssertConversion(converter, "0", 0);
  AssertConversion(converter, "4294967295", 4294967295UL);

  // Non-representable values
  //   AssertConversionFails(converter, "-1");
  AssertConversionFails(converter, "4294967296");

  AssertConversionFails(converter, "");
  AssertConversionFails(converter, "0.0");
  AssertConversionFails(converter, "e");
}

TEST(StringConversion, ToInt64) {
  StringConverter<Int64Type> converter;

  AssertConversion(converter, "0", 0);
  AssertConversion(converter, "9223372036854775807", 9223372036854775807LL);
  AssertConversion(converter, "-9223372036854775808", -9223372036854775807LL - 1);

  // Non-representable values
  AssertConversionFails(converter, "9223372036854775808");
  AssertConversionFails(converter, "-9223372036854775809");

  AssertConversionFails(converter, "");
  AssertConversionFails(converter, "0.0");
  AssertConversionFails(converter, "e");
}

TEST(StringConversion, ToUInt64) {
  StringConverter<UInt64Type> converter;

  AssertConversion(converter, "0", 0);
  AssertConversion(converter, "18446744073709551615", 18446744073709551615ULL);

  // Non-representable values
  //   AssertConversionFails(converter, "-1");
  AssertConversionFails(converter, "18446744073709551616");

  AssertConversionFails(converter, "");
  AssertConversionFails(converter, "0.0");
  AssertConversionFails(converter, "e");
}

}  // namespace arrow
