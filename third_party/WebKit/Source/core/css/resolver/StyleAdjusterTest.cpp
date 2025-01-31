// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/dom/NodeComputedStyle.h"
#include "core/layout/LayoutTestHelper.h"
#include "platform/testing/runtime_enabled_features_test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

class StyleAdjusterTest : public RenderingTest {
 public:
  StyleAdjusterTest() : RenderingTest(SingleChildLocalFrameClient::Create()) {}
};

TEST_F(StyleAdjusterTest, TouchActionPropagatedAcrossIframes) {
  GetDocument().SetBaseURLOverride(KURL("http://test.com"));
  SetBodyInnerHTML(R"HTML(
    <style>body { margin: 0; } iframe { display: block; } </style>
    <iframe id='owner' src='http://test.com' width='500' height='500'
    style='touch-action: none'>
    </iframe>
  )HTML");
  SetChildFrameHTML(R"HTML(
    <style>body { margin: 0; } #target { width: 200px; height: 200px; }
    </style>
    <div id='target' style='touch-action: pinch-zoom'></div>
  )HTML");
  GetDocument().View()->UpdateAllLifecyclePhases();

  Element* target = ChildDocument().getElementById("target");
  EXPECT_EQ(TouchAction::kTouchActionNone,
            target->GetComputedStyle()->GetEffectiveTouchAction());

  Element* owner = GetDocument().getElementById("owner");
  owner->setAttribute(HTMLNames::styleAttr, "touch-action: auto");
  GetDocument().View()->UpdateAllLifecyclePhases();
  EXPECT_EQ(TouchAction::kTouchActionPinchZoom,
            target->GetComputedStyle()->GetEffectiveTouchAction());
}

TEST_F(StyleAdjusterTest, TouchActionPanningReEnabledByScrollers) {
  GetDocument().SetBaseURLOverride(KURL("http://test.com"));
  SetBodyInnerHTML(R"HTML(
    <style>#ancestor { margin: 0; touch-action: pinch-zoom; }
    #scroller { overflow: scroll; width: 100px; height: 100px; }
    #target { width: 200px; height: 200px; } </style>
    <div id='ancestor'><div id='scroller'><div id='target'>
    </div></div></div>
  )HTML");
  GetDocument().View()->UpdateAllLifecyclePhases();

  Element* target = GetDocument().getElementById("target");
  EXPECT_EQ(TouchAction::kTouchActionManipulation,
            target->GetComputedStyle()->GetEffectiveTouchAction());
}

TEST_F(StyleAdjusterTest, TouchActionPropagatedWhenAncestorStyleChanges) {
  GetDocument().SetBaseURLOverride(KURL("http://test.com"));
  SetBodyInnerHTML(R"HTML(
    <style>#ancestor { margin: 0; touch-action: pan-x; }
    #potential-scroller { width: 100px; height: 100px; overflow: hidden; }
    #target { width: 200px; height: 200px; }</style>
    <div id='ancestor'><div id='potential-scroller'><div id='target'>
    </div></div></div>
  )HTML");
  GetDocument().View()->UpdateAllLifecyclePhases();

  Element* target = GetDocument().getElementById("target");
  EXPECT_EQ(TouchAction::kTouchActionPanX,
            target->GetComputedStyle()->GetEffectiveTouchAction());

  Element* ancestor = GetDocument().getElementById("ancestor");
  ancestor->setAttribute(HTMLNames::styleAttr, "touch-action: pan-y");
  GetDocument().View()->UpdateAllLifecyclePhases();
  EXPECT_EQ(TouchAction::kTouchActionPanY,
            target->GetComputedStyle()->GetEffectiveTouchAction());

  Element* potential_scroller =
      GetDocument().getElementById("potential-scroller");
  potential_scroller->setAttribute(HTMLNames::styleAttr, "overflow: scroll");
  GetDocument().View()->UpdateAllLifecyclePhases();
  EXPECT_EQ(TouchAction::kTouchActionPan,
            target->GetComputedStyle()->GetEffectiveTouchAction());
}

TEST_F(StyleAdjusterTest, TouchActionRestrictedByLowerAncestor) {
  GetDocument().SetBaseURLOverride(KURL("http://test.com"));
  SetBodyInnerHTML(R"HTML(
    <div id='ancestor' style='touch-action: pan'>
    <div id='parent' style='touch-action: pan-right pan-y'>
    <div id='target' style='touch-action: pan-x'>
    </div></div></div>
  )HTML");
  GetDocument().View()->UpdateAllLifecyclePhases();

  Element* target = GetDocument().getElementById("target");
  EXPECT_EQ(TouchAction::kTouchActionPanRight,
            target->GetComputedStyle()->GetEffectiveTouchAction());

  Element* parent = GetDocument().getElementById("parent");
  parent->setAttribute(HTMLNames::styleAttr, "touch-action: auto");
  GetDocument().View()->UpdateAllLifecyclePhases();
  EXPECT_EQ(TouchAction::kTouchActionPanX,
            target->GetComputedStyle()->GetEffectiveTouchAction());
}
}  // namespace blink
