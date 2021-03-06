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

#include "game_runner.hpp"

#include "data/game_traits.hpp"
#include "data/map.hpp"
#include "data/sound_ids.hpp"
#include "data/strings.hpp"
#include "engine/physical_components.hpp"
#include "game_logic/actor_tag.hpp"
#include "game_logic/ingame_systems.hpp"
#include "game_logic/interaction/teleporter.hpp"
#include "game_logic/trigger_components.hpp"
#include "loader/resource_loader.hpp"
#include "ui/utils.hpp"

#include <cassert>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>


namespace rigel {

using namespace engine;
using namespace game_logic;
using namespace std;

using data::PlayerModel;
using engine::components::WorldPosition;


namespace {

// Update game logic at 15 FPS. This is not exactly the speed at which the
// game runs on period-appropriate hardware, but it's very close, and it nicely
// fits into 60 FPS, giving us 4 render frames for 1 logic update.
//
// On a 486 with a fast graphics card, the game runs at roughly 15.5 FPS, with
// a slower (non-VLB) graphics card, it's roughly 14 FPS. On a fast 386 (40 MHz),
// it's roughly 13 FPS. With 15 FPS, the feel should therefore be very close to
// playing the game on a 486 at the default game speed setting.
constexpr auto GAME_LOGIC_UPDATE_DELAY = 1.0/15.0;

constexpr auto TEMPORARY_ITEM_EXPIRATION_TIME = 700;
constexpr auto ITEM_ABOUT_TO_EXPIRE_TIME = TEMPORARY_ITEM_EXPIRATION_TIME - 30;

char EPISODE_PREFIXES[] = {'L', 'M', 'N', 'O'};


std::string levelFileName(const int episode, const int level) {
  assert(episode >=0 && episode < 4);
  assert(level >=0 && level < 8);

  std::string fileName;
  fileName += EPISODE_PREFIXES[episode];
  fileName += std::to_string(level + 1);
  fileName += ".MNI";
  return fileName;
}


template<typename ValueT>
std::string vec2String(const base::Point<ValueT>& vec, const int width) {
  std::stringstream stream;
  stream
    << std::setw(width) << std::fixed << std::setprecision(2) << vec.x << ", "
    << std::setw(width) << std::fixed << std::setprecision(2) << vec.y;
  return stream.str();
}

}


GameRunner::GameRunner(
  data::PlayerModel* pPlayerModel,
  const int episode,
  const int levelNumber,
  const data::Difficulty difficulty,
  GameMode::Context context,
  boost::optional<base::Vector> playerPositionOverride,
  bool showWelcomeMessage
)
  : mpRenderer(context.mpRenderer)
  , mpServiceProvider(context.mpServiceProvider)
  , mEntities(mEventManager)
  , mEntityFactory(
      context.mpRenderer,
      &mEntities,
      &context.mpResources->mActorImagePackage,
      difficulty)
  , mpPlayerModel(pPlayerModel)
  , mPlayerModelAtLevelStart(*mpPlayerModel)
  , mLevelFinished(false)
  , mAccumulatedTime(0.0)
  , mShowDebugText(false)
  , mRadarDishCounter(mEntities, mEventManager)
  , mHudRenderer(
      mpPlayerModel,
      levelNumber + 1,
      mpRenderer,
      *context.mpResources)
  , mMessageDisplay(mpServiceProvider, context.mpUiRenderer)
  , mIngameViewPortRenderTarget(
      context.mpRenderer,
      data::GameTraits::inGameViewPortSize.width,
      data::GameTraits::inGameViewPortSize.height)
{
  mEventManager.subscribe<rigel::events::CheckPointActivated>(*this);
  mEventManager.subscribe<rigel::events::PlayerMessage>(*this);
  mEventManager.subscribe<rigel::events::ScreenFlash>(*this);
  mEventManager.subscribe<rigel::events::TutorialMessage>(*this);
  mEventManager.subscribe<rigel::game_logic::events::ShootableKilled>(*this);

  using namespace std::chrono;
  auto before = high_resolution_clock::now();

  loadLevel(episode, levelNumber, difficulty, *context.mpResources);

  if (playerPositionOverride) {
    *mPlayerEntity.component<WorldPosition>() = *playerPositionOverride;
  }

  mpSystems->centerViewOnPlayer();

  if (showWelcomeMessage) {
    mMessageDisplay.setMessage(data::Messages::WelcomeToDukeNukem2);
  }

  if (mEarthQuakeEffect) { // overrides welcome message
    showTutorialMessage(data::TutorialMessageId::EarthQuake);
  }

  if (mRadarDishCounter.radarDishesPresent()) { // overrides earth quake
    mMessageDisplay.setMessage(data::Messages::FindAllRadars);
  }

  auto after = high_resolution_clock::now();
  std::cout << "Level load time: " <<
    duration<double>(after - before).count() * 1000.0 << " ms\n";
}


GameRunner::~GameRunner() {
}


void GameRunner::handleEvent(const SDL_Event& event) {
  if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) {
    return;
  }

