// Gemeinsamer Takt der Offbeat-Plugins: Offbeat Clutch schreibt Schlag/Tempo jedes Blocks, Offbeat Gearbox liest
// und läuft taktgenau mit (auch über Prozessgrenzen, z. B. Carla-Bridges). Shared Memory „/offbeat-clock“
// mit Seqlock; Lesen und Schreiben sind lock-frei und ohne Speicheranforderung (Audio-Thread).
// Quelle: offbeat-clutch/src/dsp/SharedClock.hpp – in offbeat-gearbox kopiert, Änderungen dort zurückspielen.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace offbeat {

class SharedClock {
public:
    static constexpr uint32_t kMagic = 0x4f424331; // "OBC1"

    explicit SharedClock(const char* name = "/offbeat-clock")
    {
#ifdef _WIN32
        char winName[64] = "Local\\";
        for (int i = 0, j = 6; name[i] && j < 62; ++i)
            if (name[i] != '/')
                winName[j++] = name[i], winName[j] = 0;
        map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Data), winName);
        if (map)
            d = (Data*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Data));
#else
        const int fd = shm_open(name, O_CREAT | O_RDWR, 0600);
        if (fd < 0)
            return;
        struct stat st {};
        if (fstat(fd, &st) == 0 && st.st_size < (off_t)sizeof(Data))
            (void)ftruncate(fd, sizeof(Data)); // macOS: nur einmal möglich, daher nur bei neuem Objekt
        void* p = mmap(nullptr, sizeof(Data), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (p != MAP_FAILED)
            d = (Data*)p;
#endif
    }
    ~SharedClock()
    {
#ifdef _WIN32
        if (d)
            UnmapViewOfFile(d);
        if (map)
            CloseHandle(map);
#else
        if (d)
            munmap(d, sizeof(Data));
#endif
    }
    SharedClock(const SharedClock&) = delete;
    SharedClock& operator=(const SharedClock&) = delete;

    bool valid() const { return d != nullptr; }

    // Schlag zu Beginn des aktuellen Blocks, Tempo; running = Takt läuft
    void write(bool running, double beat, double bpm)
    {
        if (!d)
            return;
        const uint32_t s = d->seq.load(std::memory_order_relaxed);
        d->seq.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        d->beat = beat, d->bpm = bpm, d->wall = now(), d->running = running ? 1u : 0u, d->magic = kMagic;
        std::atomic_thread_fence(std::memory_order_release);
        d->seq.store(s + 2, std::memory_order_release);
    }

    // Schlag jetzt (aus der Schreibzeit hochgerechnet) und Tempo; false = kein frischer, laufender Takt
    bool read(double& beat, double& bpm, double maxAge = 0.5) const
    {
        if (!d)
            return false;
        for (int tries = 0; tries < 4; ++tries) {
            const uint32_t s1 = d->seq.load(std::memory_order_acquire);
            if (s1 & 1u)
                continue;
            const double b = d->beat, t = d->bpm, w = d->wall;
            const uint32_t run = d->running, magic = d->magic;
            std::atomic_thread_fence(std::memory_order_acquire);
            if (d->seq.load(std::memory_order_relaxed) != s1)
                continue;
            const double age = now() - w;
            if (magic != kMagic || !run || t <= 0.0 || age < -0.01 || age > maxAge)
                return false;
            beat = b + (age > 0.0 ? age : 0.0) * t / 60.0;
            bpm = t;
            return true;
        }
        return false;
    }

    static double now()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

private:
    struct Data {
        std::atomic<uint32_t> seq;
        uint32_t magic;
        double beat, bpm, wall;
        uint32_t running;
    };
    Data* d = nullptr;
#ifdef _WIN32
    HANDLE map = nullptr;
#endif
};

} // namespace offbeat
