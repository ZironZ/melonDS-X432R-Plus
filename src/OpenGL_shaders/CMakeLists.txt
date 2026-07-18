set(OPENGL_SHADER_FILES
    2DLayerPreVS.glsl
    2DLayerPreFS.glsl
    2DSpritePreVS.glsl
    2DSpritePreFS.glsl
    2DSpriteVS.glsl
    2DSpriteFS.glsl
    2DCompositorVS.glsl
    2DCompositorFS.glsl
    2DNativePrepassFS.glsl
    2DNativeUpscaleFS.glsl
    2DNativeBoundaryGuardFS.glsl
    2DNativeResolveFS.glsl
    2DNative3DResolveFS.glsl
    2DOverlayEndpointFS.glsl
    2DOverlayBaselineCompositeFS.glsl
    2DOverlayCompositeFS.glsl
    2DOverlayDebugFS.glsl
    2DMasterBrightnessFS.glsl
    2DFullscreenPassVS.glsl
    2DRGBAToYUVAFS.glsl
    2DSpline36FS.glsl
    2DArtCNN_YUVAToRGBA2xFS.glsl
    2DAlphaReplaceFS.glsl
    2DMidpointAlphaFS.glsl
    2DArtCNN_C4F16_Conv0CS.glsl
    2DArtCNN_C4F16_Conv1CS.glsl
    2DArtCNN_C4F16_Conv2CS.glsl
    2DArtCNN_C4F16_Conv3CS.glsl
    2DArtCNN_C4F16_Conv4CS.glsl
    2DArtCNN_C4F16_Conv5CS.glsl
    2DArtCNN_C4F16_Conv6CS.glsl
    2DArtCNN_C4F16_DepthToSpaceCS.glsl
    2DArtCNN_C4F16DN_Conv0CS.glsl
    2DArtCNN_C4F16DN_Conv1CS.glsl
    2DArtCNN_C4F16DN_Conv2CS.glsl
    2DArtCNN_C4F16DN_Conv3CS.glsl
    2DArtCNN_C4F16DN_Conv4CS.glsl
    2DArtCNN_C4F16DN_Conv5CS.glsl
    2DArtCNN_C4F16DN_Conv6CS.glsl
    2DArtCNN_C4F16DN_DepthToSpaceCS.glsl
    2DNNEDI3_VerticalCS.glsl
    2DNNEDI3_HorizontalCS.glsl
    2DCuNNy4x32_InCS.glsl
    2DCuNNy4x32_Conv1CS.glsl
    2DCuNNy4x32_Conv2CS.glsl
    2DCuNNy4x32_Conv3CS.glsl
    2DCuNNy4x32_Conv4CS.glsl
    2DCuNNy4x32_OutShuffleCS.glsl
    2DXBRZ_PreprocessFS.glsl
    2DXBRZ_FreescaleFS.glsl
    3DTexcacheRepackFS.glsl
    3DAlphaAwareMipFS.glsl

    FinalPassVS.glsl
    FinalPassFS.glsl
    CaptureVS.glsl
    CaptureFS.glsl
    CaptureDownscaleVS.glsl
    CaptureDownscaleFS.glsl

    3DClearVS.glsl
    3DClearFS.glsl
    3DClearBitmapVS.glsl
    3DClearBitmapFS.glsl
    3DRenderVS.glsl
    3DRenderFS.glsl
    3DFinalPassVS.glsl
    3DFinalPassEdgeFS.glsl
    3DFinalPassFogFS.glsl
)

foreach(SH_INPUT ${OPENGL_SHADER_FILES})
    get_filename_component(SH_NAME ${SH_INPUT} NAME_WE)
    set(SH_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${SH_NAME}.h")
    add_custom_command(
        OUTPUT ${SH_OUTPUT}
        DEPENDS ${SH_INPUT}
        COMMAND
            ${CMAKE_COMMAND}
            -D INPUT_FILE=${SH_INPUT}
            -D OUTPUT_FILE=${SH_OUTPUT}
            -D VAR_NAME=${SH_NAME}
            -P "${CMAKE_CURRENT_LIST_DIR}/../../cmake/MakeEmbed.cmake"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    list(APPEND OPENGL_SHADER_OUT ${SH_OUTPUT})
endforeach()

add_custom_target(OpenGL_shaders ALL DEPENDS ${OPENGL_SHADER_OUT})
add_dependencies(core OpenGL_shaders)
