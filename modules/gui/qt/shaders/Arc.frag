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

layout(std140, binding = 0) uniform buf {
  mat4 qt_Matrix;
  float qt_Opacity;

  vec2 size;

  vec4 insideColor; // Not relevant if discardInside is not 0.
  vec4 outsideColor; // Not relevant if discardOutside is not 0.

  int discardInside; // WARNING: Intentionally not boolean.
  int discardOutside; // WARNING: Intentionally not boolean.
};

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

void main()
{
  float radius = size.x;

  vec2 denormalCoord = qt_TexCoord0 * size;

  float dist = sqrt(pow(denormalCoord.x - radius, 2) + pow(denormalCoord.y - radius, 2));

  if (dist < radius)
  {
    // Inside the arc:
    if (discardInside != 0)
      discard;

    fragColor = insideColor * qt_Opacity;
  }
  else
  {
    // Outside the arc:
    if (discardOutside != 0)
      discard;

    fragColor = outsideColor * qt_Opacity;
  }
}
