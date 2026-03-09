#include "three_mf_writer.h"

#include "chromaprint3d/error.h"

#include <spdlog/spdlog.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(CHROMAPRINT3D_HAS_ZLIB)
#    include <zlib.h>
#endif

namespace ChromaPrint3D::detail {

namespace {

constexpr uint32_t kLocalFileHeaderSignature   = 0x04034B50u;
constexpr uint32_t kCentralDirHeaderSignature  = 0x02014B50u;
constexpr uint32_t kEndOfCentralDirSignature   = 0x06054B50u;
constexpr uint16_t kZipVersionNeeded           = 20u;
constexpr uint16_t kZipVersionMadeBy           = 20u;
constexpr uint16_t kCompressionMethodStoreOnly = 0u;
constexpr uint16_t kCompressionMethodDeflate   = 8u;

struct CentralDirEntry {
    std::string file_name;
    uint16_t compression_method  = 0;
    uint32_t crc32               = 0;
    uint32_t compressed_size     = 0;
    uint32_t uncompressed_size   = 0;
    uint32_t local_header_offset = 0;
    uint16_t mod_time            = 0;
    uint16_t mod_date            = 0;
};

void AppendU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xffu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
}

void AppendU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xffu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xffu));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xffu));
}

std::pair<uint16_t, uint16_t> MakeDosTimestamp(bool deterministic) {
    if (deterministic) { return {0, 0}; }
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    int year = tm.tm_year + 1900;
    if (year < 1980) { year = 1980; }
    uint16_t dos_time = static_cast<uint16_t>(((tm.tm_hour & 0x1f) << 11) |
                                              ((tm.tm_min & 0x3f) << 5) | ((tm.tm_sec / 2) & 0x1f));
    uint16_t dos_date =
        static_cast<uint16_t>(((year - 1980) << 9) | ((tm.tm_mon + 1) << 5) | (tm.tm_mday & 0x1f));
    return {dos_time, dos_date};
}

const std::array<uint32_t, 256>& Crc32Table() {
    static const std::array<uint32_t, 256> table = []() {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j) { c = (c & 1u) ? (0xEDB88320u ^ (c >> 1u)) : (c >> 1u); }
            t[i] = c;
        }
        return t;
    }();
    return table;
}

uint32_t ComputeCrc32(const std::string& data) {
    uint32_t crc      = 0xFFFFFFFFu;
    const auto& table = Crc32Table();
    for (unsigned char ch : data) {
        crc = table[(crc ^ static_cast<uint32_t>(ch)) & 0xFFu] ^ (crc >> 8u);
    }
    return crc ^ 0xFFFFFFFFu;
}

void ValidatePartList(const std::vector<OpcPart>& parts) {
    if (parts.empty()) { throw InputError("No OPC parts to package"); }
    std::unordered_set<std::string> seen;
    seen.reserve(parts.size() * 2);
    for (const auto& part : parts) {
        if (part.path_in_zip.empty()) { throw InputError("OPC part path is empty"); }
        if (!seen.insert(part.path_in_zip).second) {
            throw InputError("Duplicate OPC part path: " + part.path_in_zip);
        }
    }
}

bool ShouldTryDeflate(const OpcPart& part, const ThreeMfExportOptions& options) {
#if defined(CHROMAPRINT3D_3MF_FORCE_STORE)
    (void)part;
    (void)options;
    return false;
#else
    switch (options.compression_mode) {
    case ThreeMfCompressionMode::StoreOnly:
        return false;
    case ThreeMfCompressionMode::Deflate:
        return true;
    case ThreeMfCompressionMode::AutoThreshold:
        return part.data.size() >= options.compression_threshold_bytes;
    }
    return false;
#endif
}

