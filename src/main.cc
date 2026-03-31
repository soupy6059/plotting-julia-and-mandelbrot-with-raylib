#include <iostream>
#include <random>
#include <cassert>
#include <mutex>
#include <functional>
#include <semaphore>
#include <complex>
#include <array>
#include <cinttypes>
#include <string>
#include <random>
#include <thread>
#include <cstring>
#include "craylib.hpp"

#ifndef DEBUG
#define DEBUG 0
#endif

namespace core {
    #define anno(X) do { if constexpr(DEBUG) std::clog << (X); } while(false)

    template<typename U> constexpr U
    implicit_cast(typename std::type_identity<U>::type Thing) {
        return Thing;
    }

    struct r2 {
        double X, Y;

        constexpr r2(double X, double Y): X{X}, Y{Y} {}

        constexpr r2(std::complex<double> Z) { 
            memcpy(this, &Z, sizeof(*this));
        }

        constexpr r2(const r2&) = default;

        constexpr r2 &operator=(const core::r2&) = default;

        r2 operator+(r2 const&Other) {
            return {X + Other.X, Y + Other.Y};
        }

        r2 operator-(r2 const&Other) {
            return {X - Other.X, Y - Other.Y};
        }

        r2 operator*(double const&Other) {
            return {X * Other, Y * Other};
        }

        operator std::complex<double>() {
            return {X, Y};
        }

        #ifdef RAYLIB
        r2(rl::Vector2 RayVec):
        X{implicit_cast<double>(RayVec.x)},
        Y{implicit_cast<double>(RayVec.y)} {}

        operator rl::Vector2() {
            return {implicit_cast<float>(X), implicit_cast<float>(Y)};
        }
        #endif
    };
    r2 operator*(double Other, r2 &Vec) {
        return {Other*Vec.X,Other*Vec.Y};
    }
};

namespace screen {
    constexpr const uint64_t Width = 1000;

    constexpr const uint64_t Height = 1000;

    constexpr const char *const Name = "Julia Plotting";

    constinit static double Zoom = 1.;

    constinit static core::r2 Center {0.,0.};

    static rl::Color ELECTRIC_AQUA = rl::GetColor(0x51E5FFFF);
    
    static rl::Color INDIGO = rl::GetColor(0x440381FF);

    static inline uint64_t at(uint64_t X, uint64_t Y) {
        return X + Width * Y;
    }

};

using cplx = std::complex<double>;

template<typename type> using func = std::function<type>;

namespace julia {
    static rl::Color JuliaSet[screen::Width*screen::Height];

    static rl::Color MandelbrotSet[screen::Width*screen::Height];

    static cplx DestinationSet[screen::Width*screen::Height];

    constexpr uint64_t N = 1000;

    static auto Func = [](cplx C) -> func<cplx(cplx)> {
        return [C](cplx Z) -> cplx {
            return std::pow(Z, cplx{2.0}) + C;
        };
    };
};

namespace rl_combat {
    static inline core::r2 screen_to_graph_normal(core::r2 ScrPos) {
        using namespace core;
        using namespace screen;
        ScrPos = {ScrPos.X - Width/2, ScrPos.Y - Height/2};
        ScrPos.Y *= -1;
        r2 GraphPos = {
            ScrPos.X * 4.f/implicit_cast<double>(Width),
            ScrPos.Y * 4.f/implicit_cast<double>(Height)
        };
        return GraphPos;
    }

