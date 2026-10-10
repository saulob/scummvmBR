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

#include "common/archive.h"
#include "common/array.h"
#include "common/compression/unzip.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/fs.h"
#include "common/hashmap.h"
#include "common/md5.h"
#include "common/memstream.h"
#include "common/ptr.h"
#include "common/stream.h"
#include "common/textconsole.h"
#include "common/translation.h"

#include "engines/traducoes.h"

namespace ScummBR {

// Name of the archive with the translated files in SearchMan. It is searched
// before the game directory, which is added with priority 0.
static const char *const kArchiveName = "traducao";
static const int kArchivePriority = 10;

static const char kPatchMagic[] = "SBRPTCH1";
static const char kNewFileMagic[] = "SBRNOVO1"; // A file the game does not have
static const char kPatchSuffix[] = ".patch";
static const char kPackageSuffix[] = ".traducao"; // A ZIP file with the .patch of a game

enum {
	kOpCopy = 0, // Bytes copied from the original file
	kOpData = 1  // New bytes, stored in the patch
};

struct PatchOp {
	uint32 dstStart; // Where the bytes go in the translated file
	uint32 length;
	uint32 offset;   // In the original file for kOpCopy, in literals for kOpData
	bool copy;
};

// A translated file: the original file of the game plus the patch. A new
// file has no original, only one kOpData operation with the whole file.
struct PatchedFile {
	Common::ArchiveMemberPtr original;
	uint32 size;
	Common::Array<PatchOp> ops;
	Common::Array<byte> literals;
};

typedef Common::SharedPtr<const PatchedFile> PatchedFilePtr;

// Builds the translated file on the fly, reading from the original file only
// the parts that the patch copies.
class PatchedFileStream : public Common::SeekableReadStream {
public:
	PatchedFileStream(const PatchedFilePtr &file, Common::SeekableReadStream *original)
		: _file(file), _original(original), _pos(0), _eos(false), _err(false) {}

	bool err() const override { return _err; }
	void clearErr() override { _err = false; _eos = false; }
	bool eos() const override { return _eos; }
	int64 pos() const override { return _pos; }
	int64 size() const override { return _file->size; }
	bool seek(int64 offset, int whence = SEEK_SET) override;
	uint32 read(void *dataPtr, uint32 dataSize) override;

private:
	const PatchOp &findOp(uint32 pos) const;

	PatchedFilePtr _file;
	Common::ScopedPtr<Common::SeekableReadStream> _original;
	uint32 _pos;
	bool _eos;
	bool _err;
};

const PatchOp &PatchedFileStream::findOp(uint32 pos) const {
	// The last operation that starts at or before pos
	const Common::Array<PatchOp> &ops = _file->ops;
	uint lo = 0;
	uint hi = ops.size() - 1;
	while (lo < hi) {
		const uint mid = (lo + hi + 1) / 2;
		if (ops[mid].dstStart <= pos)
			lo = mid;
		else
			hi = mid - 1;
	}
	return ops[lo];
}

bool PatchedFileStream::seek(int64 offset, int whence) {
	if (whence == SEEK_CUR)
		offset += _pos;
	else if (whence == SEEK_END)
		offset += _file->size;

	if (offset < 0 || offset > _file->size)
		return false;

	_pos = (uint32)offset;
	_eos = false;
	return true;
}

uint32 PatchedFileStream::read(void *dataPtr, uint32 dataSize) {
	byte *dst = (byte *)dataPtr;
	uint32 done = 0;

	while (done < dataSize) {
		if (_pos >= _file->size) {
			_eos = true;
			break;
		}

		const PatchOp &op = findOp(_pos);
		const uint32 skip = _pos - op.dstStart;
		const uint32 len = MIN(op.length - skip, dataSize - done);

		if (op.copy) {
			// The engines mostly read in order, so the original file is
			// usually already in place; a needless seek empties its buffer.
			const uint32 from = op.offset + skip;
			if ((_original->pos() != from && !_original->seek(from)) || _original->read(dst + done, len) != len) {
				_err = true;
				break;
			}
		} else {
			memcpy(dst + done, &_file->literals[op.offset + skip], len);
		}

		done += len;
		_pos += len;
	}

	return done;
}

class TraducaoArchive : public Common::Archive {
public:
	void addFile(const Common::Path &path, const PatchedFilePtr &file) { _files[path] = file; }

	bool hasFile(const Common::Path &path) const override {
		return _files.contains(path);
	}

