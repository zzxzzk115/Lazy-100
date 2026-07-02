#pragma once

#include "lazy100/common/types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace lazy100::net
{
    // One-shot async HTTP(S) GET into memory. start() kicks the transfer off, then the main
    // loop polls done() once per frame (no callbacks into user code, no blocking). Backends:
    // libcurl on a worker thread (desktop), emscripten_fetch (wasm).
    class Fetch
    {
    public:
        Fetch();
        ~Fetch(); // aborts/joins an in-flight transfer

        Fetch(const Fetch&)            = delete;
        Fetch& operator=(const Fetch&) = delete;

        void start(const std::string& url); // restartable once the previous transfer is done

        bool active() const; // started and still in flight
        bool done() const;   // finished (successfully or not)
        bool ok() const;     // done, transport succeeded, and HTTP status == 200

        const std::vector<u8>& data() const;  // response body (valid once done)
        const std::string&     error() const; // human-readable failure reason (valid once done)

    private:
        struct Impl;
        std::unique_ptr<Impl> p_;
    };
} // namespace lazy100::net