#if defined(CHROMAPRINT3D_HAS_ZLIB)
bool TryDeflateRaw(const std::string& input, int level, std::vector<uint8_t>& output) {
    if (input.empty()) {
        output.clear();
        return true;
    }
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<uInt>::max())) { return false; }

    z_stream stream{};
    const int rc_init = deflateInit2(&stream, level, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    if (rc_init != Z_OK) { return false; }

    uLong bound = compressBound(static_cast<uLong>(input.size()));
    if (bound > static_cast<uLong>(std::numeric_limits<uInt>::max())) {
        deflateEnd(&stream);
        return false;
    }
    output.assign(static_cast<std::size_t>(bound), 0u);

    stream.next_in   = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in  = static_cast<uInt>(input.size());
    stream.next_out  = output.data();
    stream.avail_out = static_cast<uInt>(output.size());

    const int rc_deflate = deflate(&stream, Z_FINISH);
    if (rc_deflate != Z_STREAM_END) {
        deflateEnd(&stream);
        return false;
    }
    if (deflateEnd(&stream) != Z_OK) { return false; }

    output.resize(static_cast<std::size_t>(stream.total_out));
    return true;
}
#endif

void WarnOnceNoZlibForCompression() {
#if !defined(CHROMAPRINT3D_HAS_ZLIB)
    static bool warned = false;
    if (!warned) {
        warned = true;
        spdlog::warn("3MF ZIP compression requested but zlib is unavailable, falling back to "
                     "store-only");
    }
#endif
}

void WarnOnceForceStoreEnabled() {
#if defined(CHROMAPRINT3D_3MF_FORCE_STORE)
    static bool warned = false;
    if (!warned) {
        warned = true;
        spdlog::warn("3MF ZIP compression is disabled by CHROMAPRINT3D_3MF_FORCE_STORE, using "
                     "store-only");
    }
#endif
}

} // namespace

