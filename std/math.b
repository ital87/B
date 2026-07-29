namespace math {

pub const double PI = 3.141592653589793;
pub const double E = 2.718281828459045;
pub const double TAU = 6.283185307179586;

pub int absInt(int v) {
    if (v < 0) {
        return 0 - v;
    }
    return v;
}

pub double abs(double v) {
    if (v < 0.0) {
        return 0.0 - v;
    }
    return v;
}

pub int minInt(int a, int b) {
    if (a < b) {
        return a;
    }
    return b;
}

pub int maxInt(int a, int b) {
    if (a > b) {
        return a;
    }
    return b;
}

pub double min(double a, double b) {
    if (a < b) {
        return a;
    }
    return b;
}

pub double max(double a, double b) {
    if (a > b) {
        return a;
    }
    return b;
}

pub int clampInt(int v, int low, int high) {
    if (v < low) {
        return low;
    }
    if (v > high) {
        return high;
    }
    return v;
}

pub double floor(double v) {
    int truncated = (int)v;
    double result = (double)truncated;
    if (result > v) {
        return result - 1.0;
    }
    return result;
}

pub double ceil(double v) {
    double lowered = floor(v);
    if (lowered < v) {
        return lowered + 1.0;
    }
    return lowered;
}

pub double round(double v) {
    if (v < 0.0) {
        return 0.0 - floor(0.0 - v + 0.5);
    }
    return floor(v + 0.5);
}

pub double fmod(double v, double divisor) {
    if (divisor == 0.0) {
        return 0.0;
    }
    double quotient = v / divisor;
    double whole = (double)((int)quotient);
    return v - whole * divisor;
}

pub double sqrt(double v) {
    if (v < 0.0) {
        return 0.0;
    }
    if (v == 0.0) {
        return 0.0;
    }
    double guess = v;
    if (guess < 1.0) {
        guess = 1.0;
    }
    for (int i = 0; i < 40; i = i + 1) {
        double next = 0.5 * (guess + v / guess);
        double delta = next - guess;
        if (delta < 0.0) {
            delta = 0.0 - delta;
        }
        guess = next;
        if (delta < 0.0000000000001) {
            return guess;
        }
    }
    return guess;
}

pub double powInt(double base, int exponent) {
    bool inverted = exponent < 0;
    int remaining = absInt(exponent);
    double result = 1.0;
    double factor = base;
    while (remaining > 0) {
        if (remaining % 2 == 1) {
            result = result * factor;
        }
        factor = factor * factor;
        remaining = remaining / 2;
    }
    if (inverted) {
        if (result == 0.0) {
            return 0.0;
        }
        return 1.0 / result;
    }
    return result;
}

double normalizeAngle(double radians) {
    double reduced = fmod(radians, TAU);
    if (reduced > PI) {
        return reduced - TAU;
    }
    if (reduced < 0.0 - PI) {
        return reduced + TAU;
    }
    return reduced;
}

pub double sin(double radians) {
    double x = normalizeAngle(radians);
    double term = x;
    double sum = x;
    double squared = x * x;
    for (int n = 1; n < 12; n = n + 1) {
        double denominator = (double)((2 * n) * (2 * n + 1));
        term = 0.0 - term * squared / denominator;
        sum = sum + term;
    }
    return sum;
}

pub double cos(double radians) {
    double x = normalizeAngle(radians);
    double term = 1.0;
    double sum = 1.0;
    double squared = x * x;
    for (int n = 1; n < 12; n = n + 1) {
        double denominator = (double)((2 * n - 1) * (2 * n));
        term = 0.0 - term * squared / denominator;
        sum = sum + term;
    }
    return sum;
}

pub double tan(double radians) {
    double divisor = cos(radians);
    if (divisor == 0.0) {
        return 0.0;
    }
    return sin(radians) / divisor;
}

pub double exp(double v) {
    double term = 1.0;
    double sum = 1.0;
    for (int n = 1; n < 30; n = n + 1) {
        term = term * v / (double)n;
        sum = sum + term;
    }
    return sum;
}

pub double ln(double v) {
    if (v <= 0.0) {
        return 0.0;
    }
    int shifts = 0;
    double scaled = v;
    while (scaled > 2.0) {
        scaled = scaled / 2.0;
        shifts = shifts + 1;
    }
    while (scaled < 0.5) {
        scaled = scaled * 2.0;
        shifts = shifts - 1;
    }
    double z = (scaled - 1.0) / (scaled + 1.0);
    double squared = z * z;
    double term = z;
    double sum = z;
    for (int n = 1; n < 30; n = n + 1) {
        term = term * squared;
        sum = sum + term / (double)(2 * n + 1);
    }
    return 2.0 * sum + (double)shifts * 0.6931471805599453;
}

pub double pow(double base, double exponent) {
    if (base <= 0.0) {
        return 0.0;
    }
    return exp(exponent * ln(base));
}

pub double hypot(double a, double b) {
    return sqrt(a * a + b * b);
}

}
