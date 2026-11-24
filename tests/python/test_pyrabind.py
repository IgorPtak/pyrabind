import datetime
import gc
import math
import pathlib
import unittest

import numpy as np
import pandas as pd
import test_module as tm


class TestPyrabindPython(unittest.TestCase):
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

    def test_smart_pointers(self) -> None:
        sp = tm.make_shared_node(10, "root")
        self.assertEqual(sp.id, 10)
        self.assertEqual(sp.name, "root")
        self.assertGreaterEqual(tm.shared_node_use_count(sp), 1)
        self.assertGreaterEqual(tm.retain_shared_node(sp), 1)
        self.assertEqual(tm.bump_retained_node(5), 15)
        self.assertEqual(sp.id, 15)
        tm.clear_retained_node()

        tok = tm.make_unique_token(7)
        self.assertTrue(tok.active())
        self.assertEqual(tm.consume_unique_token(tok), 70)
        with self.assertRaises(Exception):
            tm.consume_unique_token(tok)
        with self.assertRaises(Exception):
            tok.active()

    def test_complex_chrono_and_list(self) -> None:
        self.assertEqual(tm.complex_add_mul(1 + 2j, 3 + 4j), -3 + 5j)
        self.assertEqual(tm.complex_add_mul(2.0, 5.0), 2 + 5j)

        d_sum = tm.add_duration_ms(
            datetime.timedelta(milliseconds=250),
            datetime.timedelta(milliseconds=750),
        )
        self.assertEqual(d_sum, datetime.timedelta(seconds=1))

        t0 = datetime.datetime(2025, 1, 15, 12, 0, 0)
        t1 = tm.shift_timestamp(t0, datetime.timedelta(seconds=30))
        self.assertEqual((t1 - t0).total_seconds(), 30.0)

        self.assertEqual(tm.reverse_int_list([1, 2, 3]), [3, 2, 1])
        self.assertEqual(tm.tag_str_list(["a", "b"], "p:"), ["p:a", "p:b"])

    def test_kw_only_and_call_guard(self) -> None:
        self.assertEqual(tm.kw_combine(1, b=2), 125)
        self.assertEqual(tm.kw_combine(a=1, b=2, c=3), 123)
        with self.assertRaises(TypeError):
            tm.kw_combine(1, 2)

        g0 = tm.guard_counts()
        gw = tm.guarded_work(10)
        g1 = tm.guard_counts()
        self.assertEqual(g1[0], g0[0] + 1)
        self.assertEqual(g1[1], g0[1] + 1)
        self.assertEqual(gw, 10 + g1[0])

    def test_static_properties_and_custom_exception(self) -> None:
        self.assertEqual(tm.StaticBox.CONST_VAL, 42)
        with self.assertRaises(AttributeError):
            tm.StaticBox.CONST_VAL = 99

        self.assertEqual(tm.StaticBox.mutable_val, 10)
        tm.StaticBox.mutable_val = 25
        self.assertEqual(tm.StaticBox.mutable_val, 25)
        tm.StaticBox.mutable_val = 10

        self.assertEqual(tm.StaticBox.double_const, 84)
        self.assertEqual(tm.StaticBox.computed, 100)
        tm.StaticBox.computed = 250
        self.assertEqual(tm.StaticBox.computed, 250)
        tm.StaticBox.computed = 100

        with self.assertRaises(tm.CustomDomainError) as ctx:
            tm.fail_domain("bad domain input")
        self.assertIn("bad domain input", str(ctx.exception))

    def test_iterators(self) -> None:
        bag = tm.NumberBag([10, 20, 30], {"a": 1, "b": 2})
        self.assertEqual(list(bag), [10, 20, 30])
        self.assertEqual(list(bag.keys()), ["a", "b"])
        self.assertEqual(list(bag.values()), [1, 2])
        self.assertEqual(list(bag.items()), [("a", 1), ("b", 2)])

    def test_opaque_bind_vector_and_map(self) -> None:
        ov = tm.OpaqueVec()
        ov.append(tm.OpaqueItem(1, "one"))
        tm.push_opaque(ov, tm.OpaqueItem(2, "two"))
        self.assertEqual(len(ov), 2)
        self.assertEqual(ov[1].label, "two")
        ov[0] = tm.OpaqueItem(9, "nine")
        self.assertEqual(
            list(ov),
            [tm.OpaqueItem(9, "nine"), tm.OpaqueItem(2, "two")],
        )
        self.assertEqual(ov.pop().tag, 2)
        self.assertEqual(len(ov), 1)
        ov.clear()
        self.assertEqual(len(ov), 0)

        om = tm.OpaqueMap()
        om["k1"] = tm.OpaqueItem(3, "three")
        tm.set_opaque_entry(om, "k2", tm.OpaqueItem(4, "four"))
        self.assertEqual(len(om), 2)
        self.assertIn("k2", om)
        self.assertEqual(om["k1"].tag, 3)
        self.assertEqual(list(om.keys()), ["k1", "k2"])
        self.assertEqual([(k, v.tag) for k, v in om.items()], [("k1", 3), ("k2", 4)])
        del om["k1"]
        self.assertEqual(len(om), 1)
        self.assertNotIn("k1", om)

    def test_extended_operators(self) -> None:
        m0 = tm.ModInt(0)
        m3 = tm.ModInt(3)
        m5 = tm.ModInt(5)
        m6 = tm.ModInt(6)
        self.assertEqual((+m5).value, 5)
        self.assertFalse(bool(m0))
        self.assertTrue(bool(m5))
        self.assertTrue(not m0)
        self.assertEqual((m5 % m3).value, 2)
        self.assertEqual((m6 & m3).value, 2)
        self.assertEqual((tm.ModInt(4) | tm.ModInt(1)).value, 5)
        self.assertEqual((m6 ^ m3).value, 5)
        self.assertEqual((m3 << tm.ModInt(2)).value, 12)
        self.assertEqual((tm.ModInt(12) >> tm.ModInt(2)).value, 3)
        self.assertEqual((~m0).value, -1)
        m5 += m3
        self.assertEqual(m5.value, 8)
        m5 %= m3
        self.assertEqual(m5.value, 2)
        self.assertIsInstance(hash(tm.ModInt(7)), int)
        self.assertAlmostEqual(abs(tm.SignedScalar(-3.5)), 3.5)


if __name__ == "__main__":
    unittest.main(verbosity=2)
