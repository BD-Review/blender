/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#pragma once

#include <iostream>

#include "BLI_math_color.h"

namespace blender {

/**
 * CPP based color structures.
 *
 * Strongly typed color storage structures with space and alpha association.
 * Will increase readability and visibility of typically mistakes when
 * working with colors.
 *
 * The storage structs can hold 4 channels (r, g, b and a).
 *
 * Usage:
 *
 * Convert an srgb byte color to a linearrgb premultiplied.
 * ```
 * ColorSrgb4b srgb_color;
 * ColorSceneLinear4f<eAlpha::Premultiplied> linearrgb_color =
 *     BLI_color_convert_to_linear(srgb_color).to_premultiplied_alpha();
 * ```
 *
 * The API is structured to make most use of inlining. Most notably is that space
 * conversions must be done via `BLI_color_convert_to*` functions.
 *
 * - Conversions between spaces (srgb <=> scene linear) should always be done by
 *   invoking the `BLI_color_convert_to*` methods.
 * - Encoding colors (compressing to store colors inside a less precision storage)
 *   should be done by invoking the `encode` and `decode` methods.
 * - Changing alpha association should be done by invoking `to_multiplied_alpha` or
 *   `to_straight_alpha` methods.
 *
 * # Encoding.
 *
 * Color encoding is used to store colors with less precision using uint8_t in
 * stead of floats. This encoding is supported for the `eSpace::SceneLinear`.
 * To make this clear to the developer the a `eSpace::SceneLinearByteEncoded`
 * space is added.
 *
 * # sRGB precision
 *
 * The sRGB colors can be stored using `uint8_t` or `float` colors. The conversion
 * between the two precisions are available as methods. (`to_srgb4b` and
 * `to_srgb4f`).
 *
 * # Alpha conversion
 *
 * Alpha conversion is only supported in SceneLinear space.
 *
 * Extending this file:
 * - This file can be extended with `ColorHex/Hsl/Hsv` for different representations
 *   of rgb based colors.
 * - Add ColorXyz.
 */

/* Enumeration containing the different alpha modes. */
enum class eAlpha {
  /* Color and alpha are unassociated. */
  Straight,
  /* Color and alpha are associated. */
  Premultiplied,
};
std::ostream &operator<<(std::ostream &stream, const eAlpha &space);

/* Enumeration containing internal spaces. */
enum class eSpace {
  /* sRGB color space. */
  Srgb,
  /* Blender internal scene linear color space (maps to SceneReference role in OCIO). */
  SceneLinear,
  /* Blender internal scene linear color space compressed to be stored in 4 uint8_t. */
  SceneLinearByteEncoded,
};
std::ostream &operator<<(std::ostream &stream, const eSpace &space);

/* Template class to store RGBA values with different precision, space and alpha association. */
template<typename ChannelStorageType, eSpace Space, eAlpha Alpha> class ColorRGBA {
 public:
  ChannelStorageType r, g, b, a;
  constexpr ColorRGBA() = default;

  constexpr ColorRGBA(const ChannelStorageType rgba[4])
      : r(rgba[0]), g(rgba[1]), b(rgba[2]), a(rgba[3])
  {
  }

  constexpr ColorRGBA(const ChannelStorageType r,
                      const ChannelStorageType g,
                      const ChannelStorageType b,
                      const ChannelStorageType a)
      : r(r), g(g), b(b), a(a)
  {
  }

  operator ChannelStorageType *()
  {
    return &r;
  }

  operator const ChannelStorageType *() const
  {
    return &r;
  }

  friend std::ostream &operator<<(std::ostream &stream,
                                  const ColorRGBA<ChannelStorageType, Space, Alpha> &c)
  {

    stream << Space << Alpha << "(" << c.r << ", " << c.g << ", " << c.b << ", " << c.a << ")";
    return stream;
  }

  friend bool operator==(const ColorRGBA<ChannelStorageType, Space, Alpha> &a,
                         const ColorRGBA<ChannelStorageType, Space, Alpha> &b)
  {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
  }

  friend bool operator!=(const ColorRGBA<ChannelStorageType, Space, Alpha> &a,
                         const ColorRGBA<ChannelStorageType, Space, Alpha> &b)
  {
    return !(a == b);
  }

  uint64_t hash() const
  {
    uint64_t x1 = *reinterpret_cast<const uint32_t *>(&r);
    uint64_t x2 = *reinterpret_cast<const uint32_t *>(&g);
    uint64_t x3 = *reinterpret_cast<const uint32_t *>(&b);
    uint64_t x4 = *reinterpret_cast<const uint32_t *>(&a);
    return (x1 * 1283591) ^ (x2 * 850177) ^ (x3 * 735391) ^ (x4 * 442319);
  }
};

/* Forward declarations of concrete color classes. */
template<eAlpha Alpha> class ColorSceneLinear4f;
template<eAlpha Alpha> class ColorSceneLinearByteEncoded4b;
template<typename ChannelStorageType> class ColorSrgb4;

/* Forward declation of precision conversion methods. */
BLI_INLINE ColorSrgb4<float> BLI_color_convert_to_srgb4f(const ColorSrgb4<uint8_t> &srgb4b);
BLI_INLINE ColorSrgb4<uint8_t> BLI_color_convert_to_srgb4b(const ColorSrgb4<float> &srgb4f);

template<eAlpha Alpha>
class ColorSceneLinear4f final : public ColorRGBA<float, eSpace::SceneLinear, Alpha> {
 public:
  constexpr ColorSceneLinear4f<Alpha>() : ColorRGBA<float, eSpace::SceneLinear, Alpha>()
  {
  }

