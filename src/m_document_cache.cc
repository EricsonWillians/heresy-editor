//------------------------------------------------------------------------
//  BOUNDED MAP DOCUMENT CACHE
//------------------------------------------------------------------------

#include "m_document_cache.h"

#include <algorithm>
#include <iterator>

MapDocumentCache::Entries::iterator MapDocumentCache::find(
		const SString &mapName) noexcept
{
	return std::find_if(entries_.begin(), entries_.end(),
			[&mapName](const CachedMapDocument &entry)
			{
				return entry.mapName.noCaseEqual(mapName);
			});
}

MapDocumentCache::Entries::const_iterator MapDocumentCache::find(
		const SString &mapName) const noexcept
{
	return std::find_if(entries_.begin(), entries_.end(),
			[&mapName](const CachedMapDocument &entry)
			{
				return entry.mapName.noCaseEqual(mapName);
			});
}

bool MapDocumentCache::makeRoom(const SString &replacing)
{
	if (capacity_ == 0)
		return false;
	if (find(replacing) != entries_.end() || entries_.size() < capacity_)
		return true;

	// Preserve every dirty document.  The least-recently-used clean document
	// is the only safe automatic eviction candidate.
	for (auto entry = entries_.rbegin(); entry != entries_.rend(); ++entry)
	{
		if (!entry->document.hasChanges())
		{
			entries_.erase(std::next(entry).base());
			return true;
		}
	}

	return false;
}

bool MapDocumentCache::store(const SString &mapName, Document &&document,
		const LoadingData &loading)
{
	SString normalized = mapName.asUpper();
	if (normalized.empty() || !makeRoom(normalized))
		return false;

	auto existing = find(normalized);
	if (existing != entries_.end())
		entries_.erase(existing);
	entries_.emplace_front(normalized, std::move(document), loading);
	return true;
}

std::optional<CachedMapDocument> MapDocumentCache::take(
		const SString &mapName)
{
	auto entry = find(mapName);
	if (entry == entries_.end())
		return {};

	CachedMapDocument result = std::move(*entry);
	entries_.erase(entry);
	return result;
}

bool MapDocumentCache::contains(const SString &mapName) const noexcept
{
	return find(mapName) != entries_.end();
}

bool MapDocumentCache::isDirty(const SString &mapName) const noexcept
{
	auto entry = find(mapName);
	return entry != entries_.end() && entry->document.hasChanges();
}

size_t MapDocumentCache::dirtyCount() const noexcept
{
	return static_cast<size_t>(std::count_if(entries_.begin(), entries_.end(),
			[](const CachedMapDocument &entry)
			{
				return entry.document.hasChanges();
			}));
}

std::vector<SString> MapDocumentCache::dirtyMapNames() const
{
	std::vector<SString> result;
	for (const CachedMapDocument &entry : entries_)
		if (entry.document.hasChanges())
			result.push_back(entry.mapName);
	std::sort(result.begin(), result.end());
	return result;
}

std::vector<SString> MapDocumentCache::mapNames() const
{
	std::vector<SString> result;
	for (const CachedMapDocument &entry : entries_)
		result.push_back(entry.mapName);
	return result;
}

void MapDocumentCache::erase(const SString &mapName) noexcept
{
	auto entry = find(mapName);
	if (entry != entries_.end())
		entries_.erase(entry);
}

void MapDocumentCache::rename(const SString &oldName,
		const SString &newName) noexcept
{
	auto entry = find(oldName);
	if (entry == entries_.end())
		return;

	auto replaced = find(newName);
	if (replaced != entries_.end() && replaced != entry)
		entries_.erase(replaced);
	entry->mapName = newName.asUpper();
}

void MapDocumentCache::updateLoadingContext(const LoadingData &loading)
{
	for (CachedMapDocument &entry : entries_)
	{
		const SString mapName = entry.loading.levelName;
		entry.loading = loading;
		entry.loading.levelName = mapName.good() ? mapName : entry.mapName;
	}
}
