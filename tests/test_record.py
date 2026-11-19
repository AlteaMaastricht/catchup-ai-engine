"""Game log files classified by seconds / side."""

import json
import tempfile
import unittest
from pathlib import Path

from catchup.board import WHITE
from catchup.record import (
    GameSession,
    game_dir,
    log_filename,
    mode_name,
    seconds_bucket,
)
from catchup.search import SearchStats
from catchup.state import GameState


class TestBuckets(unittest.TestCase):

    def test_seconds_folder_and_mode(self):
        self.assertEqual(seconds_bucket(30), 30)
        self.assertEqual(seconds_bucket(600.4), 600)
        self.assertEqual(mode_name(WHITE), "human_white")
        self.assertEqual(mode_name(None), "engine_vs_engine")
        self.assertEqual(log_filename(WHITE), "human_vs_ai.jsonl")
        self.assertEqual(log_filename(None), "engine_vs_engine.jsonl")
        self.assertEqual(game_dir(300, Path("/tmp")).name, "300s")


class TestGameSession(unittest.TestCase):

    def test_writes_jsonl_under_seconds_folder(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            session = GameSession(30.0, WHITE, source="gui", root=root)
            session.note("Started.")
            session.add_turn([0], WHITE, "human", 1.5, 30.0, 28.5)
            stats = SearchStats()
            stats.depth = 6
            stats.completed = 6
            stats.value = 89
            stats.nodes = 1000
            stats.elapsed = 1.2
            stats.turn = (1, 2)
            session.add_turn([1, 2], 1, "engine", 1.2, 30.0, 28.8,
                             budget_s=1.2, stats=stats)
            session.undo(2)
            self.assertEqual(session.record["turns"], [])
            self.assertEqual(session.record["undo_count"], 1)
            session.add_turn([0], WHITE, "human", 1.0, 30.0, 29.0)
            path = session.finish(abandoned=True)
            self.assertEqual(path, root / "30s" / "human_vs_ai.jsonl")
            row = json.loads(path.read_text(encoding="utf-8").splitlines()[0])
            self.assertEqual(row["seconds_per_side"], 30.0)
            self.assertEqual(row["mode"], "human_white")
            self.assertTrue(row["abandoned"])
            self.assertEqual(row["turns"][0]["cells"], ["A1"])
            self.assertEqual(row["engine"]["tt_bits"], 20)

    def test_skips_empty_games(self):
        with tempfile.TemporaryDirectory() as tmp:
            session = GameSession(600.0, None, root=Path(tmp))
            self.assertIsNone(session.finish(abandoned=True))

    def test_finished_result(self):
        state = GameState()
        state.play_turn([30])
        with tempfile.TemporaryDirectory() as tmp:
            session = GameSession(600.0, WHITE, root=Path(tmp))
            session.add_turn([30], WHITE, "human", 0.1, 600.0, 599.9)
            path = session.finish(state=state, abandoned=False)
            row = json.loads(path.read_text(encoding="utf-8"))
            self.assertIsNone(row["result"])
            self.assertFalse(row["abandoned"])


if __name__ == "__main__":
    unittest.main()
