#include "position.hpp"
#include "search.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace catchup;

namespace {

struct Opening {
    std::uint32_t seed;
    int stones;
};

// Frozen opening set for reproducible Elo gates. The first 24 match the
// original suite; the rest add stone counts so later games are new positions.
const Opening OPENINGS[] = {
    {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0},
    {101, 8}, {102, 8}, {103, 8}, {104, 8}, {105, 8}, {106, 8}, {107, 8}, {108, 8},
    {201, 16}, {202, 16}, {203, 16}, {204, 16}, {205, 16}, {206, 16}, {207, 16}, {208, 16},
    {301, 4}, {302, 4}, {303, 4}, {304, 4}, {305, 4}, {306, 4}, {307, 4}, {308, 4},
    {401, 12}, {402, 12}, {403, 12}, {404, 12}, {405, 12}, {406, 12}, {407, 12}, {408, 12},
    {501, 20}, {502, 20}, {503, 20}, {504, 20}, {505, 20}, {506, 20}, {507, 20}, {508, 20},
    {601, 24}, {602, 24}, {603, 24}, {604, 24}, {605, 24}, {606, 24}, {607, 24}, {608, 24},
    {701, 28}, {702, 28}, {703, 28}, {704, 28}, {705, 28}, {706, 28}, {707, 28}, {708, 28},
    {801, 32}, {802, 32}, {803, 32}, {804, 32}, {805, 32}, {806, 32}, {807, 32}, {808, 32},
    {901, 36}, {902, 36}, {903, 36}, {904, 36}, {905, 36}, {906, 36}, {907, 36}, {908, 36},
    {1001, 40}, {1002, 40}, {1003, 40}, {1004, 40}, {1005, 40}, {1006, 40}, {1007, 40}, {1008, 40},
    {1101, 44}, {1102, 44}, {1103, 44}, {1104, 44}, {1105, 44}, {1106, 44}, {1107, 44}, {1108, 44},
};
const int N_OPENINGS = static_cast<int>(sizeof(OPENINGS) / sizeof(OPENINGS[0]));
static_assert(N_OPENINGS == 96, "frozen opening suite is 96 positions");

std::uint64_t openings_hash() {
    std::uint64_t h = 14695981039346656037ull;
    for (int i = 0; i < N_OPENINGS; ++i) {
        h ^= OPENINGS[i].seed;
        h *= 1099511628211ull;
        h ^= static_cast<std::uint64_t>(OPENINGS[i].stones);
        h *= 1099511628211ull;
    }
    h ^= static_cast<std::uint64_t>(N_OPENINGS);
    h *= 1099511628211ull;
    return h;
}

std::string hash_hex(std::uint64_t h) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

const int DEFAULT_WEIGHTS[15] = {
    100, 2, 1, 2, 0, 25, 8, 20, 40, 4, 16, 25, 8, 80, 10
};

struct EngineOpts {
    int weights[15];
    int tt_bits = 22;
    bool use_countermove = true;
    bool use_stable_stop = true;
    bool use_history = true;
    bool use_lmr = true;
    double lmr_divisor = 1.8;
    int lmr_quiet_cap = 5;
    bool use_iid = false;
    bool tt_keep_deep = false;
    bool lmp_by_placements = false;
    bool swing_asymmetric = false;
    bool history_gift_split = false;
    bool use_race = false;
    bool use_unique = false;
    bool use_forced_gift = false;
    bool use_nmp = true;
    double move_wall = 45.0;
};

Engine make_engine(const EngineOpts& o) {
    return Engine(true, true, true, o.use_history, true, o.use_lmr, false, 64, 120,
                  o.weights[0], o.weights[1], o.weights[2], o.weights[3], o.weights[4],
                  o.weights[5], o.weights[6], o.weights[7], o.weights[8], o.weights[9],
                  o.weights[10], o.weights[11], o.weights[12],
                  true, true, o.use_iid, o.weights[13], o.weights[14],
                  o.tt_bits, o.use_countermove, o.use_stable_stop,
                  o.lmr_divisor, o.lmr_quiet_cap);
}

void apply_opts(Engine& e, const EngineOpts& o) {
    e.tt_keep_deep = o.tt_keep_deep;
    e.lmp_by_placements = o.lmp_by_placements;
    e.swing_asymmetric = o.swing_asymmetric;
    e.history_gift_split = o.history_gift_split;
    e.use_race = o.use_race;
    e.use_unique = o.use_unique;
    e.use_forced_gift = o.use_forced_gift;
    e.use_nmp = o.use_nmp;
    e.use_iid = o.use_iid;
    e.move_wall = o.move_wall;
}

double fractional_reserve(double total_time) {
    double reserve = 0.06 * total_time;
    if (reserve < 2.0) reserve = 2.0;
    if (reserve > total_time) reserve = total_time;
    return reserve;
}

bool parse_weights(const std::string& text, int* out) {
    std::stringstream ss(text);
    std::string tok;
    int n = 0;
    while (std::getline(ss, tok, ',') && n < 15) {
        out[n++] = std::atoi(tok.c_str());
    }
    return n == 15;
}

double score_to_elo(double s) {
    if (s <= 0.0) return -999.0;
    if (s >= 1.0) return 999.0;
    return 400.0 * std::log10(s / (1.0 - s));
}

double elo_to_p(double elo) {
    return 1.0 / (1.0 + std::pow(10.0, -elo / 400.0));
}

int play_game(Engine& white, Engine& black, GameState state, double time_each,
              bool use_clock, double total_time) {
    white.new_game();
    black.new_game();
    double reserve = fractional_reserve(total_time);
    Clock clock_w(total_time, reserve);
    Clock clock_b(total_time, reserve);
    Engine* engines[2] = {&white, &black};
    Clock* clocks[2] = {&clock_w, &clock_b};
    int moves = 0;
    while (!state.is_over() && moves < 80) {
        int mover = state.to_move;
        double budget = time_each;
        if (use_clock) budget = clocks[mover]->allocate(state);
        int empty_before = state.empty_count;
        auto result = engines[mover]->find_turn(state, budget);
        state.play_turn(result.first);
        int stones = empty_before - state.empty_count;
        if (stones < 0) stones = 0;
        if (use_clock) {
            clocks[mover]->note_own_stones(stones);
            clocks[1 - mover]->note_reply_stones(stones);
            clocks[mover]->consume(result.second.elapsed);
        }
        moves += 1;
    }
    return state.compare_final();
}

struct GameOutcome {
    int from_a = 0;
    std::uint32_t seed = 0;
    int stones = 0;
    bool a_white = false;
};

GameOutcome play_indexed(Engine& a, Engine& b, int g, double time_each,
                         bool use_clock, double total_time) {
    const Opening& op = OPENINGS[g % N_OPENINGS];
    GameState start = op.stones ? random_position(op.seed, op.stones) : GameState();
    bool a_white = (g % 2) == 0;
    int result = a_white
        ? play_game(a, b, start, time_each, use_clock, total_time)
        : play_game(b, a, start, time_each, use_clock, total_time);
    GameOutcome out;
    out.from_a = a_white ? result : -result;
    out.seed = op.seed;
    out.stones = op.stones;
    out.a_white = a_white;
    return out;
}

void usage() {
    std::printf(
        "Catchup match runner (Elo gate). Lazy SMP is deferred; this is 1-thread.\n\n"
        "  ./catchup-match [options]\n\n"
        "  --games N          games to play (default 48, both colours of each opening)\n"
        "  --time S           seconds per turn (default 0.5)\n"
        "  --clock            use Clock::allocate with --total instead of fixed per-turn\n"
        "  --total S          game clock seconds per side with --clock (default 60)\n"
        "  --parallel N       games in parallel (default 1). Each worker has its own engines.\n"
        "  --move-wall S      hard per-move abort in seconds (default 45, 0 disables)\n"
        "  --expect-openings-hash HEX\n"
        "                     refuse to run unless the frozen opening set hashes to HEX\n"
        "  --tt-bits N        shared TT size bits 12-24 (default 22)\n"
        "  --a-iid            enable internal iterative deepening on A\n"
        "  --b-iid            enable internal iterative deepening on B\n"
        "  --a-tt-guard       keep a TT entry that is already depth+2 on A\n"
        "  --b-tt-guard       keep a TT entry that is already depth+2 on B\n"
        "  --a-lmp-scale      scale quiet LMP by placements left on A\n"
        "  --b-lmp-scale      scale quiet LMP by placements left on B\n"
        "  --a-swing          asymmetric swing key 2*og+3*ed on A\n"
        "  --b-swing          asymmetric swing key 2*og+3*ed on B\n"
        "  --a-gift-hist      split history by the gift flag on A\n"
        "  --b-gift-hist      split history by the gift flag on B\n"
        "  --a-race           disjoint-pocket race step on A\n"
        "  --b-race           disjoint-pocket race step on B\n"
        "  --a-unique         unique merge-connector term on A\n"
        "  --b-unique         unique merge-connector term on B\n"
        "  --a-forced-gift    forced-gift penalty on A\n"
        "  --b-forced-gift    forced-gift penalty on B\n"
        "  --a-weights 15ints comma-separated eval weights for engine A\n"
        "  --b-weights 15ints comma-separated eval weights for engine B\n"
        "  --a-lmr-div D      LMR table divisor for A (default 1.8)\n"
        "  --b-lmr-div D      LMR table divisor for B (default 1.8)\n"
        "  --a-quiet-cap N    quiet LMR cap at depth >= 10 for A (default 5)\n"
        "  --b-quiet-cap N    quiet LMR cap at depth >= 10 for B (default 5)\n"
        "  --b-no-counter     disable countermove on B\n"
        "  --b-no-stable      disable PV-stable early stop on B\n"
        "  --a-no-nmp         disable null-move pruning on A\n"
        "  --b-no-nmp         disable null-move pruning on B\n"
        "  --b-no-history     disable history on B\n"
        "  --jsonl PATH       append one JSON object per game\n"
        "  --sprt             sequential probability ratio test and stop early\n"
        "  --elo0 E           SPRT H0 Elo (default 0)\n"
        "  --elo1 E           SPRT H1 Elo (default 10)\n"
        "  --alpha A          SPRT type-I  (default 0.05)\n"
        "  --beta B           SPRT type-II (default 0.05)\n");
}

}  // namespace