	int listMembers(Common::ArchiveMemberList &list) const override {
		for (const auto &file : _files)
			list.push_back(Common::ArchiveMemberPtr(new Common::GenericArchiveMember(file._key, *this)));
		return _files.size();
	}

	const Common::ArchiveMemberPtr getMember(const Common::Path &path) const override {
		if (!hasFile(path))
			return Common::ArchiveMemberPtr();
		return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(path, *this));
	}

	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &path) const override {
		if (!_files.contains(path))
			return nullptr;

		const PatchedFilePtr &file = _files.getVal(path);
		Common::SeekableReadStream *original = nullptr;
		if (file->original) {
			original = file->original->createReadStream();
			if (!original)
				return nullptr;
		}
		return new PatchedFileStream(file, original);
	}

private:
	Common::HashMap<Common::Path, PatchedFilePtr, Common::Path::IgnoreCase_Hash, Common::Path::IgnoreCase_EqualTo> _files;
};

// Reads a new file, which the game does not have: the whole file follows
// the size.
static PatchedFile *loadNewFile(Common::SeekableReadStream &in, const Common::String &fileName, Common::U32String &reason) {
	const uint32 size = in.readUint32LE();
	if (in.err() || in.eos() || size == 0 || size != in.size() - in.pos()) {
		reason = Common::U32String::format(_("The translation file %s%s is corrupted."), fileName.c_str(), kPatchSuffix);
		return nullptr;
	}

	Common::ScopedPtr<PatchedFile> file(new PatchedFile());
	file->size = size;
	file->literals.resize(size);
	if (in.read(&file->literals[0], size) != size) {
		reason = Common::U32String::format(_("The translation file %s%s is corrupted."), fileName.c_str(), kPatchSuffix);
		return nullptr;
	}

	PatchOp op;
	op.dstStart = 0;
	op.length = size;
	op.offset = 0;
	op.copy = false;
	file->ops.push_back(op);
	return file.release();
}

// Reads a patch and checks it against the original file of the game.
static PatchedFile *loadPatch(Common::SeekableReadStream &in, const Common::String &fileName, Common::U32String &reason) {
	char magic[8];
	if (in.read(magic, sizeof(magic)) != sizeof(magic)) {
		reason = Common::U32String::format(_("The translation file %s%s is corrupted."), fileName.c_str(), kPatchSuffix);
		return nullptr;
	}
	if (memcmp(magic, kNewFileMagic, sizeof(magic)) == 0)
		return loadNewFile(in, fileName, reason);
	if (memcmp(magic, kPatchMagic, sizeof(magic)) != 0) {
		reason = Common::U32String::format(_("The translation file %s%s is corrupted."), fileName.c_str(), kPatchSuffix);
		return nullptr;
	}

	const uint32 originalSize = in.readUint32LE();
	uint8 originalMd5[16];
	in.read(originalMd5, sizeof(originalMd5));
	const uint32 size = in.readUint32LE();
	const uint32 count = in.readUint32LE();

	Common::ScopedPtr<PatchedFile> file(new PatchedFile());
	file->size = size;

	uint32 dst = 0;
	bool valid = !in.err() && !in.eos() && count <= size;
	for (uint32 i = 0; valid && i < count; i++) {
		PatchOp op;
		op.dstStart = dst;
		op.copy = in.readByte() == kOpCopy;

		if (op.copy) {
			op.offset = in.readUint32LE();
			op.length = in.readUint32LE();
			valid = op.offset <= originalSize && op.length <= originalSize - op.offset;
		} else {
			op.length = in.readUint32LE();
			op.offset = file->literals.size();
			valid = op.length > 0 && op.length <= in.size() - in.pos();
			if (valid) {
				file->literals.resize(op.offset + op.length);
				valid = in.read(&file->literals[op.offset], op.length) == op.length;
			}
		}

		valid = valid && !in.err() && !in.eos() && op.length > 0 && op.length <= size - dst;
		dst += op.length;
		file->ops.push_back(op);
	}

	if (!valid || dst != size || in.pos() != in.size()) {
		reason = Common::U32String::format(_("The translation file %s%s is corrupted."), fileName.c_str(), kPatchSuffix);
		return nullptr;
	}

	file->original = SearchMan.getMember(Common::Path(fileName));
	Common::ScopedPtr<Common::SeekableReadStream> original(file->original ? file->original->createReadStream() : nullptr);
	if (!original) {
		reason = Common::U32String::format(_("The file %s was not found in the game folder."), fileName.c_str());
		return nullptr;
	}

	uint8 md5[16];
	if (original->size() != originalSize || !Common::computeStreamMD5(*original, md5) || memcmp(md5, originalMd5, sizeof(md5)) != 0) {
		reason = Common::U32String::format(_("The game file %s is not from the edition the translation was made for."), fileName.c_str());
		return nullptr;
	}

	return file.release();
}