    static inline core::r2 screen_to_graph(core::r2 ScrPos) {
        ScrPos = {ScrPos.X - screen::Width/2, ScrPos.Y - screen::Height/2};
        ScrPos.Y *= -1;
        core::r2 GraphPos = {
            ScrPos.X * 4.f/core::implicit_cast<double>(screen::Width),
            ScrPos.Y * 4.f/core::implicit_cast<double>(screen::Height)
        };
        return GraphPos * screen::Zoom - screen::Center;
    }
    static inline core::r2 graph_to_screen(core::r2 GPos) {
        GPos = (GPos+screen::Center) * (1./screen::Zoom);
        GPos = {
            GPos.X * core::implicit_cast<double>(screen::Width) /4.,
            GPos.Y *core::implicit_cast<double>(screen::Height)/4.
        };
        GPos = {GPos.X, GPos.Y * -1.};
        GPos = {
            GPos.X + core::implicit_cast<double>(screen::Width)/2.,
            GPos.Y + core::implicit_cast<double>(screen::Height)/2.
        };
        return GPos;
    }
    static inline rl::Color
    color_lerp(rl::Color Src, rl::Color Dest, double Reduct) {
        rl::Color Color = {0};
        Color.r = Reduct * Dest.r + (1.-Reduct) * Src.r;
        Color.g = Reduct * Dest.g + (1.-Reduct) * Src.g;
        Color.b = Reduct * Dest.b + (1.-Reduct) * Src.b;
        Color.a = 255;
        return Color;
    }
    constexpr static inline auto screen_to_cplx(core::r2 SrcPos) -> cplx {
        return std::bit_cast<cplx>(screen_to_graph(SrcPos));
    }
};

static std::binary_semaphore ComputingPixelJulia{1};

constinit static std::atomic<uint64_t> Alive = 0;

constinit thread_local bool SplitHorizontally = true;

constexpr static auto go_compute_julia =
[](cplx JuliaConstant, uint64_t DrawingThreadCount) -> void {
    using namespace std::placeholders;
    using namespace std::complex_literals;
    using namespace std;
    
    constexpr uint64_t ThreadCountMax = 1 << 10;

    uint64_t XsPerThread = screen::Width/DrawingThreadCount + 1;
    if constexpr(DEBUG) assert(!Alive.load(memory_order::acquire));
    Alive.fetch_add(DrawingThreadCount, memory_order::relaxed);

    counting_semaphore ComputeRights{thread::hardware_concurrency() - 2};
    auto JuliaFunc = julia::Func(JuliaConstant);

    double EscapeRadius = pow((1. + sqrt(1 + 4.*abs(JuliaConstant)))/2., 2.0);

    function<void(uint64_t,uint64_t,uint64_t,uint64_t)> ComputeLine; 
    ComputeLine = [&,JuliaConstant,JuliaFunc](uint64_t StartX, uint64_t StartY,
    uint64_t SizeOfChunkX, uint64_t SizeOfChunkY) -> void {
        ComputeRights.acquire();
        uint64_t Work = 0;
        constexpr static uint64_t WorkCapacity = 1 << 21; // defualt == 1 << 21
        for(uint64_t X = StartX; X < SizeOfChunkX + StartX && X < screen::Width; ++X) {
            for(uint64_t Y = StartY; Y < SizeOfChunkY + StartY && Y < screen::Height; ++Y) {
                using namespace core;
                cplx Z = rl_combat::screen_to_cplx(r2{implicit_cast<double>(X), implicit_cast<double>(Y)});
                uint64_t K = 0;
                for(; K < julia::N; ++K) {
                    if(norm(Z) >= EscapeRadius) break;
                    Z = JuliaFunc(Z);
                    ++Work;
                }
                constexpr static auto BetterGradient [[maybe_unused]] = [](double Factor) -> double { return 1./(1.+exp(-10.*(Factor-0.25))); };
                constexpr static auto BetterGradient2 [[maybe_unused]] = [](double Factor) -> double { return pow(Factor, 0.2); };
                const double Colour = implicit_cast<double>(K%5)/implicit_cast<double>(5);
                julia::JuliaSet[screen::at(X,Y)] = rl_combat::color_lerp(screen::ELECTRIC_AQUA, screen::INDIGO, Colour);
                julia::DestinationSet[screen::at(X,Y)] = Z;

                if(!SplitHorizontally && Work >= WorkCapacity && SizeOfChunkX > 3 && Alive.load(memory_order::acquire) < ThreadCountMax) {
                    SizeOfChunkX *= 2.f/3.f;
                    Alive.fetch_add(1, memory_order::relaxed);
                    thread{ComputeLine, StartX + SizeOfChunkX-1, StartY, SizeOfChunkX/2.f+2, SizeOfChunkY}.detach();
                    Work = 0;
                    SplitHorizontally = !SplitHorizontally;
                } else if(Work >= WorkCapacity && SizeOfChunkY > 3 && Alive.load(memory_order::acquire) < ThreadCountMax) {
                    SizeOfChunkY *= 2.f/3.f;
                    Alive.fetch_add(1, memory_order::relaxed);
                    thread{ComputeLine, StartX, StartY + SizeOfChunkY-1, SizeOfChunkX, SizeOfChunkY/2.f+2}.detach();
                    Work = 0;
                    SplitHorizontally = !SplitHorizontally;
                }
            }
        } // end for(X) for(Y) for(K)
        if constexpr(DEBUG) if(Work >= WorkCapacity) clog << "Thread " << this_thread::get_id() << " did " << Work << " work\n";
        if(Alive.fetch_sub(1, memory_order::acq_rel) == 1) Alive.notify_all();
        ComputeRights.release();
    }; // end thread's code

    for(uint64_t InitThread = 0; InitThread < DrawingThreadCount; ++InitThread) {
        thread{ ComputeLine, InitThread*XsPerThread, 0, XsPerThread, screen::Height}.detach();
    }

    uint64_t Old = Alive.load(memory_order::acquire);
    while (Old != 0) {
        Alive.wait(Old);
        Old = Alive.load(memory_order::acquire);
    }

    ComputingPixelJulia.release();
};

