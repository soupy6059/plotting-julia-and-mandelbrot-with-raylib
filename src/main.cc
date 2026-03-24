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
#include "craylib.hpp"

#ifndef DEBUG
#define DEBUG 0
#endif

namespace screen {
    constexpr const uint64_t Width = 1000;
    constexpr const uint64_t Height = 1000;
    constexpr const char *const Name = "Julia Plotting";
    static inline uint64_t at(uint64_t X, uint64_t Y) {
        return X + Width * Y;
    }
};

using cplx = std::complex<double>;
template<typename type> using func = std::function<type>;
namespace julia {
    std::thread Computation;
    static rl::Color JuliaSet[screen::Width*screen::Height];
    static rl::Color MandelbrotSet[screen::Width*screen::Height];
    static cplx DestinationSet[screen::Width*screen::Height];
    constexpr uint64_t N = 1000;
    static auto Func = [](cplx C) -> func<cplx(cplx)> {
        return [C](cplx Z) -> cplx {
            return std::pow(Z, cplx{5.0}) + C;
        };
    };
};

namespace rl_combat {
    static inline rl::Vector2 screen_to_graph(rl::Vector2 ScrPos) {
        ScrPos = {ScrPos.x - screen::Width/2, ScrPos.y - screen::Height/2};
        ScrPos.y *= -1;
        return {ScrPos.x * 4.f/(float)screen::Width, ScrPos.y * 4.f/(float)screen::Height};
    }
    static inline rl::Vector2 graph_to_screen(rl::Vector2 GPos) {
        GPos = {GPos.x * screen::Width/4, GPos.y * screen::Height/4};
        GPos = {GPos.x, GPos.y * -1};
        GPos = {GPos.x + screen::Width / 2, GPos.y + screen::Height / 2};
        return GPos;
    }
    static inline rl::Color color_lerp(rl::Color Src, rl::Color Dest, float Reduct) {
        rl::Color Color = {0};
        Color.r = Reduct * Dest.r + (1.f-Reduct) * Src.r;
        Color.g = Reduct * Dest.g + (1.f-Reduct) * Src.g;
        Color.b = Reduct * Dest.b + (1.f-Reduct) * Src.b;
        Color.a = 255;
        return Color;
    }
};

