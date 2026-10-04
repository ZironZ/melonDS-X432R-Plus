// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later

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
