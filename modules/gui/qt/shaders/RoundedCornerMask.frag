#version 440

/*****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

layout(location = 0) in vec2 coord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float radius;
};

void main()
{
    // Only the corner quadrant (both components negative) can be outside the arc:
    float dist = length(min(coord, vec2(0.0))) - radius; // > 0 outside the arc

    // Anti-aliasing over one device pixel:
    float aa = max(fwidth(dist), 1e-4);
    float outside = clamp(dist / aa + 0.5, 0.0, 1.0);

    // Blending is done with the blend constant (premultiplied color) as source
    // factor and OneMinusSrcColor as destination factor, so with `k` being the
    // output this yields `dst = color * k + dst * (1 - k)` in all the channels,
    // including the alpha channel:
    fragColor = vec4(outside * qt_Opacity);
}
