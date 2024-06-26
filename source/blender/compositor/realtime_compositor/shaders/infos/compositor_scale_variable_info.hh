/* SPDX-FileCopyrightText: 2024 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "gpu_shader_create_info.hh"

GPU_SHADER_CREATE_INFO(compositor_scale_variable)
    .local_group_size(16, 16)
    .sampler(0, ImageType::FLOAT_2D, "input_tx")
    .sampler(1, ImageType::FLOAT_2D, "x_scale_tx")
    .sampler(2, ImageType::FLOAT_2D, "y_scale_tx")
    .image(0, GPU_RGBA16F, Qualifier::WRITE, ImageType::FLOAT_2D, "output_img")
    .compute_source("compositor_scale_variable.glsl")
    .do_static_compilation(true);