std::vector<uint8_t> BuildZipPackage(const std::vector<OpcPart>& parts,
                                     const ThreeMfExportOptions& options) {
    ValidatePartList(parts);
    std::vector<uint8_t> zip_bytes;
    std::vector<CentralDirEntry> central_entries;
    central_entries.reserve(parts.size());

    auto [dos_time, dos_date] = MakeDosTimestamp(options.deterministic);
    WarnOnceForceStoreEnabled();

    for (const auto& part : parts) {
        if (part.path_in_zip.size() >
            static_cast<std::size_t>(std::numeric_limits<uint16_t>::max())) {
            throw InputError("ZIP entry file name too long: " + part.path_in_zip);
        }
        if (part.data.size() > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())) {
            throw InputError("ZIP entry too large: " + part.path_in_zip);
        }
        if (zip_bytes.size() > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())) {
            throw InputError("ZIP offset exceeds ZIP32 limit");
        }

        uint16_t compression_method = kCompressionMethodStoreOnly;
        const uint8_t* payload_data = reinterpret_cast<const uint8_t*>(part.data.data());
        std::size_t payload_size    = part.data.size();
        std::vector<uint8_t> deflated_payload;
        const bool try_deflate            = ShouldTryDeflate(part, options);
        bool deflate_attempted_and_failed = false;

        if (try_deflate) {
#if defined(CHROMAPRINT3D_HAS_ZLIB)
            if (TryDeflateRaw(part.data, options.deflate_level, deflated_payload) &&
                deflated_payload.size() < part.data.size()) {
                compression_method = kCompressionMethodDeflate;
                payload_data       = deflated_payload.data();
                payload_size       = deflated_payload.size();
            } else {
                deflate_attempted_and_failed = true;
            }
#else
            WarnOnceNoZlibForCompression();
            deflate_attempted_and_failed = true;
#endif
        }

        if (deflate_attempted_and_failed) {
            spdlog::debug("3MF ZIP entry '{}' falls back to store-only", part.path_in_zip);
        }

        if (payload_size > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())) {
            throw InputError("ZIP payload exceeds ZIP32 limit: " + part.path_in_zip);
        }

        CentralDirEntry entry;
        entry.compression_method  = compression_method;
        entry.file_name           = part.path_in_zip;
        entry.crc32               = ComputeCrc32(part.data);
        entry.uncompressed_size   = static_cast<uint32_t>(part.data.size());
        entry.compressed_size     = static_cast<uint32_t>(payload_size);
        entry.local_header_offset = static_cast<uint32_t>(zip_bytes.size());
        entry.mod_time            = dos_time;
        entry.mod_date            = dos_date;

        AppendU32(zip_bytes, kLocalFileHeaderSignature);
        AppendU16(zip_bytes, kZipVersionNeeded);
        AppendU16(zip_bytes, 0u); // general purpose bit flag
        AppendU16(zip_bytes, entry.compression_method);
        AppendU16(zip_bytes, entry.mod_time);
        AppendU16(zip_bytes, entry.mod_date);
        AppendU32(zip_bytes, entry.crc32);
        AppendU32(zip_bytes, entry.compressed_size);
        AppendU32(zip_bytes, entry.uncompressed_size);
        AppendU16(zip_bytes, static_cast<uint16_t>(entry.file_name.size()));
        AppendU16(zip_bytes, 0u); // extra length
        zip_bytes.insert(zip_bytes.end(), entry.file_name.begin(), entry.file_name.end());
        zip_bytes.insert(zip_bytes.end(), payload_data, payload_data + payload_size);

        central_entries.push_back(std::move(entry));
    }

    if (zip_bytes.size() > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())) {
        throw InputError("ZIP central directory offset exceeds ZIP32 limit");
    }
    uint32_t central_dir_offset = static_cast<uint32_t>(zip_bytes.size());

    for (const auto& entry : central_entries) {
        AppendU32(zip_bytes, kCentralDirHeaderSignature);
        AppendU16(zip_bytes, kZipVersionMadeBy);
        AppendU16(zip_bytes, kZipVersionNeeded);
        AppendU16(zip_bytes, 0u); // general purpose bit flag
        AppendU16(zip_bytes, entry.compression_method);
        AppendU16(zip_bytes, entry.mod_time);
        AppendU16(zip_bytes, entry.mod_date);
        AppendU32(zip_bytes, entry.crc32);
        AppendU32(zip_bytes, entry.compressed_size);
        AppendU32(zip_bytes, entry.uncompressed_size);
        AppendU16(zip_bytes, static_cast<uint16_t>(entry.file_name.size()));
        AppendU16(zip_bytes, 0u); // extra length
        AppendU16(zip_bytes, 0u); // comment length
        AppendU16(zip_bytes, 0u); // disk number start
        AppendU16(zip_bytes, 0u); // internal attrs
        AppendU32(zip_bytes, 0u); // external attrs
        AppendU32(zip_bytes, entry.local_header_offset);
        zip_bytes.insert(zip_bytes.end(), entry.file_name.begin(), entry.file_name.end());
    }

    if (zip_bytes.size() > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())) {
        throw InputError("ZIP end record offset exceeds ZIP32 limit");
    }
    uint32_t central_dir_size = static_cast<uint32_t>(zip_bytes.size()) - central_dir_offset;
    if (central_entries.size() > static_cast<std::size_t>(std::numeric_limits<uint16_t>::max())) {
        throw InputError("ZIP entry count exceeds ZIP32 limit");
    }

    AppendU32(zip_bytes, kEndOfCentralDirSignature);
    AppendU16(zip_bytes, 0u); // disk number
    AppendU16(zip_bytes, 0u); // start disk number
    AppendU16(zip_bytes, static_cast<uint16_t>(central_entries.size()));
    AppendU16(zip_bytes, static_cast<uint16_t>(central_entries.size()));
    AppendU32(zip_bytes, central_dir_size);
    AppendU32(zip_bytes, central_dir_offset);
    AppendU16(zip_bytes, 0u); // comment length

    return zip_bytes;
}

// ---------------------------------------------------------------------------
// IZipSink — abstract output target for StreamingZipWriter
// ---------------------------------------------------------------------------

