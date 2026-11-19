#include "position.hpp"
#include "search.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <tuple>

using namespace catchup;

namespace {

Engine make_engine(bool use_tt = true, bool use_pvs = true, bool use_killers = true,
                   bool use_history = true, bool use_aspiration = true,
                   bool use_lmr = true, int w_tempo = 0) {
    return Engine(use_tt, use_pvs, use_killers, use_history, use_aspiration,
                  use_lmr, false, 64, 120, 100, 2, 1, 2, w_tempo);
}

void bench_nps(double seconds) {
    std::printf("== nodes / second ==\n");
    Engine engine = make_engine();
    GameState warm = random_position(1, 20);
    engine.find_turn(warm, std::min(1.5, std::max(0.4, seconds)));

    struct Phase { const char* name; int stones; };
    Phase phases[] = {{"opening", 0}, {"early", 12}, {"midgame", 28}, {"late", 46}};
    for (auto p : phases) {
        GameState state = p.stones ? random_position(100 + p.stones, p.stones)
                                   : GameState();
        auto result = engine.find_turn(state, seconds);
        const SearchStats& stats = result.second;
        std::string turn;
        for (std::size_t i = 0; i < stats.turn.size(); ++i) {
            if (i) turn += " ";
            turn += CELL_NAMES[stats.turn[i]];
        }
        std::printf("  %-8s  empty=%2d  depth=%d  %7d nodes in %5.2fs  %7.0f n/s  %s\n",
                    p.name, state.empty_count, stats.completed, stats.nodes,
                    stats.elapsed, stats.nps(), turn.c_str());
    }
}

void bench_depth_by_phase(double seconds) {
    std::printf("\n== depth reached by phase (%.1fs / position) ==\n", seconds);
    struct Phase { const char* name; int stones; };
    Phase phases[] = {{"opening", 0}, {"early", 12}, {"midgame", 28}, {"late", 46}};
    for (auto p : phases) {
        int depths[4];
        int nodes[4];
        for (int seed = 0; seed < 4; ++seed) {
            GameState state = p.stones
                ? random_position(200 + p.stones + seed, p.stones)
                : GameState();
            Engine engine = make_engine();
            auto result = engine.find_turn(state, seconds);
            depths[seed] = result.second.completed;
            nodes[seed] = result.second.nodes;
        }
        double mean_d = (depths[0] + depths[1] + depths[2] + depths[3]) / 4.0;
        int mean_n = (nodes[0] + nodes[1] + nodes[2] + nodes[3]) / 4;
        std::printf("  %-8s  depths [%d, %d, %d, %d]   mean %.1f   mean nodes %d\n",
                    p.name, depths[0], depths[1], depths[2], depths[3], mean_d, mean_n);
    }
}

std::tuple<int, int, int, int> self_play(Engine& white, Engine& black,
                                         double time_each, int max_moves = 80) {
    GameState state;
    white.new_game();
    black.new_game();
    Engine* engines[2] = {&white, &black};
    int nodes[2] = {0, 0};
    int moves = 0;
    while (!state.is_over() && moves < max_moves) {
        int mover = state.to_move;
        auto result = engines[mover]->find_turn(state, time_each);
        state.play_turn(result.first);
        nodes[mover] += result.second.nodes;
        moves += 1;
    }
    return {state.compare_final(), nodes[WHITE], nodes[BLACK], moves};
}

void bench_ab_matches(double seconds, int games) {
    std::printf("\n== A/B self-play (%.2fs / turn, %d games / pairing) ==\n",
                seconds, games);
    struct Pairing {
        const char* label;
        bool b_tt, b_pvs, b_killers, b_history, b_asp, b_lmr;
        int b_tempo;
    };
    Pairing pairings[] = {
        {"full vs no TT", false, true, true, true, true, true, 110},
        {"full vs no PVS", true, false, true, true, true, true, 110},
        {"full vs no killers", true, true, false, true, true, true, 110},
        {"full vs no history", true, true, true, false, true, true, 110},
        {"full vs no aspiration", true, true, true, true, false, true, 110},
        {"full vs no LMR", true, true, true, true, true, false, 110},
        {"full vs tempo=40", true, true, true, true, true, true, 40},
    };
    for (auto& p : pairings) {
        int wins[3] = {0, 0, 0};
        for (int i = 0; i < games; ++i) {
            Engine a = make_engine();
            Engine b = make_engine(p.b_tt, p.b_pvs, p.b_killers, p.b_history,
                                   p.b_asp, p.b_lmr, p.b_tempo);
            int result;
            if (i % 2 == 0) {
                result = std::get<0>(self_play(a, b, seconds));
                if (result > 0) wins[0] += 1;
                else if (result < 0) wins[2] += 1;
                else wins[1] += 1;
            } else {
                result = std::get<0>(self_play(b, a, seconds));
                if (result < 0) wins[0] += 1;
                else if (result > 0) wins[2] += 1;
                else wins[1] += 1;
            }
        }
        std::printf("  %-22s  A %d  draw %d  B %d\n", p.label, wins[0], wins[1], wins[2]);
    }
}

void bench_enhancement_nps(double seconds) {
    std::printf("\n== enhancement cost on a mid-game position (%.1fs) ==\n", seconds);
    GameState state = random_position(7, 24);
    struct Cfg { const char* name; Engine (*make)(); };
    auto run = [&](const char* name, Engine engine) {
        GameState copy = state.clone();
        auto result = engine.find_turn(copy, seconds);
        const SearchStats& stats = result.second;
        std::printf("  %-16s  depth %d  %7d nodes  %7.0f n/s  value %+d\n",
                    name, stats.completed, stats.nodes, stats.nps(), stats.value);
    };
    run("full", make_engine());
    run("no TT", make_engine(false));
    run("no PVS", make_engine(true, false));
    run("no killers", make_engine(true, true, false));
    run("no history", make_engine(true, true, true, false));
    run("no aspiration", make_engine(true, true, true, true, false));
    run("no LMR", make_engine(true, true, true, true, true, false));
    run("tempo=40", make_engine(true, true, true, true, true, true, 40));
    run("plain alpha-beta", Engine(false, false, false, false, false, false,
                                   false, 64));
}

void bench_tt_bits(double seconds) {
    std::printf("\n== TT size on a mid-game position (%.1fs) ==\n", seconds);
    GameState state = random_position(9, 24);
    for (int bits : {18, 20, 21}) {
        Engine engine(true, true, true, true, true, true, false, 64, 120,
                      100, 2, 1, 2, 0, 25, 8, 20, 40, 4, 16, 25, 8,
                      true, true, false, 80, 10, bits);
        GameState copy = state.clone();
        auto result = engine.find_turn(copy, seconds);
        const SearchStats& stats = result.second;
        std::printf("  bits=%d  depth %d  %7d nodes  %7.0f n/s  value %+d  tt %d/%d cut %d\n",
                    bits, stats.completed, stats.nodes, stats.nps(), stats.value,
                    stats.tt_hits, stats.tt_probes, stats.tt_cutoffs);
    }
}

}  // namespace

int main(int argc, char** argv) {
    bool quick = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--quick") quick = true;
        else if (std::string(argv[i]) == "-h" || std::string(argv[i]) == "--help") {
            std::printf("Nodes/sec, depth-by-phase, and A/B self-play for Catchup.\n"
                        "  ./catchup-bench           full suite\n"
                        "  ./catchup-bench --quick   shorter run\n");
            return 0;
        }
    }
    auto started = std::chrono::steady_clock::now();
    if (quick) {
        bench_nps(0.6);
        bench_depth_by_phase(0.8);
        bench_enhancement_nps(0.6);
        bench_tt_bits(0.6);
        bench_ab_matches(0.15, 2);
    } else {
        bench_nps(2.0);
        bench_depth_by_phase(2.5);
        bench_enhancement_nps(1.5);
        bench_tt_bits(1.0);
        bench_ab_matches(0.35, 4);
    }
    double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    std::printf("\nfinished in %.1fs  (board has %d cells)\n", elapsed, NUM_CELLS);
    return 0;
}
