add_executable(policy-tests
    PolicyTestHarness.h
    PolicyTestMain.cpp
    CapturePolicyTests.cpp
    CleanLegacyEligibilityTests.cpp
    RecordedCaseTests.cpp
    ScalePolicyTests.cpp
    TextureRepackPolicyTests.cpp
    "${CMAKE_SOURCE_DIR}/src/WholeSceneCapturePolicy.cpp"
    "${CMAKE_SOURCE_DIR}/src/WholeSceneScalePolicy.cpp")

target_include_directories(policy-tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

add_test(NAME policy-tests COMMAND policy-tests)