  constexpr ColorSceneLinear4f<Alpha>(const float *rgba)
      : ColorRGBA<float, eSpace::SceneLinear, Alpha>(rgba)
  {
  }

  constexpr ColorSceneLinear4f<Alpha>(float r, float g, float b, float a)
      : ColorRGBA<float, eSpace::SceneLinear, Alpha>(r, g, b, a)
  {
  }

  /**
   * Convert to its byte encoded counter space.
   **/
  ColorSceneLinearByteEncoded4b<Alpha> to_byte_encoded() const
  {
    ColorSceneLinearByteEncoded4b<Alpha> encoded;
    linearrgb_to_srgb_uchar4(encoded, *this);
    return encoded;
  }

  /**
   * Convert color and alpha association to premultiplied alpha.
   *
   * Will assert when called on a color premultiplied with alpha.
   */
  ColorSceneLinear4f<eAlpha::Premultiplied> to_premultiplied_alpha() const
  {
    BLI_assert(Alpha == eAlpha::Straight);
    ColorSceneLinear4f<eAlpha::Premultiplied> premultiplied;
    straight_to_premul_v4_v4(premultiplied, *this);
    return premultiplied;
  }

  /**
   * Convert color and alpha association to straight alpha.
   *
   * Will assert when called on a color with straight alpha..
   */
  ColorSceneLinear4f<eAlpha::Straight> to_straight_alpha() const
  {
    BLI_assert(Alpha == eAlpha::Premultiplied);
    ColorSceneLinear4f<eAlpha::Straight> straighten;
    premul_to_straight_v4_v4(straighten, *this);
    return straighten;
  }
};

template<eAlpha Alpha>
class ColorSceneLinearByteEncoded4b final
    : public ColorRGBA<uint8_t, eSpace::SceneLinearByteEncoded, Alpha> {
 public:
  constexpr ColorSceneLinearByteEncoded4b() = default;

  constexpr ColorSceneLinearByteEncoded4b(const float *rgba)
      : ColorRGBA<uint8_t, eSpace::SceneLinearByteEncoded, Alpha>(rgba)
  {
  }

  constexpr ColorSceneLinearByteEncoded4b(float r, float g, float b, float a)
      : ColorRGBA<uint8_t, eSpace::SceneLinearByteEncoded, Alpha>(r, g, b, a)
  {
  }

  /**
   * Convert to back to float color.
   **/
  ColorSceneLinear4f<Alpha> to_byte_decoded() const
  {
    ColorSceneLinear4f<Alpha> decoded;
    srgb_to_linearrgb_uchar4(decoded, *this);
    return decoded;
  }
};

/**
 * Srgb color template class. Should not be used directly. When needed please use
 * the convenience `ColorSrgb4b` and `ColorSrgb4f` declarations.
 */
template<typename ChannelStorageType>
class ColorSrgb4 final : public ColorRGBA<ChannelStorageType, eSpace::Srgb, eAlpha::Straight> {
 public:
  constexpr ColorSrgb4() : ColorRGBA<ChannelStorageType, eSpace::Srgb, eAlpha::Straight>()
  {
  }

