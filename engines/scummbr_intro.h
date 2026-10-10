/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef ENGINES_SCUMMBR_INTRO_H
#define ENGINES_SCUMMBR_INTRO_H

/**
 * ScummVMBR intro video, played before a game that runs with the built-in
 * pt-BR translation (see engines/traducoes.h).
 *
 * The video is a .bik file built into the executable (scummbr_intro.inc, made
 * by devtools/scummbr/scummbr_intro.py). Without Bink support it is skipped.
 */
namespace ScummBR {

/**
 * Plays the intro over the whole game screen, which must be in 8-bit palette
 * mode. The video is scaled to cover the screen, with no black bars.
 *
 * Escape, or the engine action @p skipAction (-1 for none), fades the picture
 * and the sound out; nothing else skips it. The screen and the palette are
 * left black for the game to take over.
 */
void playIntro(int skipAction = -1);

} // End of namespace ScummBR

#endif
