import gc
import math
import pathlib
import unittest

import numpy as np
import pandas as pd
import test_module as tm


class TestNanorbindPython(unittest.TestCase):
    def test_functions_defaults_overloads(self) -> None:
        self.assertAlmostEqual(tm.PI, 3.141592653589793)
        self.assertEqual(tm.add_defaults(5), 115)
        self.assertEqual(tm.add_defaults(5, c=200), 215)
        self.assertEqual(tm.classify(7), "int:7")
        self.assertEqual(tm.classify(7.5), "double:7")
        self.assertAlmostEqual(tm.math.square(4.5), 20.25)

    def test_enums_and_exceptions(self) -> None:
        self.assertEqual(int(tm.Color.Green), 2)
        self.assertEqual(tm.Color.Green.value, 2)
        self.assertEqual(tm.Red, tm.Color.Red)
        self.assertNotEqual(tm.Red, tm.Blue)

        with self.assertRaises(ValueError):
            tm.check_positive(-1)
        with self.assertRaises(KeyError):
            tm.lookup_key({"a": 1}, "missing")

    def test_classes_operators_inheritance_lifetimes(self) -> None:
        a = tm.Vec2(1.0, 2.0)
        b = tm.Vec2(3.0, 4.0)
        c = 2.0 * a + b
        self.assertEqual(c, tm.Vec2(5.0, 8.0))
        self.assertEqual(-a, tm.Vec2(-1.0, -2.0))
        self.assertTrue(a < b)
        self.assertAlmostEqual(tm.Vec2.zero().norm, 0.0)
        c.scale(0.5)
        self.assertAlmostEqual(c.x, 2.5)

        dog = tm.Dog("Rex")
        self.assertEqual(dog.speak(), "Rex: woof")
        self.assertEqual(dog.fetch(), "Rex fetched the ball")
        self.assertEqual(tm.greet_animal(dog), "hello Rex: woof")

        box = tm.Container(3, 42)
        self.assertEqual(len(box), 3)
        self.assertEqual(box[0], 42)
        box[-1] = 77
        self.assertEqual(box[2], 77)
        self.assertTrue(77 in box)
        with self.assertRaises(IndexError):
            _ = box[10]

        emb = box.get_embedded()
        self.assertEqual(emb.id, 99)
        res = tm.Resource(123)
        box.attach(res)
        del res
        gc.collect()
        self.assertEqual(box.attached_id(), 123)

        acc = tm.Accumulator(5)
        self.assertEqual(acc.add_const(3), 35)
        self.assertEqual(acc.add(3), 8)

    def test_stl_and_callables(self) -> None:
        self.assertIsNone(tm.opt_double(None))
        self.assertEqual(tm.opt_double(21), 42)
        self.assertEqual(tm.swap_pair(("ok", 9)), (9, "ok"))
        self.assertEqual(tm.make_triplet(1, 2.5, "hi"), (1, 2.5, "hi"))
        self.assertEqual(tm.reverse_arr3([1, 2, 3]), [3, 2, 1])
        self.assertEqual(tm.unique_tags({"b", "a"}), {"a", "b"})
        self.assertEqual(tm.describe_variant(4), "i:4")
        self.assertEqual(tm.describe_variant(4.25), "d:4")
        self.assertEqual(tm.describe_variant("x"), "s:x")
        self.assertEqual(tm.path_filename(pathlib.Path("/tmp/demo.txt")), "demo.txt")
        self.assertEqual(tm.map_doubles([1.0, 2.0, 3.0], lambda v: v * 10.0), [10.0, 20.0, 30.0])
        adder = tm.make_adder(7)
        self.assertEqual(adder(5), 12)

    def test_ndarray_numpy_and_dataframe(self) -> None:
        u = np.array([1.0, 2.0, 3.0, 4.0], dtype=np.float64)
        v = np.array([10.0, 20.0, 30.0, 40.0], dtype=np.float64)
        self.assertAlmostEqual(tm.nd_dot_1d(u, v), 300.0)
        self.assertAlmostEqual(tm.nd_dot_1d(u[::2], v[::2]), 100.0)

        mat = np.array([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]], dtype=np.float64)
        s, tr = tm.nd_sum_and_trace(mat)
        self.assertAlmostEqual(s, 21.0)
        self.assertAlmostEqual(tr, 6.0)

        out1 = tm.nd_scale_1d(u, 2.5, -1.0)
        self.assertIsInstance(out1, np.ndarray)
        self.assertTrue(np.allclose(out1, u * 2.5 - 1.0))

        out2 = tm.nd_make_2d(2, 3)
        self.assertEqual(out2.shape, (2, 3))
        self.assertAlmostEqual(out2[0, 2], 3.0)
        self.assertAlmostEqual(out2[1, 0], 11.0)

        df = pd.DataFrame(
            {
                "id": np.array([1, 2, 3], dtype=np.int64),
                "x": np.array([1.5, 2.5, 3.5], dtype=np.float64),
                "tag": pd.Series(["a", "b", "c"], dtype=object),
            }
        )
        res = tm.transform_df(df)
        self.assertIsInstance(res, pd.DataFrame)
        self.assertEqual(list(res.columns), ["id", "x", "scaled", "tag"])
        self.assertTrue(np.allclose(res["scaled"].to_numpy(), [4.0, 7.0, 10.0]))
        self.assertEqual(res["tag"].tolist(), ["a", "b", "c"])

        res_dict = tm.transform_df({"id": [1, 2], "x": [2.0, 3.0], "tag": ["p", "q"]})
        self.assertTrue(np.allclose(res_dict["scaled"].to_numpy(), [5.0, 8.0]))


if __name__ == "__main__":
    unittest.main(verbosity=2)