inline void go_compute_mandelbrot() {
    using namespace std::complex_literals;
    using namespace std;

    vector<thread> ChunkLoader(thread::hardware_concurrency()-2);
    uint64_t ChunkSize = screen::Width/ChunkLoader.size() + 1;

    auto ChunkLoad = [ChunkSize](uint64_t Chunk) -> void {
        const uint64_t ChunkStart = ChunkSize * Chunk;
        for(uint64_t X = ChunkStart; X < ChunkStart + ChunkSize && X < screen::Width; ++X) {
            for(uint64_t Y = 0; Y < screen::Height; ++Y) {
                using namespace rl_combat;
                using namespace core;
                cplx C = screen_to_cplx(r2{implicit_cast<double>(X),implicit_cast<double>(Y)});
                cplx Z = 0.+0.i;
                uint64_t K = 0;
                for(; K < julia::N; ++K) {
                    if(abs(Z) >= 2.) break;
                    Z = julia::Func(C)(Z);
                }
                julia::MandelbrotSet[screen::at(X,Y)] =
                    color_lerp(rl::RAYWHITE, rl::RED, 
                            implicit_cast<double>(K)
                            /implicit_cast<double>(julia::N)
                    );
            }
        }
    };

    for(uint64_t Chunk = 0; Chunk < ChunkLoader.size(); ++Chunk) {
        ChunkLoader.at(Chunk) = thread{ChunkLoad, Chunk};
    }

    for(thread &Chunk: ChunkLoader) Chunk.join();
}

