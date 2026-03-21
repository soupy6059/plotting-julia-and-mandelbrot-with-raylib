#include <iostream>
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

struct raylib {
    struct screen {
        uint64_t Width; 
        uint64_t Height;
        std::string Name;
        inline uint64_t at(uint64_t X, uint64_t Y) {
            return X + Width * Y;
        }
    } Screen;
    
    enum thread_name : uint64_t {
        JuliaPixel
    };
    std::array<std::thread,1> Pool;
    raylib(screen Screen): Screen{Screen} {
        rl::InitWindow(Screen.Width, Screen.Height, Screen.Name.c_str());
    }
    raylib &set_target_FPS(uint64_t FPS) {
        rl::SetTargetFPS(FPS);
        return *this;
    }
    static constexpr uint64_t CLEARBACKGROUND = (1 << 0);
    raylib &draw(std::function<void(raylib*)> fn, uint64_t OPTIONS = CLEARBACKGROUND) {
        while(!rl::WindowShouldClose()) {
            rl::BeginDrawing();
                if(OPTIONS & CLEARBACKGROUND) rl::ClearBackground(rl::RAYWHITE);
                fn(this);
            rl::EndDrawing();
        } 
        for(std::thread &Thread: Pool) Thread.join();
        return *this;
    }
    ~raylib() {
        rl::CloseWindow();
    }
    void draw_pixel(rl::Vector2 Z, rl::Color Colour) {
        Z = {Z.x * Screen.Width/4, Z.y * Screen.Height/4};
        Z = {Z.x, Z.y * -1};
        Z = {Z.x + Screen.Width / 2, Z.y + Screen.Height / 2};
        rl::DrawPixelV(Z, Colour);
    }
    rl::Vector2 screen_to_graph(rl::Vector2 ScrPos) {
        ScrPos = {ScrPos.x - Screen.Width/2, ScrPos.y - Screen.Height/2};
        ScrPos.y *= -1;
        return {ScrPos.x * 4.f/(float)Screen.Width, ScrPos.y * 4.f/(float)Screen.Height};
    }
    rl::Vector2 graph_to_screen(rl::Vector2 GPos) {
        GPos = {GPos.x * Screen.Width/4, GPos.y * Screen.Height/4};
        GPos = {GPos.x, GPos.y * -1};
        GPos = {GPos.x + Screen.Width / 2, GPos.y + Screen.Height / 2};
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

using cplx = std::complex<double>;
template<typename type> using func = std::function<type>;

static auto julia = [](cplx C) -> func<cplx(cplx)> {
    return [C](cplx Z) -> cplx {
        return std::pow(Z, cplx{2.0}) + C;
    };
};

std::binary_semaphore ComputingPixelJulia{1};
inline auto go_compute_pixel_julia(raylib &Raylib, std::vector<rl::Color> &Colours, cplx JuliaConstant, uint64_t DrawingThreadCount) {
    using namespace std::complex_literals;
    using namespace std;
    constexpr uint64_t N = 1000;
    
    binary_semaphore PushRight{1};
    vector<thread> ComputePool(DrawingThreadCount);
    ComputePool.reserve(1920);
    atomic<uint64_t> Alive = ComputePool.size();

    uint64_t XsPerThread = Raylib.Screen.Width/ComputePool.size() + 1;

    counting_semaphore ComputeRights{thread::hardware_concurrency() - 2};
    auto JuliaFunc = julia(JuliaConstant);

    function<void(uint64_t,uint64_t)> ComputeLine; 
    ComputeLine = [&,JuliaFunc](uint64_t Start, uint64_t SizeOfChunk) -> void {
        ComputeRights.acquire();
        uint64_t Work = 0;
        const uint64_t WorkCapacity = 1 << 21;
        static_assert(WorkCapacity == 2097152);
        for(uint64_t X = Start; X < SizeOfChunk + Start && X < Raylib.Screen.Width; ++X) {
            for(uint64_t Y = 0; Y < Raylib.Screen.Height; ++Y) {
                rl::Vector2 GraphCord = Raylib.screen_to_graph({(float)X,(float)Y});
                cplx Z = (double)GraphCord.x + (double)GraphCord.y*1.0i;
                uint64_t K = 0;
                for(; K < N; ++K) {
                    if(abs(Z) >= abs(JuliaConstant)+1.) break;
                    Z = JuliaFunc(Z);
                    ++Work;
                }
                constexpr const auto BetterGradient = [](double Factor) -> float {
                    return static_cast<float>(
                        1./(1.+exp(-10.*(Factor-0.25)))
                    );
                };
                const float Colour = static_cast<float>(K)/static_cast<float>(N);
                Colours[Raylib.Screen.at(X,Y)] = Raylib.color_lerp(rl::DARKBLUE, rl::ORANGE, BetterGradient(Colour));
            }
           if(Work >= WorkCapacity && SizeOfChunk > 3) {
                SizeOfChunk *= 2.f/3.f;
                Alive.fetch_add(1, memory_order::relaxed);
                PushRight.acquire();
                ComputePool.push_back(thread{
                    ComputeLine, Start + SizeOfChunk-1, SizeOfChunk/2.f+2
                });
                (ComputePool.end()-1)->detach();
                PushRight.release();
                Work = 0;
            }
        }
        if constexpr(DEBUG) if(Work >= WorkCapacity) clog << "Thread " << this_thread::get_id() << " did " << Work << " work\n";
        if(Alive.fetch_sub(1, memory_order::acq_rel) == 1) Alive.notify_all();
        ComputeRights.release();
    };

    for(uint64_t InitThread = 0; InitThread < ComputePool.size(); ++InitThread) {
        ComputePool.at(InitThread) = thread{ ComputeLine, InitThread*XsPerThread, XsPerThread };
        ComputePool.at(InitThread).detach();
    }

    uint64_t Old = Alive.load(memory_order::acquire);
    while (Old != 0) {
        Alive.wait(Old);
        Old = Alive.load(memory_order::acquire);
    }

    ComputingPixelJulia.release();
}

void go_compute_mandelbrot(raylib &Raylib, std::vector<rl::Color> &Colours) {
    using namespace std::complex_literals;
    using namespace std;
    constexpr uint64_t N = 1000;

    vector<thread> ChunkLoader(thread::hardware_concurrency()-2);
    uint64_t ChunkSize = Raylib.Screen.Width/ChunkLoader.size() + 1;

    auto ChunkLoad = [=](raylib &Raylib, vector<rl::Color> &Colours, uint64_t Chunk) -> void {
        const uint64_t ChunkStart = ChunkSize * Chunk;
        for(uint64_t X = ChunkStart; X < ChunkStart + ChunkSize && X < Raylib.Screen.Width; ++X) {
            for(uint64_t Y = 0; Y < Raylib.Screen.Height; ++Y) {
                rl::Vector2 GraphCord = Raylib.screen_to_graph({(float)X,(float)Y});
                cplx C = (double)GraphCord.x + (double)GraphCord.y*1.0i;
                cplx Z = 0.+0.i;
                uint64_t K = 0;
                for(; K < N; ++K) {
                    if(abs(Z) >= 2.) break;
                    Z = julia(C)(Z);
                }
                Colours[Raylib.Screen.at(X,Y)] = Raylib.color_lerp(rl::RAYWHITE, rl::RED, (float)K/(float)N);
            }
        }
    };

    for(uint64_t Chunk = 0; Chunk < ChunkLoader.size(); ++Chunk) {
        ChunkLoader.at(Chunk) = thread{ChunkLoad, ref(Raylib), ref(Colours), Chunk};
    }

    for(thread &Chunk: ChunkLoader) Chunk.join();
}

int main() {
    using namespace std;
    using namespace std::complex_literals;

    struct {func<cplx(cplx)> fn; cplx Constant; } Julia = {nullptr, -1.};
    Julia.fn = julia(Julia.Constant);

    raylib Raylib {{1000,1000,"Julia"}};
    Raylib.set_target_FPS(15);

    vector<rl::Color> Pixels(Raylib.Screen.Width*Raylib.Screen.Height);
    auto GoComputeJulia = [](raylib &Raylib, decltype(Pixels) &Pixels, cplx &Constant) -> void {
        go_compute_pixel_julia(Raylib, Pixels, Constant, 100);
    };

    vector<rl::Color> Mandelbrot(Raylib.Screen.Width*Raylib.Screen.Height);
    cout << "Threads Allowed: " << thread::hardware_concurrency() << '\n';
    cout << "Computing Mandelbrot..." << '\n';
    go_compute_mandelbrot(Raylib, Mandelbrot);

    bool DisplayMandelbrot = true;

    Raylib.draw([&](raylib *This) {
        if(rl::IsMouseButtonDown(rl::MOUSE_BUTTON_LEFT)) {
            rl::Vector2 MousePos = rl::GetMousePosition();
            MousePos = This->screen_to_graph(MousePos);
            Julia.Constant = cplx{(double)MousePos.x + ((double)MousePos.y)*1.i};

            if(ComputingPixelJulia.try_acquire()) {
                using enum raylib::thread_name;
                if(thread &Thrd = This->Pool.at(JuliaPixel); Thrd.joinable()) Thrd.join();
                This->Pool.at(JuliaPixel) = thread{GoComputeJulia, ref(*This), ref(Pixels), ref(Julia.Constant)};
            }
        }

        if(rl::IsKeyPressed(rl::KEY_SPACE)) {
            DisplayMandelbrot = !DisplayMandelbrot;
        }
        

        for(uint64_t X = 0; X < This->Screen.Width; ++X)
            for(uint64_t Y = 0; Y < This->Screen.Height; ++Y)
                if(rl::Color MandelColor = Mandelbrot[X+Y*This->Screen.Width]; DisplayMandelbrot) {
                    rl::Color Pixel = Pixels[This->Screen.at(X,Y)];
                    rl::Color Blend = This->color_lerp(MandelColor, Pixel, 0.25f);
                    rl::DrawPixel(X, Y, Blend);
                }
                else rl::DrawPixel(X, Y, Pixels[This->Screen.at(X,Y)]);

        rl::DrawFPS(10,10);
        string Str = "C == {" + to_string(real(Julia.Constant)) + " + " + to_string(imag(Julia.Constant)) + "i}";
        rl::DrawText(Str.c_str(), 10, 30, 20, rl::ORANGE);
    });
}
