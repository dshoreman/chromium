// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/css/properties/longhands/flood_color.h"

#include "core/css/parser/CSSParserContext.h"
#include "core/css/parser/CSSPropertyParserHelpers.h"
#include "core/css/properties/ComputedStyleUtils.h"

namespace blink {
namespace CSSLonghand {

const CSSValue* FloodColor::ParseSingleValue(
    CSSParserTokenRange& range,
    const CSSParserContext& context,
    const CSSParserLocalContext&) const {
  return CSSPropertyParserHelpers::ConsumeColor(range, context.Mode());
}

const blink::Color FloodColor::ColorIncludingFallback(
    bool visited_link,
    const ComputedStyle& style) const {
  StyleColor result = style.FloodColor();
  if (!result.IsCurrentColor())
    return result.GetColor();
  return visited_link ? style.VisitedLinkColor() : style.GetColor();
}

const CSSValue* FloodColor::CSSValueFromComputedStyleInternal(
    const ComputedStyle& style,
    const SVGComputedStyle&,
    const LayoutObject*,
    Node*,
    bool allow_visited_style) const {
  return ComputedStyleUtils::CurrentColorOrValidColor(style,
                                                      style.FloodColor());
}

}  // namespace CSSLonghand
}  // namespace blink
