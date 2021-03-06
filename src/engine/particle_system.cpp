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

#include "particle_system.hpp"

#include "data/unit_conversions.hpp"
#include "engine/random_number_generator.hpp"
#include "engine/renderer.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <memory>


namespace rigel { namespace engine {

namespace {

const auto PARTICLE_SYSTEM_LIFE_TIME = 28;

const auto INITIAL_INDEX_LIMIT = 15;

const std::array<std::int16_t, 43> VERTICAL_MOVEMENT_TABLE{
  -8, -8, -8, -8, -4, -4, -4, -2, -1, 0, 0, 1, 2, 4, 4, 4,
  8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 24, 1
};

constexpr auto SPAWN_OFFSET = base::Vector{0, -1};


struct Particle {
  Particle() = default;

  void update() {
    static_assert(
      (PARTICLE_SYSTEM_LIFE_TIME - 1) + INITIAL_INDEX_LIMIT <
      VERTICAL_MOVEMENT_TABLE.size(),
      "");

    mOffsetY += VERTICAL_MOVEMENT_TABLE[mMovementIndexY++];
  }

  base::Vector offsetAtTime(const int framesElapsed) const {
    return {mVelocityX * framesElapsed, mOffsetY};
  }

  std::int16_t mVelocityX;
  std::int16_t mOffsetY = 0;
  std::int16_t mMovementIndexY;
};


using ParticlesList = std::array<Particle, 64>;


std::unique_ptr<ParticlesList> createParticles(
  RandomNumberGenerator& randomGenerator,
  const int velocityScaleX
) {
  auto pParticles = std::make_unique<ParticlesList>();
  for (auto& particle : *pParticles) {
    const auto randomVariation = randomGenerator.gen() % 20;
    particle.mVelocityX = static_cast<std::int16_t>(velocityScaleX == 0
      ? 10 - randomVariation
      : velocityScaleX * (randomVariation + 1));
    particle.mMovementIndexY =
      randomGenerator.gen() % (INITIAL_INDEX_LIMIT + 1);
  }
  return pParticles;
}

}


struct ParticleCloud {
  ParticleCloud(
    const base::Vector& origin,
    const base::Color& color,
    std::unique_ptr<ParticlesList> pParticles
  )
    : mpParticles(std::move(pParticles))
    , mOrigin(origin)
    , mColor(color)
  {
  }

  void update() {
    for (auto& particle : *mpParticles) {
      particle.update();
    }

    ++mFramesElapsed;
  }

  void render(Renderer& renderer, const base::Vector& scrollOffset) {
    const auto screenSpaceOrigin =
      data::tileVectorToPixelVector(mOrigin - scrollOffset);
    for (auto& particle : *mpParticles) {
      const auto particlePosition =
        screenSpaceOrigin + particle.offsetAtTime(mFramesElapsed);
      renderer.drawPoint(particlePosition, mColor);
    }
  }


  bool isExpired() const {
    return mFramesElapsed >= PARTICLE_SYSTEM_LIFE_TIME;
  }

  std::unique_ptr<ParticlesList> mpParticles;
  base::Vector mOrigin;
  base::Color mColor;
  int mFramesElapsed = 0;
};


ParticleSystem::ParticleSystem(
  RandomNumberGenerator* pRandomGenerator,
  Renderer* pRenderer
)
  : mpRandomGenerator(pRandomGenerator)
  , mpRenderer(pRenderer)
{
}


ParticleSystem::~ParticleSystem() {
}


void ParticleSystem::spawnParticles(
  const base::Vector& origin,
  const base::Color& color,
  int velocityScaleX
) {
  auto pParticles = createParticles(*mpRandomGenerator, velocityScaleX);
  mParticleClouds.emplace_back(
    origin + SPAWN_OFFSET, color, std::move(pParticles));
}


void ParticleSystem::update() {
  using namespace std;

  const auto it = remove_if(
    begin(mParticleClouds),
    end(mParticleClouds),
    mem_fn(&ParticleCloud::isExpired));
  mParticleClouds.erase(it, end(mParticleClouds));

  for (auto& system : mParticleClouds) {
    system.update();
  }
}


void ParticleSystem::render(const base::Vector& scrollOffset) {
  for (auto& system : mParticleClouds) {
    system.render(*mpRenderer, scrollOffset);
  }
}

}}