// A translation built into the executable: the <gameid>.traducao package
struct EmbeddedTraducao {
	const char *gameId;
	const byte *data;
	uint32 size;
};

// kEmbeddedTraducoes, made from dists/traducoes by gerar_traducao.py
#include "engines/traducoes_embutidas.inc"

// Loads every <FILE>.patch of a .traducao package into the archive. Takes
// ownership of the stream. Returns how many files it loaded, or -1 with the
// reason.
static int loadPackage(Common::SeekableReadStream *stream, const Common::String &gameId, TraducaoArchive &archive, Common::U32String &reason) {
	Common::ScopedPtr<Common::Archive> package(Common::makeZipArchive(stream));
	Common::ArchiveMemberList members;
	if (!package || package->listMembers(members) == 0) {
		reason = Common::U32String::format(_("The translation file %s%s is corrupted."), gameId.c_str(), kPackageSuffix);
		return -1;
	}

	int count = 0;
	for (const Common::ArchiveMemberPtr &member : members) {
		Common::String fileName = member->getFileName();
		if (!fileName.hasSuffixIgnoreCase(kPatchSuffix))
			continue;
		fileName.erase(fileName.size() - strlen(kPatchSuffix));

		Common::ScopedPtr<Common::SeekableReadStream> in(member->createReadStream());
		PatchedFile *file = nullptr;
		if (in)
			file = loadPatch(*in, fileName, reason);
		else
			reason = Common::U32String::format(_("The translation file %s%s is corrupted."), fileName.c_str(), kPatchSuffix);
		if (!file)
			return -1;

		archive.addFile(Common::Path(fileName), PatchedFilePtr(file));
		count++;
	}

	if (!count) {
		reason = Common::U32String::format(_("The translation file %s%s is corrupted."), gameId.c_str(), kPackageSuffix);
		return -1;
	}
	return count;
}

bool mountTraducao(const Common::String &gameId, Common::U32String &reason) {
	unmountTraducao();

	Common::ScopedPtr<TraducaoArchive> archive(new TraducaoArchive());
	int count;
	const char *source;

	// A traducoes/<gameid>.traducao next to the executable replaces the
	// built-in translation of the game, so a new one can be tried without
	// rebuilding.
	Common::Path root = ConfMan.getPath("traducoespath");
	if (root.empty())
		root = Common::Path("traducoes");
	const Common::FSNode package = Common::FSNode(root).getChild(gameId + kPackageSuffix);

	if (package.exists() && !package.isDirectory()) {
		source = "pasta";
		Common::SeekableReadStream *stream = package.createReadStream();
		if (!stream) {
			reason = Common::U32String::format(_("Could not read %s."), package.getPath().toString(Common::Path::kNativeSeparator).c_str());
			warning("Traducao de %s nao aplicada: %s", gameId.c_str(), reason.encode().c_str());
			return false;
		}
		count = loadPackage(stream, gameId, *archive, reason);
	} else {
		source = "embutida";
		const EmbeddedTraducao *traducao = kEmbeddedTraducoes;
		while (traducao->gameId && gameId != traducao->gameId)
			traducao++;
		if (!traducao->gameId) {
			reason = Common::U32String::format(_("This ScummVMBR has no translation for %s."), gameId.c_str());
			warning("Traducao de %s nao aplicada: %s", gameId.c_str(), reason.encode().c_str());
			return false;
		}
		count = loadPackage(new Common::MemoryReadStream(traducao->data, traducao->size), gameId, *archive, reason);
	}

	if (count < 0) {
		warning("Traducao de %s nao aplicada: %s", gameId.c_str(), reason.encode().c_str());
		return false;
	}

	SearchMan.add(kArchiveName, archive.release(), kArchivePriority);
	debug(1, "Traducao de %s aplicada (%s): %d arquivo(s)", gameId.c_str(), source, count);
	return true;
}

void unmountTraducao() {
	SearchMan.remove(kArchiveName);
}

} // End of namespace ScummBR
