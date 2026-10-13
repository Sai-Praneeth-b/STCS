import unittest
from shared.framing import FrameBuffer, encode_frame

class FramingTests(unittest.TestCase):
    def test_t13_fragmented_frame(self):
        raw = encode_frame(b"hello")
        b = FrameBuffer()
        self.assertEqual(b.feed(raw[:2]), [])
        self.assertEqual(b.feed(raw[2:]), [b"hello"])

    def test_t14_multiple_frames(self):
        raw = encode_frame(b"a") + encode_frame(b"bb")
        self.assertEqual(FrameBuffer().feed(raw), [b"a", b"bb"])

if __name__ == "__main__": unittest.main()
