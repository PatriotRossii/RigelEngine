/* Copyright (C) 2016, Nikolai Wuttke. All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "menu_element_renderer.hpp"

#include "base/warnings.hpp"
#include "data/game_traits.hpp"
#include "data/unit_conversions.hpp"
#include "engine/timing.hpp"
#include "loader/resource_loader.hpp"

RIGEL_DISABLE_WARNINGS
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
RIGEL_RESTORE_WARNINGS

#include <cassert>
#include <cmath>
#include <stdexcept>


/* FONT FINDINGS
 *
 * Font actor: 29 (2-planar, 1st mask, 2nd grayscale)
 *
 * |   0 | A |
 * | ...     |
 * |  25 | Z |
 * |  26 | 0 |
 * |  27 | 1 |
 * |  ...    |
 * |  35 | 9 |
 * |  36 | ? |
 * |  37 | , |
 * |  38 | . |
 * |  39 | ! |
 * |  40 | <big block> |
 * |  41 | a |
 * |  42 | b |
 * |  ...    |
 * |  66 | z |
 *
 * STATUS.MNI-based fonts:
 *
 * Multiple fonts here:
 *   1. Orange, small, nearly complete ASCII. 1 char == 1 tile. Used in Menus
 *        col  0, row 21: ASCII chars  22 -  61
 *        col  0, row 22: ASCII chars  62 - 90, 97-107
 *        col 17, row 23: ASCII chars 108 - 122
 *
 *   2. Big, numbers (green) and letters (white). 1 char == 4 tiles. Used for
 *      bonus screen
 *        col 0, row 0: ASCII chars 48-57, 65-74
 *        col 0, row 2: ASCII chars 75-90,37,61,46,33
 *
 *   3. Small, bold, white, letters and some punctuation. 1 char == 1 tile.
 *      Used for in-game messages
 *        col 20, row  6: ASCII chars 48-84
 *        col 17, row 24: ASCII chars 85-90,44,46,33,63
 *
 *   4. Blue, gray background. Numbers only (see hud_renderer.cpp). Used for
 *      score and level number display.
 *
 *
 ******************************************************************************
 *
 * Other STATUS.MNI stuff:
 *
 * 1. Rotating arrow for menu selection
 *     8 images, 2x2 tiles. Starts at col 0, row 9
 *
 * 2. Toggle box for menu (options)
 *
 *     2 images, 2x2 tiles.
 *       - col 20, row 7: Unchecked
 *       - col 22, row 7: Checked
 *
 * 3. Message box borders. Each 1 tile big, all in row 4
 *     | col | function     |
 *     |   0 | top-left     |
 *     |   1 | top          |
 *     |   2 | top-right    |
 *     |   3 | right        |
 *     |   4 | bottom-right |
 *     |   5 | bottom       |
 *     |   6 | bottom-left  |
 *     |   7 | left         |
 *
 * 4. Blinking cursor, for save name/hi-score entry
 *
 *     4 images, 1x1 tile. Starts at col 9, row 4
 *
 *
 *
 ******************************************************************************
 *
 * MsgBox slide in animation:
 *
 *
 * void enterMsgBox(int yPos, int width, int height) {
 *   auto xPos = (40 - width) / 2;
 *   auto centeredY = yPos + height/2;
 *
 *   int animatedWidth = 1;
 *   for (int i=19; i > xPos; --i) {
 *     animatedWidth += 2;
 *     // x, y, width, height
 *     drawMsgBox(i, centeredY, animatedWidth, 2);
 *     // Wait one 140 Hz tick
 *   }
 *
 *   int targetPosY = yPos + (height % 2 == 0 ? 1 : 0);
 *   int animatedHeight = 0;
 *   for (int i=centeredY; targetPosY < i; --i) {
 *     animatedHeight += 2;
 *     drawMsgBox(xPos, i, width, animatedHeight);
 *     // Wait one 140 Hz tick
 *   }
 * }
 *
 */