  const auto keyPressed = std::uint8_t{event.type == SDL_KEYDOWN};
  switch (event.key.keysym.sym) {
    // TODO: Refactor: This can be clearer and less repetitive.
    case SDLK_UP:
      mCombinedInputState.mMovingUp = mCombinedInputState.mMovingUp || keyPressed;
      mInputState.mMovingUp = keyPressed;
      break;
    case SDLK_DOWN:
      mCombinedInputState.mMovingDown = mCombinedInputState.mMovingDown || keyPressed;
      mInputState.mMovingDown = keyPressed;
      break;
    case SDLK_LEFT:
      mCombinedInputState.mMovingLeft = mCombinedInputState.mMovingLeft || keyPressed;
      mInputState.mMovingLeft = keyPressed;
      break;
    case SDLK_RIGHT:
      mCombinedInputState.mMovingRight = mCombinedInputState.mMovingRight || keyPressed;
      mInputState.mMovingRight = keyPressed;
      break;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
      mCombinedInputState.mJumping = mCombinedInputState.mJumping || keyPressed;
      mInputState.mJumping = keyPressed;
      break;
    case SDLK_LALT:
    case SDLK_RALT:
      mCombinedInputState.mShooting = mCombinedInputState.mShooting || keyPressed;
      mInputState.mShooting = keyPressed;

      // To make shooting feel responsive even when updating the attack system
      // only at game-logic rate, we notify the system about button presses
      // immediately. The system will queue up one requested shot for the
      // next logic update.
      //
      // Without this, fire button presses can get lost since firing is
      // only allowed if the button is released between two shots. If the
      // release happens between two logic updates, the system wouldn't see it,
      // therefore thinking you're still holding the button.
      mpSystems->buttonStateChanged(mInputState);
      break;
  }

  // Debug keys
  // ----------------------------------------------------------------------
  if (keyPressed) {
    return;
  }

  auto& debuggingSystem = mpSystems->debuggingSystem();
  switch (event.key.keysym.sym) {
    case SDLK_b:
      debuggingSystem.toggleBoundingBoxDisplay();
      break;

    case SDLK_c:
      debuggingSystem.toggleWorldCollisionDataDisplay();
      break;

    case SDLK_d:
      mShowDebugText = !mShowDebugText;
      break;

    case SDLK_g:
      debuggingSystem.toggleGridDisplay();
      break;

    case SDLK_s:
      mSingleStepping = !mSingleStepping;
      break;

    case SDLK_SPACE:
      if (mSingleStepping) {
        mDoNextSingleStep = true;
      }
      break;
  }
}


