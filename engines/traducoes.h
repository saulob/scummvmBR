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

#ifndef ENGINES_TRADUCOES_H
#define ENGINES_TRADUCOES_H

#include "common/str.h"
#include "common/ustr.h"

/**
 * ScummVMBR built-in game translations.
 *
 * A translation is a set of <FILE>.patch, each one turning the original <FILE>
 * of the game into the translated one, or adding a <FILE> the game does not
 * have (for engines that read loose files before their packages, like Kyra).
 * They are made by
 * devtools/scummbr/gerar_traducao.py, which also documents the format. All the
 * patches of a game go in one package, dists/traducoes/<gameid>.traducao (a
 * ZIP file).
 *
 * Those packages are built into the executable (traducoes_embutidas.inc). A
 * traducoes/<gameid>.traducao next to the executable (the "traducoespath"
 * setting) replaces the built-in translation of that game, to try out a new
 * one without rebuilding.
 *
 * The translated files are served from memory, ahead of the game directory in
 * SearchMan, so the game files on disk are never changed.
 */
namespace ScummBR {

/**
 * Mounts the translation of the given game.
 *
 * Nothing is mounted when the translation is missing, or when a game file is
 * not the one its patch was made for. @p reason then says why, in the GUI
 * language, to be shown to the player.
 */
bool mountTraducao(const Common::String &gameId, Common::U32String &reason);

/** Removes the translation mounted by mountTraducao(), if any. */
void unmountTraducao();

} // End of namespace ScummBR

#endif
