// IVector<T> / IIterable<T> / IIterator<T> for the three collections the
// layout types expose: Panel.Children, Grid.ColumnDefinitions and
// Grid.RowDefinitions.
//
// One template serves all three. The IIDs differ per specialization -- they
// are computed from the type signature rather than declared -- so they are
// passed in rather than deduced, and come from the harvested table.

#ifndef OPENXAML_COLLECTION_H
#define OPENXAML_COLLECTION_H

#include <vector>

#include "com.h"

namespace openxaml::winrt {

// What a collection does with an item beyond holding a reference to it.
// Children need their layout object exposed to the parent panel; definitions
// do not, so the hook is a no-op for them.
template <class ItemAbi>
struct CollectionTraits;

template <class VectorAbi, class IterableAbi, class IteratorAbi, class ItemAbi,
          class ViewAbi>
class Vector;

// The iterator is a separate object with its own identity, as WinRT requires.
// It holds a reference to the vector, so an iterator outliving the collection
// it came from is safe rather than dangling.
template <class VectorAbi, class IterableAbi, class IteratorAbi, class ItemAbi,
          class ViewAbi>
class VectorIterator final : public ComObject, public IteratorAbi {
public:
    using Owner = Vector<VectorAbi, IterableAbi, IteratorAbi, ItemAbi, ViewAbi>;

    VectorIterator(Owner* owner, const GUID& iid, const wchar_t* name)
        : owner_(owner), iid_(iid), name_(name) {
        owner_->AddRef();
    }
    ~VectorIterator() override { owner_->Release(); }

    const wchar_t* RuntimeClassName() const override { return name_; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(iid_, IteratorAbi)
        OPENXAML_QI_ARM(IID_IUnknown, IteratorAbi)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, IteratorAbi)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Current(ItemAbi** current) override {
        if (!current) return E_POINTER;
        return owner_->GetAt(position_, current);
    }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* has) override {
        if (!has) return E_POINTER;
        *has = position_ < owner_->Count() ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* has) override {
        if (!has) return E_POINTER;
        if (position_ < owner_->Count()) ++position_;
        *has = position_ < owner_->Count() ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMany(unsigned, ItemAbi**, unsigned*) override {
        return E_NOTIMPL;
    }

private:
    Owner* owner_;
    const GUID& iid_;
    const wchar_t* name_;
    unsigned position_ = 0;
};

template <class VectorAbi, class IterableAbi, class IteratorAbi, class ItemAbi,
          class ViewAbi>
class Vector final : public ComObject, public VectorAbi, public IterableAbi {
public:
    using Traits = CollectionTraits<ItemAbi>;
    using Iterator = VectorIterator<VectorAbi, IterableAbi, IteratorAbi, ItemAbi, ViewAbi>;

    struct Iids {
        const GUID& vector;
        const GUID& iterable;
        const GUID& iterator;
    };

    // `owner` is the object this collection is a member of. Its lifetime, not
    // the collection's own count, decides when the memory goes away.
    Vector(const Iids& iids, const wchar_t* name, ComObject* owner = nullptr)
        : iids_(iids), name_(name), owner_(owner) {}
    ~Vector() override {
        for (auto& entry : entries_) entry.item->Release();
    }

    ULONG Retain() override { return owner_ ? owner_->Retain() : ComObject::Retain(); }
    ULONG ReleaseOne() override {
        return owner_ ? owner_->ReleaseOne() : ComObject::ReleaseOne();
    }

    const wchar_t* RuntimeClassName() const override { return name_; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(iids_.vector, VectorAbi)
        OPENXAML_QI_ARM(iids_.iterable, IterableAbi)
        OPENXAML_QI_ARM(IID_IUnknown, VectorAbi)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, VectorAbi)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    // --- IIterable ---
    HRESULT STDMETHODCALLTYPE First(IteratorAbi** iterator) override {
        if (!iterator) return E_POINTER;
        *iterator = new Iterator(this, iids_.iterator, name_);
        return S_OK;
    }

    // --- IVector ---
    HRESULT STDMETHODCALLTYPE GetAt(unsigned index, ItemAbi** item) override {
        if (!item) return E_POINTER;
        if (index >= entries_.size()) return E_BOUNDS;
        entries_[index].item->AddRef();
        *item = entries_[index].item;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Size(unsigned* size) override {
        if (!size) return E_POINTER;
        *size = static_cast<unsigned>(entries_.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IndexOf(ItemAbi* value, unsigned* index, boolean* found) override {
        if (!index || !found) return E_POINTER;
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].item == value) {
                *index = static_cast<unsigned>(i);
                *found = 1;
                return S_OK;
            }
        }
        *index = 0;
        *found = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Append(ItemAbi* item) override {
        Entry entry;
        const HRESULT hr = Adopt(item, &entry);
        if (FAILED(hr)) return hr;
        entries_.push_back(entry);
        Rebuild();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE InsertAt(unsigned index, ItemAbi* item) override {
        if (index > entries_.size()) return E_BOUNDS;
        Entry entry;
        const HRESULT hr = Adopt(item, &entry);
        if (FAILED(hr)) return hr;
        entries_.insert(entries_.begin() + index, entry);
        Rebuild();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetAt(unsigned index, ItemAbi* item) override {
        if (index >= entries_.size()) return E_BOUNDS;
        Entry entry;
        const HRESULT hr = Adopt(item, &entry);
        if (FAILED(hr)) return hr;
        entries_[index].item->Release();
        entries_[index] = entry;
        Rebuild();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RemoveAt(unsigned index) override {
        if (index >= entries_.size()) return E_BOUNDS;
        entries_[index].item->Release();
        entries_.erase(entries_.begin() + index);
        Rebuild();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RemoveAtEnd() override {
        if (entries_.empty()) return E_BOUNDS;
        entries_.back().item->Release();
        entries_.pop_back();
        Rebuild();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clear() override {
        for (auto& entry : entries_) entry.item->Release();
        entries_.clear();
        Rebuild();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetView(ViewAbi**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMany(unsigned, unsigned, ItemAbi**, unsigned*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE ReplaceAll(unsigned, ItemAbi**) override { return E_NOTIMPL; }

    unsigned Count() const { return static_cast<unsigned>(entries_.size()); }

    // What the owning element hands to the layout core. Kept as a stable
    // vector rather than rebuilt per call, because layout asks for it inside
    // its inner loops.
    const std::vector<typename Traits::Projected>& Projected() const { return projected_; }

private:
    struct Entry {
        ItemAbi* item;
        typename Traits::Projected projected;
    };

    // A collection can only hold objects this DLL created: laying out an
    // element means owning its layout state, and a foreign implementation of
    // the same interface has none we can reach. Saying so with E_INVALIDARG
    // beats accepting it and silently leaving it out of the layout.
    HRESULT Adopt(ItemAbi* item, Entry* entry) {
        if (!item) return E_INVALIDARG;
        const HRESULT hr = Traits::Project(item, &entry->projected);
        if (FAILED(hr)) return hr;
        item->AddRef();
        entry->item = item;
        return S_OK;
    }

    void Rebuild() {
        projected_.clear();
        projected_.reserve(entries_.size());
        for (const auto& entry : entries_) projected_.push_back(entry.projected);
    }

    Iids iids_;
    const wchar_t* name_;
    ComObject* owner_;
    std::vector<Entry> entries_;
    std::vector<typename Traits::Projected> projected_;
};

}  // namespace openxaml::winrt

#endif  // OPENXAML_COLLECTION_H
