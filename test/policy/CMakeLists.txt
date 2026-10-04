add_executable(policy-tests
    PolicyTestHarness.h
    PolicyTestMain.cpp
    AffineReconstructionSettingsTests.cpp
    TextureAlphaSettingsTests.cpp
    BGVRAMUploadCacheTests.cpp
    CapturePolicyTests.cpp
    CleanLegacyEligibilityTests.cpp
    CompositorInputPolicyTests.cpp
    ComputeGeometryTests.cpp
    OutputPlanTests.cpp
    OBJBandPolicyTests.cpp
    RecordedCaseTests.cpp
    ScalePolicyTests.cpp
    TextureRepackPolicyTests.cpp
    TextureMipTests.cpp
    TextureReconstructionTests.cpp
    TextureCacheLifetimeTests.cpp
    WidescreenProfileTests.cpp
    WideWindowPolicyTests.cpp
    WideDisplayPolicyTests.cpp
    WideCapturePolicyTests.cpp
    WideTransitionPolicyTests.cpp
    "${CMAKE_SOURCE_DIR}/src/frontend/ScreenLayout.cpp"
    "${CMAKE_SOURCE_DIR}/src/WholeSceneCapturePolicy.cpp"
    "${CMAKE_SOURCE_DIR}/src/WholeSceneCompositorInput.cpp"
    "${CMAKE_SOURCE_DIR}/src/WholeSceneOutputPlan.cpp"
    "${CMAKE_SOURCE_DIR}/src/WholeSceneOBJBandPolicy.cpp"
    "${CMAKE_SOURCE_DIR}/src/WholeSceneScalePolicy.cpp")

target_include_directories(policy-tests PRIVATE "${CMAKE_SOURCE_DIR}/src")

add_test(NAME policy-tests COMMAND policy-tests)
