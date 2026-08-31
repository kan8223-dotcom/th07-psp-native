import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class PspMeRenderLeanCacheOwnershipContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / "psp/audio_me.h").read_text(encoding="utf-8")
        cls.audio = (ROOT / "psp/audio_me.c").read_text(encoding="utf-8")

    def test_feature_is_hardware_rejected(self):
        self.assertIn("TH07_PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP", self.header)
        self.assertIn(
            "lean render cache ownership is hardware-rejected", self.header
        )

    def test_submit_always_keeps_full_output_preparation(self):
        submit = body(self.audio, "int th07_psp_me_render_stream_submit(")
        self.assertNotIn("TH07_PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP", submit)
        self.assertIn("sceKernelDcacheWritebackInvalidateRange(outputArea->vertices", submit)
        self.assertIn("sceKernelDcacheWritebackInvalidateRange(runArea->runs", submit)
        self.assertIn("me_render_stream_finish_sc_transition", submit)
        self.assertIn("box->command = ME_CMD_RENDER_STREAM", submit)
        self.assertIn("I-ME7 contract", submit)

    def test_ge_promotion_always_keeps_wbi_sync_and_state(self):
        mark = body(
            self.audio, "int th07_psp_me_render_stream_mark_ge_in_flight("
        )
        self.assertNotIn("TH07_PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP", mark)
        self.assertIn("sceKernelDcacheWritebackInvalidateRange", mark)
        self.assertIn('__asm__ volatile("sync")', mark)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_STATE_GE_IN_FLIGHT", mark)


if __name__ == "__main__":
    unittest.main()