namespace {

class IZipSink {
public:
    virtual ~IZipSink()                                                         = default;
    virtual void Append(const void* data, std::size_t len)                      = 0;
    virtual void WriteAt(std::size_t offset, const void* data, std::size_t len) = 0;
    virtual std::size_t Size() const                                            = 0;
};

class BufferZipSink final : public IZipSink {
public:
    explicit BufferZipSink(std::vector<uint8_t>& buf) : buf_(buf) {}

    void Append(const void* data, std::size_t len) override {
        auto* p = static_cast<const uint8_t*>(data);
        buf_.insert(buf_.end(), p, p + len);
    }

    void WriteAt(std::size_t offset, const void* data, std::size_t len) override {
        std::memcpy(buf_.data() + offset, data, len);
    }

    std::size_t Size() const override { return buf_.size(); }

private:
    std::vector<uint8_t>& buf_;
};

class FileZipSink final : public IZipSink {
public:
    explicit FileZipSink(const std::string& path) : ofs_(path, std::ios::binary | std::ios::trunc) {
        if (!ofs_.is_open()) { throw IOError("Cannot open file for writing: " + path); }
    }

    void Append(const void* data, std::size_t len) override {
        ofs_.write(static_cast<const char*>(data), static_cast<std::streamsize>(len));
        if (!ofs_.good()) { throw IOError("Failed to write to 3MF file"); }
        size_ += len;
    }

    void WriteAt(std::size_t offset, const void* data, std::size_t len) override {
        ofs_.seekp(static_cast<std::streamoff>(offset));
        ofs_.write(static_cast<const char*>(data), static_cast<std::streamsize>(len));
        ofs_.seekp(0, std::ios::end);
        if (!ofs_.good()) { throw IOError("Failed to patch 3MF file header"); }
    }

    std::size_t Size() const override { return size_; }

private:
    std::ofstream ofs_;
    std::size_t size_ = 0;
};

} // namespace

// ---------------------------------------------------------------------------
// StreamingZipWriter
// ---------------------------------------------------------------------------

struct StreamingZipWriter::Impl {
    std::unique_ptr<IZipSink> sink;
    ThreeMfExportOptions options;
    std::vector<CentralDirEntry> central_entries;
    uint16_t dos_time = 0;
    uint16_t dos_date = 0;

    bool in_stream_entry     = false;
    bool stream_is_deflating = false;
    std::string stream_filename;
    std::size_t stream_data_start   = 0;
    uint32_t stream_crc             = 0xFFFFFFFFu;
    std::size_t stream_uncompressed = 0;

#if defined(CHROMAPRINT3D_HAS_ZLIB)
    z_stream zstream{};
    bool zstream_active = false;
#endif

    Impl(std::unique_ptr<IZipSink> s, ThreeMfExportOptions opts)
        : sink(std::move(s)), options(std::move(opts)) {
        auto [t, d] = MakeDosTimestamp(options.deterministic);
        dos_time    = t;
        dos_date    = d;
    }

    ~Impl() {
#if defined(CHROMAPRINT3D_HAS_ZLIB)
        if (zstream_active) { deflateEnd(&zstream); }
#endif
    }

    void SinkAppendU16(uint16_t v) {
        uint8_t b[2] = {static_cast<uint8_t>(v & 0xffu), static_cast<uint8_t>((v >> 8) & 0xffu)};
        sink->Append(b, 2);
    }

    void SinkAppendU32(uint32_t v) {
        uint8_t b[4] = {
            static_cast<uint8_t>(v & 0xffu),
            static_cast<uint8_t>((v >> 8) & 0xffu),
            static_cast<uint8_t>((v >> 16) & 0xffu),
            static_cast<uint8_t>((v >> 24) & 0xffu),
        };
        sink->Append(b, 4);
    }

    void SinkAppendBytes(const void* data, std::size_t len) { sink->Append(data, len); }

