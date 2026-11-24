import unittest

import numpy as np
import pandas as pd

try:
    import pyra_stats as ps
except ImportError:
    import pyrastats as ps


class TestPyraStatsPython(unittest.TestCase):
    def test_robust_summary(self) -> None:
        arr = np.array([1.0, 2.0, 3.0, 4.0, 100.0])
        st = ps.robust_summary(arr)
        self.assertEqual(st.n, 5)
        self.assertAlmostEqual(st.median, 3.0)
        self.assertAlmostEqual(st.mean, 22.0)
        self.assertAlmostEqual(st.q25, 2.0)
        self.assertAlmostEqual(st.q75, 4.0)
        self.assertGreater(st.mad, 0.0)

    def test_pairwise_euclidean(self) -> None:
        a = np.array([[0.0, 0.0], [3.0, 4.0]])
        b = np.array([[0.0, 0.0], [6.0, 8.0]])
        d = ps.pairwise_euclidean(a, b)
        self.assertEqual(d.shape, (2, 2))
        self.assertAlmostEqual(d[0, 0], 0.0)
        self.assertAlmostEqual(d[0, 1], 10.0)
        self.assertAlmostEqual(d[1, 0], 5.0)
        self.assertAlmostEqual(d[1, 1], 5.0)

    def test_ridge_regressor(self) -> None:
        rng = np.random.default_rng(42)
        X = rng.normal(size=(80, 3))
        true_w = np.array([2.5, -1.5, 0.8])
        y = 1.2 + X @ true_w
        model = ps.RidgeRegressor(alpha=1e-6).fit(X, y)
        self.assertTrue(model.is_fitted)
        self.assertGreater(model.score_r2(X, y), 0.9999)
        preds = model.predict(X[:5])
        self.assertTrue(np.allclose(preds, y[:5], atol=1e-4))
        self.assertTrue(np.allclose(model.coef, true_w, atol=1e-4))
        self.assertAlmostEqual(model.intercept, 1.2, places=4)

        with self.assertRaises(TypeError):
            ps.RidgeRegressor(1.0)  # alpha is kw_only

    def test_standardize_table_and_arrow(self) -> None:
        df = pd.DataFrame(
            {
                "val": [1.0, 2.0, np.nan, 4.0],
                "grp": ["a", None, "c", "d"],
            }
        )
        std_df = ps.standardize_table(df, ["val"])
        self.assertTrue(pd.isna(std_df.loc[2, "val"]))
        valid = std_df["val"].dropna().to_numpy()
        self.assertAlmostEqual(float(np.mean(valid)), 0.0, places=12)

        batch = ps.stats_to_arrow(std_df)
        self.assertTrue(hasattr(batch, "__arrow_c_array__"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
