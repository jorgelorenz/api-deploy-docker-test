#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include <omp.h>

// ---------------------------------------------------------------------------
// Minimal JSON helpers (fixed schema, no external libs)
// ---------------------------------------------------------------------------

static void skip_ws(const char *&p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
}

static bool expect_char(const char *&p, char c) {
    skip_ws(p);
    if (*p == c) { ++p; return true; }
    return false;
}

static bool parse_string(const char *&p, std::string &out) {
    skip_ws(p);
    if (*p != '"') return false;
    ++p;
    out.clear();
    while (*p && *p != '"') { out += *p; ++p; }
    if (*p == '"') { ++p; return true; }
    return false;
}

static bool parse_number(const char *&p, double &out) {
    skip_ws(p);
    char *end = nullptr;
    out = strtod(p, &end);
    if (end == p) return false;
    p = end;
    return true;
}

static bool parse_int(const char *&p, long long &out) {
    skip_ws(p);
    char *end = nullptr;
    out = strtoll(p, &end, 10);
    if (end == p) return false;
    p = end;
    return true;
}

struct SimInput {
    double S0;
    double volatility;
    double r;
    double q;
    long long N;
    std::vector<double> dates;
};

static bool parse_input(const char *json, SimInput &in) {
    const char *p = json;
    if (!expect_char(p, '{')) return false;

    bool got_S0 = false, got_vol = false, got_r = false, got_q = false, got_N = false, got_dates = false;

    while (true) {
        std::string key;
        if (!parse_string(p, key)) break;
        if (!expect_char(p, ':')) return false;

        if (key == "S0")         { if (!parse_number(p, in.S0)) return false; got_S0 = true; }
        else if (key == "volatility") { if (!parse_number(p, in.volatility)) return false; got_vol = true; }
        else if (key == "r")     { if (!parse_number(p, in.r)) return false; got_r = true; }
        else if (key == "q")     { if (!parse_number(p, in.q)) return false; got_q = true; }
        else if (key == "N")     { if (!parse_int(p, in.N)) return false; got_N = true; }
        else if (key == "dates") {
            if (!expect_char(p, '[')) return false;
            in.dates.clear();
            double v;
            while (parse_number(p, v)) {
                in.dates.push_back(v);
                skip_ws(p);
                if (*p == ',') ++p;
            }
            if (!expect_char(p, ']')) return false;
            got_dates = true;
        } else {
            // skip unknown value (simple: skip until , or })
            skip_ws(p);
            if (*p == '"') { std::string tmp; parse_string(p, tmp); }
            else { double tmp; parse_number(p, tmp); }
        }

        skip_ws(p);
        if (*p == ',') { ++p; continue; }
        break;
    }

    return got_S0 && got_vol && got_r && got_q && got_N && got_dates;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

static bool validate(const SimInput &in, std::string &err) {
    if (in.S0 <= 0)          { err = "S0 must be > 0"; return false; }
    if (in.volatility < 0)   { err = "volatility must be >= 0"; return false; }
    if (in.N < 1 || in.N > 10000000) { err = "N must be between 1 and 10000000"; return false; }
    if (in.dates.empty())    { err = "dates must not be empty"; return false; }
    for (size_t i = 0; i < in.dates.size(); ++i) {
        if (in.dates[i] <= 0) { err = "all dates must be > 0"; return false; }
        if (i > 0 && in.dates[i] <= in.dates[i - 1]) { err = "dates must be strictly increasing"; return false; }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Monte Carlo GBM simulation
// ---------------------------------------------------------------------------

static void run_simulations(const SimInput &in, std::vector<std::vector<double>> &results) {
    const int N = static_cast<int>(in.N);
    const int T = static_cast<int>(in.dates.size());
    results.resize(N, std::vector<double>(T));

    // Pre-compute dt and drift/diffusion per interval
    std::vector<double> dt(T);
    std::vector<double> drift(T);
    std::vector<double> diffusion(T);

    for (int j = 0; j < T; ++j) {
        dt[j] = (j == 0) ? in.dates[0] : in.dates[j] - in.dates[j - 1];
        drift[j] = (in.r - in.q - 0.5 * in.volatility * in.volatility) * dt[j];
        diffusion[j] = in.volatility * std::sqrt(dt[j]);
    }

    #pragma omp parallel
    {
        // Each thread gets its own RNG seeded by thread id + fixed seed
        unsigned int seed = 42u + static_cast<unsigned int>(omp_get_thread_num());
        std::mt19937 rng(seed);
        std::normal_distribution<double> norm(0.0, 1.0);

        #pragma omp for schedule(static)
        for (int i = 0; i < N; ++i) {
            double S = in.S0;
            for (int j = 0; j < T; ++j) {
                double Z = norm(rng);
                S = S * std::exp(drift[j] + diffusion[j] * Z);
                results[i][j] = S;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// JSON output
// ---------------------------------------------------------------------------

static void write_json(FILE *out, const std::vector<std::vector<double>> &results) {
    fprintf(out, "{\"simulations\":[");
    for (size_t i = 0; i < results.size(); ++i) {
        if (i > 0) fputc(',', out);
        fputc('[', out);
        for (size_t j = 0; j < results[i].size(); ++j) {
            if (j > 0) fputc(',', out);
            fprintf(out, "%.6f", results[i][j]);
        }
        fputc(']', out);
    }
    fprintf(out, "]}");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
    // Health mode
    if (argc >= 2 && strcmp(argv[1], "--health") == 0) {
        printf("{\"status\":\"ok\"}");
        return 0;
    }

    // Simulation mode: read JSON from stdin
    std::string input;
    {
        char buf[4096];
        while (size_t n = fread(buf, 1, sizeof(buf), stdin)) {
            input.append(buf, n);
        }
    }

    if (input.empty()) {
        fprintf(stderr, "No input provided\n");
        return 1;
    }

    SimInput in;
    if (!parse_input(input.c_str(), in)) {
        fprintf(stderr, "Failed to parse input JSON\n");
        return 1;
    }

    std::string err;
    if (!validate(in, err)) {
        fprintf(stderr, "Validation error: %s\n", err.c_str());
        return 1;
    }

    std::vector<std::vector<double>> results;
    run_simulations(in, results);
    write_json(stdout, results);

    return 0;
}
