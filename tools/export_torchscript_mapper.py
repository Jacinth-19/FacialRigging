#!/usr/bin/env python3
"""Train / export a TorchScript viseme mapper for `MlVisemeMapper` (requires torch).

Input : float tensor [T, 17] = 13 MFCC/20, loudness, voicing, log2(pitch/median), centroid/4000
Output: float tensor [T, 9]  = viseme logits (Silence, AA, EE, IH, OH, UW, MBP, FV, L_TH)

  python3 tools/export_torchscript_mapper.py --out viseme_mlp.pt                # untrained skeleton
  python3 tools/export_torchscript_mapper.py --features feats.npy --labels y.npy --out viseme_mlp.pt

Features can be dumped from the C++ side with `fr_cli --dump-features` (see docs/AUDIO.md).
Use in the app with: --mapper ml --model-pt viseme_mlp.pt
"""
import argparse, sys
try:
    import torch, torch.nn as nn
except ImportError:
    sys.exit("PyTorch is required: pip install torch")

class VisemeMLP(nn.Module):
    def __init__(self, nin=17, hidden=64, nout=9):
        super().__init__()
        self.net = nn.Sequential(nn.Linear(nin, hidden), nn.ReLU(), nn.Linear(hidden, hidden), nn.ReLU(), nn.Linear(hidden, nout))
    def forward(self, x):  # x: [T, 17]
        return self.net(x)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True); ap.add_argument("--features"); ap.add_argument("--labels")
    ap.add_argument("--epochs", type=int, default=200); ap.add_argument("--hidden", type=int, default=64)
    a = ap.parse_args()
    m = VisemeMLP(hidden=a.hidden)
    if a.features and a.labels:
        import numpy as np
        X = torch.tensor(np.load(a.features), dtype=torch.float32); Y = torch.tensor(np.load(a.labels), dtype=torch.long)
        opt = torch.optim.Adam(m.parameters(), 1e-3); loss = nn.CrossEntropyLoss()
        for e in range(a.epochs):
            opt.zero_grad(); l = loss(m(X), Y); l.backward(); opt.step()
            if e % 20 == 0: print(f"epoch {e} loss {l.item():.4f}")
    m.eval()
    scripted = torch.jit.script(m)
    scripted.save(a.out)
    print("wrote", a.out)

if __name__ == "__main__":
    main()