  constexpr ColorSrgb4(const ChannelStorageType *rgba)
      : ColorRGBA<ChannelStorageType, eSpace::Srgb, eAlpha::Straight>(rgba)
  {
  }

  constexpr ColorSrgb4(ChannelStorageType r,
                       ChannelStorageType g,
                       ChannelStorageType b,
                       ChannelStorageType a)
      : ColorRGBA<ChannelStorageType, eSpace::Srgb, eAlpha::Straight>(r, g, b, a)
  {
  }

  /**
   * Change precision of color to float.
   *
   * Will fail to compile when invoked on a float color.
   */
  ColorSrgb4<float> to_srgb4f() const
  {
    BLI_assert(typeof(r) == uint8_t);
    return BLI_color_convert_to_srgb4f(*this);
  }

  /**
   * Change precision of color to uint8_t.
   *
   * Will fail to compile when invoked on a uint8_t color.
   */
  ColorSrgb4<uint8_t> to_srgb4b() const
  {
    BLI_assert(typeof(r) == float);
    return BLI_color_convert_to_srgb4b(*this);
  }
};

using ColorSrgb4f = ColorSrgb4<float>;
using ColorSrgb4b = ColorSrgb4<uint8_t>;

BLI_INLINE ColorSrgb4b BLI_color_convert_to_srgb4b(const ColorSrgb4f &srgb4f)
{
  ColorSrgb4b srgb4b;
  rgba_float_to_uchar(srgb4b, srgb4f);
  return srgb4b;
}

BLI_INLINE ColorSrgb4f BLI_color_convert_to_srgb4f(const ColorSrgb4b &srgb4b)
{
  ColorSrgb4f srgb4f;
  rgba_uchar_to_float(srgb4f, srgb4b);
  return srgb4f;
}

BLI_INLINE ColorSceneLinear4f<eAlpha::Straight> BLI_color_convert_to_scene_linear(
    const ColorSrgb4f &srgb4f)
{
  ColorSceneLinear4f<eAlpha::Straight> scene_linear;
  srgb_to_linearrgb_v4(scene_linear, srgb4f);
  return scene_linear;
}

BLI_INLINE ColorSceneLinear4f<eAlpha::Straight> BLI_color_convert_to_scene_linear(
    const ColorSrgb4b &srgb4b)
{
  ColorSceneLinear4f<eAlpha::Straight> scene_linear;
  srgb_to_linearrgb_uchar4(scene_linear, srgb4b);
  return scene_linear;
}

BLI_INLINE ColorSrgb4f
BLI_color_convert_to_srgb4f(const ColorSceneLinear4f<eAlpha::Straight> &scene_linear)
{
  ColorSrgb4f srgb4f;
  linearrgb_to_srgb_v4(srgb4f, scene_linear);
  return srgb4f;
}

BLI_INLINE ColorSrgb4b
BLI_color_convert_to_srgb4b(const ColorSceneLinear4f<eAlpha::Straight> &scene_linear)
{
  ColorSrgb4b srgb4b;
  linearrgb_to_srgb_uchar4(srgb4b, scene_linear);
  return srgb4b;
}

/* Internal roles. For convenience to shorten the type names and hide complexity
 * in areas where transformations are unlikely to happen. */
using ColorSceneReference4f = ColorSceneLinear4f<eAlpha::Premultiplied>;
using ColorSceneReference4b = ColorSceneLinearByteEncoded4b<eAlpha::Premultiplied>;
using ColorTheme4b = ColorSrgb4b;
using ColorGeometry4f = ColorSceneReference4f;
using ColorGeometry4b = ColorSceneLinearByteEncoded4b<eAlpha::Premultiplied>;

}  // namespace blender