void GameRunner::updateAndRender(engine::TimeDelta dt) {
  if (mLevelFinished) {
    return;
  }

  int screenShakeOffsetX = 0;

  // **********************************************************************
  // Updating
  // **********************************************************************

  auto doTimeStep = [&, this]() {
    mBackdropFlashColor = boost::none;
    mScreenFlashColor = boost::none;

    if (mReactorDestructionFramesElapsed) {
      updateReactorDestructionEvent();
    }

    mHudRenderer.updateAnimation();
    updateTemporaryItemExpiration();
    mpSystems->update(mCombinedInputState, mEntities);
    mMessageDisplay.update();
    mCombinedInputState = mInputState;

    if (mEarthQuakeEffect) {
      screenShakeOffsetX = mEarthQuakeEffect->update();
    }
  };

  if (mSingleStepping) {
    if (mDoNextSingleStep) {
      doTimeStep();
      mDoNextSingleStep = false;
    }
  } else {
    mAccumulatedTime += dt;
    for (;
      mAccumulatedTime >= GAME_LOGIC_UPDATE_DELAY;
      mAccumulatedTime -= GAME_LOGIC_UPDATE_DELAY
    ) {
      doTimeStep();
    }
  }


  // **********************************************************************
  // Rendering
  // **********************************************************************
  {
    engine::RenderTargetTexture::Binder
      bindRenderTarget(mIngameViewPortRenderTarget, mpRenderer);
    if (!mScreenFlashColor) {
      mpSystems->render(mEntities, mBackdropFlashColor);
    } else {
      mpRenderer->clear(*mScreenFlashColor);
    }
    mHudRenderer.render();
  }

  // This clear is important, since we might shift the render target's position
  // on some frames due to the earth quake effect. If we don't clear, traces
  // of/part of an older frame might incorrectly remain on screen.
  mpRenderer->clear();

  mIngameViewPortRenderTarget.render(
    mpRenderer,
    data::GameTraits::inGameViewPortOffset.x + screenShakeOffsetX,
    data::GameTraits::inGameViewPortOffset.y);
  mMessageDisplay.render();

  if (mShowDebugText) {
    showDebugText();
  }

  handlePlayerDeath();
  handleLevelExit();
  handleTeleporter();
}


bool GameRunner::levelFinished() const {
  return mLevelFinished;
}


void GameRunner::receive(const rigel::events::CheckPointActivated& event) {
  mActivatedCheckpoint = CheckpointData{
    mpPlayerModel->makeCheckpoint(), event.mPosition};
  mMessageDisplay.setMessage(data::Messages::FoundRespawnBeacon);
}


void GameRunner::receive(const rigel::events::PlayerMessage& event) {
  mMessageDisplay.setMessage(event.mText);
}


void GameRunner::receive(const rigel::events::ScreenFlash& event) {
  mScreenFlashColor = event.mColor;
}


void GameRunner::receive(const rigel::events::TutorialMessage& event) {
  showTutorialMessage(event.mId);
}


void GameRunner::receive(const game_logic::events::ShootableKilled& event) {
  using game_logic::components::ActorTag;
  auto entity = event.mEntity;
  if (!entity.has_component<ActorTag>()) {
    return;
  }

  const auto& position = *entity.component<const WorldPosition>();

  using AT = ActorTag::Type;
  const auto type = entity.component<ActorTag>()->mType;
  switch (type) {
    case AT::Reactor:
      onReactorDestroyed(position);
      break;

    default:
      break;
  }
}


