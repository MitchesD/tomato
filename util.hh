#pragma once

#include <cstdlib>
#include <cmath>
#include <sstream>
#include <vector>

#include <GL/gl.h>

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


template<typename T>
std::string num2str(T i) { std::ostringstream oss; oss << i; return oss.str(); }
