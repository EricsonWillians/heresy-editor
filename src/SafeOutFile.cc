//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2021 Ioan Chera
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//------------------------------------------------------------------------

#include "lib_file.h"
#include "lib_util.h"
#include "Errors.h"
#include "SafeOutFile.h"
#include "sys_debug.h"

#include <atomic>
#include <chrono>
#include <fstream>

#ifdef _WIN32
#include "WindowsInclude.h"
#else
#include <unistd.h>
#endif

namespace
{

std::atomic<uint64_t> temporarySequence{0};

uint64_t ProcessIdentifier() noexcept
{
#ifdef _WIN32
	return static_cast<uint64_t>(GetCurrentProcessId());
#else
	return static_cast<uint64_t>(getpid());
#endif
}

fs::path TemporaryPathFor(const fs::path &target)
{
	const uint64_t timestamp = static_cast<uint64_t>(
			std::chrono::steady_clock::now().time_since_epoch().count());

	for (int attempt = 0; attempt < 100; ++attempt)
	{
		fs::path candidate = target;
		candidate += ".heresy-tmp-";
		candidate += std::to_string(ProcessIdentifier());
		candidate += "-";
		candidate += std::to_string(timestamp + temporarySequence.fetch_add(1));

		std::error_code error;
		if (!fs::exists(candidate, error) && !error)
			return candidate;
	}

	throw std::runtime_error("Could not allocate a temporary output path.");
}

void ReplaceDestination(const fs::path &temporary, const fs::path &destination)
{
#ifdef _WIN32
	if (!MoveFileExW(temporary.c_str(), destination.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		throw fs::filesystem_error("Could not replace destination",
				temporary, destination,
				std::error_code(GetLastError(), std::system_category()));
	}
#else
	std::error_code error;
	fs::rename(temporary, destination, error);
	if (error)
		throw fs::filesystem_error("Could not replace destination",
				temporary, destination, error);
#endif
}

} // namespace

//
// Writes data to file
//
void BufferedOutFile::write(const void *vdata, size_t size)
{
	auto data = static_cast<const uint8_t*>(vdata);
	mData.insert(mData.end(), data, data + size);
}

// WARNING: this throws.
void BufferedOutFile::commit(const Validator &validator)
{
	fs::path temporary;
	try
	{
		temporary = TemporaryPathFor(mPath);

		std::ofstream stream;
		stream.exceptions(std::ios::badbit | std::ios::failbit);
		stream.open(temporary, std::ios::out | std::ios::binary | std::ios::trunc);
		stream.write(reinterpret_cast<const char*>(mData.data()), mData.size());
		stream.flush();
		stream.close();

		std::error_code permissionError;
		if (fs::exists(mPath, permissionError) && !permissionError)
		{
			fs::perms permissions = fs::status(mPath, permissionError).permissions();
			if (!permissionError)
				fs::permissions(temporary, permissions, permissionError);
		}

		if (validator && !validator(temporary))
			throw std::runtime_error("The temporary output failed validation.");

		ReplaceDestination(temporary, mPath);
	}
	catch(const std::exception &e)
	{
		if (!temporary.empty())
		{
			std::error_code ignored;
			fs::remove(temporary, ignored);
		}

		gLog.printf("Failed safely writing %s: %s\n",
			reinterpret_cast<const char *>(mPath.u8string().c_str()), e.what());
		throw std::runtime_error(e.what());
	}
}