void GameRunner::loadLevel(
  const int episode,
  const int levelNumber,
  const data::Difficulty difficulty,
  const loader::ResourceLoader& resources
) {
  auto loadedLevel = loader::loadLevel(
    levelFileName(episode, levelNumber), resources, difficulty);
  mPlayerEntity = mEntityFactory.createEntitiesForLevel(loadedLevel.mActors);

  mLevelData = LevelData{
    std::move(loadedLevel.mMap),
    std::move(loadedLevel.mActors),
    loadedLevel.mBackdropSwitchCondition
  };
  mMapAtLevelStart = mLevelData.mMap;

  mpSystems = std::make_unique<IngameSystems>(
    difficulty,
    &mScrollOffset,
    mPlayerEntity,
    mpPlayerModel,
    &mLevelData.mMap,
    engine::MapRenderer::MapRenderData{std::move(loadedLevel)},
    mpServiceProvider,
    &mEntityFactory,
    &mRandomGenerator,
    &mRadarDishCounter,
    mpRenderer,
    mEntities,
    mEventManager);

  if (loadedLevel.mEarthquake) {
    mEarthQuakeEffect =
      engine::EarthQuakeEffect{mpServiceProvider, &mRandomGenerator};
  }

  mpServiceProvider->playMusic(loadedLevel.mMusicFile);
}


void GameRunner::onReactorDestroyed(const base::Vector& position) {
  mScreenFlashColor = loader::INGAME_PALETTE[7];
  mEntityFactory.createProjectile(
    ProjectileType::ReactorDebris,
    position + base::Vector{-1, 0},
    ProjectileDirection::Left);
  mEntityFactory.createProjectile(
    ProjectileType::ReactorDebris,
    position + base::Vector{3, 0},
    ProjectileDirection::Right);

  const auto shouldDoSpecialEvent =
    mLevelData.mBackdropSwitchCondition ==
      data::map::BackdropSwitchCondition::OnReactorDestruction;
  if (!mReactorDestructionFramesElapsed && shouldDoSpecialEvent) {
    mpSystems->switchBackdrops();
    mBackdropSwitched = true;
    mReactorDestructionFramesElapsed = 0;
  }
}


void GameRunner::updateReactorDestructionEvent() {
  auto& framesElapsed = *mReactorDestructionFramesElapsed;
  if (framesElapsed >= 14) {
    return;
  }

  if (framesElapsed == 13) {
    mMessageDisplay.setMessage(data::Messages::DestroyedEverything);
  } else if (framesElapsed % 2 == 1) {
    mBackdropFlashColor = base::Color{255, 255, 255, 255};
    mpServiceProvider->playSound(data::SoundId::BigExplosion);
  }

  ++framesElapsed;
}


void GameRunner::handleLevelExit() {
  using engine::components::Active;
  using engine::components::BoundingBox;
  using game_logic::components::Trigger;
  using game_logic::components::TriggerType;

  mEntities.each<Trigger, WorldPosition, Active>(
    [this](
      entityx::Entity,
      const Trigger& trigger,
      const WorldPosition& triggerPosition,
      const Active&
    ) {
      if (trigger.mType != TriggerType::LevelExit || mLevelFinished) {
        return;
      }

      const auto& playerPosition = *mPlayerEntity.component<WorldPosition>();
      const auto playerBBox = toWorldSpace(
        *mPlayerEntity.component<BoundingBox>(), playerPosition);

      const auto playerAboveOrAtTriggerHeight =
        playerBBox.bottom() <= triggerPosition.y;
      const auto touchingTriggerOnXAxis =
        triggerPosition.x >= playerBBox.left() &&
        triggerPosition.x <= (playerBBox.right() + 1);

      const auto triggerActivated =
        playerAboveOrAtTriggerHeight && touchingTriggerOnXAxis;

      if (triggerActivated) {
        if (mRadarDishCounter.radarDishesPresent()) {
          showTutorialMessage(data::TutorialMessageId::RadarsStillFunctional);
        } else {
          mLevelFinished = true;
        }
      }
    });
}


void GameRunner::handlePlayerDeath() {
  const auto& playerState =
    *mPlayerEntity.component<game_logic::components::PlayerControlled>();

  const auto playerDead =
    playerState.mState == player::PlayerState::Dead &&
    mpPlayerModel->isDead();
  if (playerDead) {
    if (mActivatedCheckpoint) {
      restartFromCheckpoint();
    } else {
      restartLevel();
    }
  }
}


