#include <iostream>
#include <functional>
#include <complex>
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
    } Screen;
    
    enum thread_name : uint64_t {
        JuliaPixel, MANDELBROT
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
};

using cplx = std::complex<double>;
template<typename type> using func = std::function<type>;

func<cplx(cplx)> julia(cplx C) {
    return [C](cplx Z) -> cplx {
        return std::pow(Z, cplx{2.0}) + C;
    };
}

std::atomic<bool> ComputingPixelJulia = false;
inline auto go_compute_pixel_julia(raylib &Raylib, std::vector<rl::Color> &Colours, cplx JuliaConstant) {
    using namespace std::complex_literals;
    ComputingPixelJulia = true;
    constexpr uint64_t N = 1000;
    for(uint64_t X = 0; X < Raylib.Screen.Width; ++X) {
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
                Z = std::pow(Z, 2.0) + JuliaConstant;
            }
            if(TooLarge) {
                unsigned Factor = (unsigned)(((float)N-(float)K)/(float)N * 255.f);
                if(Factor > 255) Factor = 255;
                unsigned Color = (Factor << 6) | (Factor << 4) | (Factor << 2) | 0x00;
                Colours[X+Raylib.Screen.Width*Y] = rl::GetColor(Color);
            }
            else Colours[X+Raylib.Screen.Width*Y] = rl::ORANGE;
        }
    }
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
                Z = std::pow(Z, 2.0) + C;
            }
            if(TooLarge) {
                unsigned Factor = (unsigned)(((float)N-(float)K)/(float)N * 255.f);
                if(Factor > 255) Factor = 255;
                unsigned Color = (Factor << 6) | (Factor << 4) | (Factor << 2) | 0x00;
                Colours[X+Raylib.Screen.Width*Y] = rl::GetColor(Color);

                Colours[X+Raylib.Screen.Width*Y] = rl::RAYWHITE;
            }
            else Colours[X+Raylib.Screen.Width*Y] = rl::RED;
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
        go_compute_pixel_julia(Raylib, Pixels, Constant);
    };

    vector<rl::Color> Mandelbrot(Raylib.Screen.Width*Raylib.Screen.Height);
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
                if(rl::Color MandelColor = Mandelbrot[X+Y*This->Screen.Width]; DisplayMandelbrot && rl::ColorToInt(MandelColor) != rl::ColorToInt(rl::RAYWHITE)) {
                    rl::Color Pixel = Pixels[X+Y*This->Screen.Width];
                    rl::Color Blend = rl::GetColor(((long)rl::ColorToInt(MandelColor) + (long)rl::ColorToInt(Pixel))/2);
                    rl::DrawPixel(X, Y, Blend);
                }
                else
                    rl::DrawPixel(X,Y, Pixels[X+Y*This->Screen.Width]);

        rl::DrawFPS(10,10);
        string Str = "C == {" + to_string(real(Julia.Constant)) + " + " + to_string(imag(Julia.Constant)) + "i}";
        rl::DrawText(Str.c_str(), 10, 30, 20, rl::ORANGE);
    });
}
