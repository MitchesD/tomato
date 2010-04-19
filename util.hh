#pragma once

#include <cstdlib>
#include <cmath>
#include <sstream>
#include <vector>
#include <stdexcept>

#include <GL/gl.h>
#include <SDL.h>

#define PI 3.1415926535

/// Timer
struct Countdown {
	Countdown(float seconds = 0): endtime(SDL_GetTicks() + seconds*1000) { }
	bool operator()() const { return SDL_GetTicks() >= endtime; }
	unsigned int endtime;
};


/// FPS counter
struct FPS {
	FPS(): record(SDL_GetTicks()), time(0) { }
	void update() { time = (SDL_GetTicks() - record) / 1000.0f; record = SDL_GetTicks(); }
	float getTime() const { return time; }
	float getFPS() const { return 1.0f / time; }
	void debugPrint() const { std::cout << "FPS: " << getFPS() << " (" << getTime() << " ms)" << std::endl; }
	unsigned int record;
	float time;
};


/// Struct to store color information
struct Color {
	float r, ///< red component
		  g, ///< green
		  b, ///< blue
		  a; ///< alpha value
	/// create nec Color object with given channels
	Color(float r_ = 0.0, float g_ = 0.0, float b_ = 0.0, float a_ = 1.0): r(r_), g(g_), b(b_), a(a_) {}
	/// overload float cast
	operator float*() { return reinterpret_cast<float*>(this); }
	/// overload float const cast
	operator float const*() const { return reinterpret_cast<float const*>(this); }
};


/// Math

template<typename T>
int inline sign(T num) { return num > 0 ? 1 : (num < 0 ? -1 : 0); }

bool inline randbool() { return rand() % 2 == 0; }

int inline randint(int hi) { return rand() % hi; }

int inline randint(int lo, int hi) {
	return (rand() % (hi - lo + 1)) + lo;
}

float inline randf(float lo, float hi) {
	return (rand() / float(RAND_MAX) * (hi - lo )) + lo;
}

void inline swapdir(int& dir) { if (dir == 1) dir = -1; else dir = 1; }

int inline randdir() { return randbool() ? 1 : -1; }

void inline randdir(int& dx, int &dy) {
	if (randbool()) { dx = randdir(); dy = randint(-1,1); }
	else { dx = randint(-1,1); dy = randdir(); }
}

/// Limit val to range [min, max]
template <typename T> T clamp(T val, T min = 0, T max = 1) {
	if (min > max) throw std::logic_error("min > max");
	if (val < min) return min;
	if (val > max) return max;
	return val;
}

/// Linear interpolation
template <typename T> T lerp(T x1, T x2, T factor = 0.5) {
	return x1 + (x2 - x1) * factor;
}


template<typename T>
std::string num2str(T i) { std::ostringstream oss; oss << i; return oss.str(); }
