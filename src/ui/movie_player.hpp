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

#pragma once

#include "base/warnings.hpp"
#include "data/movie.hpp"
#include "engine/texture.hpp"
#include "engine/timing.hpp"

RIGEL_DISABLE_WARNINGS
#include <boost/optional.hpp>
RIGEL_RESTORE_WARNINGS

#include <functional>
#include <vector>


namespace rigel { namespace ui {

class MoviePlayer {
public:
  /** The frame callback will be invoked each time a new animation frame is
   * shown. If it returns a number, it will be used to set the new frame delay
   * for all subsequent frames.
   */
  using FrameCallbackFunc = std::function<boost::optional<int>(int)>;

  explicit MoviePlayer(engine::Renderer* pRenderer);

  void playMovie(
    const data::Movie& movie,
    int frameDelayInFastTicks,
    const boost::optional<int>& repetitions = boost::none,
    FrameCallbackFunc frameCallback = nullptr);

  void setFrameDelay(int fastTicks);

  void updateAndRender(engine::TimeDelta timeDelta);
  bool hasCompletedPlayback() const;

private:
  struct FrameData {
    engine::OwningTexture mImage;
    int mStartRow;
  };

  void invokeFrameCallbackIfPresent(int whichFrame);

private:
  engine::Renderer* mpRenderer;
  engine::OwningTexture mBaseImage;
  std::vector<FrameData> mAnimationFrames;
  FrameCallbackFunc mFrameCallback = nullptr;

  bool mHasShownFirstFrame = false;
  int mCurrentFrame = 0;
  boost::optional<int> mRemainingRepetitions = 0;
  engine::TimeDelta mFrameDelay = 0.0;
  engine::TimeDelta mElapsedTime = 0.0;
};

}}