int main() {
    using namespace std;
    using namespace std::complex_literals;

    srand(time(NULL));

    rl::InitWindow(screen::Width, screen::Height, screen::Name);
    rl::SetTargetFPS(15);

    if constexpr(DEBUG) clog << "Threads Allowed: " << thread::hardware_concurrency() << '\n';
    cout << "Computing Mandelbrot..." << '\n';
    go_compute_mandelbrot();

    bool DisplayMandelbrot = true;
    bool DisplayDestinationSet = false;

    cplx JuliaConstant = 0.;
    while(!rl::WindowShouldClose()) {
        rl::ClearBackground(rl::RAYWHITE);
        rl::BeginDrawing();
        if(rl::IsMouseButtonDown(rl::MOUSE_BUTTON_LEFT)) {
            // might default this?
            JuliaConstant = rl_combat::screen_to_graph_normal(rl::GetMousePosition());

            if(ComputingPixelJulia.try_acquire()) {
                thread{go_compute_julia, JuliaConstant, thread::hardware_concurrency() - 2}.detach();
            }
        }

        if(rl::IsKeyPressed(rl::KEY_SPACE)) DisplayMandelbrot = !DisplayMandelbrot;
        if(rl::IsKeyPressed(rl::KEY_M)) DisplayDestinationSet = !DisplayDestinationSet;
        if(ComputingPixelJulia.try_acquire()) {
            if(rl::IsKeyDown(rl::KEY_UP)) { screen::Zoom += 0.1 * rl::GetFrameTime(); }
            if(rl::IsKeyDown(rl::KEY_DOWN)) { screen::Zoom -= 0.1 * rl::GetFrameTime(); }
            if(rl::IsKeyDown('W')) screen::Center = screen::Center + core::r2{0.0,0.1} * rl::GetFrameTime();
            if(rl::IsKeyDown('A')) screen::Center = screen::Center + core::r2{-0.1,0.0} * rl::GetFrameTime();
            if(rl::IsKeyDown('S')) screen::Center = screen::Center + core::r2{0.0,-0.1} * rl::GetFrameTime();
            if(rl::IsKeyDown('D')) screen::Center = screen::Center + core::r2{0.1,0.0} * rl::GetFrameTime();
            ComputingPixelJulia.release();
        }

        for(uint64_t X = 0; X < screen::Width; ++X)
            for(uint64_t Y = 0; Y < screen::Height; ++Y) {
                if(DisplayMandelbrot) {
                    rl::Color MandelColor = julia::MandelbrotSet[screen::at(X,Y)];
                    rl::Color JuliaColor = julia::JuliaSet[screen::at(X,Y)];
                    rl::Color Blend = rl_combat::color_lerp(MandelColor, JuliaColor, 0.25f);
                    rl::DrawPixel(X, Y, Blend);
                }
                else rl::DrawPixel(X, Y, julia::JuliaSet[screen::at(X,Y)]);
                if(DisplayDestinationSet)
                    rl::DrawCircleV(core::implicit_cast<core::r2>(julia::DestinationSet[screen::at(X,Y)]), 10.f, rl::BLACK);
            }

        rl::DrawFPS(10,10);

        // Pretty Printing
        {
            string Str = "C == {" + to_string(real(JuliaConstant)) + " + " + to_string(imag(JuliaConstant)) + "i}";
            rl::DrawText(Str.c_str(), 10, 30, 20, rl::ORANGE);
            string Zoom = "Zoom == " + to_string(screen::Zoom) + "\n";
            rl::DrawText(Zoom.c_str(), 10, 50, 20, rl::ORANGE);
            string CenterPos = "Center = " + to_string(screen::Center.X) + ", " + to_string(screen::Center.Y);
            rl::DrawText(CenterPos.c_str(), 10, 70, 20, rl::RED);

            // cplx FixedPoint = (1.-sqrt(1.-4.*JuliaConstant))/2.;
            // rl::DrawCircleV(CplxToGraph(FixedPoint), 5.f, rl::BLACK);
            // rl::DrawCircleV(CplxToGraph(conj(FixedPoint)), 5.f, rl::BLACK);
            // rl::DrawCircleV(CplxToGraph(-real(FixedPoint) + 1.i * imag(FixedPoint)), 5.f, rl::BLACK);
        }
        // Pretty Printing DEBUG
        if constexpr(DEBUG) {
            rl::DrawText(
                ("Alive == " + to_string(Alive.load(memory_order::acquire))).c_str(),
            10, 90, 20, rl::ORANGE);
        }

        rl::EndDrawing();
    }

    rl::CloseWindow();
}
