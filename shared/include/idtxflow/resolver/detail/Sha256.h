#pragma once

/**
 * @file Sha256.h
 * @brief Minimal header-only SHA-256 implementation for cache key generation.
 *
 * This is a self-contained, dependency-free SHA-256 implementation using only
 * the C++ standard library. It is used internally by HttpAssetCache to generate
 * deterministic cache directory names from URLs.
 *
 * Not intended for cryptographic security — used solely for content-addressable
 * cache key generation where collision resistance is sufficient.
 *
 * Based on the FIPS 180-4 specification.
 */

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

namespace idtxflow
{
namespace resolver
{
namespace detail
{

class Sha256
{
  public:
    /**
     * Compute the SHA-256 hex digest of the given input string.
     *
     * @param input The string to hash.
     * @return 64-character lowercase hexadecimal string.
     */
    static inline std::string HashString(const std::string& input)
    {
        Sha256 ctx;
        ctx.Update(reinterpret_cast<const uint8_t*>(input.data()), input.size());
        return ctx.Finalize();
    }

  private:
    // SHA-256 initial hash values (first 32 bits of the fractional parts
    // of the square roots of the first 8 primes)
    static constexpr std::array<uint32_t, 8> kInitHash = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    // SHA-256 round constants (first 32 bits of the fractional parts
    // of the cube roots of the first 64 primes)
    static constexpr std::array<uint32_t, 64> kRoundConstants = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

    std::array<uint32_t, 8> state_;
    std::array<uint8_t, 64> buffer_;
    size_t buffer_len_ = 0;
    uint64_t total_len_ = 0;

    Sha256()
        : state_(kInitHash),
          buffer_{}
    {
    }

    static inline uint32_t RotateRight(uint32_t value, unsigned int count)
    {
        return (value >> count) | (value << (32 - count));
    }

    static inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z)
    {
        return (x & y) ^ (~x & z);
    }

    static inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z)
    {
        return (x & y) ^ (x & z) ^ (y & z);
    }

    static inline uint32_t Sigma0(uint32_t x)
    {
        return RotateRight(x, 2) ^ RotateRight(x, 13) ^ RotateRight(x, 22);
    }

    static inline uint32_t Sigma1(uint32_t x)
    {
        return RotateRight(x, 6) ^ RotateRight(x, 11) ^ RotateRight(x, 25);
    }

    static inline uint32_t sigma0(uint32_t x)
    {
        return RotateRight(x, 7) ^ RotateRight(x, 18) ^ (x >> 3);
    }

    static inline uint32_t sigma1(uint32_t x)
    {
        return RotateRight(x, 17) ^ RotateRight(x, 19) ^ (x >> 10);
    }

    void ProcessBlock(const uint8_t block[64])
    {
        // Prepare the message schedule
        std::array<uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i)
        {
            w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | (static_cast<uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i)
        {
            w[i] = sigma1(w[i - 2]) + w[i - 7] + sigma0(w[i - 15]) + w[i - 16];
        }

        // Initialize working variables
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

        // Compression function main loop
        for (int i = 0; i < 64; ++i)
        {
            uint32_t t1 = h + Sigma1(e) + Ch(e, f, g) + kRoundConstants[i] + w[i];
            uint32_t t2 = Sigma0(a) + Maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        // Add the compressed chunk to the current hash value
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    void Update(const uint8_t* data, size_t length)
    {
        total_len_ += length;

        // If we have buffered data, try to complete a block
        if (buffer_len_ > 0)
        {
            size_t needed = 64 - buffer_len_;
            size_t copy_len = (length < needed) ? length : needed;
            std::memcpy(buffer_.data() + buffer_len_, data, copy_len);
            buffer_len_ += copy_len;
            data += copy_len;
            length -= copy_len;

            if (buffer_len_ == 64)
            {
                ProcessBlock(buffer_.data());
                buffer_len_ = 0;
            }
        }

        // Process full blocks directly from input
        while (length >= 64)
        {
            ProcessBlock(data);
            data += 64;
            length -= 64;
        }

        // Buffer remaining bytes
        if (length > 0)
        {
            std::memcpy(buffer_.data(), data, length);
            buffer_len_ = length;
        }
    }

    std::string Finalize()
    {
        // Pad the message
        uint64_t total_bits = total_len_ * 8;

        // Append bit '1' (0x80 byte)
        uint8_t pad = 0x80;
        Update(&pad, 1);

        // Append zeros until message length ≡ 56 (mod 64)
        uint8_t zero = 0x00;
        while (buffer_len_ != 56)
        {
            Update(&zero, 1);
        }

        // Append original length in bits as 64-bit big-endian
        uint8_t len_bytes[8];
        for (int i = 7; i >= 0; --i)
        {
            len_bytes[i] = static_cast<uint8_t>(total_bits & 0xFF);
            total_bits >>= 8;
        }
        // Write length directly to buffer and process (avoid Update to prevent
        // further total_len_ modification)
        std::memcpy(buffer_.data() + 56, len_bytes, 8);
        ProcessBlock(buffer_.data());
        buffer_len_ = 0;

        // Produce the final hash as a hex string
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (uint32_t word: state_)
        {
            oss << std::setw(8) << word;
        }
        return oss.str();
    }
};

} // namespace detail
} // namespace resolver
} // namespace idtxflow