int main(int argc, char** argv) {
    int games = 48;
    double time_each = 0.5;
    bool use_clock = false;
    double total_time = 60.0;
    int tt_bits = 22;
    int parallel = 1;
    double move_wall = 45.0;
    std::string expect_hash;
    EngineOpts opt_a, opt_b;
    std::memcpy(opt_a.weights, DEFAULT_WEIGHTS, sizeof(DEFAULT_WEIGHTS));
    std::memcpy(opt_b.weights, DEFAULT_WEIGHTS, sizeof(DEFAULT_WEIGHTS));
    bool sprt = false;
    double elo0 = 0.0;
    double elo1 = 10.0;
    double alpha = 0.05;
    double beta = 0.05;
    std::string jsonl;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* flag) {
            if (i + 1 >= argc) {
                std::cerr << flag << " needs a value\n";
                std::exit(2);
            }
            return std::string(argv[++i]);
        };
        if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (a == "--games") games = std::atoi(need("--games").c_str());
        else if (a == "--time") time_each = std::atof(need("--time").c_str());
        else if (a == "--clock") use_clock = true;
        else if (a == "--total") total_time = std::atof(need("--total").c_str());
        else if (a == "--parallel") parallel = std::atoi(need("--parallel").c_str());
        else if (a == "--move-wall") move_wall = std::atof(need("--move-wall").c_str());
        else if (a == "--expect-openings-hash") expect_hash = need("--expect-openings-hash");
        else if (a == "--tt-bits") tt_bits = std::atoi(need("--tt-bits").c_str());
        else if (a == "--a-iid") opt_a.use_iid = true;
        else if (a == "--b-iid") opt_b.use_iid = true;
        else if (a == "--a-tt-guard") opt_a.tt_keep_deep = true;
        else if (a == "--b-tt-guard") opt_b.tt_keep_deep = true;
        else if (a == "--a-lmp-scale") opt_a.lmp_by_placements = true;
        else if (a == "--b-lmp-scale") opt_b.lmp_by_placements = true;
        else if (a == "--a-swing") opt_a.swing_asymmetric = true;
        else if (a == "--b-swing") opt_b.swing_asymmetric = true;
        else if (a == "--a-gift-hist") opt_a.history_gift_split = true;
        else if (a == "--b-gift-hist") opt_b.history_gift_split = true;
        else if (a == "--a-race") opt_a.use_race = true;
        else if (a == "--b-race") opt_b.use_race = true;
        else if (a == "--a-unique") opt_a.use_unique = true;
        else if (a == "--b-unique") opt_b.use_unique = true;
        else if (a == "--a-forced-gift") opt_a.use_forced_gift = true;
        else if (a == "--b-forced-gift") opt_b.use_forced_gift = true;
        else if (a == "--a-weights") {
            if (!parse_weights(need("--a-weights"), opt_a.weights)) {
                std::cerr << "--a-weights needs 15 comma-separated ints\n";
                return 2;
            }
        } else if (a == "--b-weights") {
            if (!parse_weights(need("--b-weights"), opt_b.weights)) {
                std::cerr << "--b-weights needs 15 comma-separated ints\n";
                return 2;
            }
        } else if (a == "--a-lmr-div") opt_a.lmr_divisor = std::atof(need("--a-lmr-div").c_str());
        else if (a == "--b-lmr-div") opt_b.lmr_divisor = std::atof(need("--b-lmr-div").c_str());
        else if (a == "--a-quiet-cap") opt_a.lmr_quiet_cap = std::atoi(need("--a-quiet-cap").c_str());
        else if (a == "--b-quiet-cap") opt_b.lmr_quiet_cap = std::atoi(need("--b-quiet-cap").c_str());
        else if (a == "--b-no-counter") opt_b.use_countermove = false;
        else if (a == "--b-no-stable") opt_b.use_stable_stop = false;
        else if (a == "--a-no-nmp") opt_a.use_nmp = false;
        else if (a == "--b-no-nmp") opt_b.use_nmp = false;
        else if (a == "--b-no-history") opt_b.use_history = false;
        else if (a == "--jsonl") jsonl = need("--jsonl");
        else if (a == "--sprt") sprt = true;
        else if (a == "--elo0") elo0 = std::atof(need("--elo0").c_str());
        else if (a == "--elo1") elo1 = std::atof(need("--elo1").c_str());
        else if (a == "--alpha") alpha = std::atof(need("--alpha").c_str());
        else if (a == "--beta") beta = std::atof(need("--beta").c_str());
        else {
            std::cerr << "unknown argument: " << a << "\n";
            return 2;
        }
    }

    opt_a.tt_bits = tt_bits;
    opt_b.tt_bits = tt_bits;
    if (games < 1) games = 1;
    if (!(opt_a.lmr_divisor > 0.0) || !(opt_b.lmr_divisor > 0.0)) {
        std::cerr << "LMR divisor must be positive\n";
        return 2;
    }
    if (opt_a.lmr_quiet_cap < 1 || opt_b.lmr_quiet_cap < 1) {
        std::cerr << "quiet LMR cap must be at least 1\n";
        return 2;
    }
    if (parallel < 1) {
        std::cerr << "--parallel must be at least 1\n";
        return 2;
    }
    opt_a.move_wall = move_wall;
    opt_b.move_wall = move_wall;

    std::string have_hash = hash_hex(openings_hash());
    std::printf("openings %d  openings-hash %s\n", N_OPENINGS, have_hash.c_str());
    if (!expect_hash.empty() && expect_hash != have_hash) {
        std::cerr << "openings-hash mismatch: have " << have_hash
                  << " expected " << expect_hash << "\n";
        return 2;
    }

    int wins_a = 0, wins_b = 0, draws = 0;
    double llr = 0.0;
    double p0 = elo_to_p(elo0);
    double p1 = elo_to_p(elo1);
    double bound_h1 = std::log((1.0 - beta) / alpha);
    double bound_h0 = std::log(beta / (1.0 - alpha));

    std::ofstream out;
    if (!jsonl.empty()) {
        out.open(jsonl, std::ios::app);
        if (!out) {
            std::cerr << "cannot write " << jsonl << "\n";
            return 2;
        }
    }

    std::vector<GameOutcome> outcomes(static_cast<std::size_t>(games));
    std::vector<char> done(static_cast<std::size_t>(games), 0);
    std::atomic<int> next_g{0};
    std::atomic<int> limit{games};
    std::mutex ready_mu;
    std::condition_variable ready_cv;

    auto worker = [&]() {
        Engine ea = make_engine(opt_a);
        Engine eb = make_engine(opt_b);
        apply_opts(ea, opt_a);
        apply_opts(eb, opt_b);
        while (true) {
            int g = next_g.fetch_add(1);
            if (g >= limit.load()) return;
            outcomes[static_cast<std::size_t>(g)] =
                play_indexed(ea, eb, g, time_each, use_clock, total_time);
            {
                std::lock_guard<std::mutex> lock(ready_mu);
                done[static_cast<std::size_t>(g)] = 1;
            }
            ready_cv.notify_all();
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(parallel));
    for (int t = 0; t < parallel; ++t) pool.emplace_back(worker);

    int seen = 0;
    while (seen < games) {
        {
            std::unique_lock<std::mutex> lock(ready_mu);
            ready_cv.wait(lock, [&] {
                return done[static_cast<std::size_t>(seen)] || seen >= limit.load();
            });
            if (!done[static_cast<std::size_t>(seen)]) break;
        }
        const GameOutcome& played = outcomes[static_cast<std::size_t>(seen)];
        int from_a = played.from_a;
        if (from_a > 0) wins_a += 1;
        else if (from_a < 0) wins_b += 1;
        else draws += 1;

        if (out) {
            out << "{\"game\":" << seen
                << ",\"seed\":" << played.seed
                << ",\"stones\":" << played.stones
                << ",\"a_white\":" << (played.a_white ? "true" : "false")
                << ",\"result\":" << from_a
                << "}\n";
        }

        seen += 1;
        if (sprt) {
            if (from_a > 0) llr += std::log(p1 / p0);
            else if (from_a < 0) llr += std::log((1.0 - p1) / (1.0 - p0));
            else llr += 0.5 * std::log(p1 / p0) + 0.5 * std::log((1.0 - p1) / (1.0 - p0));
            if (llr >= bound_h1) {
                std::printf("SPRT: accept H1 (Elo >= %.1f) after %d games  llr=%.3f\n",
                            elo1, seen, llr);
                limit.store(seen);
                break;
            }
            if (llr <= bound_h0) {
                std::printf("SPRT: accept H0 (Elo <= %.1f) after %d games  llr=%.3f\n",
                            elo0, seen, llr);
                limit.store(seen);
                break;
            }
        }
    }
    limit.store(seen);
    for (auto& th : pool) th.join();

    int n = wins_a + wins_b + draws;
    double score = n ? (wins_a + 0.5 * draws) / n : 0.5;
    double elo = score_to_elo(score);
    double se = n ? std::sqrt(std::max(0.0, score * (1.0 - score) / n)) : 0.0;
    double z = 1.959964;
    double elo_lo = score_to_elo(std::max(0.0, score - z * se));
    double elo_hi = score_to_elo(std::min(1.0, score + z * se));

    std::printf("games %d  A %d  draw %d  B %d  score %.3f\n",
                n, wins_a, draws, wins_b, score);
    std::printf("elo %+.1f  (95%% CI %+.1f .. %+.1f)\n", elo, elo_lo, elo_hi);
    if (sprt) std::printf("llr %.3f  bounds [%.3f, %.3f]\n", llr, bound_h0, bound_h1);
    return 0;
}
