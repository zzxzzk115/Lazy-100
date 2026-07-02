#include "lazy100/net/fetch.hpp"

#include "lazy100/common/log.hpp"

#include <atomic>
#include <cstring>
#include <utility>

#if defined(__EMSCRIPTEN__)
#    include <emscripten/fetch.h>
#else
#    include <curl/curl.h>

#    include <mutex>
#    include <thread>
#endif

namespace lazy100::net
{
#if defined(__EMSCRIPTEN__)

    // wasm backend: emscripten_fetch. The transfer's shared state is heap-owned and
    // reference-counted between the Fetch object and the in-flight browser request, so
    // destroying a Fetch mid-transfer can't leave the completion callback with a dangling
    // pointer (the callback just drops its reference).
    struct Fetch::Impl
    {
        struct State
        {
            std::atomic<int>  refs {2}; // Fetch + in-flight request
            std::atomic<bool> finished {false};
            bool              success = false;
            std::vector<u8>   body;
            std::string       error;

            static void release(State* s)
            {
                if (s->refs.fetch_sub(1) == 1)
                    delete s;
            }
        };

        State* state   = nullptr;
        bool   started = false;

        static void on_success(emscripten_fetch_t* f)
        {
            auto* s = static_cast<State*>(f->userData);
            s->body.assign(reinterpret_cast<const u8*>(f->data),
                           reinterpret_cast<const u8*>(f->data) + f->numBytes);
            s->success = f->status == 200;
            if (!s->success)
                s->error = "HTTP " + std::to_string(f->status);
            s->finished.store(true, std::memory_order_release);
            emscripten_fetch_close(f);
            State::release(s);
        }

        static void on_error(emscripten_fetch_t* f)
        {
            auto* s    = static_cast<State*>(f->userData);
            s->success = false;
            s->error   = "network error (HTTP " + std::to_string(f->status) + ")";
            s->finished.store(true, std::memory_order_release);
            emscripten_fetch_close(f);
            State::release(s);
        }

        void begin(const std::string& url)
        {
            state   = new State();
            started = true;

            emscripten_fetch_attr_t attr;
            emscripten_fetch_attr_init(&attr);
            std::strcpy(attr.requestMethod, "GET");
            attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
            attr.userData   = state;
            attr.onsuccess  = &on_success;
            attr.onerror    = &on_error;
            emscripten_fetch(&attr, url.c_str());
        }

        void reset()
        {
            if (state)
                State::release(state); // in-flight request keeps its own reference
            state   = nullptr;
            started = false;
        }

        ~Impl() { reset(); }

        bool done() const { return state && state->finished.load(std::memory_order_acquire); }
    };

#else

    // Desktop backend: libcurl easy API on a detached-join worker thread. The worker only
    // touches `state`; the main thread reads it after `finished` is set (release/acquire).
    struct Fetch::Impl
    {
        struct State
        {
            std::atomic<bool> finished {false};
            bool              success = false;
            std::vector<u8>   body;
            std::string       error;
        };

        std::unique_ptr<State> state;
        std::thread            worker;
        bool                   started = false;

        static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
        {
            auto* body = static_cast<std::vector<u8>*>(userdata);
            body->insert(body->end(), reinterpret_cast<u8*>(ptr), reinterpret_cast<u8*>(ptr) + size * nmemb);
            return size * nmemb;
        }

        static void run(State* s, std::string url)
        {
            static std::once_flag curlInit;
            std::call_once(curlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

            CURL* curl = curl_easy_init();
            if (!curl)
            {
                s->error = "curl init failed";
                s->finished.store(true, std::memory_order_release);
                return;
            }
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s->body);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "lazy100-explore/1.0");
#    if defined(CURLSSLOPT_NATIVE_CA)
            // With an OpenSSL-backed curl on Windows there is no CA bundle on disk; use the
            // system certificate store instead so https://raw.githubusercontent.com verifies.
            curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_NATIVE_CA));
#    endif

            const CURLcode rc = curl_easy_perform(curl);
            long           status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            if (rc != CURLE_OK)
            {
                s->error = curl_easy_strerror(rc);
                s->body.clear();
            }
            else if (status != 200)
            {
                s->error = "HTTP " + std::to_string(status);
                s->body.clear();
            }
            else
                s->success = true;
            curl_easy_cleanup(curl);
            s->finished.store(true, std::memory_order_release);
        }

        void begin(const std::string& url)
        {
            state   = std::make_unique<State>();
            started = true;
            worker  = std::thread(&run, state.get(), url);
        }

        void reset()
        {
            if (worker.joinable())
                worker.join(); // curl has a 30s cap, so a mid-flight join is bounded
            state.reset();
            started = false;
        }

        ~Impl() { reset(); }

        bool done() const { return state && state->finished.load(std::memory_order_acquire); }
    };

#endif

    namespace
    {
        const std::vector<u8> kEmptyBody;
        const std::string     kEmptyError;
        const std::string     kBusyError = "transfer in flight";
    } // namespace

    Fetch::Fetch() : p_(std::make_unique<Impl>()) {}
    Fetch::~Fetch() = default;

    void Fetch::start(const std::string& url)
    {
        if (active())
        {
            LZ_WARN("net: start() ignored, transfer already in flight");
            return;
        }
        p_->reset();
        p_->begin(url);
    }

    bool Fetch::active() const { return p_->started && !p_->done(); }
    bool Fetch::done() const { return p_->started && p_->done(); }
    bool Fetch::ok() const { return done() && p_->state->success; }

    const std::vector<u8>& Fetch::data() const { return done() ? p_->state->body : kEmptyBody; }
    const std::string&     Fetch::error() const { return done() ? p_->state->error : (active() ? kBusyError : kEmptyError); }
} // namespace lazy100::net
