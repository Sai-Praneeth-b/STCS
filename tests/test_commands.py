import unittest
from server.application import Application

class CommandTests(unittest.TestCase):
    def setUp(self): self.app = Application()
    def call(self, c, p=None): return self.app.handle(c, p or {}, "alice")
    def test_t02_create(self): self.assertTrue(self.call("CREATE", {"title":"A"})[0])
    def test_t03_list(self): self.assertEqual(len(self.call("LIST")[1]), 0)
    def test_t04_claim(self):
        task = self.call("CREATE", {"title":"A"})[1]
        self.assertTrue(self.call("CLAIM", {"task_id":task["task_id"]})[0])
    def test_t05_complete(self):
        task = self.call("CREATE", {"title":"A"})[1]
        self.call("CLAIM", {"task_id":task["task_id"]})
        self.assertTrue(self.call("COMPLETE", {"task_id":task["task_id"]})[0])
    def test_t06_not_found(self): self.assertEqual(self.call("GET", {"task_id":999})[2], "NOT_FOUND")

if __name__ == "__main__": unittest.main()
