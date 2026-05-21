/*
    Minimal coded-file demo for the Leopard-RS C API.

    Encoded file layout used by this demo:

        [32-byte header]
        [k padded original data blocks]
        [p parity/recovery blocks]

    Decode-side erasures are represented exactly the way Leopard expects them:
    set original_data[i] to NULL for an erased data block and recovery_data[j]
    to NULL for an erased parity block.
*/

#include "../leopard.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
    #include <malloc.h>
#endif

namespace {

const uint64_t kDefaultBlockBytes = 1024 * 1024;
const size_t kAlignmentBytes = 32;

struct Header
{
    char magic[8];
    uint64_t original_bytes;
    uint64_t block_bytes;
    uint32_t original_count;
    uint32_t recovery_count;
};

static_assert(sizeof(Header) == 32, "Unexpected demo header size");

const char kMagic[8] = { 'L', 'E', 'O', 'C', 'O', 'D', 'E', '1' };

void PrintUsage(const char* program)
{
    std::cerr
        << "Usage:\n"
        << "  " << program << " encode <input-file> <coded-file> <parity-blocks> [block-bytes]\n"
        << "  " << program << " decode <coded-file> <output-file> [--erase-data 0,2] [--erase-parity 1]\n"
        << "\n"
        << "Notes:\n"
        << "  block-bytes defaults to 1048576 and must be a multiple of 64.\n"
        << "  parity-blocks must be between 1 and k, where k is the number of data blocks.\n";
}

bool ParseUnsigned(const std::string& text, unsigned* value)
{
    if (text.empty())
        return false;

    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (errno != 0 || *end != '\0' || parsed > 0xffffffffUL)
        return false;

    *value = static_cast<unsigned>(parsed);
    return true;
}

bool ParseU64(const std::string& text, uint64_t* value)
{
    if (text.empty())
        return false;

    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
    if (errno != 0 || *end != '\0')
        return false;

    *value = static_cast<uint64_t>(parsed);
    return true;
}

bool ParseIndexList(const std::string& text, std::set<unsigned>* indexes)
{
    indexes->clear();
    if (text.empty())
        return true;

    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ','))
    {
        unsigned value = 0;
        if (!ParseUnsigned(item, &value))
            return false;
        indexes->insert(value);
    }

    return true;
}

bool Contains(const std::set<unsigned>& indexes, unsigned value)
{
    return indexes.find(value) != indexes.end();
}

uint8_t* AllocateBlock(uint64_t bytes)
{
    void* ptr = nullptr;

#ifdef _WIN32
    ptr = _aligned_malloc(static_cast<size_t>(bytes), kAlignmentBytes);
    if (!ptr)
        return nullptr;
#else
    if (posix_memalign(&ptr, kAlignmentBytes, static_cast<size_t>(bytes)) != 0)
        return nullptr;
#endif

    std::memset(ptr, 0, static_cast<size_t>(bytes));
    return static_cast<uint8_t*>(ptr);
}