    void SinkWriteU32At(std::size_t offset, uint32_t v) {
        uint8_t b[4] = {
            static_cast<uint8_t>(v & 0xffu),
            static_cast<uint8_t>((v >> 8) & 0xffu),
            static_cast<uint8_t>((v >> 16) & 0xffu),
            static_cast<uint8_t>((v >> 24) & 0xffu),
        };
        sink->WriteAt(offset, b, 4);
    }

    void UpdateCrc(const void* data, std::size_t len) {
        const auto* p     = static_cast<const uint8_t*>(data);
        const auto& table = Crc32Table();
        for (std::size_t i = 0; i < len; ++i) {
            stream_crc =
                table[(stream_crc ^ static_cast<uint32_t>(p[i])) & 0xFFu] ^ (stream_crc >> 8u);
        }
    }

    void FlushDeflate(int flush) {
#if defined(CHROMAPRINT3D_HAS_ZLIB)
        uint8_t buf[16384];
        int rc;
        do {
            zstream.next_out  = buf;
            zstream.avail_out = sizeof(buf);
            rc                = deflate(&zstream, flush);
            if (rc == Z_STREAM_ERROR) { throw IOError("zlib deflate stream error"); }
            std::size_t have = sizeof(buf) - zstream.avail_out;
            if (have > 0) { sink->Append(buf, have); }
        } while (zstream.avail_out == 0);
#else
        (void)flush;
#endif
    }
};

StreamingZipWriter::StreamingZipWriter(std::vector<uint8_t>& output,
                                       const ThreeMfExportOptions& options)
    : impl_(std::make_unique<Impl>(std::make_unique<BufferZipSink>(output), options)) {}

StreamingZipWriter::StreamingZipWriter(const std::string& file_path,
                                       const ThreeMfExportOptions& options)
    : impl_(std::make_unique<Impl>(std::make_unique<FileZipSink>(file_path), options)) {}

StreamingZipWriter::~StreamingZipWriter() = default;

void StreamingZipWriter::WriteWholeEntry(const std::string& path, const std::string& data) {
    if (impl_->in_stream_entry) { throw InputError("Cannot write whole entry while streaming"); }

    if (path.size() > static_cast<std::size_t>(std::numeric_limits<uint16_t>::max())) {
        throw InputError("ZIP entry file name too long: " + path);
    }
    if (data.size() > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())) {
        throw InputError("ZIP entry too large: " + path);
    }

    uint16_t compression_method = kCompressionMethodStoreOnly;
    const uint8_t* payload_data = reinterpret_cast<const uint8_t*>(data.data());
    std::size_t payload_size    = data.size();
    std::vector<uint8_t> deflated;

    bool want_deflate = false;
    if (impl_->options.compression_mode == ThreeMfCompressionMode::Deflate) {
        want_deflate = true;
    } else if (impl_->options.compression_mode == ThreeMfCompressionMode::AutoThreshold) {
        want_deflate = data.size() >= impl_->options.compression_threshold_bytes;
    }

#if defined(CHROMAPRINT3D_HAS_ZLIB)
    if (want_deflate && !data.empty()) {
        if (TryDeflateRaw(data, impl_->options.deflate_level, deflated) &&
            deflated.size() < data.size()) {
            compression_method = kCompressionMethodDeflate;
            payload_data       = deflated.data();
            payload_size       = deflated.size();
        }
    }
#else
    (void)want_deflate;
