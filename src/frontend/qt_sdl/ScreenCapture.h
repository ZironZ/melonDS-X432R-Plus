// Copyright 2026 ZironZ
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <QImage>
#include <QString>
#include "glad/glad.h"

// The caller supplies the presentation shader, quad VAO, and its current
// source/history textures and effect uniforms. No history is advanced here.
// Images use internal resolution, without window layout or OSD.
bool ReadScreenPresentation(GLuint program, GLuint vertexArray, GLuint source,
                            std::array<QImage, 2>& images, QString* error);
