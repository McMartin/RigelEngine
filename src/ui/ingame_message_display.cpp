/* Copyright (C) 2018, Nikolai Wuttke. All rights reserved.
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

#include "ingame_message_display.hpp"

#include "base/warnings.hpp"
#include "data/sound_ids.hpp"
#include "ui/menu_element_renderer.hpp"

#include "game_service_provider.hpp"

RIGEL_DISABLE_WARNINGS
#include <atria/variant/match_boost.hpp>
RIGEL_RESTORE_WARNINGS


namespace rigel { namespace ui {

namespace {

constexpr auto NEXT_LINE_MARKER = '*';
constexpr auto CHARS_PER_LINE = 37;

}

IngameMessageDisplay::IngameMessageDisplay(
  IGameServiceProvider* pServiceProvider,
  MenuElementRenderer* pTextRenderer
)
  : mpTextRenderer(pTextRenderer)
  , mpServiceProvider(pServiceProvider)
{
}


void IngameMessageDisplay::setMessage(std::string message) {
  if (!message.empty()) {
    mMessage = std::move(message);
    mPrintedMessage.clear();
    mState = Printing{};
  }
}


void IngameMessageDisplay::update() {
  atria::variant::match(mState,
    [](const Idle&) {},

    [this](Printing& state) {
      const auto nextChar = mMessage[state.effectiveOffset()];

      const auto foundNextLineMarker = nextChar == NEXT_LINE_MARKER;
      if (!foundNextLineMarker) {
        mPrintedMessage.push_back(nextChar);
        if (nextChar != ' ') {
          mpServiceProvider->playSound(data::SoundId::IngameMessageTyping);
        }
      }

      ++state.mCharsPrinted;

      const auto messageConsumed = state.effectiveOffset() >= mMessage.size();
      const auto endOfLine =
        state.mCharsPrinted == CHARS_PER_LINE ||
        foundNextLineMarker ||
        messageConsumed;

      if (endOfLine) {
        mState = Waiting{state.effectiveOffset()};
      }
    },

    [this](Waiting& state) {
      --state.mFramesElapsed;
      if (state.mFramesElapsed == 0) {
        mPrintedMessage.clear();

        if (state.mNextOffset < mMessage.size()) {
          mState = Printing{state.mNextOffset};
        } else {
          mState = Idle{};
        }
      }
    });
}


void IngameMessageDisplay::render() {
  if (!mPrintedMessage.empty()) {
    mpTextRenderer->drawSmallWhiteText(0, 0, mPrintedMessage);
  }
}

}}
