/*
    Copyright 2026 ZironZ

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#pragma once

#include <cstdio>
#include <type_traits>
#include <vector>

namespace policytest
{

using TestFunc = void (*)();

struct TestCase
{
    const char* Name;
    TestFunc Func;
};

std::vector<TestCase>& Registry();

struct Registrar
{
    Registrar(const char* name, TestFunc func);
};

extern int FailureCount;
extern const char* CurrentTestName;

void ReportFailure(const char* file, int line, const char* message);

template <typename T>
long long ToComparableValue(T value)
{
    if constexpr (std::is_enum_v<T>)
        return static_cast<long long>(static_cast<std::underlying_type_t<T>>(value));
    else
        return static_cast<long long>(value);
}

}

#define POLICY_TEST(name) \
    static void PolicyTest_##name(); \
    static ::policytest::Registrar PolicyTestRegistrar_##name(#name, &PolicyTest_##name); \
    static void PolicyTest_##name()

#define CHECK(expr) \
    do { \
        if (!(expr)) \
            ::policytest::ReportFailure(__FILE__, __LINE__, #expr); \
    } while (0)

#define CHECK_EQ(actual, expected) \
    do { \
        const auto policytestActual = (actual); \
        const auto policytestExpected = (expected); \
        if (!(policytestActual == policytestExpected)) \
        { \
            char policytestMessage[512]; \
            std::snprintf(policytestMessage, sizeof(policytestMessage), \
                          "%s == %s (actual %lld, expected %lld)", \
                          #actual, #expected, \
                          ::policytest::ToComparableValue(policytestActual), \
                          ::policytest::ToComparableValue(policytestExpected)); \
            ::policytest::ReportFailure(__FILE__, __LINE__, policytestMessage); \
        } \
    } while (0)
