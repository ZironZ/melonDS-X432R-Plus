// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QFile>
#include <QVector2D>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

int RunNNEDI3CompositionDiagnostic(const QStringList& args);
int RunTextureScaleRegionTests(bool corruptOrigin);
int RunScalerAlphaFusionTests(bool benchmark);

static void Check(bool good, const char* message)
{
    if (!good) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    if (app.arguments().contains("--test-scaler-alpha-fusion"))
        return RunScalerAlphaFusionTests(app.arguments().contains("--benchmark"));
    if (app.arguments().contains("--test-edge-regions"))
        return RunTextureScaleRegionTests(app.arguments().contains("--corrupt-edge-origin"));
    if (app.arguments().contains("--compare-source"))
        return RunNNEDI3CompositionDiagnostic(app.arguments());
    bool bounded = !app.arguments().contains("--unbounded");
    QSurfaceFormat format;
    format.setVersion(4, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;
    context.setFormat(format);
    Check(context.create(), "OpenGL 4.3 context");
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    Check(context.makeCurrent(&surface), "offscreen context current");
    auto* gl = context.extraFunctions();
    gl->initializeOpenGLFunctions();
    QOpenGLShaderProgram vertical, horizontal;
    auto compile = [](QOpenGLShaderProgram& shader, const char* file) {
        QFile input(QString(SHADER_SOURCE_DIR) + '/' + file);
        Check(input.open(QIODevice::ReadOnly), "read production shader");
        Check(shader.addShaderFromSourceCode(QOpenGLShader::Compute, input.readAll()), "compile compute shader");
        Check(shader.link(), "link compute shader");
    };
    compile(vertical, "2DNNEDI3_VerticalCS.glsl");
    compile(horizontal, "2DNNEDI3_HorizontalCS.glsl");
    GLuint fb;
    gl->glGenFramebuffers(1, &fb);
    auto texture = [&](int w, int h, const float* pixels) {
        GLuint id;
        gl->glGenTextures(1, &id);
        gl->glBindTexture(GL_TEXTURE_2D, id);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, pixels);
        return id;
    };
    auto read = [&](GLuint tex, int w, int h) {
        std::vector<float> pixels(w * h * 4);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fb);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        Check(gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "readback framebuffer");
        gl->glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, pixels.data());
        return pixels;
    };
    auto pass = [&](QOpenGLShaderProgram& shader, GLuint input, bool v, bool bound, bool premultiply = false) {
        GLuint output = texture(v ? 16 : 32, v ? 32 : 16, nullptr);
        shader.bind();
        gl->glUniform2i(shader.uniformLocation("uSrcSize"), 16, 16);
        shader.setUniformValue("Source", 0);
        shader.setUniformValue("uPredictAlpha", true);
        shader.setUniformValue("uAlphaOnly", false);
        shader.setUniformValue("uBoundedCoverageAlpha", bound);
        gl->glUniform1i(shader.uniformLocation("uPremultiplyInput"), premultiply);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, input);
        gl->glBindImageTexture(0, output, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        gl->glDispatchCompute(2, 2, 1);
        gl->glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
        return output;
    };
    // Real production shaders and half-float storage: preserve weak coverage,
    // don't sprout coverage/holes around a hard edge, and don't change RGB.
    int cases = 0;
    for (bool v : {false, true})
    for (int pattern = 0; pattern < 7; pattern++)
    {
        std::vector<float> source(16 * 16 * 4);
        for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            int i = (y * 16 + x) * 4;
            source[i] = float(x) / 16;
            source[i + 1] = float(y) / 16;
            source[i + 2] = float((x + y) % 3) / 4;
            source[i + 3] = pattern == 0 ? 0.0f : pattern == 1 ? 1.0f :
                pattern == 2 ? 1.0f / 128.0f : pattern == 3 ? float((v ? y : x) >= 8) :
                pattern == 4 ? float(x == y) : pattern == 5 ? float(x >= 8 && y >= 8) : float(x == 8 && y == 8);
        }
        GLuint input = texture(16, 16, source.data());
        auto& shader = v ? vertical : horizontal;
        GLuint output = pass(shader, input, v, bounded);
        GLuint reference = pass(shader, input, v, false);
        int w = v ? 16 : 32, h = v ? 32 : 16;
        auto pixels = read(output, w, h), rgbReference = read(reference, w, h);
        if (v)
        {
            // Input premultiplication must preserve alpha, including weak and
            // zero coverage, and multiply copied native rows exactly once.
            GLuint premultOutput = pass(shader, input, v, bounded, true);
            auto premultPixels = read(premultOutput, w, h);
            for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                int i = (y * w + x) * 4;
                Check(premultPixels[i + 3] == pixels[i + 3], "premultiplied input preserves predicted alpha");
                if ((y & 1) == 0)
                {
                    int native = ((y / 2) * 16 + x) * 4;
                    for (int c = 0; c < 3; c++)
                        Check(premultPixels[i + c] == source[native + c] * source[native + 3],
                              "premultiply copied native RGB once");
                }
            }
            gl->glDeleteTextures(1, &premultOutput);
        }
        for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            int i = (y * w + x) * 4, axis = v ? y : x;
            for (int c = 0; c < 3; c++)
                Check(pixels[i + c] == rgbReference[i + c], "RGB unchanged by coverage rule");
            float alpha = pixels[i + 3];
            Check(std::isfinite(alpha) && alpha >= 0 && alpha <= 1, "finite coverage");
            if (pattern < 3)
                Check(alpha == source[3], "constant coverage, including 1/128, preserved exactly");
            if (pattern == 3 && axis != 15)
                Check(alpha == float(axis >= 16), "hard edge has no exterior lobe or interior hole");
            int crossAxis = v ? x : y;
            if (pattern == 5 && (axis < 15 || crossAxis < 8))
                Check(alpha == 0, "opaque corner does not grow coverage outside its adjacent cells");
            if (pattern == 6 && (axis < 15 || axis > 17 || crossAxis != 8))
                Check(alpha == 0, "isolated point does not acquire detached coverage");
            if ((axis & 1) == 0)
                Check(alpha == source[((v ? y / 2 : y) * 16 + (v ? x : x / 2)) * 4 + 3],
                      "native samples, including a one-pixel diagonal, preserved");
        }
        gl->glDeleteTextures(1, &reference);
        gl->glDeleteTextures(1, &output);
        gl->glDeleteTextures(1, &input);
        cases++;
    }
    // The phase-alignment pass must not reintroduce a lobe after prediction.
    QOpenGLShaderProgram phase;
    Check(phase.addShaderFromSourceCode(QOpenGLShader::Vertex,
        "#version 140\nvoid main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);"
        " gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }"), "phase vertex shader");
    QFile phaseFile(QString(SHADER_SOURCE_DIR) + "/2DSpline36FS.glsl");
    Check(phaseFile.open(QIODevice::ReadOnly), "read phase shader");
    Check(phase.addShaderFromSourceCode(QOpenGLShader::Fragment, phaseFile.readAll()), "compile phase shader");
    Check(phase.link(), "link phase shader");
    GLuint vao;
    gl->glGenVertexArrays(1, &vao);
    gl->glBindVertexArray(vao);
    for (int pattern = 0; pattern < 2; pattern++)
    {
        std::vector<float> source(16 * 16 * 4, 0.5f);
        for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            source[(y * 16 + x) * 4 + 3] = pattern == 0 ? 1.0f / 128.0f : float(x >= 8);
        GLuint input = texture(16, 16, source.data());
        GLuint output = texture(16, 16, nullptr);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fb);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
        phase.bind();
        phase.setUniformValue("Source", 0);
        phase.setUniformValue("uOutputSize", QVector2D(16, 16));
        phase.setUniformValue("uSourceShift", QVector2D(-0.5f, -0.5f));
        phase.setUniformValue("uTransparentSourceAware", false);
        phase.setUniformValue("uBoundedCoverageAlpha", bounded);
        phase.setUniformValue("uResolvePremultipliedRGB", false);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, input);
        gl->glViewport(0, 0, 16, 16);
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        auto pixels = read(output, 16, 16);
        phase.setUniformValue("uResolvePremultipliedRGB", true);
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        auto straight = read(output, 16, 16);
        for (size_t i = 0; i < pixels.size(); i += 4)
        {
            Check(straight[i + 3] == pixels[i + 3], "premultiplied resolve preserves bounded alpha");
            for (int c = 0; c < 3; c++)
            {
                Check(std::isfinite(straight[i + c]) && straight[i + c] >= 0 && straight[i + c] <= 1,
                      "resolved straight RGB remains finite and bounded");
                if (straight[i + 3] == 0)
                    Check(straight[i + c] == 0, "zero-alpha resolve has no hidden color");
                else
                    Check(std::abs(straight[i + c] * straight[i + 3] -
                        std::fmin(pixels[i + c], pixels[i + 3])) < 0.001f,
                        "resolve recovers constrained premultiplied color within half-float precision");
            }
        }
        for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            float alpha = pixels[(y * 16 + x) * 4 + 3];
            if (pattern == 0)
                Check(alpha == 1.0f / 128.0f, "phase filter preserves weak constant coverage");
            else if (x != 8)
                Check(alpha == float(x > 8), "phase filter does not reintroduce edge lobes");
        }
        gl->glDeleteTextures(1, &output);
        gl->glDeleteTextures(1, &input);
        cases++;
    }
    gl->glDeleteVertexArrays(1, &vao);
    Check(gl->glGetError() == GL_NO_ERROR, "OpenGL errors");
    gl->glDeleteFramebuffers(1, &fb);
    std::printf("%d production-shader coverage cases passed\n", cases);
}