static std::binary_semaphore ComputingPixelJulia{1};
static auto go_compute_julia = [](cplx JuliaConstant, uint64_t DrawingThreadCount) {
    using namespace std::complex_literals;
    using namespace std;
    
    atomic<uint64_t> Alive = DrawingThreadCount;
    uint64_t ThreadCountMax = 1 << 10;

    uint64_t XsPerThread = screen::Width/DrawingThreadCount + 1;

    counting_semaphore ComputeRights{thread::hardware_concurrency() - 2};
    auto JuliaFunc = julia::Func(JuliaConstant);

    double EscapeRadius = pow( (1. + sqrt(1 + 4.*abs(JuliaConstant)))/2., 2.0);

    function<void(uint64_t,uint64_t,uint64_t,uint64_t)> ComputeLine; 
    ComputeLine = [&,JuliaConstant,JuliaFunc](uint64_t StartX, uint64_t StartY, uint64_t SizeOfChunkX, uint64_t SizeOfChunkY) -> void {
        ComputeRights.acquire();
        uint64_t Work = 0;
        const uint64_t WorkCapacity = 1 << 21; // defualt == 1 << 21
        for(uint64_t X = StartX; X < SizeOfChunkX + StartX && X < screen::Width; ++X) {
            for(uint64_t Y = StartY; Y < SizeOfChunkY + StartY && Y < screen::Height; ++Y) {
                rl::Vector2 GraphCord = rl_combat::screen_to_graph({(float)X,(float)Y});
                cplx Z {(double)GraphCord.x, (double)GraphCord.y};
                uint64_t K = 0;
                for(; K < julia::N; ++K) {
                    if(norm(Z) >= EscapeRadius) break;
                    Z = JuliaFunc(Z);
                    ++Work;
                }
                constexpr const auto BetterGradient [[maybe_unused]] = [](double Factor) -> float {
                    return static_cast<float>(
                        1./(1.+exp(-10.*(Factor-0.25)))
                    );
                };
                constexpr const auto BetterGradient2 [[maybe_unused]] = [](double Factor) -> float { return pow(Factor, 0.2); };
                const float Colour = static_cast<float>(K)/static_cast<float>(julia::N);
                julia::JuliaSet[screen::at(X,Y)] = rl_combat::color_lerp(rl::DARKBLUE, rl::ORANGE, BetterGradient2(Colour));
                julia::DestinationSet[screen::at(X,Y)] = move(Z);

                if(Work >= WorkCapacity && SizeOfChunkX > 3 && rand()%2 && Alive.load(memory_order::acquire) < ThreadCountMax) {
                    SizeOfChunkX *= 2.f/3.f;
                    Alive.fetch_add(1, memory_order::relaxed);
                    thread{ComputeLine, StartX + SizeOfChunkX-1, StartY, SizeOfChunkX/2.f+2, SizeOfChunkY}.detach();
                    Work = 0;
                } else if(Work >= WorkCapacity && SizeOfChunkY > 3 && Alive.load(memory_order::acquire) < ThreadCountMax) {
                    SizeOfChunkY *= 2.f/3.f;
                    Alive.fetch_add(1, memory_order::relaxed);
                    thread{ComputeLine, StartX, StartY + SizeOfChunkY-1, SizeOfChunkX, SizeOfChunkY/2.f+2}.detach();
                    Work = 0;
                }
            }
        }
        if constexpr(DEBUG) if(Work >= WorkCapacity) clog << "Thread " << this_thread::get_id() << " did " << Work << " work\n";
        if(Alive.fetch_sub(1, memory_order::acq_rel) == 1) Alive.notify_all();
        ComputeRights.release();
    };

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
                rl::Vector2 GraphCord = rl_combat::screen_to_graph({(float)X,(float)Y});
                cplx C = (double)GraphCord.x + (double)GraphCord.y*1.0i;
                cplx Z = 0.+0.i;
                uint64_t K = 0;
                for(; K < julia::N; ++K) {
                    if(abs(Z) >= 2.) break;
                    Z = julia::Func(C)(Z);
                }
                julia::MandelbrotSet[screen::at(X,Y)] = rl_combat::color_lerp(rl::RAYWHITE, rl::RED, (float)K/(float)julia::N);
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
            rl::Vector2 MousePos = rl::GetMousePosition();
            MousePos = rl_combat::screen_to_graph(MousePos);
            JuliaConstant = cplx{(double)MousePos.x + ((double)MousePos.y)*1.i};

            if(ComputingPixelJulia.try_acquire()) {
                if(thread &Thrd = julia::Computation; Thrd.joinable()) Thrd.join();
                julia::Computation = thread{go_compute_julia, JuliaConstant, thread::hardware_concurrency() - 2};
            }
        }

        if(rl::IsKeyPressed(rl::KEY_SPACE)) {
            DisplayMandelbrot = !DisplayMandelbrot;
        }
        if(rl::IsKeyPressed(rl::KEY_D)) {
            DisplayDestinationSet = !DisplayDestinationSet;
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
                if(DisplayDestinationSet) {
                    cplx Z = julia::DestinationSet[screen::at(X,Y)];
                    rl::Vector2 ScreenPosOfZ = rl_combat::graph_to_screen({(float)real(Z),(float)imag(Z)});
                    rl::DrawCircleV(ScreenPosOfZ, 10.f, rl::BLACK);
                }
            }

        rl::DrawFPS(10,10);
        //string Str = "C == {" + to_string(real(JuliaConstant)) + " + " + to_string(imag(JuliaConstant)) + "i}";
        //rl::DrawText(Str.c_str(), 10, 30, 20, rl::ORANGE);
        rl::EndDrawing();
    }

    if(julia::Computation.joinable()) julia::Computation.join();
    rl::CloseWindow();
}
