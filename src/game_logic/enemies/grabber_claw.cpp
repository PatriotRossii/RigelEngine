/* Copyright (C) 2019, Nikolai Wuttke. All rights reserved.
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

#include "grabber_claw.hpp"

#include "base/match.hpp"
#include "engine/sprite_tools.hpp"
#include "game_logic/behavior_controller.hpp"
#include "game_logic/damage_components.hpp"
#include "game_logic/global_dependencies.hpp"


namespace rigel::game_logic::behaviors {

void GrabberClaw::update(
  GlobalDependencies& d,
  GlobalState& s,
  bool isOnScreen,
  entityx::Entity entity
) {
  using game_logic::components::Shootable;

  auto& position = *entity.component<engine::components::WorldPosition>();
  auto& animationFrame =
    entity.component<engine::components::Sprite>()->mFramesToRender[0];

  base::match(mState,
    [&, this](const Extending&) {
      if (mExtensionStep == 0) {
        entity.component<Shootable>()->mInvincible = false;
      }

      ++position.y;
      ++mExtensionStep;
      if (mExtensionStep == 5) {
        mState = Grabbing{};
      }
    },

    [&, this](Grabbing& state) {
      if (mExtensionStep == 5) {
        ++position.y;
        mExtensionStep = 6;
        entity.assign<game_logic::components::PlayerDamaging>(1);
        engine::startAnimationLoop(entity, 1, 1, 3);
      }

      ++state.mFramesElapsed;
      if (state.mFramesElapsed == 19) {
        mState = Retracting{};
      }
    },

    [&, this](const Retracting&) {
      if (mExtensionStep == 6) {
        entity.remove<game_logic::components::PlayerDamaging>();
        entity.remove<engine::components::AnimationLoop>();
        animationFrame = 1;
      }

      --position.y;
      --mExtensionStep;
      if (mExtensionStep == 0) {
        mState = Waiting{};
      }
    },

    [&, this](Waiting& state) {
      if (state.mFramesElapsed == 0) {
        entity.component<Shootable>()->mInvincible = true;
      }

      ++state.mFramesElapsed;
      if (state.mFramesElapsed == 10) {
        mState = Extending{};
      }
    });

  engine::synchronizeBoundingBoxToSprite(entity);
}


void GrabberClaw::render(
  entityx::Entity entity,
  const base::Vector& positionInScreenSpace,
  std::vector<engine::CustomDrawRequest>& output
) {
  const auto& state =
    entity.component<components::BehaviorController>()->get<GrabberClaw>();
  const auto currentFrame =
    entity.component<engine::components::Sprite>()->mFramesToRender[0];

  // Mounting pole
  for (int i = 0; i < state.mExtensionStep + 1; ++i) {
    output.push_back({0, positionInScreenSpace - base::Vector{0, i + 1}});
  }

  // Claw
  output.push_back({currentFrame, positionInScreenSpace});
}

}
