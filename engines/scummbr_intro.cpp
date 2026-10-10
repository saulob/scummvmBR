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

#include "common/scummsys.h"

#include "engines/scummbr_intro.h"

#ifdef USE_BINK
#include "audio/mixer.h"

#include "common/array.h"
#include "common/events.h"
#include "common/memstream.h"
#include "common/system.h"

#include "engines/engine.h"

#include "graphics/cursorman.h"
#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "video/bink_decoder.h"
#endif

namespace ScummBR {

#ifdef USE_BINK
// The intro video, embedded from a .bik file by devtools/scummbr/scummbr_intro.py.
#include "engines/scummbr_intro.inc"

// Colours are binned to 5 bits per channel to build the intro palette.
static const int kIntroBins = 32 * 32 * 32;

static inline int introBinKey(int r, int g, int b) {
	return (r << 10) | (g << 5) | b;
}

// The Bink decoder outputs ARGB32; returns the 5-bit colour bin of a pixel.
static inline int introPixelBin(uint32 pixel, const Graphics::PixelFormat &format) {
	const int r = (pixel >> format.rShift) & 0xFF;
	const int g = (pixel >> format.gShift) & 0xFF;
	const int b = (pixel >> format.bShift) & 0xFF;
	return introBinKey(r >> 3, g >> 3, b >> 3);
}

// Expands a 5-bit channel value back to 8 bits.
static inline int introExpand(int v) {
	return (v << 3) | (v >> 2);
}

struct IntroBox {
	uint8 lo[3];
	uint8 hi[3];
	uint32 count;
};

// Trims a box to the bounding box of the colour bins that hold pixels.
static void introShrinkBox(IntroBox &box, const uint32 *hist) {
	uint8 lo[3] = { 31, 31, 31 };
	uint8 hi[3] = { 0, 0, 0 };
	uint32 count = 0;

	for (int r = box.lo[0]; r <= box.hi[0]; r++) {
		for (int g = box.lo[1]; g <= box.hi[1]; g++) {
			for (int b = box.lo[2]; b <= box.hi[2]; b++) {
				const uint32 n = hist[introBinKey(r, g, b)];
				if (!n)
					continue;

				const int c[3] = { r, g, b };
				for (int i = 0; i < 3; i++) {
					if (c[i] < lo[i])
						lo[i] = (uint8)c[i];
					if (c[i] > hi[i])
						hi[i] = (uint8)c[i];
				}
				count += n;
			}
		}
	}

	box.count = count;
	if (count) {
		for (int i = 0; i < 3; i++) {
			box.lo[i] = lo[i];
			box.hi[i] = hi[i];
		}
	}
}

// Median cut over the colour histogram. The most populated box is split at
// the median of its widest axis until there are 255 boxes, and each box
// becomes one palette entry. Entry 0 is kept black and the boxes use 1-255.
// lut maps every colour bin used by the video to its palette entry.
static void introBuildPalette(const uint32 *hist, byte *palette, byte *lut) {
	Common::Array<IntroBox> boxes;
	IntroBox root = { { 0, 0, 0 }, { 31, 31, 31 }, 0 };
	introShrinkBox(root, hist);
	if (root.count)
		boxes.push_back(root);

	while (boxes.size() < 255) {
		int best = -1;
		for (uint i = 0; i < boxes.size(); i++) {
			const IntroBox &box = boxes[i];
			// A single colour cannot be split.
			if (box.lo[0] == box.hi[0] && box.lo[1] == box.hi[1] && box.lo[2] == box.hi[2])
				continue;
			if (best < 0 || box.count > boxes[best].count)
				best = i;
		}
		if (best < 0)
			break;

		const IntroBox box = boxes[best];
		int axis = 0;
		for (int i = 1; i < 3; i++) {
			if (box.hi[i] - box.lo[i] > box.hi[axis] - box.lo[axis])
				axis = i;
		}

		// Pixel count for each position along the split axis.
		uint32 proj[32];
		memset(proj, 0, sizeof(proj));
		for (int r = box.lo[0]; r <= box.hi[0]; r++) {
			for (int g = box.lo[1]; g <= box.hi[1]; g++) {
				for (int b = box.lo[2]; b <= box.hi[2]; b++) {
					const int c[3] = { r, g, b };
					proj[c[axis]] += hist[introBinKey(r, g, b)];
				}
			}
		}

		// Cut where half of the pixels fall on each side.
		int cut = box.lo[axis];
		uint32 acc = 0;
		for (int v = box.lo[axis]; v < box.hi[axis]; v++) {
			cut = v;
			acc += proj[v];
			if (acc * 2 >= box.count)
				break;
		}

		IntroBox left = box;
		IntroBox right = box;
		left.hi[axis] = (uint8)cut;
		right.lo[axis] = (uint8)(cut + 1);
		introShrinkBox(left, hist);
		introShrinkBox(right, hist);

		boxes[best] = left;
		boxes.push_back(right);
	}

	memset(palette, 0, 256 * 3);
	memset(lut, 0, kIntroBins);
	for (uint i = 0; i < boxes.size(); i++) {
		const IntroBox &box = boxes[i];
		uint64 sum[3] = { 0, 0, 0 };

		for (int r = box.lo[0]; r <= box.hi[0]; r++) {
			for (int g = box.lo[1]; g <= box.hi[1]; g++) {
				for (int b = box.lo[2]; b <= box.hi[2]; b++) {
					const int key = introBinKey(r, g, b);
					const uint32 n = hist[key];
					if (!n)
						continue;

					const int c[3] = { r, g, b };
					for (int k = 0; k < 3; k++)
						sum[k] += (uint64)n * introExpand(c[k]);
					lut[key] = (byte)(i + 1);
				}
			}
		}

		for (int k = 0; k < 3; k++)
			palette[(i + 1) * 3 + k] = (byte)(sum[k] / box.count);
	}
}
#endif

void playIntro(int skipAction) {
#ifdef USE_BINK
	OSystem *system = g_system;
	if (system->getScreenFormat().bytesPerPixel != 1)
		return;

	const Graphics::PixelFormat format = Graphics::PixelFormat::createFormatARGB32();

	// First pass: collect the colours the video uses, so that the palette
	// is built from the video itself. The playback below decodes the same frames.
	uint32 *hist = new uint32[kIntroBins]();
	{
		Video::BinkDecoder analyzer;
		if (!analyzer.loadStream(new Common::MemoryReadStream(kScummBRIntroBik, kScummBRIntroBikSize)) ||
			!analyzer.setOutputPixelFormat(format)) {
			delete[] hist;
			return;
		}

		const Graphics::Surface *frame;
		while ((frame = analyzer.decodeNextFrame()) != nullptr) {
			for (int y = 0; y < frame->h; y++) {
				const uint32 *row = (const uint32 *)frame->getBasePtr(0, y);
				for (int x = 0; x < frame->w; x++)
					hist[introPixelBin(row[x], format)]++;
			}
		}
	}

	byte palette[256 * 3];
	byte *lut = new byte[kIntroBins];
	introBuildPalette(hist, palette, lut);
	delete[] hist;

	Video::BinkDecoder bink;
	if (!bink.loadStream(new Common::MemoryReadStream(kScummBRIntroBik, kScummBRIntroBikSize)) ||
		!bink.setOutputPixelFormat(format)) {
		delete[] lut;
		return;
	}

	// Scale the video to cover the whole game screen, cropping the parts that
	// do not fit, so there are no black bars.
	const int screenW = system->getWidth();
	const int screenH = system->getHeight();
	const int videoW = bink.getWidth();
	const int videoH = bink.getHeight();
	int dstW = screenW;
	int dstH = screenH;
	if (videoW * screenH > videoH * screenW)
		dstW = videoW * screenH / videoH;
	else
		dstH = videoH * screenW / videoW;
	const int cropX = (dstW - screenW) / 2;
	const int cropY = (dstH - screenH) / 2;

	const bool mouseVisible = CursorMan.isVisible();
	CursorMan.showMouse(false);
	system->getPaletteManager()->setPalette(palette, 0, 256);
	system->getMixer()->stopAll();
	bink.start();

	// Escape starts a short fade out of the palette and the sound. Otherwise
	// the video plays to the end, and no other key or click skips it.
	const uint32 kFadeMs = 500;
	bool fading = false;
	uint32 fadeStart = 0;
	byte fadePalette[256 * 3];
	while (!Engine::shouldQuit()) {
		Common::Event event;
		while (system->getEventManager()->pollEvent(event)) {
			const bool skip =
				(event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE) ||
				(skipAction >= 0 && event.type == Common::EVENT_CUSTOM_ENGINE_ACTION_START && event.customType == (Common::CustomEventType)skipAction);
			if (skip && !fading) {
				fading = true;
				fadeStart = system->getMillis();
			}
		}

		if (fading) {
			const uint32 elapsed = system->getMillis() - fadeStart;
			if (elapsed >= kFadeMs)
				break;

			const int level = 255 - elapsed * 255 / kFadeMs;
			for (int i = 0; i < 256 * 3; i++)
				fadePalette[i] = (byte)(palette[i] * level / 255);
			system->getPaletteManager()->setPalette(fadePalette, 0, 256);
			bink.setVolume((byte)level);
		} else if (bink.endOfVideo()) {
			break;
		}

		if (bink.needsUpdate()) {
			const Graphics::Surface *frame = bink.decodeNextFrame();
			if (frame) {
				Graphics::Surface *screen = system->lockScreen();
				for (int y = 0; y < screenH; y++) {
					const uint32 *src = (const uint32 *)frame->getBasePtr(0, (y + cropY) * videoH / dstH);
					byte *dst = (byte *)screen->getBasePtr(0, y);
					for (int x = 0; x < screenW; x++)
						dst[x] = lut[introPixelBin(src[(x + cropX) * videoW / dstW], format)];
				}

				system->unlockScreen();
				system->updateScreen();
			}
		}

		if (fading)
			system->updateScreen();

		system->delayMillis(10);
	}
	bink.stop();

	// Leave the screen black; the normal game loading takes over from here.
	system->fillScreen(0);
	memset(palette, 0, sizeof(palette));
	system->getPaletteManager()->setPalette(palette, 0, 256);
	system->updateScreen();

	CursorMan.showMouse(mouseVisible);
	delete[] lut;
#endif
}

} // End of namespace ScummBR
