#include "sim/content_blob.hpp"

#include <cstring>

#include "core/bitstream.hpp"
#include "core/hash.hpp"

namespace sim {
namespace {

constexpr char kMagic[4] = {'G', 'C', 'N', 'T'};

/// Header is magic + version + count; the record is 125 bits today. Both are
/// upper bounds used only to size the write buffer, which is trimmed to what was
/// actually written — so generous slack costs nothing and survives a new field.
constexpr std::size_t kHeaderBytes = 8U;
constexpr std::size_t kMaxRecordBytes = 24U;

/// Bit widths. Named because the reader and the writer must agree exactly, and a
/// mismatch here is the kind of bug that shows up as one wrong field ten records
/// later rather than as a parse failure.
constexpr int kIdBits = 16;
constexpr int kFlagBits = 32;
constexpr int kWeightBits = 16;
constexpr int kStackBits = 8;
constexpr int kSlotBits = 3;  ///< kEquipSlotCount == 8
constexpr int kRangeBits = 8;
constexpr int kEffectBits = 8;

constexpr std::int32_t kStatMin = -32768;
constexpr std::int32_t kStatMax = 32767;

}  // namespace

std::vector<std::uint8_t> write_content_blob(const ItemTypeRegistry& registry) {
    const std::vector<ItemTypeId> ids = registry.ids();

    std::vector<std::uint8_t> buffer(kHeaderBytes +
                                     ids.size() * kMaxRecordBytes);
    core::BitWriter writer(buffer.data(), buffer.size());

    writer.write_bytes(kMagic, sizeof kMagic);
    writer.write_bits(kContentVersion, 16);
    writer.write_bits(static_cast<std::uint32_t>(ids.size()), 16);

    for (const ItemTypeId id : ids) {
        const ItemType& type = registry.get(id);
        writer.write_bits(type.id, kIdBits);
        writer.write_bits(type.flags.bits(), kFlagBits);
        writer.write_bits(type.weight, kWeightBits);
        writer.write_bits(type.max_stack, kStackBits);

        writer.write_bool(type.equippable);
        writer.write_bits(static_cast<std::uint32_t>(type.slot), kSlotBits);
        writer.write_ranged(type.attack, kStatMin, kStatMax);
        writer.write_ranged(type.defense, kStatMin, kStatMax);
        writer.write_bool(type.attack_kind == AttackKind::Ranged);
        writer.write_bits(type.attack_range, kRangeBits);
        writer.write_bits(type.effect, kEffectBits);
    }

    writer.flush();
    // Checked once, at the end: BitWriter overflow is sticky precisely so this is
    // the only place it has to be looked at.
    if (writer.overflowed()) {
        return {};
    }
    buffer.resize(writer.bytes_written());
    return buffer;
}

bool read_content_blob(const std::uint8_t* data, std::size_t size,
                       ItemTypeRegistry& out) {
    if (data == nullptr) {
        return false;
    }
    core::BitReader reader(data, size);

    char magic[sizeof kMagic] = {};
    reader.read_bytes(magic, sizeof magic);
    if (std::memcmp(magic, kMagic, sizeof kMagic) != 0) {
        return false;
    }

    const auto version = static_cast<std::uint16_t>(reader.read_bits(16));
    if (version != kContentVersion) {
        return false;
    }

    const std::uint32_t count = reader.read_bits(16);

    // Filled locally and only moved into `out` once everything validated, so a
    // truncated blob cannot leave the caller with half a catalogue.
    ItemTypeRegistry parsed;
    for (std::uint32_t i = 0; i < count; ++i) {
        ItemType type;
        type.id = static_cast<ItemTypeId>(reader.read_bits(kIdBits));
        type.flags = ItemFlags{reader.read_bits(kFlagBits)};
        type.weight = static_cast<std::uint16_t>(reader.read_bits(kWeightBits));
        type.max_stack =
            static_cast<std::uint8_t>(reader.read_bits(kStackBits));

        type.equippable = reader.read_bool();
        type.slot = static_cast<EquipSlot>(reader.read_bits(kSlotBits));
        type.attack =
            static_cast<std::int16_t>(reader.read_ranged(kStatMin, kStatMax));
        type.defense =
            static_cast<std::int16_t>(reader.read_ranged(kStatMin, kStatMax));
        type.attack_kind =
            reader.read_bool() ? AttackKind::Ranged : AttackKind::Melee;
        type.attack_range =
            static_cast<std::uint8_t>(reader.read_bits(kRangeBits));
        type.effect = static_cast<std::uint8_t>(reader.read_bits(kEffectBits));

        // kItemNone means "nothing here" in a tile slot, so it can never be a
        // registered type; ItemTypeRegistry::add asserts on it. A blob claiming
        // otherwise is corrupt, not merely odd.
        if (type.id == kItemNone) {
            return false;
        }
        parsed.add(type);
    }

    if (reader.overflowed()) {
        return false;
    }

    out = std::move(parsed);
    return true;
}

std::uint64_t content_hash(const ItemTypeRegistry& registry) {
    // Hashing the serialised form rather than the in-memory struct is deliberate:
    // padding bytes in ItemType are unspecified, so hashing the struct would give
    // two builds different answers for identical content.
    const std::vector<std::uint8_t> blob = write_content_blob(registry);
    if (blob.empty()) {
        return 0U;
    }
    return core::fnv1a_64(blob.data(), blob.size());
}

}  // namespace sim
