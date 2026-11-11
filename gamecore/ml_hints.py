#!/usr/bin/env python3
"""Deterministic hint engine for Akinator.

Three real algorithms implemented on top of the 60x139 answer matrix:
  * variance      - maximum 4p(1-p) split score;
  * randomforest  - bagged randomized decision trees with Gini importance;
  * catboost      - gradient boosting with symmetric (oblivious) trees.

Protocol: one JSON object per line on stdin, one JSON object per line on stdout.
"""
import json
import math
import sys

import numpy as np

N_CHARACTERS = 60
N_QUESTIONS = 139


class HintEngine:
    def __init__(self, matrix):
        values = np.asarray(matrix, dtype=np.int8)
        if values.size != N_CHARACTERS * N_QUESTIONS:
            raise ValueError("matrix must contain 60*139 values")
        self.x = values.reshape(N_CHARACTERS, N_QUESTIONS) > 0

    @staticmethod
    def normalize(ids):
        return np.asarray(sorted({int(c) for c in ids if 0 <= int(c) < N_CHARACTERS}),
                          dtype=np.int64)

    @staticmethod
    def features(excluded):
        excluded = {int(q) for q in excluded}
        return np.asarray([q for q in range(N_QUESTIONS) if q not in excluded], dtype=np.int64)

    def labels(self, ids):
        counts = self.x[ids].sum(axis=1)
        labels = (counts > np.sort(counts)[len(counts) // 2]).astype(np.float64)
        positives = int(labels.sum())
        return labels, 0 < positives < len(ids)

    def variance(self, ids, feats):
        if len(ids) < 2 or feats.size == 0:
            return -1, 0.0
        yes = self.x[np.ix_(ids, feats)].sum(axis=0).astype(np.float64)
        p = yes / float(len(ids))
        scores = 4.0 * p * (1.0 - p)
        best = int(np.argmax(scores))
        if scores[best] <= 0.0:
            return -1, 0.0
        return int(feats[best]), float(scores[best])

    def random_forest(self, ids, feats):
        if len(ids) < 2 or feats.size == 0:
            return -1, 0.0
        labels, usable = self.labels(ids)
        if not usable:
            return self.variance(ids, feats)
        x = self.x[ids]
        importance = np.zeros(N_QUESTIONS, dtype=np.float64)
        rng = np.random.default_rng(0x5EED1234)
        mtry = max(2, int(math.sqrt(feats.size)))
        for _ in range(100):
            sample = rng.integers(0, len(ids), size=len(ids))
            self._grow(x, labels, sample, feats, importance, mtry, rng, 0)
        return self.best(importance, feats)

    def _grow(self, x, labels, node, feats, importance, mtry, rng, depth):
        total = len(node)
        if depth >= 4 or total < 4:
            return
        y = labels[node]
        positives = int(y.sum())
        if positives == 0 or positives == total:
            return
        subset = rng.choice(feats, size=min(mtry, feats.size), replace=False)
        xn = x[node]
        yes = xn[:, subset].sum(axis=0)
        yes_pos = (xn[:, subset] * y[:, None]).sum(axis=0)
        no = total - yes
        valid = (yes >= 2) & (no >= 2)
        if not valid.any():
            return
        parent = 2.0 * (positives / total) * (1.0 - positives / total)
        rate_yes = yes_pos / np.maximum(yes, 1)
        rate_no = (positives - yes_pos) / np.maximum(no, 1)
        gains = parent - (yes / total) * (2.0 * rate_yes * (1.0 - rate_yes)) \
                       - (no / total) * (2.0 * rate_no * (1.0 - rate_no))
        gains[~valid] = -np.inf
        best = int(np.argmax(gains))
        if gains[best] <= 1e-12:
            return
        feature = int(subset[best])
        importance[feature] += float(gains[best]) * total
        split = xn[:, feature]
        self._grow(x, labels, node[split], feats, importance, mtry, rng, depth + 1)
        self._grow(x, labels, node[~split], feats, importance, mtry, rng, depth + 1)

    def catboost(self, ids, feats):
        if len(ids) < 2 or feats.size == 0:
            return -1, 0.0
        labels, usable = self.labels(ids)
        if not usable:
            return self.variance(ids, feats)
        base = float(labels.mean())
        if not 0.0 < base < 1.0:
            return self.variance(ids, feats)

        x = self.x[ids]
        n = len(ids)
        raw = np.full(n, math.log(base / (1.0 - base)))
        importance = np.zeros(N_QUESTIONS, dtype=np.float64)
        for _ in range(100):
            p = 1.0 / (1.0 + np.exp(-raw))
            gradient = p - labels
            hessian = np.maximum(p * (1.0 - p), 1e-12)
            leaf = np.zeros(n, dtype=np.int64)
            leaf_count = 1
            for _level in range(3):
                onehot = np.zeros((n, leaf_count))
                onehot[np.arange(n), leaf] = 1.0
                counts = onehot.sum(axis=0)
                g_leaf = onehot.T @ gradient
                h_leaf = onehot.T @ hessian
                x_leaf = onehot.T @ x[:, feats]
                g_yes = onehot.T @ (gradient[:, None] * x[:, feats])
                h_yes = onehot.T @ (hessian[:, None] * x[:, feats])
                valid = (x_leaf >= 2) & (counts[:, None] - x_leaf >= 2)
                no_g = g_leaf[:, None] - g_yes
                no_h = h_leaf[:, None] - h_yes
                gain_leaf = g_yes ** 2 / (h_yes + 1.0) \
                          + no_g ** 2 / (no_h + 1.0) \
                          - g_leaf[:, None] ** 2 / (h_leaf[:, None] + 1.0)
                gains = np.where(valid, gain_leaf, 0.0).sum(axis=0)
                best = int(np.argmax(gains))
                if gains[best] <= 1e-12:
                    break
                feature = int(feats[best])
                importance[feature] += float(gains[best])
                leaf = leaf * 2 + x[:, feature].astype(np.int64)
                leaf_count *= 2
            if leaf_count == 1:
                continue
            leaf_g = np.bincount(leaf, weights=gradient, minlength=leaf_count)
            leaf_h = np.bincount(leaf, weights=hessian, minlength=leaf_count)
            raw -= 0.15 * (leaf_g / (leaf_h + 1.0))[leaf]
        return self.best(importance, feats)

    @staticmethod
    def best(importance, feats):
        values = importance[feats]
        best = int(np.argmax(values))
        if values[best] <= 0.0:
            return -1, 0.0
        return int(feats[best]), float(values[best])

    def hint(self, kind, ids, excluded):
        ids = self.normalize(ids)
        feats = self.features(excluded)
        if kind == "variance":
            return self.variance(ids, feats)
        if kind == "randomforest":
            return self.random_forest(ids, feats)
        if kind == "catboost":
            return self.catboost(ids, feats)
        raise ValueError("unknown hint kind: %s" % kind)


def respond(payload):
    sys.stdout.write(json.dumps(payload, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def main():
    engine = None
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            request = json.loads(line)
            command = request.get("cmd")
            if command == "init":
                engine = HintEngine(request["matrix"])
                respond({"ok": True})
            elif command == "hint":
                if engine is None:
                    raise ValueError("hint engine is not initialized")
                question, score = engine.hint(request.get("kind", ""),
                                              request.get("candidates", []),
                                              request.get("excluded", []))
                respond({"question": int(question), "score": float(score)})
            else:
                raise ValueError("unknown command: %s" % command)
        except Exception as error:
            respond({"error": str(error)})


if __name__ == "__main__":
    main()