void FreeBlock(void* ptr)
{
    if (!ptr)
        return;

#ifdef _WIN32
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

void FreeBlocks(std::vector<uint8_t*>* blocks)
{
    for (size_t i = 0; i < blocks->size(); ++i)
        FreeBlock((*blocks)[i]);
    blocks->clear();
}

bool FileSize(std::ifstream* file, uint64_t* bytes)
{
    file->seekg(0, std::ios::end);
    if (!file->good())
        return false;

    const std::streamoff size = file->tellg();
    if (size < 0)
        return false;

    file->seekg(0, std::ios::beg);
    if (!file->good())
        return false;

    *bytes = static_cast<uint64_t>(size);
    return true;
}

bool BlockBytesAreSupported(uint64_t block_bytes)
{
    if (block_bytes == 0 || block_bytes % 64 != 0)
        return false;

    if (block_bytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        return false;

    if (block_bytes > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
        return false;

    if (block_bytes > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
        return false;

    return true;
}

bool ReadExactOrFail(std::ifstream* file, void* data, uint64_t bytes)
{
    file->read(static_cast<char*>(data), static_cast<std::streamsize>(bytes));
    return file->good();
}

bool SkipBytes(std::ifstream* file, uint64_t bytes)
{
    file->seekg(static_cast<std::streamoff>(bytes), std::ios::cur);
    return file->good();
}

bool WriteBytes(std::ofstream* file, const void* data, uint64_t bytes)
{
    file->write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
    return file->good();
}

bool CheckInit()
{
    const LeopardResult result = static_cast<LeopardResult>(leo_init());
    if (result != Leopard_Success)
    {
        std::cerr << "leo_init failed: " << leo_result_string(result) << "\n";
        return false;
    }
    return true;
}

bool ValidateHeader(const Header& header)
{
    if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0)
    {
        std::cerr << "Input is not a coded file produced by this demo\n";
        return false;
    }
    if (!BlockBytesAreSupported(header.block_bytes))
    {
        std::cerr << "Invalid block size in coded file\n";
        return false;
    }
    if (header.original_count == 0 || header.recovery_count == 0 ||
        header.recovery_count > header.original_count)
    {
        std::cerr << "Invalid block counts in coded file\n";
        return false;
    }
    if (static_cast<uint64_t>(header.original_count) + header.recovery_count > 65536)
    {
        std::cerr << "Coded file exceeds Leopard's 65536 total-block limit\n";
        return false;
    }
    if (header.block_bytes > std::numeric_limits<uint64_t>::max() / header.original_count)
    {
        std::cerr << "Coded file block metadata overflows uint64_t\n";
        return false;
    }
    if (header.original_bytes > header.block_bytes * header.original_count)
    {
        std::cerr << "Coded file original size exceeds its data block capacity\n";
        return false;
    }
    return true;
}

bool CheckIndexRange(const std::set<unsigned>& indexes, unsigned count, const char* label)
{
    for (std::set<unsigned>::const_iterator it = indexes.begin(); it != indexes.end(); ++it)
    {
        if (*it >= count)
        {
            std::cerr << "Erased " << label << " index " << *it
                      << " is outside the range 0.." << (count - 1) << "\n";
            return false;
        }
    }
    return true;
}

bool WriteDecodedOutput(const std::string& output_path,
                        const Header& header,
                        const std::vector<uint8_t*>& original_data,
                        const std::vector<uint8_t*>& work_data)
{
    std::ofstream output(output_path.c_str(), std::ios::binary);
    if (!output)
    {
        std::cerr << "Failed to open " << output_path << " for writing\n";
        return false;
    }

    uint64_t remaining = header.original_bytes;
    for (unsigned i = 0; i < header.original_count; ++i)
    {
        const uint8_t* block = original_data[i];
        if (!block && i < work_data.size())
            block = work_data[i];
        if (!block)
        {
            std::cerr << "Missing decoded data block " << i << "\n";
            return false;
        }

        const uint64_t to_write = std::min<uint64_t>(header.block_bytes, remaining);
        if (to_write > 0 && !WriteBytes(&output, block, to_write))
        {
            std::cerr << "Failed to write decoded data block " << i << "\n";
            return false;
        }

        remaining -= to_write;
    }

    return true;
}

int EncodeFile(const std::string& input_path, const std::string& coded_path,
               unsigned recovery_count, uint64_t block_bytes)
{
    if (!BlockBytesAreSupported(block_bytes))
    {
        std::cerr << "block-bytes must be a positive multiple of 64 that fits this process\n";
        return 1;
    }

    std::ifstream input(input_path.c_str(), std::ios::binary);
    if (!input)
    {
        std::cerr << "Failed to open " << input_path << "\n";
        return 1;
    }

    uint64_t original_bytes = 0;
    if (!FileSize(&input, &original_bytes))
    {
        std::cerr << "Failed to determine size of " << input_path << "\n";
        return 1;
    }
    uint64_t original_count_u64 = original_bytes / block_bytes;
    if (original_bytes % block_bytes != 0)
        ++original_count_u64;
    if (original_count_u64 == 0)
        original_count_u64 = 1;
    if (original_count_u64 > 65536)
    {
        std::cerr << "Too many original blocks for this demo\n";
        return 1;
    }

    const unsigned original_count = static_cast<unsigned>(original_count_u64);
    if (recovery_count == 0 || recovery_count > original_count)
    {
        std::cerr << "parity-blocks must be between 1 and k (" << original_count << ")\n";
        return 1;
    }
    if (static_cast<uint64_t>(original_count) + recovery_count > 65536)
    {
        std::cerr << "k + parity-blocks must be <= 65536 for Leopard\n";
        return 1;
    }

    std::vector<uint8_t*> original_data(original_count, nullptr);
    for (unsigned i = 0; i < original_count; ++i)
    {
        original_data[i] = AllocateBlock(block_bytes);
        if (!original_data[i])
        {
            std::cerr << "Failed to allocate data block " << i << "\n";
            FreeBlocks(&original_data);
            return 1;
        }

        const uint64_t offset = static_cast<uint64_t>(i) * block_bytes;
        const uint64_t remaining = original_bytes > offset ? original_bytes - offset : 0;
        const uint64_t to_read = std::min(block_bytes, remaining);
        if (to_read > 0 && !ReadExactOrFail(&input, original_data[i], to_read))
        {
            std::cerr << "Failed to read data block " << i << "\n";
            FreeBlocks(&original_data);
            return 1;
        }
    }

    const unsigned work_count = leo_encode_work_count(original_count, recovery_count);
    std::vector<uint8_t*> work_data(work_count, nullptr);
    for (unsigned i = 0; i < work_count; ++i)
    {
        work_data[i] = AllocateBlock(block_bytes);
        if (!work_data[i])
        {
            std::cerr << "Failed to allocate encode work block " << i << "\n";
            FreeBlocks(&original_data);
            FreeBlocks(&work_data);
            return 1;
        }
    }

    if (!CheckInit())
    {
        FreeBlocks(&original_data);
        FreeBlocks(&work_data);
        return 1;
    }

    std::vector<const void*> original_ptrs(original_count, nullptr);
    for (unsigned i = 0; i < original_count; ++i)
        original_ptrs[i] = original_data[i];

    LeopardResult result = leo_encode(
        block_bytes,
        original_count,
        recovery_count,
        work_count,
        &original_ptrs[0],
        reinterpret_cast<void**>(&work_data[0]));
    if (result != Leopard_Success)
    {
        std::cerr << "leo_encode failed: " << leo_result_string(result) << "\n";
        FreeBlocks(&original_data);
        FreeBlocks(&work_data);
        return 1;
    }

    std::ofstream coded(coded_path.c_str(), std::ios::binary);
    if (!coded)
    {
        std::cerr << "Failed to open " << coded_path << " for writing\n";
        FreeBlocks(&original_data);
        FreeBlocks(&work_data);
        return 1;
    }

    Header header = {};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.original_bytes = original_bytes;
    header.block_bytes = block_bytes;
    header.original_count = original_count;
    header.recovery_count = recovery_count;

    if (!WriteBytes(&coded, &header, sizeof(header)))
    {
        std::cerr << "Failed to write coded file header\n";
        FreeBlocks(&original_data);
        FreeBlocks(&work_data);
        return 1;
    }

    for (unsigned i = 0; i < original_count; ++i)
    {
        if (!WriteBytes(&coded, original_data[i], block_bytes))
        {
            std::cerr << "Failed to write data block " << i << "\n";
            FreeBlocks(&original_data);
            FreeBlocks(&work_data);
            return 1;
        }
    }

    for (unsigned i = 0; i < recovery_count; ++i)
    {
        if (!WriteBytes(&coded, work_data[i], block_bytes))
        {
            std::cerr << "Failed to write parity block " << i << "\n";
            FreeBlocks(&original_data);
            FreeBlocks(&work_data);
            return 1;
        }
    }

    std::cout << "Encoded k=" << original_count
              << " data blocks and p=" << recovery_count << " parity blocks\n";
    std::cout << "Block size: " << block_bytes << " bytes\n";
    std::cout << "Wrote coded file: " << coded_path << "\n";

    FreeBlocks(&original_data);
    FreeBlocks(&work_data);
    return 0;
}

int DecodeFile(const std::string& coded_path, const std::string& output_path,
               const std::set<unsigned>& erased_data,
               const std::set<unsigned>& erased_parity)
{
    std::ifstream coded(coded_path.c_str(), std::ios::binary);
    if (!coded)
    {
        std::cerr << "Failed to open " << coded_path << "\n";
        return 1;
    }

    Header header = {};
    if (!ReadExactOrFail(&coded, &header, sizeof(header)) || !ValidateHeader(header))
        return 1;

    if (!CheckIndexRange(erased_data, header.original_count, "data") ||
        !CheckIndexRange(erased_parity, header.recovery_count, "parity"))
        return 1;

    if (erased_data.size() > header.recovery_count - erased_parity.size())
    {
        std::cerr << "Not enough parity blocks remain to recover "
                  << erased_data.size() << " erased data blocks\n";
        return 1;
    }

    std::vector<uint8_t*> original_data(header.original_count, nullptr);
    std::vector<uint8_t*> recovery_data(header.recovery_count, nullptr);

    for (unsigned i = 0; i < header.original_count; ++i)
    {
        if (Contains(erased_data, i))
        {
            if (!SkipBytes(&coded, header.block_bytes))
            {
                std::cerr << "Failed to skip erased data block " << i << "\n";
                FreeBlocks(&original_data);
                FreeBlocks(&recovery_data);
                return 1;
            }
            continue;
        }

        original_data[i] = AllocateBlock(header.block_bytes);
        if (!original_data[i] || !ReadExactOrFail(&coded, original_data[i], header.block_bytes))
        {
            std::cerr << "Failed to read data block " << i << "\n";
            FreeBlocks(&original_data);
            FreeBlocks(&recovery_data);
            return 1;
        }
    }

    if (erased_data.empty())
    {
        if (!WriteDecodedOutput(output_path, header, original_data, std::vector<uint8_t*>()))
        {
            FreeBlocks(&original_data);
            FreeBlocks(&recovery_data);
            return 1;
        }

        std::cout << "Decoded " << output_path << "\n";
        std::cout << "Erased data blocks: none\n";
        std::cout << "Erased parity blocks:";
        if (erased_parity.empty())
            std::cout << " none";
        for (std::set<unsigned>::const_iterator it = erased_parity.begin(); it != erased_parity.end(); ++it)
            std::cout << " " << *it;
        std::cout << "\n";

        FreeBlocks(&original_data);
        FreeBlocks(&recovery_data);
        return 0;
    }

    for (unsigned i = 0; i < header.recovery_count; ++i)
    {
        if (Contains(erased_parity, i))
        {
            if (!SkipBytes(&coded, header.block_bytes))
            {
                std::cerr << "Failed to skip erased parity block " << i << "\n";
                FreeBlocks(&original_data);
                FreeBlocks(&recovery_data);
                return 1;
            }
            continue;
        }

        recovery_data[i] = AllocateBlock(header.block_bytes);
        if (!recovery_data[i] || !ReadExactOrFail(&coded, recovery_data[i], header.block_bytes))
        {
            std::cerr << "Failed to read parity block " << i << "\n";
            FreeBlocks(&original_data);
            FreeBlocks(&recovery_data);
            return 1;
        }
    }

    const unsigned work_count = leo_decode_work_count(header.original_count, header.recovery_count);
    std::vector<uint8_t*> work_data(work_count, nullptr);
    for (unsigned i = 0; i < work_count; ++i)
    {
        work_data[i] = AllocateBlock(header.block_bytes);
        if (!work_data[i])
        {
            std::cerr << "Failed to allocate decode work block " << i << "\n";
            FreeBlocks(&original_data);
            FreeBlocks(&recovery_data);
            FreeBlocks(&work_data);
            return 1;
        }
    }

    if (!CheckInit())
    {
        FreeBlocks(&original_data);
        FreeBlocks(&recovery_data);
        FreeBlocks(&work_data);
        return 1;
    }

    std::vector<const void*> original_ptrs(header.original_count, nullptr);
    std::vector<const void*> recovery_ptrs(header.recovery_count, nullptr);
    for (unsigned i = 0; i < header.original_count; ++i)
        original_ptrs[i] = original_data[i];
    for (unsigned i = 0; i < header.recovery_count; ++i)
        recovery_ptrs[i] = recovery_data[i];

    LeopardResult result = leo_decode(
        header.block_bytes,
        header.original_count,
        header.recovery_count,
        work_count,
        &original_ptrs[0],
        &recovery_ptrs[0],
        reinterpret_cast<void**>(&work_data[0]));
    if (result != Leopard_Success)
    {
        std::cerr << "leo_decode failed: " << leo_result_string(result) << "\n";
        FreeBlocks(&original_data);
        FreeBlocks(&recovery_data);
        FreeBlocks(&work_data);
        return 1;
    }

    if (!WriteDecodedOutput(output_path, header, original_data, work_data))
    {
        FreeBlocks(&original_data);
        FreeBlocks(&recovery_data);
        FreeBlocks(&work_data);
        return 1;
    }

    std::cout << "Decoded " << output_path << "\n";
    std::cout << "Erased data blocks:";
    if (erased_data.empty())
        std::cout << " none";
    for (std::set<unsigned>::const_iterator it = erased_data.begin(); it != erased_data.end(); ++it)
        std::cout << " " << *it;
    std::cout << "\n";

    std::cout << "Erased parity blocks:";
    if (erased_parity.empty())
        std::cout << " none";
    for (std::set<unsigned>::const_iterator it = erased_parity.begin(); it != erased_parity.end(); ++it)
        std::cout << " " << *it;
    std::cout << "\n";

    FreeBlocks(&original_data);
    FreeBlocks(&recovery_data);
    FreeBlocks(&work_data);
    return 0;
}

int MainEncode(int argc, char** argv)
{
    if (argc < 5 || argc > 6)
    {
        PrintUsage(argv[0]);
        return 2;
    }

    unsigned recovery_count = 0;
    if (!ParseUnsigned(argv[4], &recovery_count))
    {
        std::cerr << "Invalid parity-blocks value\n";
        return 2;
    }

    uint64_t block_bytes = kDefaultBlockBytes;
    if (argc == 6 && !ParseU64(argv[5], &block_bytes))
    {
        std::cerr << "Invalid block-bytes value\n";
        return 2;
    }

    return EncodeFile(argv[2], argv[3], recovery_count, block_bytes);
}

int MainDecode(int argc, char** argv)
{
    if (argc < 4)
    {
        PrintUsage(argv[0]);
        return 2;
    }

    std::set<unsigned> erased_data;
    std::set<unsigned> erased_parity;

    for (int i = 4; i < argc; ++i)
    {
        const std::string arg(argv[i]);
        if (arg == "--erase-data")
        {
            if (++i >= argc || !ParseIndexList(argv[i], &erased_data))
            {
                std::cerr << "Invalid --erase-data value\n";
                return 2;
            }
        }
        else if (arg == "--erase-parity")
        {
            if (++i >= argc || !ParseIndexList(argv[i], &erased_parity))
            {
                std::cerr << "Invalid --erase-parity value\n";
                return 2;
            }
        }
        else
        {
            std::cerr << "Unknown option: " << arg << "\n";
            return 2;
        }
    }

    return DecodeFile(argv[2], argv[3], erased_data, erased_parity);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        PrintUsage(argv[0]);
        return 2;
    }

    const std::string mode(argv[1]);
    if (mode == "--help" || mode == "-h")
    {
        PrintUsage(argv[0]);
        return 0;
    }
    if (mode == "encode")
        return MainEncode(argc, argv);
    if (mode == "decode")
        return MainDecode(argc, argv);

    PrintUsage(argv[0]);
    return 2;
}
