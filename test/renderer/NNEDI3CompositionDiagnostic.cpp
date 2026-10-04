// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QImage>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QByteArray>
#include <QStringList>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace {
void Require(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}

int RunNNEDI3CompositionDiagnostic(const QStringList& args)
{
    auto argument = [&](const QString& key) {
        const int i = args.indexOf(key);
        Require(i >= 0 && i + 1 < args.size(), "missing diagnostic argument");
        return args[i + 1];
    };
    const QString inputPath = argument("--compare-source");
    const QDir output(argument("--compare-output"));
    const int scale = argument("--compare-scale").toInt();
    Require(scale == 2 || scale == 4, "comparison scale must be 2 or 4");
    QImage input(inputPath);
    Require(!input.isNull(), "read native RGBA source");
    input = input.convertToFormat(QImage::Format_RGBA8888);
    const int width = input.width(), height = input.height();
    std::vector<float> native(width * height * 4);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (int y = 0; y < height; y++)
    {
        const auto* row = input.constScanLine(y);
        hash.addData(QByteArray::fromRawData(reinterpret_cast<const char*>(row), width * 4));
        for (int x = 0; x < width * 4; x++) native[y * width * 4 + x] = row[x] / 255.f;
    }
    QSurfaceFormat format;
    std::vector<float> sceneBackground;
    QString backgroundPath;
    if (args.contains("--compare-background"))
    {
        backgroundPath = argument("--compare-background");
        QImage background(backgroundPath);
        Require(!background.isNull() && background.size() == input.size(), "background dimensions");
        background = background.convertToFormat(QImage::Format_RGBA8888);
        sceneBackground.resize(native.size());
        for (int y = 0; y < height; y++)
        for (int x = 0; x < width * 4; x++)
            sceneBackground[y * width * 4 + x] = background.constScanLine(y)[x] / 255.f;
    }
    std::vector<const char*> backgroundNames = {"black", "gray", "white"};
    if (!sceneBackground.empty()) backgroundNames.push_back("scene");
    auto backgroundValue = [&](const char* name, size_t component) {
        return QString(name) == "scene" ? sceneBackground[component] :
            QString(name) == "black" ? 0.f : QString(name) == "gray" ? .25f : 1.f;
    };
    format.setVersion(4, 3); format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context; context.setFormat(format);
    Require(context.create(), "OpenGL context");
    QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
    Require(context.makeCurrent(&surface), "offscreen context");
    auto* gl = context.extraFunctions(); gl->initializeOpenGLFunctions();
    GLuint fb, vao;
    gl->glGenFramebuffers(1, &fb); gl->glGenVertexArrays(1, &vao); gl->glBindVertexArray(vao);
    auto compile = [&](QOpenGLShaderProgram& shader, const char* file, bool compute) {
        if (!compute)
            Require(shader.addShaderFromSourceCode(QOpenGLShader::Vertex,
                "#version 140\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}"), "vertex shader");
        QFile source(QString(SHADER_SOURCE_DIR) + '/' + file);
        Require(source.open(QIODevice::ReadOnly), "read production shader");
        QByteArray code = source.readAll();
        // Qt's shader preamble detection needs the version before long license
        // comments. Preserve those comments while moving only the directive.
        const int version = code.indexOf("#version");
        const int versionEnd = code.indexOf('\n', version);
        if (version > 0 && versionEnd > version)
        {
            const QByteArray directive = code.mid(version, versionEnd - version + 1);
            code.remove(version, directive.size());
            code.prepend(directive);
        }
        if (!shader.addShaderFromSourceCode(compute ? QOpenGLShader::Compute : QOpenGLShader::Fragment,
                                           code))
        {
            std::fprintf(stderr, "%s: %s\n", file, shader.log().toUtf8().constData());
            Require(false, "compile production shader");
        }
        Require(shader.link(), "link shader");
    };
    QOpenGLShaderProgram vertical, horizontal, phase, pad;
    compile(vertical, "2DNNEDI3_VerticalCS.glsl", true);
    compile(horizontal, "2DNNEDI3_HorizontalCS.glsl", true);
    compile(phase, "2DSpline36FS.glsl", false);
    compile(pad, "2DTransparentRGBPadFS.glsl", false);
    auto texture = [&](int w, int h, const float* data, bool rgba8 = false) {
        GLuint id; gl->glGenTextures(1, &id); gl->glBindTexture(GL_TEXTURE_2D, id);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, rgba8 ? GL_RGBA8 : GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, data);
        return id;
    };
    auto fragment = [&](QOpenGLShaderProgram& shader, GLuint source, GLuint target, int w, int h) {
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fb);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
        Require(gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "fragment framebuffer");
        gl->glViewport(0, 0, w, h); shader.bind(); shader.setUniformValue("Source", 0);
        gl->glActiveTexture(GL_TEXTURE0); gl->glBindTexture(GL_TEXTURE_2D, source);
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    };
    auto chain = [&](GLuint source, bool bounded, bool premultiplyInput = false, bool resolvePremultipliedRGB = false) {
        GLuint current = source;
        int w = width, h = height;
        for (int factor = 1; factor < scale; factor *= 2)
        for (bool v : {true, false})
        {
            auto& shader = v ? vertical : horizontal;
            GLuint target = texture(v ? w : w * 2, v ? h * 2 : h, nullptr);
            shader.bind(); shader.setUniformValue("Source", 0);
            shader.setUniformValue("uPredictAlpha", true);
            shader.setUniformValue("uAlphaOnly", false);
            shader.setUniformValue("uBoundedCoverageAlpha", bounded);
            gl->glUniform1i(shader.uniformLocation("uPremultiplyInput"), premultiplyInput && factor == 1 && v);
            gl->glUniform2i(shader.uniformLocation("uSrcSize"), w, h);
            gl->glActiveTexture(GL_TEXTURE0); gl->glBindTexture(GL_TEXTURE_2D, current);
            gl->glBindImageTexture(0, target, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            gl->glDispatchCompute((w + 7) / 8, (h + 7) / 8, 1);
            gl->glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
            if (current != source) gl->glDeleteTextures(1, &current);
            current = target;
            if (v) h *= 2; else w *= 2;
        }
        GLuint target = texture(w, h, nullptr);
        phase.bind();
        gl->glUniform2f(phase.uniformLocation("uOutputSize"), w, h);
        const float shift = scale == 4 ? -1.5f : -.5f;
        gl->glUniform2f(phase.uniformLocation("uSourceShift"), shift, shift);
        phase.setUniformValue("uTransparentSourceAware", false);
        phase.setUniformValue("uBoundedCoverageAlpha", bounded);
        phase.setUniformValue("uResolvePremultipliedRGB", resolvePremultipliedRGB);
        fragment(phase, current, target, w, h);
        gl->glDeleteTextures(1, &current);
        return target;
    };
    Require(QDir().mkpath(output.path()), "create output directory");
    auto save = [&](const char* name, GLuint tex) {
        std::vector<float> pixels(width * scale * height * scale * 4);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fb);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        Require(gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "readback framebuffer");
        gl->glReadPixels(0, 0, width * scale, height * scale, GL_RGBA, GL_FLOAT, pixels.data());
        QFile file(output.filePath(QString(name) + ".rgba32f"));
        Require(file.open(QIODevice::WriteOnly), "open float output");
        const qint64 size = pixels.size() * sizeof(float);
        Require(file.write(reinterpret_cast<const char*>(pixels.data()), size) == size, "write float output");
        gl->glDeleteTextures(1, &tex);
    };
    GLuint source = texture(width, height, native.data(), true);
    GLuint prepared = texture(width, height, nullptr, true);
    pad.bind(); pad.setUniformValue("uPadRadius", 8); pad.setUniformValue("uPrecomposeBackdrop", false);
    gl->glUniform2i(pad.uniformLocation("uSourceSize"), width, height);
    fragment(pad, source, prepared, width, height);
    save("separate", chain(prepared, true));
    save("premult-integrated", chain(prepared, true, true, true));
    gl->glDeleteTextures(1, &prepared); gl->glDeleteTextures(1, &source);
    auto premult = native;
    for (size_t i = 0; i < premult.size(); i += 4)
        for (int c = 0; c < 3; c++) premult[i+c] *= premult[i+3];
    source = texture(width, height, premult.data(), true);
    save("premult-bounded", chain(source, true));
    save("premult-unbounded", chain(source, false));
    gl->glDeleteTextures(1, &source);
    for (const char* name : backgroundNames)
    {
        auto flat = native;
        for (size_t i = 0; i < flat.size(); i += 4)
        {
            for (int c = 0; c < 3; c++) flat[i+c] = premult[i+c] + backgroundValue(name, i+c) * (1.f-native[i+3]);
            flat[i+3] = 1.f;
        }
        source = texture(width, height, flat.data());
        const QByteArray fileName = QByteArray("flat-") + name;
        save(fileName.constData(), chain(source, true));
        gl->glDeleteTextures(1, &source);
    }
    if (args.contains("--compare-xbrz"))
    {
        QOpenGLShaderProgram preprocess, freescale;
        compile(preprocess, "2DXBRZ_PreprocessFS.glsl", false);
        compile(freescale, "2DXBRZ_FreescaleFS.glsl", false);
        GLuint info = texture(width, height, nullptr, true);
        auto xbrz = [&](GLuint inputTex) {
            fragment(preprocess, inputTex, info, width, height);
            GLuint result = texture(width * scale, height * scale, nullptr);
            freescale.bind(); freescale.setUniformValue("InfoTex", 1);
            gl->glUniform2f(freescale.uniformLocation("uOutputSize"), width * scale, height * scale);
            gl->glActiveTexture(GL_TEXTURE1); gl->glBindTexture(GL_TEXTURE_2D, info);
            fragment(freescale, inputTex, result, width * scale, height * scale);
            return result;
        };
        source = texture(width, height, native.data(), true);
        prepared = texture(width, height, nullptr, true);
        pad.bind(); pad.setUniformValue("uPadRadius", 2);
        fragment(pad, source, prepared, width, height);
        save("xbrz-padded", xbrz(prepared));
        gl->glDeleteTextures(1, &prepared); gl->glDeleteTextures(1, &source);
        auto mask = native;
        for (size_t i = 0; i < mask.size(); i += 4)
            for (int c = 0; c < 3; c++) mask[i+c] = mask[i+3];
        source = texture(width, height, mask.data(), true);
        save("xbrz-mask", xbrz(source)); gl->glDeleteTextures(1, &source);
        source = texture(width, height, premult.data(), true);
        save("xbrz-premult", xbrz(source)); gl->glDeleteTextures(1, &source);
        for (const char* name : backgroundNames)
        {
            auto flat = native;
            for (size_t i = 0; i < flat.size(); i += 4)
            {
                for (int c = 0; c < 3; c++) flat[i+c] = premult[i+c] + backgroundValue(name, i+c) * (1.f-native[i+3]);
                flat[i+3] = 1.f;
            }
            source = texture(width, height, flat.data());
            const QByteArray fileName = QByteArray("xbrz-flat-") + name;
            save(fileName.constData(), xbrz(source)); gl->glDeleteTextures(1, &source);
        }
        gl->glDeleteTextures(1, &info);
    }
    if (args.contains("--compare-cunny"))
    {
        source = texture(width, height, native.data(), true);
        prepared = texture(width, height, nullptr, true);
        pad.bind(); pad.setUniformValue("uPadRadius", 6);
        fragment(pad, source, prepared, width, height);
        gl->glDeleteTextures(1, &source);
        auto cunny = [&](GLuint prepared) {
            GLuint current = prepared;
            int currentWidth = width, currentHeight = height;
            for (const char* name : {"In", "Conv1", "Conv2", "Conv3", "Conv4", "OutShuffle"})
            {
                QOpenGLShaderProgram shader;
                const QByteArray fileName = QByteArray("2DCuNNy4x32_") + name + "CS.glsl";
                compile(shader, fileName.constData(), true);
                const bool last = QString(name) == "OutShuffle";
                GLuint target = texture(width * (last ? 2 : 4), height * 2, nullptr);
                shader.bind();
                for (const char* prefix : {"LUMA", "MAIN", "in", "conv1", "conv2", "conv3", "conv4", "conv5", "conv6", "conv7", "conv8"})
                {
                    const bool base = QString(prefix) == "LUMA" || QString(prefix) == "MAIN";
                    const QByteArray p(prefix);
                    gl->glUniform1i(shader.uniformLocation((p + "_raw").constData()), base ? 1 : 0);
                    gl->glUniform4f(shader.uniformLocation((p + "_mul").constData()), 1, 1, 1, 1);
                    gl->glUniform2f(shader.uniformLocation((p + "_pt").constData()),
                        1.f / (base ? width : currentWidth), 1.f / (base ? height : currentHeight));
                    if (base) gl->glUniform2f(shader.uniformLocation((p + "_size").constData()), width, height);
                }
                gl->glActiveTexture(GL_TEXTURE0); gl->glBindTexture(GL_TEXTURE_2D, current);
                gl->glActiveTexture(GL_TEXTURE1); gl->glBindTexture(GL_TEXTURE_2D, prepared);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                gl->glBindImageTexture(0, target, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
                gl->glDispatchCompute((width + 7) / 8, (height + 7) / 8, 1);
                gl->glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
                if (current != prepared) gl->glDeleteTextures(1, &current);
                current = target; currentWidth = width * (last ? 2 : 4); currentHeight = height * 2;
            }
            if (scale == 4)
            {
                GLuint target = texture(width * scale, height * scale, nullptr);
                phase.bind();
                gl->glUniform2f(phase.uniformLocation("uOutputSize"), width * scale, height * scale);
                gl->glUniform2f(phase.uniformLocation("uSourceShift"), 0, 0);
                phase.setUniformValue("uTransparentSourceAware", false);
                phase.setUniformValue("uBoundedCoverageAlpha", false);
                fragment(phase, current, target, width * scale, height * scale);
                gl->glDeleteTextures(1, &current); current = target;
            }
            return current;
        };
        save("cunny-rgb", cunny(prepared));
        gl->glDeleteTextures(1, &prepared);
        source = texture(width, height, premult.data(), true);
        save("cunny-premult", cunny(source));
        gl->glDeleteTextures(1, &source);
        for (const char* name : backgroundNames)
        {
            auto flat = native;
            for (size_t i = 0; i < flat.size(); i += 4)
            {
                for (int c = 0; c < 3; c++) flat[i+c] = premult[i+c] + backgroundValue(name, i+c) * (1.f-native[i+3]);
                flat[i+3] = 1.f;
            }
            source = texture(width, height, flat.data());
            const QByteArray fileName = QByteArray("cunny-flat-") + name;
            save(fileName.constData(), cunny(source));
            gl->glDeleteTextures(1, &source);
        }
    }
    Require(gl->glGetError() == GL_NO_ERROR, "GPU errors");
    QJsonObject manifest{{"source", inputPath}, {"source_rgba_sha256", QString(hash.result().toHex())},
        {"width", width * scale}, {"height", height * scale}, {"scale", scale},
        {"xbrz_products", args.contains("--compare-xbrz")},
        {"cunny_products", args.contains("--compare-cunny")},
        {"cunny_treatment_products", args.contains("--compare-cunny")},
        {"background", backgroundPath},
        {"format", "native-endian float32 RGBA, top source row first"},
        {"renderer", QString(reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER)))}};
    QFile file(output.filePath("manifest.json")); Require(file.open(QIODevice::WriteOnly), "manifest");
    file.write(QJsonDocument(manifest).toJson());
    gl->glDeleteFramebuffers(1, &fb); gl->glDeleteVertexArrays(1, &vao);
    std::printf("Production-shader comparison products saved.\n");
    return 0;
}
