// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScreenCapture.h"
#include "WideMelon.h"

bool ReadScreenPresentation(GLuint program, GLuint vertexArray, GLuint source,
                            std::array<QImage, 2>& images, QString* error)
{
    images = {};
    if (error) error->clear();

    GLint oldReadFB, oldDrawFB, oldProgram, oldVAO, oldViewport[4];
    GLint oldActive, oldArray, oldTexture, oldPackBuffer, oldUnpackBuffer, oldPack[4];
    GLboolean oldMask[4];
    const GLenum packNames[] = {GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,
                                GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldReadFB);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDrawFB);
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVAO);
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &oldPackBuffer);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &oldUnpackBuffer);
    glGetBooleani_v(GL_COLOR_WRITEMASK, 0, oldMask);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &oldArray);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, source);
    GLint sourceWidth = 0, sourceHeight = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_WIDTH, &sourceWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_HEIGHT, &sourceHeight);

    const GLenum caps[] = {GL_DEPTH_TEST, GL_STENCIL_TEST, GL_SCISSOR_TEST,
                          GL_CULL_FACE, GL_RASTERIZER_DISCARD, GL_FRAMEBUFFER_SRGB, GL_DITHER};
    GLboolean wasEnabled[std::size(caps)];
    for (size_t i = 0; i < std::size(caps); i++)
    {
        wasEnabled[i] = glIsEnabled(caps[i]);
        glDisable(caps[i]);
    }
    const GLboolean blend = glIsEnabledi(GL_BLEND, 0);
    glDisablei(GL_BLEND, 0);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    for (size_t i = 0; i < std::size(packNames); i++)
    {
        glGetIntegerv(packNames[i], &oldPack[i]);
        glPixelStorei(packNames[i], i == 0 ? 1 : 0);
    }

    const GLint sizeLoc = glGetUniformLocation(program, "uScreenSize");
    const GLint transformLoc = glGetUniformLocation(program, "uTransform");
    const GLint contentLoc = glGetUniformLocation(program, "uContentWidth");
    const GLint heightLoc = glGetUniformLocation(program, "uContentHeight");
    GLfloat oldHeight;
    glGetUniformfv(program, heightLoc, &oldHeight);
    GLfloat oldSize[2], oldTransform[6], oldContent;
    glGetUniformfv(program, sizeLoc, oldSize);
    glGetUniformfv(program, transformLoc, oldTransform);
    glGetUniformfv(program, contentLoc, &oldContent);
    glUseProgram(program);
    glBindVertexArray(vertexArray);

    GLuint framebuffer = 0, texture = 0;
    glGenFramebuffers(1, &framebuffer);
    glGenTextures(1, &texture);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    bool ok = sourceWidth > 0 && sourceHeight > 0 && sourceWidth % WideMelon::Width() == 0;
    for (int screen = 0; ok && screen < 2; screen++)
    {
        const int width = sourceWidth / WideMelon::Width() * WideMelon::DisplayWidth(screen);
        const int height = sourceHeight / WideMelon::Height() * WideMelon::DisplayHeight(screen);
        QImage image(width, height, QImage::Format_RGBA8888);
        if (image.isNull()) { ok = false; break; }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        { ok = false; break; }
        glViewport(0, 0, width, height);
        glUniform2f(sizeLoc, width, height);
        const GLfloat transform[] = {width / 256.f, 0, 0, height / 192.f, 0, 0};
        glUniformMatrix2x3fv(transformLoc, 1, GL_TRUE, transform);
        // Same centered content window as the live presentation shader.
        glUniform1f(contentLoc, WideMelon::ContentWidth(screen));
        glUniform1f(heightLoc, WideMelon::ContentHeight(screen));
        glDrawArrays(GL_TRIANGLES, screen * 6, 6);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
        // The presentation vertex shader uses top-down window coordinates.
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        images[screen] = image.flipped(Qt::Vertical);
#else
        images[screen] = image.mirrored(false, true);
#endif
    }

    glUniform2fv(sizeLoc, 1, oldSize);
    glUniformMatrix2x3fv(transformLoc, 1, GL_FALSE, oldTransform);
    glUniform1f(contentLoc, oldContent);
    glUniform1f(heightLoc, oldHeight);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, oldReadFB);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, oldDrawFB);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, oldTexture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, oldArray);
    glActiveTexture(oldActive);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, oldPackBuffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, oldUnpackBuffer);
    for (size_t i = 0; i < std::size(packNames); i++) glPixelStorei(packNames[i], oldPack[i]);
    for (size_t i = 0; i < std::size(caps); i++) if (wasEnabled[i]) glEnable(caps[i]);
    if (blend) glEnablei(GL_BLEND, 0);
    glColorMaski(0, oldMask[0], oldMask[1], oldMask[2], oldMask[3]);
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    glBindVertexArray(oldVAO);
    glUseProgram(oldProgram);
    if (!ok)
    {
        images = {};
        if (error) *error = "Could not capture the completed display images.";
    }
    return ok;
}
