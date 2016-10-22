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

#include "duke_script_loader.hpp"

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/optional.hpp>

#include <cctype>
#include <unordered_set>
#include <sstream>
#include <stdexcept>


// TODO:
//
// HELPTEXT <EP> <Level> Text - define hint globe text for Episode/level
//                              combination. Numbers are 1-based
// ETE - seems unused? Maybe Shareware version only (appears only in
//       ORDERTXT.MNI)
//
// SETCURRENTPAGE - freezes animations/current displayed frame
// SETKEYS <raw byte list of scan-codes> -
//            Sets up hot-keys for menu actions in the main menu. In the
//            Quit_Select, it sets up the 'Y' and 'N' keys. Ignored for now,
//            we just hardcode those keys for Quit_Select.


namespace rigel { namespace loader {

namespace b = boost;
using namespace std;
using namespace data::script;


namespace {

void skipWhiteSpace(istream& sourceStream) {
  while (!sourceStream.eof() && std::isspace(sourceStream.peek())) {
    sourceStream.get();
  }
}


bool isCommand(const string& line) {
  return b::starts_with(line, "//");
}


void stripCommandPrefix(string& line) {
  b::trim_left_if(line, [](const auto c) { return c == '/'; });
}


template<typename Callable>
void parseScriptLines(
  istream& sourceTextStream,
  const string& endMarker,
  Callable consumeLine
) {
  skipWhiteSpace(sourceTextStream);
  for (string line; getline(sourceTextStream, line, '\n');) {
    b::trim(line);
    if (isCommand(line)) {
      b::trim_left_if(line, [](const auto c) { return c == '/'; });
      if (line == endMarker) {
        return;
      }

      istringstream lineTextStream(line);
      string command;
      lineTextStream >> command;
      b::trim(command);
      consumeLine(command, lineTextStream);
    }
  }

  throw invalid_argument(
    string("Missing end marker '" + endMarker + "' in Duke Script file"));
}


vector<string> parseMessageBoxTextDefinition(istream& sourceStream) {
  vector<string> messageLines;

  // There is unfortunately no end marker for the CENTERWINDOW section,
  // which makes parsing this a bit awkward. We keep parsing commands until
  // we find one that's not part of the message box definition commands, then
  // we assume the message box is complete and return to regular parsing.
  auto startOfLine = sourceStream.tellg();
  for (string line; getline(sourceStream, line, '\n');) {
    b::trim(line);
    if (isCommand(line)) {
      stripCommandPrefix(line);
      istringstream lineTextStream(line);
      string command;
      lineTextStream >> command;
      b::trim(command);

      if (command == "CWTEXT") {
        lineTextStream.get();
        string messageLine;
        getline(lineTextStream, messageLine, '\r');
        if (messageLine.empty()) {
          throw invalid_argument("Corrupt Duke Script file");
        }
        b::trim_right(messageLine);
        messageLines.emplace_back(messageLine);
      } else if (command == "SKLINE") {
        messageLines.emplace_back("");
      } else {
        // Since we already read a command, we have to rewind the stream to
        // allow the subsequent regular parsing to work.
        sourceStream.seekg(startOfLine);
        break;
      }

      startOfLine = sourceStream.tellg();
    }
  }

  return messageLines;
}


b::optional<data::script::Action> parseOneLineAction(
  const string& command,
  istream& lineTextStream
) {
  if (command == "FADEIN")
  {
    return Action{FadeIn{}};
  }
  else if (command == "FADEOUT")
  {
    return Action{FadeOut{}};
  }
  else if (command == "DELAY")
  {
    int amount = 0;
    lineTextStream >> amount;
    if (amount <= 0) {
      throw invalid_argument("Invalid DELAY command in Duke Script file");
    }
    return Action{Delay{amount}};
  }
  else if (command == "BABBLEON")
  {
    int duration = 0;
    lineTextStream >> duration;
    if (duration <= 0) {
      throw invalid_argument("Invalid BABBLEON command in Duke Script file");
    }
    return Action{AnimateNewsReporter{duration}};
  }
  else if (command == "BABBLEOFF")
  {
    return Action{StopNewsReporterAnimation{}};
  }
  else if (command == "NOSOUNDS")
  {
    return Action{DisableMenuFunctionality{}};
  }
  else if (command == "KEYS")
  {
    return Action{ShowKeyBindings{}};
  }
  else if (command == "GETNAMES")
  {
    int slot = 0;
    lineTextStream >> slot;
    if (slot < 0 || slot >= 8) {
      throw invalid_argument("Invalid GETNAMES command in Duke Script file");
    }
    return Action{ShowSaveSlots{slot}};
  }
  else if (command == "PAK")
  {
    // [P]ress [A]ny [K]ey - this is a shorthand for displaying actor nr. 146,
    // which is an image of the text "Press any key to continue".
    return Action{DrawSprite{0, 0, 146, 0}};
  }
  else if (command == "LOADRAW")
  {
    string imageName;
    lineTextStream >> imageName;
    b::trim(imageName);
    if (imageName.empty()) {
      throw invalid_argument("Invalid LOADRAW command in Duke Script file");
    }
    return Action{ShowFullScreenImage{imageName}};
  }
  else if (command == "Z")
  {
    int yPos = 0;
    lineTextStream >> yPos;
    return Action{ShowMenuSelectionIndicator{yPos}};
  }
  else if (command == "XYTEXT")
  {
    // They decided to pack a lot of different functionality into this single
    // command, which makes parsing it a bit more involved. There are three
    // variants:
    //
    // 1. Draw normal text
    // 2. Draw sprite
    // 3. Draw big, colorized text
    //
    // Variant 1 is the default, where we just need to take the remainder of
    // the line and draw it at the specified position.  The other two are
    // indicated by special 'markup' bytes in the text. If the text starts with
    // the byte 0xEF, the remaining text is actually interpreted as a sequence
    // of 2 numbers. The first always has 3 digits and indicates the actor ID
    // (index into ACTORINFO.MNI). The next 2 digits make up the second number,
    // which indicates the animation frame to draw for the specified actor's
    // sprite.
    //
    // If the text contains a byte >= 0xF0 at one point, the remaining text
    // will instead be drawn using a bigger font, which is also colorized using
    // the lower nibble of the markup byte as color index into the current
    // palette. E.g. if we have the text \xF7Hello, this will draw 'Hello'
    // using the big font colorized with palette index 7.
    // If there is other text preceding the 'big font' marker, it will be
    // drawn in the normal font. But the only occurence of that in the original
    // game's files has preceding spaces only, no printable characters. Thus,
    // we simplify our lives a little bit and say only preceding spaces are
    // supported, which we will then convert to an offset to the X coordinate
    // instead.
    int x = 0;
    int y = 0;
    lineTextStream >> x;
    lineTextStream >> y;

    lineTextStream.get();

    string parameters;
    getline(lineTextStream, parameters, '\r');

    if (parameters.empty()) {
      throw invalid_argument("Corrupt Duke Script file");
    }

    const auto bigTextMarkerIter =
      std::find_if(parameters.cbegin(), parameters.cend(), [](const auto ch) {
        return static_cast<uint8_t>(ch) >= 0xF0;
      });

    if (bigTextMarkerIter != parameters.cend()) {
      const auto numPrecedingCharacters = static_cast<int>(
        distance(parameters.cbegin(), bigTextMarkerIter));
      const auto colorIndex = static_cast<uint8_t>(*bigTextMarkerIter) - 0xF0;

      parameters.erase(parameters.cbegin(), next(bigTextMarkerIter));

      return Action{DrawBigText{
        x + numPrecedingCharacters,
        y,
        colorIndex,
        move(parameters)
      }};
    }

    if (static_cast<uint8_t>(parameters[0]) == 0xEF) {
      if (parameters.size() < 5) {
        throw invalid_argument("Corrupt Duke Script file");
      }

      string actorNumberString(
        parameters.cbegin() + 1, parameters.cbegin() + 4);
      string frameNumberString(
          parameters.cbegin() + 4, parameters.cbegin() + 6);

      return Action{DrawSprite{
        x + 2,
        y + 1,
        stoi(actorNumberString),
        stoi(frameNumberString)}};
    } else {
      return Action{DrawText{x, y, parameters}};
    }
  }
  else if (command == "GETPAL")
  {
    string paletteFile;
    lineTextStream >> paletteFile;
    b::trim(paletteFile);
    if (paletteFile.empty()) {
      throw invalid_argument("Invalid LOADRAW command in Duke Script file");
    }
    return Action{SetPalette{paletteFile}};
  }
  else if (command == "WAIT")
  {
    return Action{WaitForUserInput{}};
  }
  else if (command == "SHIFTWIN")
  {
    return Action{EnableTextOffset{}};
  }
  else if (command == "EXITTODEMO")
  {
    return Action{EnableTimeOutToDemo{}};
  }
  else if (command == "TOGGS")
  {
    int xPos = 0;
    int count = 0;
    lineTextStream >> xPos;
    lineTextStream >> count;

    vector<SetupCheckBoxes::CheckBoxDefinition> definitions;
    for (int i = 0; i < count; ++i) {
      SetupCheckBoxes::CheckBoxDefinition definition{0, 0};
      lineTextStream >> definition.yPos;
      lineTextStream >> definition.id;

      definitions.emplace_back(definition);
    }

    return Action{SetupCheckBoxes{xPos, definitions}};
  }
  else
  {
    assert(command != "END");
    static const unordered_set<string> notAllowedHere{
      "APAGE",
      "CENTERWINDOW",
      "CWTEXT",
      "MENU",
      "PAGESEND",
      "PAGESSTART",
      "SKLINE"
    };

    if (notAllowedHere.count(command) == 1) {
      throw invalid_argument(
        string("The command ") + command + " is not allowed in this context");
    }
  }

  return b::none;
}


PagesDefinition parsePagesDefinition(
  istream& sourceTextStream
) {
  vector<data::script::Script> pages(1);
  parseScriptLines(sourceTextStream, "PAGESEND",
    [&pages](const auto& command, auto& lineTextStream) {
      if (command == "APAGE") {
        pages.emplace_back(Script{});
      } else {
        auto maybeAction = parseOneLineAction(command, lineTextStream);
        if (maybeAction) {
          auto& currentPage = pages.back();
          currentPage.emplace_back(*maybeAction);
        }
      }
    });

  return PagesDefinition{pages};
}


b::optional<Action> parseAction(
  const std::string& command,
  istream& sourceTextStream,
  istream& currentLineStream
) {
  if (command == "CENTERWINDOW") {
    int y = 0;
    int width = 0;
    int height = 0;
    currentLineStream >> y;
    currentLineStream >> height;
    currentLineStream >> width;

    skipWhiteSpace(sourceTextStream);
    return Action{ShowMessageBox{
      y,
      width,
      height,
      parseMessageBoxTextDefinition(sourceTextStream)}};
  } else {
    return parseOneLineAction(command, currentLineStream);
  }
}


data::script::Script parseScript(istream& sourceTextStream) {
  data::script::Script script;

  parseScriptLines(sourceTextStream, "END",
    [&script, &sourceTextStream](const auto& command, auto& lineTextStream) {
      b::optional<Action> maybeAction;

      if (command == "PAGESSTART") {
        skipWhiteSpace(sourceTextStream);
        maybeAction = parsePagesDefinition(sourceTextStream);
      } else if (command == "MENU") {
        int slot = 0;
        lineTextStream >> slot;

        script.emplace_back(ConfigurePersistentMenuSelection{slot});
        script.emplace_back(ScheduleFadeInBeforeNextWaitState{});
      } else {
        maybeAction = parseAction(command, sourceTextStream, lineTextStream);
      }

      if (maybeAction) {
        script.emplace_back(*maybeAction);
      }
    });

  return script;
}

}


ScriptBundle loadScripts(const string& scriptSource) {
  istringstream sourceStream(scriptSource);

  ScriptBundle bundle;
  while (!sourceStream.eof()) {
    skipWhiteSpace(sourceStream);

    string scriptName;
    sourceStream >> scriptName;
    b::trim(scriptName);

    if (!scriptName.empty()) {
      bundle.emplace(scriptName, parseScript(sourceStream));
    }
  }

  return bundle;
}

}}
