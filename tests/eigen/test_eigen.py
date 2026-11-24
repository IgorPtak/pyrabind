import unittest

import numpy as np
import test_eigen_module as em


class TestPyrabindEigenPython(unittest.TestCase):
    def test_matrices_and_vectors(self) -> None:
        a = np.array([[1.0, 2.0], [3.0, 4.0]], order="F")
        b = np.array([[10.0, 20.0], [30.0, 40.0]], order="F")
        res = em.mat_add_scaled(a, b, scale=0.5)
        self.assertTrue(np.allclose(res, [[6.0, 12.0], [18.0, 24.0]]))

        self.assertAlmostEqual(
            em.vec_dot(np.array([1.0, 2.0, 3.0]), np.array([4.0, 5.0, 6.0])),
            32.0,
        )
        self.assertTrue(np.allclose(em.row_scale(np.array([2.0, -3.0]), 3.0), [6.0, -9.0]))
        self.assertAlmostEqual(em.fixed_det2(np.array([[2.0, 1.0], [1.0, 3.0]])), 5.0)
        self.assertEqual(em.vec_int_sum(np.array([1, 2, 3, 4], dtype=np.int32)), 10)
        self.assertTrue(
            np.allclose(em.array_square_plus(np.array([[2.0, 3.0]]), 1.0), [[5.0, 10.0]])
        )

    def test_row_major_and_views(self) -> None:
        rm = em.make_row_major(2, 3)
        self.assertEqual(rm.shape, (2, 3))
        self.assertAlmostEqual(rm[1, 2], 22.0)
        self.assertAlmostEqual(em.row_major_trace_like(rm), 10.0 + 21.0)
        self.assertAlmostEqual(em.ref_col_sum(np.asfortranarray(rm), 1), 11.0 + 21.0)

        v = np.array([1.0, 2.0, 3.0])
        em.scale_vec_inplace(v, 4.0)
        self.assertTrue(np.allclose(v, [4.0, 8.0, 12.0]))

        mf = np.array([[1.0, 2.0], [3.0, 4.0]], order="F")
        em.add_dref_inplace(mf, 10.0)
        self.assertTrue(np.allclose(mf, [[11.0, 12.0], [13.0, 14.0]]))
        self.assertAlmostEqual(em.map_const_sum(mf), 50.0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
