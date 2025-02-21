/* Copyright (C) 2017, Nikolai Wuttke. All rights reserved.
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

#pragma once

#include "base/color.hpp"
#include "base/spatial_types.hpp"

#include <vector>

namespace rigel::renderer
{
class Renderer;
}


namespace rigel::engine
{

class RandomNumberGenerator;

struct ParticleGroup;


class ParticleSystem
{
public:
  ParticleSystem(
    RandomNumberGenerator* pRandomGenerator,
    renderer::Renderer* pRenderer);
  ~ParticleSystem();

  void synchronizeTo(const ParticleSystem& other);

  void spawnParticles(
    const base::Vector& origin,
    const base::Color& color,
    int velocityScaleX = 0);

  void update();
  void render(const base::Vector& cameraPosition, float interpolation);

private:
  std::vector<ParticleGroup> mParticleGroups;
  RandomNumberGenerator* mpRandomGenerator;
  renderer::Renderer* mpRenderer;
};

} // namespace rigel::engine
