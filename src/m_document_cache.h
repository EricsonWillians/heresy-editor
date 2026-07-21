//------------------------------------------------------------------------
//  BOUNDED MAP DOCUMENT CACHE
//------------------------------------------------------------------------

#ifndef HERESY_M_DOCUMENT_CACHE_H
#define HERESY_M_DOCUMENT_CACHE_H

#include "m_loadsave.h"

#include <cstddef>
#include <list>
#include <optional>
#include <vector>

struct CachedMapDocument
{
	SString mapName;
	Document document;
	LoadingData loading;

	CachedMapDocument(const SString &name, Document &&doc,
			const LoadingData &inLoading) :
			mapName(name.asUpper()), document(std::move(doc)), loading(inLoading)
	{
	}

	CachedMapDocument(CachedMapDocument &&) noexcept = default;
	CachedMapDocument &operator=(CachedMapDocument &&) noexcept = default;

	CachedMapDocument(const CachedMapDocument &) = delete;
	CachedMapDocument &operator=(const CachedMapDocument &) = delete;
};

// The active Instance document is not stored here.  A capacity of seven gives
// the project a strict eight-document bound while retaining one active map.
class MapDocumentCache
{
public:
	static constexpr size_t DEFAULT_CAPACITY = 7;

	explicit MapDocumentCache(size_t capacity = DEFAULT_CAPACITY) :
			capacity_(capacity)
	{
	}

	bool store(const SString &mapName, Document &&document,
			const LoadingData &loading);
	std::optional<CachedMapDocument> take(const SString &mapName);

	bool contains(const SString &mapName) const noexcept;
	bool isDirty(const SString &mapName) const noexcept;
	size_t dirtyCount() const noexcept;
	std::vector<SString> dirtyMapNames() const;
	std::vector<SString> mapNames() const;
	std::vector<CachedMapDocument *> dirtyDocuments() noexcept;
	std::vector<const CachedMapDocument *> dirtyDocuments() const noexcept;

	void erase(const SString &mapName) noexcept;
	void rename(const SString &oldName, const SString &newName) noexcept;
	void updateLoadingContext(const LoadingData &loading);
	void clear() noexcept
	{
		entries_.clear();
	}

	size_t size() const noexcept
	{
		return entries_.size();
	}

	size_t capacity() const noexcept
	{
		return capacity_;
	}

private:
	using Entries = std::list<CachedMapDocument>;
	Entries::iterator find(const SString &mapName) noexcept;
	Entries::const_iterator find(const SString &mapName) const noexcept;
	bool makeRoom(const SString &replacing);

	size_t capacity_;
	Entries entries_; // Most recently cached at the front.
};

#endif // HERESY_M_DOCUMENT_CACHE_H