void GameRunner::restartLevel() {
  mpServiceProvider->fadeOutScreen();

  if (mBackdropSwitched) {
    mpSystems->switchBackdrops();
    mBackdropSwitched = false;
  }

  mLevelData.mMap = mMapAtLevelStart;

  mEntities.reset();
  mPlayerEntity = mEntityFactory.createEntitiesForLevel(
    mLevelData.mInitialActors);

  *mpPlayerModel = mPlayerModelAtLevelStart;
  mFramesElapsedHavingRapidFire = mFramesElapsedHavingCloak = 0;

  mpSystems->centerViewOnPlayer();
  updateAndRender(0);

  mpServiceProvider->fadeInScreen();
}


void GameRunner::restartFromCheckpoint() {
  mpServiceProvider->fadeOutScreen();

  const auto shouldSwitchBackAfterRespawn =
    mLevelData.mBackdropSwitchCondition ==
      data::map::BackdropSwitchCondition::OnTeleportation;
  if (mBackdropSwitched && shouldSwitchBackAfterRespawn) {
    mpSystems->switchBackdrops();
    mBackdropSwitched = false;
  }

  mpPlayerModel->restoreFromCheckpoint(mActivatedCheckpoint->mState);
  resetForRespawn(mPlayerEntity, mActivatedCheckpoint->mPosition);

  mpSystems->centerViewOnPlayer();
  updateAndRender(0);

  mpServiceProvider->fadeInScreen();
}


void GameRunner::handleTeleporter() {
  auto activeTeleporter = mpSystems->getAndResetActiveTeleporter();
  if (!activeTeleporter) {
    return;
  }

  mpServiceProvider->playSound(data::SoundId::Teleport);
  mpServiceProvider->fadeOutScreen();

  game_logic::interaction::teleportPlayer(
    mEntities,
    mPlayerEntity,
    activeTeleporter);

  const auto switchBackdrop =
    mLevelData.mBackdropSwitchCondition ==
      data::map::BackdropSwitchCondition::OnTeleportation;
  if (switchBackdrop) {
    mpSystems->switchBackdrops();
    mBackdropSwitched = !mBackdropSwitched;
  }

  mpSystems->centerViewOnPlayer();
  updateAndRender(0.0);
  mpServiceProvider->fadeInScreen();
}


void GameRunner::updateTemporaryItemExpiration() {
  using data::InventoryItemType;

  if (mpPlayerModel->hasItem(InventoryItemType::RapidFire)) {
    ++mFramesElapsedHavingRapidFire;
    if (mFramesElapsedHavingRapidFire == ITEM_ABOUT_TO_EXPIRE_TIME) {
      mMessageDisplay.setMessage(data::Messages::RapidFireTimingOut);
    }

    if (mFramesElapsedHavingRapidFire >= TEMPORARY_ITEM_EXPIRATION_TIME) {
      mpPlayerModel->removeItem(InventoryItemType::RapidFire);
      mFramesElapsedHavingRapidFire = 0;
    }
  }
}


void GameRunner::showTutorialMessage(const data::TutorialMessageId id) {
  if (!mpPlayerModel->tutorialMessages().hasBeenShown(id)) {
    mMessageDisplay.setMessage(data::messageText(id));
    mpPlayerModel->tutorialMessages().markAsShown(id);
  }
}


void GameRunner::showDebugText() {
  using engine::components::MovingBody;

  const auto& playerPos = *mPlayerEntity.component<WorldPosition>();
  const auto& playerVel = mPlayerEntity.component<MovingBody>()->mVelocity;
  std::stringstream infoText;
  infoText
    << "Scroll: " << vec2String(mScrollOffset, 4) << '\n'
    << "Player: " << vec2String(playerPos, 4)
    << ", Vel.: " << vec2String(playerVel, 5) << '\n'
    << "Entities: " << mEntities.size();

  mpServiceProvider->showDebugText(infoText.str());
}

}
