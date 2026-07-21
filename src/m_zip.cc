//------------------------------------------------------------------------
//  PRESERVATION-SAFE ZIP ARCHIVES
//------------------------------------------------------------------------

#include "m_zip.h"

#include "SafeOutFile.h"
#include "lib_file.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

#include <zlib.h>

namespace
{

constexpr uint32_t LOCAL_SIGNATURE = 0x04034b50;
constexpr uint32_t CENTRAL_SIGNATURE = 0x02014b50;
constexpr uint32_t EOCD_SIGNATURE = 0x06054b50;
constexpr size_t LOCAL_HEADER_SIZE = 30;
constexpr size_t CENTRAL_HEADER_SIZE = 46;
constexpr size_t EOCD_SIZE = 22;
constexpr size_t MAX_EOCD_SEARCH = 65535 + EOCD_SIZE;

uint16_t Read16(const std::vector<uint8_t> &data, size_t offset)
{
	if (offset > data.size() || data.size() - offset < 2)
		throw std::runtime_error("Truncated ZIP structure.");

	return static_cast<uint16_t>(data[offset]) |
			(static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t Read32(const std::vector<uint8_t> &data, size_t offset)
{
	if (offset > data.size() || data.size() - offset < 4)
		throw std::runtime_error("Truncated ZIP structure.");

	return static_cast<uint32_t>(data[offset]) |
			(static_cast<uint32_t>(data[offset + 1]) << 8) |
			(static_cast<uint32_t>(data[offset + 2]) << 16) |
			(static_cast<uint32_t>(data[offset + 3]) << 24);
}

void Append16(std::vector<uint8_t> &data, uint16_t value)
{
	data.push_back(static_cast<uint8_t>(value));
	data.push_back(static_cast<uint8_t>(value >> 8));
}

void Append32(std::vector<uint8_t> &data, uint32_t value)
{
	data.push_back(static_cast<uint8_t>(value));
	data.push_back(static_cast<uint8_t>(value >> 8));
	data.push_back(static_cast<uint8_t>(value >> 16));
	data.push_back(static_cast<uint8_t>(value >> 24));
}

void Patch32(std::vector<uint8_t> &data, size_t offset, uint32_t value)
{
	if (offset > data.size() || data.size() - offset < 4)
		throw std::runtime_error("Invalid ZIP central-directory record.");

	data[offset] = static_cast<uint8_t>(value);
	data[offset + 1] = static_cast<uint8_t>(value >> 8);
	data[offset + 2] = static_cast<uint8_t>(value >> 16);
	data[offset + 3] = static_cast<uint8_t>(value >> 24);
}

bool EqualName(const std::string &left, const std::string &right) noexcept
{
	if (left.size() != right.size())
		return false;

	for (size_t index = 0; index < left.size(); ++index)
	{
		unsigned char a = static_cast<unsigned char>(left[index]);
		unsigned char b = static_cast<unsigned char>(right[index]);
		if (a >= 'A' && a <= 'Z')
			a = static_cast<unsigned char>(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z')
			b = static_cast<unsigned char>(b - 'A' + 'a');
		if (a != b)
			return false;
	}

	return true;
}

void ValidateManagedName(const std::string &name)
{
	if (name.empty() || name.front() == '/' || name.front() == '\\' ||
			name.find('\\') != std::string::npos ||
			name.find('\0') != std::string::npos)
	{
		throw std::runtime_error("Invalid ZIP entry path.");
	}

	size_t start = 0;
	while (start <= name.size())
	{
		size_t end = name.find('/', start);
		std::string component = name.substr(start,
				end == std::string::npos ? std::string::npos : end - start);
		if (component == "..")
			throw std::runtime_error("ZIP entry path traversal is not allowed.");
		if (end == std::string::npos)
			break;
		start = end + 1;
	}

	if (name.size() > std::numeric_limits<uint16_t>::max())
		throw std::runtime_error("ZIP entry path is too long.");
}

std::vector<uint8_t> Deflate(const std::vector<uint8_t> &input)
{
	if (input.empty())
		return {};

	z_stream stream{};
	if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
			-MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
	{
		throw std::runtime_error("Could not initialize ZIP compression.");
	}

	std::vector<uint8_t> output(deflateBound(&stream,
			static_cast<uLong>(input.size())));
	stream.next_in = const_cast<Bytef *>(
			reinterpret_cast<const Bytef *>(input.data()));
	stream.avail_in = static_cast<uInt>(input.size());
	stream.next_out = reinterpret_cast<Bytef *>(output.data());
	stream.avail_out = static_cast<uInt>(output.size());

	int result = deflate(&stream, Z_FINISH);
	if (result != Z_STREAM_END)
	{
		deflateEnd(&stream);
		throw std::runtime_error("Could not compress ZIP entry.");
	}

	output.resize(stream.total_out);
	if (deflateEnd(&stream) != Z_OK)
		throw std::runtime_error("Could not finish ZIP compression.");

	return output;
}

std::vector<uint8_t> Inflate(const uint8_t *input, size_t inputSize,
		size_t outputSize)
{
	if (outputSize > std::numeric_limits<uInt>::max() ||
			inputSize > std::numeric_limits<uInt>::max())
	{
		throw std::runtime_error("ZIP entry is too large for this build.");
	}

	std::vector<uint8_t> output(outputSize == 0 ? 1 : outputSize);
	z_stream stream{};
	if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
		throw std::runtime_error("Could not initialize ZIP decompression.");

	stream.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(input));
	stream.avail_in = static_cast<uInt>(inputSize);
	stream.next_out = reinterpret_cast<Bytef *>(output.data());
	stream.avail_out = static_cast<uInt>(output.size());

	int result = inflate(&stream, Z_FINISH);
	const size_t actual = stream.total_out;
	inflateEnd(&stream);

	if (result != Z_STREAM_END || actual != outputSize)
		throw std::runtime_error("Invalid deflated ZIP entry.");

	output.resize(outputSize);
	return output;
}

uint32_t EntryCRC(const std::vector<uint8_t> &data)
{
	uLong value = crc32(0L, Z_NULL, 0);
	if (!data.empty())
		value = crc32(value, reinterpret_cast<const Bytef *>(data.data()),
				static_cast<uInt>(data.size()));
	return static_cast<uint32_t>(value);
}

} // namespace

struct ZipArchive::Entry
{
	std::string name;
	uint16_t flags = 0;
	uint16_t method = 0;
	uint32_t crc = 0;
	uint32_t compressedSize = 0;
	uint32_t uncompressedSize = 0;
	uint32_t localOffset = 0;
	size_t localEnd = 0;
	std::vector<uint8_t> centralRecord;
	std::vector<uint8_t> replacement;
	bool changed = false;
	bool removed = false;
};

ZipArchive::ZipArchive(const fs::path &path) : path_(path)
{
}

ZipArchive::~ZipArchive() = default;

std::shared_ptr<ZipArchive> ZipArchive::Create(const fs::path &path)
{
	return std::shared_ptr<ZipArchive>(new ZipArchive(path));
}

std::shared_ptr<ZipArchive> ZipArchive::Open(const fs::path &path)
{
	std::vector<uint8_t> data;
	if (!FileLoad(path, data))
		return nullptr;

	auto archive = std::shared_ptr<ZipArchive>(new ZipArchive(path));
	archive->parse(std::move(data));
	return archive;
}

bool ZipArchive::Validate(const fs::path &path) noexcept
{
	try
	{
		return Open(path) != nullptr;
	}
	catch (...)
	{
		return false;
	}
}

void ZipArchive::parse(std::vector<uint8_t> data)
{
	if (data.size() < EOCD_SIZE)
		throw std::runtime_error("File is not a complete ZIP archive.");

	const size_t lower = data.size() > MAX_EOCD_SEARCH ?
			data.size() - MAX_EOCD_SEARCH : 0;
	size_t eocd = std::string::npos;
	for (size_t candidate = data.size() - EOCD_SIZE;; --candidate)
	{
		if (Read32(data, candidate) == EOCD_SIGNATURE)
		{
			uint16_t commentLength = Read16(data, candidate + 20);
			if (candidate + EOCD_SIZE + commentLength == data.size())
			{
				eocd = candidate;
				break;
			}
		}
		if (candidate == lower)
			break;
	}

	if (eocd == std::string::npos)
		throw std::runtime_error("ZIP end-of-directory record was not found.");

	const uint16_t disk = Read16(data, eocd + 4);
	const uint16_t centralDisk = Read16(data, eocd + 6);
	const uint16_t diskEntries = Read16(data, eocd + 8);
	const uint16_t entryCount = Read16(data, eocd + 10);
	const uint32_t centralSize = Read32(data, eocd + 12);
	const uint32_t centralOffset = Read32(data, eocd + 16);
	const uint16_t commentLength = Read16(data, eocd + 20);

	if (disk != 0 || centralDisk != 0 || diskEntries != entryCount)
		throw std::runtime_error("Multi-disk ZIP archives are not supported.");
	if (entryCount == 0xffff || centralSize == 0xffffffff ||
			centralOffset == 0xffffffff)
	{
		throw std::runtime_error("ZIP64 PK3 archives are not supported.");
	}
	if (static_cast<uint64_t>(centralOffset) + centralSize > eocd)
		throw std::runtime_error("Invalid ZIP central-directory bounds.");

	archiveComment_.assign(data.begin() + eocd + EOCD_SIZE,
			data.begin() + eocd + EOCD_SIZE + commentLength);

	entries_.clear();
	entries_.reserve(entryCount);
	size_t cursor = centralOffset;
	const size_t centralEnd = static_cast<size_t>(centralOffset) + centralSize;
	for (uint16_t index = 0; index < entryCount; ++index)
	{
		if (cursor > centralEnd || centralEnd - cursor < CENTRAL_HEADER_SIZE ||
				Read32(data, cursor) != CENTRAL_SIGNATURE)
		{
			throw std::runtime_error("Invalid ZIP central-directory entry.");
		}

		const uint16_t nameLength = Read16(data, cursor + 28);
		const uint16_t extraLength = Read16(data, cursor + 30);
		const uint16_t entryCommentLength = Read16(data, cursor + 32);
		const uint16_t startDisk = Read16(data, cursor + 34);
		const size_t recordSize = CENTRAL_HEADER_SIZE + nameLength +
				extraLength + entryCommentLength;
		if (recordSize > centralEnd - cursor)
			throw std::runtime_error("Truncated ZIP central-directory entry.");

		Entry entry;
		entry.flags = Read16(data, cursor + 8);
		entry.method = Read16(data, cursor + 10);
		entry.crc = Read32(data, cursor + 16);
		entry.compressedSize = Read32(data, cursor + 20);
		entry.uncompressedSize = Read32(data, cursor + 24);
		entry.localOffset = Read32(data, cursor + 42);
		if (startDisk != 0 || entry.compressedSize == 0xffffffff ||
				entry.uncompressedSize == 0xffffffff ||
				entry.localOffset == 0xffffffff)
		{
			throw std::runtime_error("ZIP64 or multi-disk entries are not supported.");
		}

		entry.name.assign(reinterpret_cast<const char *>(data.data() + cursor +
				CENTRAL_HEADER_SIZE), nameLength);
		if (entry.name.find('\0') != std::string::npos)
			throw std::runtime_error("ZIP entry contains an invalid name.");
		entry.centralRecord.assign(data.begin() + cursor,
				data.begin() + cursor + recordSize);
		entries_.push_back(std::move(entry));
		cursor += recordSize;
	}

	centralTail_.assign(data.begin() + cursor, data.begin() + centralEnd);
	postCentral_.assign(data.begin() + centralEnd, data.begin() + eocd);

	std::vector<size_t> order(entries_.size());
	for (size_t index = 0; index < order.size(); ++index)
		order[index] = index;
	std::sort(order.begin(), order.end(), [this](size_t left, size_t right)
	{
		return entries_[left].localOffset < entries_[right].localOffset;
	});

	size_t preambleEnd = centralOffset;
	if (!order.empty())
		preambleEnd = entries_[order.front()].localOffset;
	if (preambleEnd > centralOffset)
		throw std::runtime_error("Invalid ZIP local-entry offset.");
	preamble_.assign(data.begin(), data.begin() + preambleEnd);

	for (size_t ordered = 0; ordered < order.size(); ++ordered)
	{
		Entry &entry = entries_[order[ordered]];
		const size_t nextOffset = ordered + 1 < order.size() ?
				entries_[order[ordered + 1]].localOffset : centralOffset;
		if (entry.localOffset < preambleEnd || entry.localOffset >= nextOffset ||
				nextOffset > centralOffset ||
				centralOffset - entry.localOffset < LOCAL_HEADER_SIZE ||
				Read32(data, entry.localOffset) != LOCAL_SIGNATURE)
		{
			throw std::runtime_error("Invalid ZIP local-entry record.");
		}

		const uint16_t localNameLength = Read16(data, entry.localOffset + 26);
		const uint16_t localExtraLength = Read16(data, entry.localOffset + 28);
		const uint64_t dataStart = static_cast<uint64_t>(entry.localOffset) +
				LOCAL_HEADER_SIZE + localNameLength + localExtraLength;
		const uint64_t dataEnd = dataStart + entry.compressedSize;
		if (dataEnd > nextOffset)
			throw std::runtime_error("ZIP entry data exceeds its local record.");

		entry.localEnd = nextOffset;
	}

	source_ = std::move(data);
}

ZipArchive::Entry *ZipArchive::findUnique(const std::string &name)
{
	Entry *result = nullptr;
	for (Entry &entry : entries_)
	{
		if (!entry.removed && EqualName(entry.name, name))
		{
			if (result)
				throw std::runtime_error("Ambiguous duplicate ZIP entry name: " + name);
			result = &entry;
		}
	}
	return result;
}

const ZipArchive::Entry *ZipArchive::findUnique(const std::string &name) const
{
	const Entry *result = nullptr;
	for (const Entry &entry : entries_)
	{
		if (!entry.removed && EqualName(entry.name, name))
		{
			if (result)
				throw std::runtime_error("Ambiguous duplicate ZIP entry name: " + name);
			result = &entry;
		}
	}
	return result;
}

std::vector<std::string> ZipArchive::entryNames() const
{
	std::vector<std::string> result;
	for (const Entry &entry : entries_)
		if (!entry.removed)
			result.push_back(entry.name);
	return result;
}

bool ZipArchive::contains(const std::string &name) const noexcept
{
	try
	{
		return findUnique(name) != nullptr;
	}
	catch (...)
	{
		return false;
	}
}

std::vector<uint8_t> ZipArchive::readEntry(const std::string &name) const
{
	const Entry *entry = findUnique(name);
	if (!entry)
		throw std::runtime_error("ZIP entry not found: " + name);
	if (entry->changed)
		return entry->replacement;
	if (entry->flags & 1)
		throw std::runtime_error("Encrypted ZIP entries cannot be edited.");

	const size_t localOffset = entry->localOffset;
	const uint16_t localNameLength = Read16(source_, localOffset + 26);
	const uint16_t localExtraLength = Read16(source_, localOffset + 28);
	const size_t dataStart = localOffset + LOCAL_HEADER_SIZE +
			localNameLength + localExtraLength;
	std::vector<uint8_t> result;
	if (entry->method == 0)
	{
		if (entry->compressedSize != entry->uncompressedSize)
			throw std::runtime_error("Invalid stored ZIP entry.");
		result.assign(source_.begin() + dataStart,
				source_.begin() + dataStart + entry->compressedSize);
	}
	else if (entry->method == 8)
	{
		result = Inflate(source_.data() + dataStart, entry->compressedSize,
				entry->uncompressedSize);
	}
	else
	{
		throw std::runtime_error("Unsupported ZIP compression method for entry: " +
				entry->name);
	}

	if (EntryCRC(result) != entry->crc)
		throw std::runtime_error("ZIP entry failed its CRC check: " + entry->name);
	return result;
}

void ZipArchive::setEntry(const std::string &name,
		const std::vector<uint8_t> &data)
{
	ValidateManagedName(name);
	if (data.size() > std::numeric_limits<uint32_t>::max())
		throw std::runtime_error("ZIP entry exceeds the classic ZIP size limit.");

	Entry *entry = findUnique(name);
	if (!entry)
	{
		Entry created;
		created.name = name;
		entries_.push_back(std::move(created));
		entry = &entries_.back();
	}

	entry->replacement = data;
	entry->changed = true;
	entry->removed = false;
}

void ZipArchive::removeEntry(const std::string &name)
{
	Entry *entry = findUnique(name);
	if (entry)
		entry->removed = true;
}

std::vector<uint8_t> ZipArchive::serialize() const
{
	std::vector<uint8_t> output = preamble_;
	std::vector<std::vector<uint8_t>> centralRecords;
	centralRecords.reserve(entries_.size());
	uint32_t activeCount = 0;

	for (const Entry &entry : entries_)
	{
		if (entry.removed)
			continue;
		if (output.size() > std::numeric_limits<uint32_t>::max())
			throw std::runtime_error("PK3 exceeds the classic ZIP size limit.");

		const uint32_t newOffset = static_cast<uint32_t>(output.size());
		std::vector<uint8_t> central;
		if (!entry.changed)
		{
			if (entry.localEnd < entry.localOffset ||
					entry.localEnd > source_.size())
				throw std::runtime_error("Invalid preserved ZIP local record.");
			output.insert(output.end(), source_.begin() + entry.localOffset,
					source_.begin() + entry.localEnd);
			central = entry.centralRecord;
			Patch32(central, 42, newOffset);
		}
		else
		{
			const std::vector<uint8_t> deflated = Deflate(entry.replacement);
			const bool useDeflate = !entry.replacement.empty() &&
					deflated.size() < entry.replacement.size();
			const std::vector<uint8_t> &payload = useDeflate ?
					deflated : entry.replacement;
			if (payload.size() > std::numeric_limits<uint32_t>::max())
				throw std::runtime_error("Compressed ZIP entry is too large.");

			const uint16_t flags = 1u << 11; // UTF-8 path names.
			const uint16_t method = useDeflate ? 8 : 0;
			const uint32_t crc = EntryCRC(entry.replacement);
			const uint32_t compressedSize = static_cast<uint32_t>(payload.size());
			const uint32_t uncompressedSize =
					static_cast<uint32_t>(entry.replacement.size());

			Append32(output, LOCAL_SIGNATURE);
			Append16(output, 20);
			Append16(output, flags);
			Append16(output, method);
			Append16(output, 0);       // Deterministic DOS time.
			Append16(output, 0x0021);  // 1980-01-01.
			Append32(output, crc);
			Append32(output, compressedSize);
			Append32(output, uncompressedSize);
			Append16(output, static_cast<uint16_t>(entry.name.size()));
			Append16(output, 0);
			output.insert(output.end(), entry.name.begin(), entry.name.end());
			output.insert(output.end(), payload.begin(), payload.end());

			Append32(central, CENTRAL_SIGNATURE);
			Append16(central, 20);
			Append16(central, 20);
			Append16(central, flags);
			Append16(central, method);
			Append16(central, 0);
			Append16(central, 0x0021);
			Append32(central, crc);
			Append32(central, compressedSize);
			Append32(central, uncompressedSize);
			Append16(central, static_cast<uint16_t>(entry.name.size()));
			Append16(central, 0);
			Append16(central, 0);
			Append16(central, 0);
			Append16(central, 0);
			Append32(central, 0);
			Append32(central, newOffset);
			central.insert(central.end(), entry.name.begin(), entry.name.end());
		}

		centralRecords.push_back(std::move(central));
		++activeCount;
	}

	if (activeCount > std::numeric_limits<uint16_t>::max() ||
			output.size() > std::numeric_limits<uint32_t>::max())
	{
		throw std::runtime_error("PK3 exceeds the classic ZIP entry or size limit.");
	}

	const uint32_t centralOffset = static_cast<uint32_t>(output.size());
	for (const std::vector<uint8_t> &record : centralRecords)
		output.insert(output.end(), record.begin(), record.end());
	output.insert(output.end(), centralTail_.begin(), centralTail_.end());

	const uint64_t centralSize64 = output.size() - centralOffset;
	if (centralSize64 > std::numeric_limits<uint32_t>::max())
		throw std::runtime_error("PK3 central directory is too large.");
	if (archiveComment_.size() > std::numeric_limits<uint16_t>::max())
		throw std::runtime_error("PK3 archive comment is too large.");

	output.insert(output.end(), postCentral_.begin(), postCentral_.end());
	Append32(output, EOCD_SIGNATURE);
	Append16(output, 0);
	Append16(output, 0);
	Append16(output, static_cast<uint16_t>(activeCount));
	Append16(output, static_cast<uint16_t>(activeCount));
	Append32(output, static_cast<uint32_t>(centralSize64));
	Append32(output, centralOffset);
	Append16(output, static_cast<uint16_t>(archiveComment_.size()));
	output.insert(output.end(), archiveComment_.begin(), archiveComment_.end());

	return output;
}

void ZipArchive::writeToDisk() const
{
	const std::vector<uint8_t> data = serialize();
	BufferedOutFile output(path_);
	output.write(data.data(), data.size());
	output.commit([](const fs::path &temporary)
	{
		return ZipArchive::Validate(temporary);
	});
}
