import concurrent.futures
import unittest
from server.application import Application

class ConcurrencyTests(unittest.TestCase):
    def test_t10_t12_single_claim_wins(self):
        app = Application()
        task = app.handle("CREATE", {"title":"race"}, "creator")[1]
        def claim(user): return app.handle("CLAIM", {"task_id": task["task_id"]}, user)[0]
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
            results = list(ex.map(claim, [f"u{i}" for i in range(8)]))
        self.assertEqual(sum(results), 1)

if __name__ == "__main__": unittest.main()
