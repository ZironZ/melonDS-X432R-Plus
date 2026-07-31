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

#include "PolicyTestHarness.h"

namespace policytest
{

std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> registry;
    return registry;
}

Registrar::Registrar(const char* name, TestFunc func)
{
    Registry().push_back({name, func});
}

int FailureCount = 0;
const char* CurrentTestName = "";

void ReportFailure(const char* file, int line, const char* message)
{
    std::printf("FAIL %s: %s:%d: %s\n", CurrentTestName, file, line, message);
    FailureCount++;
}

}

int main()
{
    int failedTests = 0;
    for (const auto& test : policytest::Registry())
    {
        policytest::CurrentTestName = test.Name;
        const int failuresBefore = policytest::FailureCount;
        test.Func();
        if (policytest::FailureCount != failuresBefore)
            failedTests++;
    }

    std::printf("%zu tests: %d failed test(s), %d failed check(s)\n",
                policytest::Registry().size(), failedTests, policytest::FailureCount);
    return policytest::FailureCount == 0 ? 0 : 1;
}