namespace rigel { namespace ui {

namespace {

const auto NUM_MENU_INDICATOR_STATES = 8;
const auto MENU_INDICATOR_STATE_FOR_CLEARING = NUM_MENU_INDICATOR_STATES + 1;


engine::OwningTexture createFontTexture(
  const loader::FontData& font,
  engine::Renderer* pRenderer
) {
  if (font.size() != 67u) {
    throw std::runtime_error("Wrong number of bitmaps in menu font");
  }

  const auto characterWidth = int(font.front().width());
  data::Image combinedBitmaps(
    characterWidth * font.size(),
    font.front().height());

  int insertPosX = 0;
  for (const auto& characterBitmap : font) {
    combinedBitmaps.insertImage(insertPosX, 0, characterBitmap);
    insertPosX += characterWidth;
  }

  return engine::OwningTexture{pRenderer, combinedBitmaps};
}

}


MenuElementRenderer::MenuElementRenderer(
  engine::TileRenderer* pSpriteSheetRenderer,
  engine::Renderer* pRenderer,
  const loader::ResourceLoader& resources,
  const loader::Palette16& palette
)
  : mpSpriteSheetRenderer(pSpriteSheetRenderer)
  , mBigTextRenderer(
      createFontTexture(resources.mActorImagePackage.loadFont(), pRenderer),
      pRenderer)
  , mpRenderer(pRenderer)
  , mPalette(palette)
{
}


void MenuElementRenderer::drawText(
  int x,
  int y,
  const std::string& text
) const {
  for (auto i=0u; i<text.size(); ++i) {
    const auto ch = static_cast<uint8_t>(text[i]);

    int spriteSheetIndex = 0;
    if (ch < 62) {
      spriteSheetIndex = 21*40 + (ch - 22);
    } else if (ch <= 90) {
      spriteSheetIndex = 22*40 + (ch - 62);
    } else if (ch >= 97 && ch < 108) {
      spriteSheetIndex = 22*40 + (ch - 68);
    } else if (ch >= 108 && ch <= 122) {
      spriteSheetIndex = 23*40 + 17 + (ch - 108);
    } else {
      // Skip non-renderable chars
      continue;
    }

    mpSpriteSheetRenderer->renderTile(spriteSheetIndex, x + i, y);
  }
}


void MenuElementRenderer::drawSmallWhiteText(
  int x,
  int y,
  const std::string& text
) const {
  for (auto i=0u; i<text.size(); ++i) {
    const auto ch = static_cast<uint8_t>(text[i]);

    int spriteSheetIndex = 0;
    if (ch == 44) {
      spriteSheetIndex = 24*40 + 17 + 6;
    } else if (ch == 46) {
      spriteSheetIndex = 24*40 + 17 + 7;
    } else if (ch == 33) {
      spriteSheetIndex = 24*40 + 17 + 8;
    } else if (ch == 63) {
      spriteSheetIndex = 24*40 + 17 + 9;
    } else if (ch >= 65 && ch <= 84) {
      spriteSheetIndex = 6*40 + 20 + (ch - 65);
    } else if (ch >= 85 && ch <= 90) {
      spriteSheetIndex = 24*40 + 17 + (ch - 85);
    } else {
      // Skip non-renderable chars
      continue;
    }

    mpSpriteSheetRenderer->renderTile(spriteSheetIndex, x + i, y);
  }
}


void MenuElementRenderer::drawMultiLineText(
  const int x,
  const int y,
  const std::string& text
) const {
  namespace ba = boost::algorithm;

  std::vector<std::string> lines;
  ba::split(lines, text, ba::is_any_of("\n"));
  for (int i = 0; i < int(lines.size()); ++i) {
    drawText(x, y + i, lines[i]);
  }
}


void MenuElementRenderer::drawBigText(
  int x,
  int y,
  int colorIndex,
  const std::string& text
) const {
  mpRenderer->setColorModulation(mPalette.at(colorIndex));

  for (auto i=0u; i<text.size(); ++i) {
    const auto ch = static_cast<uint8_t>(text[i]);

    int index = 0;
    if (ch >= 65 && ch <= 90) {
      index = ch - 65;
    } else if (ch >= 48 && ch <= 57) {
      index = ch - 48 + 26;
    } else if (ch >= 97 && ch <= 122) {
      index = ch - 97 + 41;
    } else if (ch == 63) {
      index = 36;
    } else if (ch == 44) {
      index = 37;
    } else if (ch == 46) {
      index = 38;
    } else if (ch == 33) {
      index = 39;
    } else {
      index = 40;
    }

    const auto position = static_cast<int>(i);
    mBigTextRenderer.renderTileSlice(index, {x + position, y-1});
  }

  mpRenderer->setColorModulation(base::Color{255, 255, 255, 255});
}


void MenuElementRenderer::drawMessageBox(
  int x,
  int y,
  int width,
  int height
) const {
  // Top border
  drawMessageBoxRow(x, y, width, 0, 1, 2);

  // Body with left and right borders
  for (int row = 1; row < height - 1; ++row) {
    drawMessageBoxRow(x, y + row, width, 7, 8, 3);
  }

  // Bottom border
  drawMessageBoxRow(x, y + height - 1, width, 6, 5, 4);
}


void MenuElementRenderer::drawCheckBox(
  const int x,
  const int y,
  const bool isChecked
) const {
  const auto offset = isChecked ? 2 : 0;
  const auto index = 7*mpSpriteSheetRenderer->tilesPerRow() + 20 + offset;

  mpSpriteSheetRenderer->renderTileQuad(index, base::Vector{x - 1, y - 1});
}


void MenuElementRenderer::drawBonusScreenText(
  const int x,
  const int y,
  const std::string& text
) const {
 //        col 0, row 0: ASCII chars 48-57, 65-74
 //        col 0, row 2: ASCII chars 75-90,37,61,46,33

  for (auto i=0u; i<text.size(); ++i) {
    const auto ch = static_cast<uint8_t>(text[i]);

    int spriteSheetIndex = 0;
    if (ch >= 48 && ch <= 57) {
      spriteSheetIndex = (ch - 48) * 2;
    } else if (ch >= 65 && ch <= 74) {
      spriteSheetIndex = 20 + (ch - 65) * 2;
    } else if (ch >= 75 && ch <= 90) {
      spriteSheetIndex =
        mpSpriteSheetRenderer->tilesPerRow() * 2 + (ch - 75) * 2;
    } else if (ch == 37) {
      spriteSheetIndex = 112;
    } else if (ch == 61) {
      spriteSheetIndex = 114;
    } else if (ch == 46) {
      spriteSheetIndex = 116;
    } else if (ch == 33) {
      spriteSheetIndex = 118;
    } else {
      // Skip non-renderable chars
      continue;
    }

    const auto index = static_cast<int>(i);
    mpSpriteSheetRenderer->renderTileQuad(
      spriteSheetIndex, base::Vector{x + index*2, y});
  }
}


void MenuElementRenderer::drawSelectionIndicator(
  const int y,
  const int state
) const {
  const auto index = 9*mpSpriteSheetRenderer->tilesPerRow() + state*2;
  mpSpriteSheetRenderer->renderTileQuad(index, base::Vector{8, y - 1});
}


void MenuElementRenderer::drawTextEntryCursor(
  const int x,
  const int y,
  const int state
) const {

}


void MenuElementRenderer::drawMessageBoxRow(
  const int x,
  const int y,
  const int width,
  const int leftIndex,
  const int middleIndex,
  const int rightIndex
) const {
  const auto baseIndex = 4*40;

  mpSpriteSheetRenderer->renderTile(baseIndex + leftIndex, x, y);

  const auto untilX = x + width - 1;
  for (int col = x + 1; col < untilX; ++col) {
    mpSpriteSheetRenderer->renderTile(baseIndex + middleIndex, col, y);
  }

  mpSpriteSheetRenderer->renderTile(baseIndex + rightIndex, x + width - 1, y);
}


void MenuElementRenderer::showMenuSelectionIndicator(int y) {
  mMenuSelectionIndicatorPosition = y;
  mPendingMenuIndicatorErase = false;
}


void MenuElementRenderer::hideMenuSelectionIndicator() {
  mPendingMenuIndicatorErase = true;
}


void MenuElementRenderer::updateAndRenderAnimatedElements(
  const engine::TimeDelta timeDelta
) {
  mElapsedTime += timeDelta;

  if (mMenuSelectionIndicatorPosition) {
    const auto yPos = *mMenuSelectionIndicatorPosition;

    if (!mPendingMenuIndicatorErase) {
      // This timing is approximate
      const auto rotations = engine::timeToFastTicks(mElapsedTime) / 15.0;
      const auto intRotations = static_cast<int>(std::round(rotations));

      drawSelectionIndicator(yPos, intRotations % NUM_MENU_INDICATOR_STATES);
    } else {
      drawSelectionIndicator(yPos, MENU_INDICATOR_STATE_FOR_CLEARING);
      mMenuSelectionIndicatorPosition = std::nullopt;
      mPendingMenuIndicatorErase = false;
    }
  }
}


}}
