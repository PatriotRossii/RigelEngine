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

#include "ega_image_decoder.hpp"

#include <data/game_traits.hpp>
#include <data/unit_conversions.hpp>
#include <loader/bitwise_iter.hpp>
#include <loader/file_utils.hpp>
#include <utils/container_tools.hpp>
#include <utils/math_tools.hpp>

#include <array>
#include <stdexcept>


namespace rigel { namespace loader {

using namespace std;
using data::GameTraits;
using data::PixelBuffer;
using data::tilesToPixels;

namespace {

using PalettizedPixelBuffer = std::vector<std::uint8_t>;
using PalettizedPixelBufferCIter = PalettizedPixelBuffer::const_iterator;


size_t inferHeight(
  const ByteBufferCIter begin,
  const ByteBufferCIter end,
  const size_t widthInTiles,
  const size_t bytesPerTile
) {
  const auto availableBytes = distance(begin, end);
  const auto numTiles = static_cast<size_t>(availableBytes / bytesPerTile);
  return utils::integerDivCeil(numTiles, widthInTiles);
}


/** Decode EGA mask plane
 *
 * Pre-conditions:
 *   source and target can be advanced pixelCount times.
 */
template<typename SourceIter, typename TargetIter>
void readEgaMaskPlane(
  SourceIter& source,
  TargetIter target,
  const size_t pixelCount
) {
  for (size_t i = 0; i < pixelCount; ++i) {
    *target++ = *source++ != 0;
  }
}

/** Decode EGA color data (4 planes)
 *
 * Pre-conditions:
 *   target points to zero-initialized buffer of bytes.
 *   target can be advanced pixelCount times.
 *   source can be advanced pixelCount*4 times.
 */
template<typename SourceIter, typename TargetIter>
void readEgaColorData(
  SourceIter& source,
  TargetIter target,
  const size_t pixelCount
) {
  for (auto plane = 0u; plane < GameTraits::egaPlanes; ++plane) {
    auto targetWritePassIter = target;

    for (auto pixel = 0u; pixel < pixelCount; ++pixel) {
      const auto planeBit = *source++;
      *targetWritePassIter++ |= planeBit << plane;
    }
  }
}


/** Decode EGA monochromatic data (1 plane)
 *
 * Pre-conditions:
 *   target points to buffer of Pixels.
 *   target can be advanced pixelCount times.
 *   source can be advanced pixelCount times.
 */
template<typename SourceIter, typename TargetIter>
void readEgaMonochromeData(
  SourceIter& source,
  TargetIter target,
  const size_t pixelCount
) {
  for (size_t i = 0; i < pixelCount; ++i) {
    const auto pixelPresent = *source++ != 0;
    *target++ = pixelPresent ?
      data::Pixel{255, 255, 255, 255} :
      data::Pixel{0, 0, 0, 255};
  }
}


/** Apply mask to decoded pixels
 *
 * Pre-conditions:
 *   pixels points to buffer of Pixels.
 *   maskValues points to buffer of bools.
 *   pixels can be advanced pixelCount times.
 *   maskValues can be advanced pixelCount times.
 */
template<typename MaskIter, typename PixelBufferIter>
void applyEgaMask(
  MaskIter maskValues,
  PixelBufferIter pixels,
  const size_t pixelCount
) {
  for (size_t i = 0; i < pixelCount; ++i, ++pixels) {
    const auto maskActive = *maskValues++;
    if (maskActive) {
      pixels->a = 0;
    }
  }
}


template<typename Callable>
data::PixelBuffer decodeTiledEgaData(
  const ByteBufferCIter begin,
  const ByteBufferCIter end,
  const std::size_t widthInTiles,
  const std::size_t heightInTiles,
  Callable decodeRow
) {
  const auto targetBufferStride = tilesToPixels(widthInTiles);
  PixelBuffer pixels(
    widthInTiles * heightInTiles * GameTraits::tileSizeSquared);

  BitWiseIterator<ByteBufferCIter> bitsIter(begin, end);
  for (auto row=0u; row<heightInTiles; ++row) {
    for (auto col=0u; col<widthInTiles; ++col) {
      for (size_t rowInTile=0u; rowInTile<GameTraits::tileSize; ++rowInTile) {
        const auto insertStart = tilesToPixels(col) +
          (tilesToPixels(row) + rowInTile)*targetBufferStride;
        const auto targetPixelIter = pixels.begin() + insertStart;

        decodeRow(bitsIter, targetPixelIter);
      }
    }
  }

  return pixels;
}

}


data::PixelBuffer decodeSimplePlanarEgaBuffer(
  const ByteBufferCIter begin,
  const ByteBufferCIter end,
  const Palette16& palette
) {
  const auto numBytes = distance(begin, end);
  assert(numBytes > 0);
  const auto numPixels =
    static_cast<size_t>(numBytes / GameTraits::egaPlanes) *
    GameTraits::pixelsPerEgaByte;

  BitWiseIterator<ByteBufferCIter> bitsIter(begin, end);
  vector<uint8_t> indexedPixels(numPixels, 0);
  readEgaColorData(bitsIter, indexedPixels.begin(), numPixels);

  return utils::transformed(indexedPixels,
    [&palette](const auto colorIndex) {
      return palette[colorIndex];
    });
}


data::Image loadTiledImage(
  const ByteBufferCIter begin,
  const ByteBufferCIter end,
  std::size_t widthInTiles,
  const Palette16& palette,
  const bool isMasked
) {
  const auto heightInTiles =
    inferHeight(begin, end, widthInTiles, GameTraits::bytesPerTile(isMasked));

  auto pixels = decodeTiledEgaData(begin, end, widthInTiles, heightInTiles,
    [&palette, isMasked](auto& sourceBitsIter, const auto targetPixelIter) {
      array<bool, GameTraits::tileSize> pixelMask;
      if (isMasked) {
        readEgaMaskPlane(
          sourceBitsIter, pixelMask.begin(), GameTraits::tileSize);
      }

      array<uint8_t, GameTraits::tileSize> indexedPixels;
      indexedPixels.fill(0);
      readEgaColorData(
        sourceBitsIter, indexedPixels.begin(), GameTraits::tileSize);

      for (auto i=0; i<GameTraits::tileSize; ++i) {
       *(targetPixelIter + i) = palette[indexedPixels[i]];
      }

      if (isMasked) {
        applyEgaMask(pixelMask.begin(), targetPixelIter, GameTraits::tileSize);
      }
    });

  return data::Image(
    std::move(pixels),
    tilesToPixels(widthInTiles),
    tilesToPixels(heightInTiles));
}


data::Image loadTiledFontBitmap(
  const ByteBufferCIter begin,
  const ByteBufferCIter end,
  const std::size_t widthInTiles
) {
  const auto heightInTiles =
    inferHeight(begin, end, widthInTiles, GameTraits::bytesPerFontTile());

  auto pixels = decodeTiledEgaData(begin, end, widthInTiles, heightInTiles,
    [](auto& sourceBitsIter, const auto targetPixelIter) {
      array<bool, GameTraits::tileSize> pixelMask;
      readEgaMaskPlane(sourceBitsIter, pixelMask.begin(), GameTraits::tileSize);

      readEgaMonochromeData(
        sourceBitsIter, targetPixelIter, GameTraits::tileSize);
      applyEgaMask(pixelMask.begin(), targetPixelIter, GameTraits::tileSize);
    });

  return data::Image(
    std::move(pixels),
    tilesToPixels(widthInTiles),
    tilesToPixels(heightInTiles));
}

}}