#endif

    CentralDirEntry entry;
    entry.file_name           = path;
    entry.compression_method  = compression_method;
    entry.crc32               = ComputeCrc32(data);
    entry.uncompressed_size   = static_cast<uint32_t>(data.size());
    entry.compressed_size     = static_cast<uint32_t>(payload_size);
    entry.local_header_offset = static_cast<uint32_t>(impl_->sink->Size());
    entry.mod_time            = impl_->dos_time;
    entry.mod_date            = impl_->dos_date;

    impl_->SinkAppendU32(kLocalFileHeaderSignature);
    impl_->SinkAppendU16(kZipVersionNeeded);
    impl_->SinkAppendU16(0u);
    impl_->SinkAppendU16(entry.compression_method);
    impl_->SinkAppendU16(entry.mod_time);
    impl_->SinkAppendU16(entry.mod_date);
    impl_->SinkAppendU32(entry.crc32);
    impl_->SinkAppendU32(entry.compressed_size);
    impl_->SinkAppendU32(entry.uncompressed_size);
    impl_->SinkAppendU16(static_cast<uint16_t>(entry.file_name.size()));
    impl_->SinkAppendU16(0u);
    impl_->SinkAppendBytes(entry.file_name.data(), entry.file_name.size());
    impl_->SinkAppendBytes(payload_data, payload_size);

    impl_->central_entries.push_back(std::move(entry));
}

void StreamingZipWriter::BeginDeflateEntry(const std::string& path) {
    if (impl_->in_stream_entry) { throw InputError("Already in a streaming entry"); }
    impl_->in_stream_entry     = true;
    impl_->stream_is_deflating = false;
    impl_->stream_filename     = path;
    impl_->stream_crc          = 0xFFFFFFFFu;
    impl_->stream_uncompressed = 0;

    // Try to init zlib deflate; fall back to store-only if it fails.
    uint16_t compression_method = kCompressionMethodStoreOnly;
#if defined(CHROMAPRINT3D_HAS_ZLIB) && !defined(CHROMAPRINT3D_3MF_FORCE_STORE)
    impl_->zstream = z_stream{};
    int rc = deflateInit2(&impl_->zstream, impl_->options.deflate_level, Z_DEFLATED, -MAX_WBITS, 8,
                          Z_DEFAULT_STRATEGY);
    if (rc == Z_OK) {
        impl_->zstream_active      = true;
        impl_->stream_is_deflating = true;
        compression_method         = kCompressionMethodDeflate;
    } else {
        spdlog::debug("Streaming entry '{}' deflateInit2 failed (rc={}), using store", path, rc);
    }
#endif

    impl_->SinkAppendU32(kLocalFileHeaderSignature);
    impl_->SinkAppendU16(kZipVersionNeeded);
    impl_->SinkAppendU16(0u); // general purpose bit flag
    impl_->SinkAppendU16(compression_method);
    impl_->SinkAppendU16(impl_->dos_time);
    impl_->SinkAppendU16(impl_->dos_date);
    impl_->SinkAppendU32(0u); // crc32 placeholder  (backpatched in EndEntry)
    impl_->SinkAppendU32(0u); // compressed size placeholder
    impl_->SinkAppendU32(0u); // uncompressed size placeholder
    impl_->SinkAppendU16(static_cast<uint16_t>(path.size()));
    impl_->SinkAppendU16(0u);
    impl_->SinkAppendBytes(path.data(), path.size());

    impl_->stream_data_start = impl_->sink->Size();
}

void StreamingZipWriter::WriteChunk(const void* data, std::size_t len) {
    if (!impl_->in_stream_entry) { throw InputError("Not in a streaming entry"); }
    if (len == 0) { return; }

    impl_->UpdateCrc(data, len);
    impl_->stream_uncompressed += len;

#if defined(CHROMAPRINT3D_HAS_ZLIB)
    if (impl_->stream_is_deflating) {
        impl_->zstream.next_in  = static_cast<Bytef*>(const_cast<void*>(data));
        impl_->zstream.avail_in = static_cast<uInt>(len);
        impl_->FlushDeflate(Z_NO_FLUSH);
        return;
    }
#endif
    impl_->SinkAppendBytes(data, len);
}

