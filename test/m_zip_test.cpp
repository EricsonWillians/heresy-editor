//------------------------------------------------------------------------
//  ZIP ARCHIVE TESTS
//------------------------------------------------------------------------

#include "m_zip.h"

#include "SafeOutFile.h"
#include "lib_file.h"
#include "testUtils/TempDirContext.hpp"

#include "gtest/gtest.h"

#include <algorithm>

namespace
{

uint16_t Read16(const std::vector<uint8_t> &data, size_t offset)
{
	return static_cast<uint16_t>(data[offset]) |
			(static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t Read32(const std::vector<uint8_t> &data, size_t offset)
{
	return static_cast<uint32_t>(data[offset]) |
			(static_cast<uint32_t>(data[offset + 1]) << 8) |
			(static_cast<uint32_t>(data[offset + 2]) << 16) |
			(static_cast<uint32_t>(data[offset + 3]) << 24);
}

std::vector<uint8_t> LocalRecord(const std::vector<uint8_t> &data,
		const std::string &wanted)
{
	size_t eocd = data.size() - 22;
	while (Read32(data, eocd) != 0x06054b50)
		--eocd;
	const uint16_t count = Read16(data, eocd + 10);
	const uint32_t centralOffset = Read32(data, eocd + 16);

	std::vector<uint32_t> offsets;
	uint32_t wantedOffset = 0xffffffff;
	size_t cursor = centralOffset;
	for (uint16_t index = 0; index < count; ++index)
	{
		EXPECT_EQ(Read32(data, cursor), 0x02014b50u);
		const uint16_t nameLength = Read16(data, cursor + 28);
		const uint16_t extraLength = Read16(data, cursor + 30);
		const uint16_t commentLength = Read16(data, cursor + 32);
		const uint32_t localOffset = Read32(data, cursor + 42);
		const std::string name(reinterpret_cast<const char *>(
				data.data() + cursor + 46), nameLength);
		offsets.push_back(localOffset);
		if (name == wanted)
			wantedOffset = localOffset;
		cursor += 46 + nameLength + extraLength + commentLength;
	}

	EXPECT_NE(wantedOffset, 0xffffffffu);
	uint32_t end = centralOffset;
	for (uint32_t offset : offsets)
		if (offset > wantedOffset)
			end = std::min(end, offset);
	return std::vector<uint8_t>(data.begin() + wantedOffset, data.begin() + end);
}

} // namespace

class ZipArchiveTest : public TempDirContext
{
};

TEST_F(ZipArchiveTest, DeflatedAndStoredEntriesRoundTrip)
{
	const fs::path path = getSubPath("roundtrip.pk3");
	auto archive = ZipArchive::Create(path);
	const std::vector<uint8_t> compressible(8192, 'H');
	const std::vector<uint8_t> tiny = { 1, 2, 3, 4 };
	archive->setEntry("textures/wall.png", compressible);
	archive->setEntry("README.txt", tiny);
	archive->writeToDisk();
	mDeleteList.push(path);

	ASSERT_TRUE(ZipArchive::Validate(path));
	auto reopened = ZipArchive::Open(path);
	ASSERT_TRUE(reopened);
	EXPECT_EQ(reopened->readEntry("TEXTURES/WALL.PNG"), compressible);
	EXPECT_EQ(reopened->readEntry("readme.txt"), tiny);
	const std::vector<ZipEntryInfo> entries = reopened->entryInfos();
	ASSERT_EQ(entries.size(), 2u);
	auto textureInfo = std::find_if(entries.begin(), entries.end(),
			[](const ZipEntryInfo &entry)
			{
				return entry.name == "textures/wall.png";
			});
	ASSERT_NE(textureInfo, entries.end());
	EXPECT_EQ(textureInfo->uncompressedSize, compressible.size());
	EXPECT_LT(textureInfo->compressedSize, textureInfo->uncompressedSize);
	EXPECT_EQ(textureInfo->compressionMethod, 8);
	EXPECT_FALSE(textureInfo->encrypted);
	EXPECT_FALSE(textureInfo->directory);
}

TEST_F(ZipArchiveTest, TargetedUpdatePreservesUnknownLocalRecordExactly)
{
	const fs::path path = getSubPath("preserve.pk3");
	auto archive = ZipArchive::Create(path);
	archive->setEntry("maps/MAP01.wad", { 'o', 'l', 'd' });
	archive->setEntry("assets/unknown.bin", std::vector<uint8_t>(16384, 0x5a));
	archive->writeToDisk();
	mDeleteList.push(path);

	std::vector<uint8_t> before;
	ASSERT_TRUE(FileLoad(path, before));
	const std::vector<uint8_t> unknownRecord =
			LocalRecord(before, "assets/unknown.bin");

	archive = ZipArchive::Open(path);
	ASSERT_TRUE(archive);
	archive->setEntry("maps/MAP01.wad", std::vector<uint8_t>(4096, 0x31));
	archive->writeToDisk();

	std::vector<uint8_t> after;
	ASSERT_TRUE(FileLoad(path, after));
	EXPECT_EQ(LocalRecord(after, "assets/unknown.bin"), unknownRecord);

	auto verified = ZipArchive::Open(path);
	ASSERT_TRUE(verified);
	EXPECT_EQ(verified->readEntry("assets/unknown.bin"),
			std::vector<uint8_t>(16384, 0x5a));
}

TEST_F(ZipArchiveTest, InvalidArchiveIsRejectedWithoutMutation)
{
	const fs::path path = getSubPath("broken.pk3");
	BufferedOutFile output(path);
	const char broken[] = "PK not a complete archive";
	output.write(broken, sizeof(broken));
	output.commit();
	mDeleteList.push(path);

	EXPECT_FALSE(ZipArchive::Validate(path));
	EXPECT_THROW(ZipArchive::Open(path), std::runtime_error);
}
