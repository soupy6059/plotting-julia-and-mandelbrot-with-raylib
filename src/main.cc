#include <iostream>
#include <functional>
#include <complex>
#include <array>
#include <cinttypes>
#include <string>
#include <random>
#include <thread>
#include "craylib.hpp"

struct raylib {
    struct screen {
        uint64_t Width; 
        uint64_t Height;
        std::string Name;
        inline uint64_t at(uint64_t X, uint64_t Y) {
            return X + Height * Y;
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

    // 0.f ==> use Src
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

std::atomic<bool> ComputingPixelJulia = false;
inline auto go_compute_pixel_julia(raylib &Raylib, std::vector<rl::Color> &Colours, cplx JuliaConstant, uint64_t DrawingThreadCount) {
    using namespace std::complex_literals;
    using namespace std;
    ComputingPixelJulia = true;
    constexpr uint64_t N = 1000;

    vector<thread> ComputePool(DrawingThreadCount);
    uint64_t XsPerThread = Raylib.Screen.Width/ComputePool.size() + 1;
    atomic<uint64_t> Done = 0;

    auto ComputeLine = [XsPerThread](uint64_t Start, raylib &Raylib, vector<rl::Color> &Colours, cplx JuliaConstant, atomic<uint64_t> &Done) -> void {
        for(uint64_t X = Start; X < XsPerThread + Start && X < Raylib.Screen.Width; ++X) {
            for(uint64_t Y = 0; Y < Raylib.Screen.Height; ++Y) {
                rl::Vector2 GraphCord = Raylib.screen_to_graph({(float)X,(float)Y});
                cplx Z = (double)GraphCord.x + (double)GraphCord.y*1.0i;
                bool TooLarge = false;
                uint64_t K = 0;
                for(; K < N; ++K) {
                    if(abs(Z) >= abs(JuliaConstant)+1.) {
                        TooLarge = true;
                        break;
                    }
                    Z = julia(JuliaConstant)(Z);
                }
                if(TooLarge) {
                    float Factor = (float)K/(float)N;
                    Colours[Raylib.Screen.at(X,Y)] = Raylib.color_lerp(rl::BLUE, rl::RAYWHITE, Factor);
                }
                else Colours[Raylib.Screen.at(X,Y)] = rl::RAYWHITE;
            }
        }
        ++Done;
    };

    for(uint64_t Thread = 0; Thread < ComputePool.size(); ++Thread) {
        ComputePool.at(Thread) = thread{ ComputeLine, Thread*XsPerThread, ref(Raylib), ref(Colours), JuliaConstant, ref(Done) };
        ComputePool.at(Thread).detach();
    }
    while(Done != ComputePool.size()) {/*sleep*/}

    ComputingPixelJulia = false;
}

void go_compute_mandelbrot(raylib &Raylib, std::vector<rl::Color> &Colours) {
    using namespace std::complex_literals;
    using namespace std;
    constexpr uint64_t N = 500;
    for(uint64_t X = 0; X < Raylib.Screen.Width; ++X) {
        for(uint64_t Y = 0; Y < Raylib.Screen.Height; ++Y) {
            rl::Vector2 GraphCord = Raylib.screen_to_graph({(float)X,(float)Y});
            cplx C = (double)GraphCord.x + (double)GraphCord.y*1.0i;
            cplx Z = 0.+0.i;
            bool TooLarge = false;
            uint64_t K = 0;
            for(; K < N; ++K) {
                if(abs(Z) >= 2.) {
                    TooLarge = true;
                    break;
                }
                Z = julia(C)(Z);
            }
            if(TooLarge) {
                float Factor = (float)K/(float)N;
                Colours[Raylib.Screen.at(X,Y)] = Raylib.color_lerp(rl::RAYWHITE, rl::RED, Factor);
            }
            else Colours[Raylib.Screen.at(X,Y)] = rl::RED;
        }
    }
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
        go_compute_pixel_julia(Raylib, Pixels, Constant, thread::hardware_concurrency() - 1);
        //go_compute_pixel_julia(Raylib, Pixels, Constant, 200);
    };

    vector<rl::Color> Mandelbrot(Raylib.Screen.Width*Raylib.Screen.Height);
    cout << "Threads Allowed: " << std::thread::hardware_concurrency() << '\n';
    cout << "Computing Mandelbrot..." << '\n';
    go_compute_mandelbrot(Raylib, Mandelbrot);

    bool DisplayMandelbrot = true;

    Raylib.draw([&](raylib *This) {
        if(rl::IsMouseButtonDown(rl::MOUSE_BUTTON_LEFT)) {
            rl::Vector2 MousePos = rl::GetMousePosition();
            MousePos = This->screen_to_graph(MousePos);
            Julia.Constant = cplx{(double)MousePos.x + ((double)MousePos.y)*1.i};

            if(!ComputingPixelJulia) {
                using enum raylib::thread_name;
                ComputingPixelJulia = true;
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
                    rl::Color Blend = This->color_lerp(MandelColor, Pixel, 0.75f);
                    rl::DrawPixel(X, Y, Blend);
                }
                else rl::DrawPixel(X, Y, Pixels[This->Screen.at(X,Y)]);

        rl::DrawFPS(10,10);
        string Str = "C == {" + to_string(real(Julia.Constant)) + " + " + to_string(imag(Julia.Constant)) + "i}";
        rl::DrawText(Str.c_str(), 10, 30, 20, rl::ORANGE);
    });
}
