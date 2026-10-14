#include "nrsc/signatures/signature_table.hpp"

#include <stdexcept>

namespace nrsc::signatures {

// Table builder FUN_1800655a0 (0x1800655a0, tail jump 0x180069638 into FUN_18006f570; first calls FUN_18006b830),
// grade C for the entry data and order (JSON exec_seq), S for the base/size source.
SignatureTable SignatureTable::build(const ModuleImage& image) {
    SignatureTable table;
    SignatureResolver resolver(image);
    auto slot = [&](unsigned off) -> std::uint64_t& {
        if (off < kFirstSlotOffset || off > kLastSlotOffset || off % 8) throw std::logic_error("bad table offset");
        return table.slots_[(off - kFirstSlotOffset) / 8];
    };
    for (const SignatureEntry& e : kSignatureEntries) {  // already in execution order
        switch (e.kind) {
            case EntryKind::Find1:
            case EntryKind::Inline:
                slot(e.slots[0]) = resolver.resolve(e.text, e.offset, e.mode);
                break;
            case EntryKind::FindAll: {
                std::array<std::uint64_t, 4> values{};  // slots keep 0 when there is no match
                resolver.find_all(e.text, e.offset, std::span<std::uint64_t>(values.data(), e.slot_count), e.fatal_line);
                for (unsigned i = 0; i < e.slot_count; ++i) slot(e.slots[i]) = values[i];
                break;
            }
            case EntryKind::Rtti:
                slot(e.slots[0]) = resolver.find_vtable(e.text);
                break;
        }
    }
    table.counter_ = resolver.lookups_succeeded();
    return table;
}

std::uint64_t SignatureTable::at(unsigned table_offset) const {
    if (table_offset < kFirstSlotOffset || table_offset > kLastSlotOffset || table_offset % 8) return 0;
    return slots_[(table_offset - kFirstSlotOffset) / 8];
}

}  // namespace nrsc::signatures
