#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>

// Enta: measures how much CPU time each module spends per second (event handlers + HUD drawing).
// Off by default; when `enabled` is false, the only cost is one relaxed atomic load per call.
class ModuleProfiler final {
public:
    using Clock = std::chrono::steady_clock;

    struct Sample {
        const void* owner; // the module, as a Listener*
        double msPerSecond;
    };

    static inline std::atomic_bool enabled = false;

    static void add(const void* owner, Clock::duration spent) {
        std::lock_guard guard(lock());
        current()[owner] += spent;
    }

    // Results from the last full second, biggest first. Call this about once a frame; it rolls the window.
    static std::vector<Sample> results() {
        std::lock_guard guard(lock());
        auto now = Clock::now();
        auto& start = windowStart();
        if (now - start >= std::chrono::seconds(1)) {
            const double seconds = std::chrono::duration<double>(now - start).count();
            auto& out = last();
            out.clear();
            for (auto& [owner, spent] : current()) {
                out.push_back({ owner, std::chrono::duration<double, std::milli>(spent).count() / seconds });
            }
            std::sort(out.begin(), out.end(), [](Sample const& a, Sample const& b) {
                return a.msPerSecond > b.msPerSecond;
            });
            current().clear();
            start = now;
        }
        return last();
    }

    static void reset() {
        std::lock_guard guard(lock());
        current().clear();
        last().clear();
        windowStart() = Clock::now();
    }

    // Times one call when profiling is on.
    class Scope {
    public:
        explicit Scope(const void* owner)
            : owner(enabled.load(std::memory_order_relaxed) ? owner : nullptr) {
            if (this->owner) begin = Clock::now();
        }
        ~Scope() {
            if (owner) add(owner, Clock::now() - begin);
        }
        Scope(Scope const&) = delete;
        Scope& operator=(Scope const&) = delete;

    private:
        const void* owner;
        Clock::time_point begin {};
    };

private:
    static std::mutex& lock() {
        static std::mutex m;
        return m;
    }
    static std::unordered_map<const void*, Clock::duration>& current() {
        static std::unordered_map<const void*, Clock::duration> m;
        return m;
    }
    static std::vector<Sample>& last() {
        static std::vector<Sample> v;
        return v;
    }
    static Clock::time_point& windowStart() {
        static Clock::time_point t = Clock::now();
        return t;
    }
};