void StreamingZipWriter::EndEntry() {
    if (!impl_->in_stream_entry) { throw InputError("Not in a streaming entry"); }

    uint16_t compression_method = kCompressionMethodStoreOnly;

#if defined(CHROMAPRINT3D_HAS_ZLIB)
    if (impl_->stream_is_deflating) {
        impl_->zstream.next_in  = nullptr;
        impl_->zstream.avail_in = 0;
        impl_->FlushDeflate(Z_FINISH);
        deflateEnd(&impl_->zstream);
        impl_->zstream_active = false;
        compression_method    = kCompressionMethodDeflate;
    }
#endif

    uint32_t final_crc = impl_->stream_crc ^ 0xFFFFFFFFu;
    uint32_t compressed_size =
        static_cast<uint32_t>(impl_->sink->Size() - impl_->stream_data_start);
    uint32_t uncompressed_size = static_cast<uint32_t>(impl_->stream_uncompressed);

    // Backpatch the local file header with actual CRC and sizes.
    std::size_t lh_start = impl_->stream_data_start - 30 - impl_->stream_filename.size();
    impl_->SinkWriteU32At(lh_start + 14, final_crc);
    impl_->SinkWriteU32At(lh_start + 18, compressed_size);
    impl_->SinkWriteU32At(lh_start + 22, uncompressed_size);

    CentralDirEntry entry;
    entry.file_name           = impl_->stream_filename;
    entry.compression_method  = compression_method;
    entry.crc32               = final_crc;
    entry.compressed_size     = compressed_size;
    entry.uncompressed_size   = uncompressed_size;
    entry.local_header_offset = static_cast<uint32_t>(lh_start);
    entry.mod_time            = impl_->dos_time;
    entry.mod_date            = impl_->dos_date;
    impl_->central_entries.push_back(std::move(entry));

    impl_->in_stream_entry     = false;
    impl_->stream_is_deflating = false;
}

void StreamingZipWriter::Finalize() {
    if (impl_->in_stream_entry) { throw InputError("Cannot finalize while streaming an entry"); }

    uint32_t central_dir_offset = static_cast<uint32_t>(impl_->sink->Size());

    for (const auto& entry : impl_->central_entries) {
        impl_->SinkAppendU32(kCentralDirHeaderSignature);
        impl_->SinkAppendU16(kZipVersionMadeBy);
        impl_->SinkAppendU16(kZipVersionNeeded);
        impl_->SinkAppendU16(0u); // general purpose bit flag
        impl_->SinkAppendU16(entry.compression_method);
        impl_->SinkAppendU16(entry.mod_time);
        impl_->SinkAppendU16(entry.mod_date);
        impl_->SinkAppendU32(entry.crc32);
        impl_->SinkAppendU32(entry.compressed_size);
        impl_->SinkAppendU32(entry.uncompressed_size);
        impl_->SinkAppendU16(static_cast<uint16_t>(entry.file_name.size()));
        impl_->SinkAppendU16(0u); // extra length
        impl_->SinkAppendU16(0u); // comment length
        impl_->SinkAppendU16(0u); // disk number start
        impl_->SinkAppendU16(0u); // internal attrs
        impl_->SinkAppendU32(0u); // external attrs
        impl_->SinkAppendU32(entry.local_header_offset);
        impl_->SinkAppendBytes(entry.file_name.data(), entry.file_name.size());
    }

    uint32_t central_dir_size = static_cast<uint32_t>(impl_->sink->Size()) - central_dir_offset;

    impl_->SinkAppendU32(kEndOfCentralDirSignature);
    impl_->SinkAppendU16(0u); // disk number
    impl_->SinkAppendU16(0u); // start disk number
    impl_->SinkAppendU16(static_cast<uint16_t>(impl_->central_entries.size()));
    impl_->SinkAppendU16(static_cast<uint16_t>(impl_->central_entries.size()));
    impl_->SinkAppendU32(central_dir_size);
    impl_->SinkAppendU32(central_dir_offset);
    impl_->SinkAppendU16(0u); // comment length
}

} // namespace ChromaPrint3D::detail
