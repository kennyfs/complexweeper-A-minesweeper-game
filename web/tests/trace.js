// Prints the solver's moves for a list of games as JSON (used by crosscheck.py).
// usage: node trace.js '[{"mode":0,"level":0,"seed":1,"orient":0}, ...]'
const CW = require('../engine.js');

function cells(s) {
  const out = [];
  for (let i = 0; i < s.game.n; i++) {
    const c = s.cellView(i);
    out.push([c.clue, +c.open, c.flag, c.mine, +c.blank]);
  }
  return out;
}

const configs = JSON.parse(process.argv[2]);
const result = configs.map((cfg) => {
  const s = new CW.Session();
  const p = CW.preset(cfg.mode, cfg.level);
  s.setSolverOrientation(cfg.orient);
  s.newGame(p.w, p.h, p.mines, cfg.mode, cfg.seed);
  const moves = [];
  for (let k = 0; k < 600; k++) {
    const m = s.solverStep();
    if (m.kind === CW.KIND_NONE) break;
    moves.push([m.kind, m.cell, m.reason, m.type, m.retyped, Math.round(m.risk * 1e4) / 1e4]);
    if (k === 20) {  // undo / redo through the history must be exact
      for (let u = 0; u < 5; u++) s.undo();
      for (let u = 0; u < 5; u++) s.solverStep();
    }
  }
  return { moves, state: s.state, depth: s.undoDepth, cells: cells(s) };
});
process.stdout.write(JSON.stringify(result));
