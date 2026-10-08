/* Complexweeper engine: a JavaScript port of cpp/src (game rules, solver, session with undo).
 *
 * It follows the C++ code closely, so the same seed gives the same board and the solver makes the
 * same moves. Cell index = row * width + column. Modes: 0 = circular complex, 1 = Minkowski.
 * Works as a plain <script> (global `CW`) and as a CommonJS module (for the tests). */
(function (root) {
  'use strict';

  const MAX_W = 40, MAX_H = 30, MAX_CELLS = MAX_W * MAX_H, MAX_MINES = 999;
  const MODE_COMPLEX = 0, MODE_HYPER = 1;
  const READY = 0, PLAYING = 1, WON = 2, LOST = 3;
  const KIND_NONE = 0, KIND_OPEN = 1, KIND_FLAG = 2, KIND_RETYPE = 3;
  const REASON_FIRST_CLICK = 0, REASON_CERTAIN = 1, REASON_GUESS = 2;
  const LEVEL_NAMES = ['Beginner', 'Intermediate', 'Expert'];
  // The four mine types as (real, imaginary).
  const TYPES = [[1, 0], [-1, 0], [0, 1], [0, -1]];
  const PRESETS = [
    [[9, 9, 7], [16, 16, 24], [30, 16, 47]],
    [[9, 9, 8], [16, 16, 25], [30, 16, 47]],
  ];

  function preset(mode, level) {
    const p = PRESETS[mode] && PRESETS[mode][level];
    return p ? { w: p[0], h: p[1], mines: p[2] } : null;
  }

  // ---------------------------------------------------------------- PCG32
  const MASK64 = (1n << 64n) - 1n;
  const PCG_MULT = 6364136223846793005n;
  const PCG_STREAM = 0xda3e39cb94b95bdbn;

  class Rng {
    constructor(seed = 1) {
      this.state = 0n;
      this.inc = 1n;
      this.reseed(BigInt(seed === 0 ? 1 : seed), PCG_STREAM);
    }
    reseed(initstate, initseq) {
      this.state = 0n;
      this.inc = ((initseq << 1n) | 1n) & MASK64;
      this.nextU32();
      this.state = (this.state + initstate) & MASK64;
      this.nextU32();
    }
    nextU32() {
      const old = this.state;
      this.state = (old * PCG_MULT + (this.inc | 1n)) & MASK64;
      const xs = Number((((old >> 18n) ^ old) >> 27n) & 0xffffffffn);
      const rot = Number(old >> 59n);
      return ((xs >>> rot) | (xs << ((32 - rot) & 31))) >>> 0;
    }
    // Uniform in [0, n) without modulo bias (Lemire's method), 0 when n == 0.
    below(n) {
      if (n === 0) return 0;
      const bound = BigInt(n);
      let m = BigInt(this.nextU32()) * bound;
      let low = m & 0xffffffffn;
      if (low < bound) {
        const threshold = (0x100000000n - bound) % bound;
        while (low < threshold) {
          m = BigInt(this.nextU32()) * bound;
          low = m & 0xffffffffn;
        }
      }
      return Number(m >> 32n);
    }
    clone() {
      const r = Object.create(Rng.prototype);
      r.state = this.state;
      r.inc = this.inc;
      return r;
    }
  }

  // ---------------------------------------------------------------- Game
  class Game {
    constructor() {
      this.w = 9; this.h = 9; this.n = 81;
      this.mode = MODE_COMPLEX;
      this.judge_loose = false;
      this.mine = new Uint8Array(MAX_CELLS);
      this.clue = new Int16Array(MAX_CELLS).fill(-1);
      this.open = new Uint8Array(MAX_CELLS);
      this.flag = new Uint8Array(MAX_CELLS);
      this.seed = 1;
      this.rng = new Rng(1);
      this.mines = 10;
      this.type_total = [0, 0, 0, 0, 0];
      this.flags_of = [0, 0, 0, 0, 0];
      this.type_count = [0, 0, 0, 0, 0];
      this.started = false; this.over = false; this.win = false;
      this.boom = -1; this.start_cell = -1; this.moves = 0;
    }

    clone() {
      const g = Object.create(Game.prototype);
      Object.assign(g, this);
      g.mine = this.mine.slice();
      g.clue = this.clue.slice();
      g.open = this.open.slice();
      g.flag = this.flag.slice();
      g.rng = this.rng.clone();
      g.type_total = this.type_total.slice();
      g.flags_of = this.flags_of.slice();
      g.type_count = this.type_count.slice();
      return g;
    }

    validCell(cell) { return Number.isInteger(cell) && cell >= 0 && cell < this.n; }

    nbrs(cell) {
      const out = [];
      if (!this.validCell(cell)) return out;
      const W = this.w, H = this.h;
      const r = Math.floor(cell / W), c = cell % W;
      for (let rr = Math.max(r - 1, 0); rr <= Math.min(r + 1, H - 1); rr++) {
        for (let cc = Math.max(c - 1, 0); cc <= Math.min(c + 1, W - 1); cc++) {
          if (rr === r && cc === c) continue;
          out.push(rr * W + cc);
        }
      }
      return out;
    }
    nbrMineCount(cell) { let k = 0; for (const j of this.nbrs(cell)) k += this.mine[j] !== 0; return k; }
    nbrFlagCount(cell) { let k = 0; for (const j of this.nbrs(cell)) k += this.flag[j] !== 0; return k; }
    isBlank(cell) { return this.validCell(cell) && this.mine[cell] === 0 && this.nbrMineCount(cell) === 0; }

    // Neighborhood sums (a, b), over the flags or over the real mines.
    sumsOf(cell, useFlag) {
      let a = 0, b = 0;
      for (const j of this.nbrs(cell)) {
        const t = useFlag ? this.flag[j] : this.mine[j];
        if (t === 0) continue;
        a += TYPES[t - 1][0];
        b += TYPES[t - 1][1];
      }
      return [a, b];
    }
    valueOf(a, b) { return this.mode === MODE_HYPER ? a * a - b * b : a * a + b * b; }

    setSeed(s) {
      this.seed = s === 0 ? 1 : s >>> 0;
      this.rng = new Rng(this.seed);
    }

    newGame(seed) {
      this.w = Math.min(Math.max(this.w, 1), MAX_W);
      this.h = Math.min(Math.max(this.h, 1), MAX_H);
      this.mines = Math.min(this.mines, MAX_MINES);
      this.n = this.w * this.h;
      this.mine.fill(0, 0, this.n);
      this.clue.fill(-1, 0, this.n);
      this.open.fill(0, 0, this.n);
      this.flag.fill(0, 0, this.n);
      this.started = false; this.over = false; this.win = false;
      this.boom = -1; this.start_cell = -1; this.moves = 0;
      this.type_total.fill(0);
      this.flags_of.fill(0);
      this.setSeed(seed);
    }

    startAt(cell) {
      if (!this.validCell(cell)) return;
      this.setSeed(this.seed);  // the same seed and first click give the same board
      this.genBoard(cell);
      this.start_cell = cell;
      this.started = true; this.over = false; this.win = false;
      this.moves = 1;
      this.checkWin();
    }

    genBoard(start) {
      const n = this.n, W = this.w, H = this.h;
      this.mine.fill(0, 0, n);
      this.clue.fill(-1, 0, n);
      this.open.fill(0, 0, n);
      const sr = Math.floor(start / W), sc = start % W;
      const pool = [];
      for (let r = 0; r < H; r++) {
        for (let c = 0; c < W; c++) {
          if (Math.abs(r - sr) <= 1 && Math.abs(c - sc) <= 1) continue;
          pool.push(r * W + c);
        }
      }
      const m = pool.length;
      for (let i = m; i > 1; i--) {
        const j = this.rng.below(i);
        const t = pool[i - 1]; pool[i - 1] = pool[j]; pool[j] = t;
      }
      let want = 0;
      for (let t = 1; t <= 4; t++) want += this.type_count[t];
      const count = Math.min(want > 0 ? want : this.mines, m);
      if (want > 0) {
        const list = [];
        for (let t = 1; t <= 4 && list.length < count; t++) {
          for (let k = 0; k < this.type_count[t] && list.length < count; k++) list.push(t);
        }
        for (let i = list.length; i > 1; i--) {
          const j = this.rng.below(i);
          const t = list[i - 1]; list[i - 1] = list[j]; list[j] = t;
        }
        for (let k = 0; k < list.length; k++) this.mine[pool[k]] = list[k];
      } else {
        for (let k = 0; k < count; k++) this.mine[pool[k]] = 1 + this.rng.below(4);
      }
      this.mines = count;
      this.computeClues();
      this.countTypes();
      this.cascadeOpen([start]);
    }

    computeClues() {
      for (let i = 0; i < this.n; i++) {
        if (this.mine[i] !== 0) { this.clue[i] = -1; continue; }
        const [a, b] = this.sumsOf(i, false);
        this.clue[i] = this.valueOf(a, b);
      }
    }
    countTypes() {
      this.type_total.fill(0);
      for (let i = 0; i < this.n; i++) if (this.mine[i] !== 0) this.type_total[this.mine[i]]++;
    }

    // Opens cells in a cascade through blank cells, going around flags.
    cascadeOpen(seeds) {
      const stack = [];
      let opened = 0;
      const visit = (i) => {
        if (this.open[i] !== 0 || this.mine[i] !== 0 || this.flag[i] !== 0) return;
        this.open[i] = 1;
        opened++;
        if (this.isBlank(i)) stack.push(i);
      };
      for (const s of seeds) if (this.validCell(s)) visit(s);
      while (stack.length) for (const j of this.nbrs(stack.pop())) visit(j);
      return opened;
    }

    reveal(cell) {
      if (!this.validCell(cell) || this.over || this.open[cell] !== 0 || this.flag[cell] !== 0) return;
      this.open[cell] = 1;
      if (this.mine[cell] !== 0) { this.lose(cell); return; }
      if (this.isBlank(cell)) this.cascadeOpen(this.nbrs(cell));
      this.moves++;
      this.checkWin();
    }

    setFlag(cell, t) {
      if (!this.validCell(cell) || t > 4) return false;
      const old = this.flag[cell];
      if (old === t) return true;
      if (old !== 0) this.flags_of[old]--;
      this.flag[cell] = t;
      if (t !== 0) this.flags_of[t]++;
      return true;
    }

    cycleFlag(cell) {
      if (!this.validCell(cell) || this.over || this.open[cell] !== 0) return false;
      this.setFlag(cell, (this.flag[cell] + 1) % 5);
      this.moves++;
      this.checkWin();
      return true;
    }

    // The chord criterion: the flags match the real mines around the cell, up to what the shown
    // numbers cannot tell apart.
    matchComboTruth(cell) {
      if (!this.validCell(cell) || this.nbrMineCount(cell) !== this.nbrFlagCount(cell)) return false;
      if (this.mode === MODE_HYPER) {
        const t = this.sumsOf(cell, false), f = this.sumsOf(cell, true);
        if (this.judge_loose) return t[0] * t[0] - t[1] * t[1] === f[0] * f[0] - f[1] * f[1];
        return Math.abs(f[0]) === Math.abs(t[0]) && Math.abs(f[1]) === Math.abs(t[1]);
      }
      let P = 0, V = 0, gp = 0, gv = 0;
      for (const j of this.nbrs(cell)) {
        if (this.mine[j] !== 0) { if (this.mine[j] <= 2) P++; else V++; }
        if (this.flag[j] !== 0) { if (this.flag[j] <= 2) gp++; else gv++; }
      }
      return gp + gv === P + V && ((gp === P && gv === V) || (gp === V && gv === P));
    }

    tryExpand(cell) {
      if (!this.validCell(cell) || this.over || this.open[cell] === 0 || this.mine[cell] !== 0) return;
      const uns = this.nbrs(cell).filter((j) => this.open[j] === 0 && this.flag[j] === 0);
      if (uns.length === 0) return;
      if (!this.matchComboTruth(cell)) return;
      for (const j of uns) {
        if (this.mine[j] !== 0) { this.open[j] = 1; this.lose(j); return; }
      }
      this.cascadeOpen(uns);
      this.moves++;
      this.checkWin();
    }

    flagsSolve() {
      for (let i = 0; i < this.n; i++) if ((this.mine[i] !== 0) !== (this.flag[i] !== 0)) return false;
      for (let i = 0; i < this.n; i++) {
        if (this.open[i] === 0 || this.mine[i] !== 0) continue;
        const [a, b] = this.sumsOf(i, true);
        if (this.valueOf(a, b) !== this.clue[i]) return false;
      }
      return true;
    }
    checkWin() {
      if (this.over || !this.flagsSolve()) return;
      this.over = true; this.win = true;
    }
    lose(cell) { this.over = true; this.win = false; this.boom = cell; }
  }

  // ---------------------------------------------------------------- Solver
  const COMPONENT_BUDGET = 50000, WINDOW_BUDGET = 10000, MAX_WINDOW_RADIUS = 2;
  const TYPE_BUDGET = 20000, REPAIR_BUDGET = 200000, FINAL_REPAIR_BUDGET = 5000000;
  const MAX_NBR = 8;

  function clueValue(mode, a, b) { return mode === MODE_HYPER ? a * a - b * b : a * a + b * b; }

  // REACH[mode][rem][a][b] is the set of displayed values that can still come out when the
  // summed vector so far is (a, b) and `rem` cells are undecided (index = value + 64).
  let REACH = null;
  function buildReach() {
    const t = new Uint8Array(2 * (MAX_NBR + 1) * 17 * 17 * 129);
    for (let m = 0; m < 2; m++) {
      for (let r = 0; r <= MAX_NBR; r++) {
        for (let a = -8; a <= 8; a++) {
          for (let b = -8; b <= 8; b++) {
            const base = (((m * (MAX_NBR + 1) + r) * 17 + (a + 8)) * 17 + (b + 8)) * 129;
            for (let da = -r; da <= r; da++) {
              const lim = r - Math.abs(da);
              for (let db = -lim; db <= lim; db++) {
                const v = clueValue(m, a + da, b + db);
                if (v >= -64 && v <= 64) t[base + v + 64] = 1;
              }
            }
          }
        }
      }
    }
    return t;
  }
  function reachable(mode, rem, a, b, value) {
    if (!REACH) REACH = buildReach();
    return REACH[(((mode * (MAX_NBR + 1) + rem) * 17 + (a + 8)) * 17 + (b + 8)) * 129 + value + 64] === 1;
  }

  // One constraint = one open number.
  function build(g, marks, fixTypes) {
    const P = { mode: g.mode, var_of: new Array(g.n).fill(-1), cell: [], dom: [], cons: [], cons_of: [] };
    const variable = (cell) => {
      if (P.var_of[cell] < 0) {
        P.var_of[cell] = P.cell.length;
        P.cell.push(cell);
        let d = 0b11111;
        if (marks[cell] !== 0) {
          d = fixTypes && g.flag[cell] >= 1 && g.flag[cell] <= 4 ? (1 << g.flag[cell]) : 0b11110;
        }
        P.dom.push(d);
        P.cons_of.push([]);
      }
      return P.var_of[cell];
    };
    for (let i = 0; i < g.n; i++) {
      if (g.open[i] === 0) continue;
      const c = { vars: [], n: 0, target: 0, need_mine: false };
      for (const j of g.nbrs(i)) if (g.open[j] === 0) c.vars.push(variable(j));
      c.n = c.vars.length;
      if (c.n === 0) continue;
      c.target = g.clue[i];
      const blank = c.target === 0 && g.isBlank(i);  // visible to the player
      c.need_mine = c.target === 0 && !blank;
      const ci = P.cons.length;
      for (let k = 0; k < c.n; k++) {
        if (blank) P.dom[c.vars[k]] &= 1;
        P.cons_of[c.vars[k]].push(ci);
      }
      P.cons.push(c);
    }
    return P;
  }

  // A smaller problem made of a subset of the constraints; back[i] = original variable of sub variable i.
  function subProblem(P, conIds, back) {
    back.length = 0;
    for (const ci of conIds) for (const v of P.cons[ci].vars) back.push(v);
    back.sort((x, y) => x - y);
    const uniq = back.filter((v, i) => i === 0 || v !== back[i - 1]);
    back.length = 0;
    back.push(...uniq);
    const Q = { mode: P.mode, cell: [], dom: [], cons: [], cons_of: [], var_of: null };
    const map = new Array(P.cell.length).fill(-1);
    for (let i = 0; i < back.length; i++) {
      map[back[i]] = i;
      Q.cell.push(P.cell[back[i]]);
      Q.dom.push(P.dom[back[i]]);
      Q.cons_of.push([]);
    }
    for (const ci of conIds) {
      const src = P.cons[ci];
      const c = { vars: src.vars.map((v) => map[v]), n: src.n, target: src.target, need_mine: src.need_mine };
      const qi = Q.cons.length;
      for (const v of c.vars) Q.cons_of[v].push(qi);
      Q.cons.push(c);
    }
    return Q;
  }

  // ---- Constraint propagation
  function supportDfs(P, c, k, a, b, nz, cur, support) {
    if (k === c.n) {
      if (clueValue(P.mode, a, b) === c.target && (!c.need_mine || nz > 0)) {
        for (let i = 0; i < c.n; i++) support[i] |= 1 << cur[i];
      }
      return;
    }
    const dom = P.dom[c.vars[k]];
    for (let t = 0; t <= 4; t++) {
      if (((dom >> t) & 1) === 0) continue;
      const da = t ? TYPES[t - 1][0] : 0, db = t ? TYPES[t - 1][1] : 0;
      if (!reachable(P.mode, c.n - k - 1, a + da, b + db, c.target)) continue;
      cur[k] = t;
      supportDfs(P, c, k + 1, a + da, b + db, nz + (t !== 0 ? 1 : 0), cur, support);
    }
  }

  // Drops every value no combination of a constraint supports, until nothing changes.
  // Returns false if the information is contradictory.
  function propagate(P) {
    for (let changed = true; changed;) {
      changed = false;
      for (const c of P.cons) {
        const cur = new Array(MAX_NBR).fill(0);
        const support = new Array(MAX_NBR).fill(0);
        supportDfs(P, c, 0, 0, 0, 0, cur, support);
        for (let i = 0; i < c.n; i++) {
          const d = P.dom[c.vars[i]];
          const nd = d & support[i];
          if (nd === 0) return false;
          if (nd !== d) { P.dom[c.vars[i]] = nd; changed = true; }
        }
      }
    }
    return true;
  }

  // ---- Exhaustive enumeration (counting solutions, or finding the first one)
  class Enumeration {
    constructor(P, vars, budget) {
      this.P = P; this.vars = vars; this.budget = budget;
      const nc = P.cons.length;
      this.sa = new Array(nc).fill(0);
      this.sb = new Array(nc).fill(0);
      this.rem = P.cons.map((c) => c.n);
      this.nz = new Array(nc).fill(0);
      this.val = new Array(P.cell.length).fill(0);
      this.cnt = P.cell.map(() => [0, 0, 0, 0, 0]);
      this.total = 0; this.nodes = 0; this.aborted = false;
      this.sym = false;
      this.mine_seen = false; this.real_seen = false; this.imag_seen = false;
      this.first_only = false; this.done = false;
      this.prefer = null;
    }

    // Symmetry breaking: only the canonical labeling of each symmetry class is visited.
    allowed(t) {
      if (!this.sym || t === 0) return true;
      const imag = t >= 3;
      if (this.P.mode === MODE_COMPLEX) {
        if (!this.mine_seen) return t === 1;
        if (imag && !this.imag_seen) return t === 3;
        return true;
      }
      if (!imag && !this.real_seen) return t === 1;
      if (imag && !this.imag_seen) return t === 3;
      return true;
    }

    // The size of the symmetry class a canonical solution stands for.
    weight() {
      if (!this.sym) return 1;
      if (this.P.mode === MODE_COMPLEX) return !this.mine_seen ? 1 : (this.imag_seen ? 8 : 4);
      return this.real_seen && this.imag_seen ? 4 : (this.real_seen || this.imag_seen ? 2 : 1);
    }

    dfs(k) {
      const P = this.P;
      if (k === this.vars.length) {
        if (this.first_only) { this.done = true; return; }
        const w = this.weight();
        this.total += w;
        for (const v of this.vars) this.cnt[v][this.val[v]] += w;
        return;
      }
      const v = this.vars[k];
      const order = [];
      const pref = this.prefer ? this.prefer[v] : 0;
      const hasPref = pref !== 0 && ((P.dom[v] >> pref) & 1) !== 0;
      if (hasPref) order.push(pref);
      for (let t = 0; t <= 4; t++) {
        if (((P.dom[v] >> t) & 1) !== 0 && !(hasPref && t === pref)) order.push(t);
      }
      for (let oi = 0; oi < order.length && !this.aborted && !this.done; oi++) {
        const t = order[oi];
        if (!this.allowed(t)) continue;
        if (++this.nodes > this.budget) { this.aborted = true; return; }
        const da = t ? TYPES[t - 1][0] : 0, db = t ? TYPES[t - 1][1] : 0;
        const cs = P.cons_of[v];
        for (const ci of cs) {
          this.sa[ci] += da; this.sb[ci] += db; this.rem[ci]--; this.nz[ci] += t !== 0 ? 1 : 0;
        }
        let ok = true;
        for (const ci of cs) {
          const c = P.cons[ci];
          if (!reachable(P.mode, this.rem[ci], this.sa[ci], this.sb[ci], c.target) ||
              (this.rem[ci] === 0 && c.need_mine && this.nz[ci] === 0)) {
            ok = false;
            break;
          }
        }
        if (ok) {
          this.val[v] = t;
          const m0 = this.mine_seen, r0 = this.real_seen, i0 = this.imag_seen;
          if (t !== 0) {
            this.mine_seen = true;
            if (t >= 3) this.imag_seen = true; else this.real_seen = true;
          }
          this.dfs(k + 1);
          this.mine_seen = m0; this.real_seen = r0; this.imag_seen = i0;
        }
        for (const ci of cs) {
          this.sa[ci] -= da; this.sb[ci] -= db; this.rem[ci]++; this.nz[ci] -= t !== 0 ? 1 : 0;
        }
      }
    }

    complete() { return !this.aborted && this.total > 0; }
  }

  class Findings {
    constructor(n) {
      this.safe = new Array(n).fill(0);
      this.mine = new Array(n).fill(0);
      this.known_mine = new Array(n).fill(0);
      this.prob = new Array(n).fill(-1);
      this.any = false;
    }
    add(v, cnt0, total) {
      if (cnt0 === total) { this.safe[v] = 1; this.any = true; }
      else if (cnt0 === 0 && !this.known_mine[v]) { this.mine[v] = 1; this.any = true; }
    }
  }

  // Variables connected through shared constraints.
  function components(P) {
    const parent = P.cell.map((_, i) => i);
    const find = (x) => {
      while (parent[x] !== x) { parent[x] = parent[parent[x]]; x = parent[x]; }
      return x;
    };
    for (const c of P.cons) for (let i = 1; i < c.n; i++) parent[find(c.vars[i])] = find(c.vars[0]);
    const groups = [];
    const idOfRoot = new Array(P.cell.length).fill(-1);
    const groupOf = new Array(P.cell.length).fill(-1);
    for (let v = 0; v < P.cell.length; v++) {
      const r = find(v);
      if (idOfRoot[r] < 0) { idOfRoot[r] = groups.length; groups.push([]); }
      groupOf[v] = idOfRoot[r];
      groups[idOfRoot[r]].push(v);
    }
    return { groups, groupOf };
  }

  // Constraints within `radius` steps of constraint `center`.
  function windowOf(P, center, radius) {
    const inside = new Array(P.cons.length).fill(0);
    const ids = [center];
    inside[center] = 1;
    for (let step = 0, begin = 0; step < radius; step++) {
      const end = ids.length;
      for (let i = begin; i < end; i++) {
        for (const v of P.cons[ids[i]].vars) {
          for (const cj of P.cons_of[v]) {
            if (!inside[cj]) { inside[cj] = 1; ids.push(cj); }
          }
        }
      }
      begin = end;
    }
    return ids;
  }

  // The mine types in the order the solver prefers them for a given orientation.
  function typeOrder(mode, orientation) {
    const flipReal = (orientation & 1) !== 0, flipImag = (orientation & 2) !== 0;
    const swapAxes = mode === MODE_COMPLEX && (orientation & 4) !== 0;
    const order = [];
    for (let k = 0; k < 4; k++) {
      let t = k + 1;
      if (flipReal) t = t === 1 ? 2 : (t === 2 ? 1 : t);
      if (flipImag) t = t === 3 ? 4 : (t === 4 ? 3 : t);
      if (swapAxes) t = t === 1 ? 3 : (t === 3 ? 1 : (t === 2 ? 4 : (t === 4 ? 2 : t)));
      order.push(t);
    }
    return order;
  }

  const f32 = Math.fround;

  function makeMove(kind, cell, reason) {
    return { kind, cell, reason: reason === undefined ? REASON_CERTAIN : reason, type: 0, retyped: 0, risk: 0 };
  }

  class Solver {
    constructor() {
      this.marks = new Uint8Array(MAX_CELLS);
      this.queue = [];  // stored in reverse order; the next step is at the back
      this.orientation = 0;
    }
    clone() {
      const s = new Solver();
      s.marks = this.marks.slice();
      s.queue = this.queue.map((m) => Object.assign({}, m));
      s.orientation = this.orientation;
      return s;
    }
    setOrientation(o) { this.orientation = o & 7; }
    markCount() { let k = 0; for (let i = 0; i < this.marks.length; i++) k += this.marks[i] !== 0; return k; }

    syncMarks(g) {
      for (let i = 0; i < g.n; i++) {
        if (this.marks[i] !== 0 && (g.open[i] !== 0 || g.flag[i] === 0)) this.marks[i] = 0;
      }
    }

    chooseType(g, cell) {
      const order = typeOrder(g.mode, this.orientation);
      const P = build(g, this.marks, true);
      const v = P.var_of[cell];
      if (v < 0) return order[0];  // no open number touches this mine: any type will do
      const { groups, groupOf } = components(P);
      const comp = groups[groupOf[v]];
      for (const t of order) {
        const Q = Object.assign({}, P, { dom: P.dom.slice() });
        Q.dom[v] = 1 << t;
        if (!propagate(Q)) continue;
        const e = new Enumeration(Q, comp, TYPE_BUDGET);
        e.first_only = true;
        e.dfs(0);
        if (e.done || e.aborted) return t;  // out of budget: assume it works
      }
      return order[0];
    }

    // Re-orients flags if the numbers contradict their current types. Returns how many changed.
    repair(g, budget) {
      if (this.markCount() === 0) return 0;
      const P = build(g, this.marks, true);
      if (propagate(P)) return 0;
      const Q = build(g, this.marks, false);
      if (!propagate(Q)) return 0;
      const { groups } = components(Q);
      let changed = 0;
      for (const comp of groups) {
        const order = [];
        const prefer = new Array(Q.cell.length).fill(0);
        for (const v of comp) {
          if (this.marks[Q.cell[v]] !== 0) { order.push(v); prefer[v] = g.flag[Q.cell[v]]; }
        }
        if (order.length === 0) continue;
        for (const v of comp) if (this.marks[Q.cell[v]] === 0) order.push(v);
        const e = new Enumeration(Q, order, budget);
        e.first_only = true;
        e.prefer = prefer;
        e.dfs(0);
        if (!e.done) continue;
        for (const v of comp) {
          const cell = Q.cell[v];
          if (this.marks[cell] !== 0 && g.flag[cell] !== e.val[v]) {
            g.setFlag(cell, e.val[v]);
            changed++;
          }
        }
      }
      return changed;
    }

    analyze(g) {
      const marks = this.marks;
      const P = build(g, marks, false);
      if (!propagate(P)) return false;
      const nv = P.cell.length;
      const F = new Findings(nv);
      for (let v = 0; v < nv; v++) F.known_mine[v] = marks[P.cell[v]] !== 0 ? 1 : 0;
      const extraOpens = [], extraMines = [];

      // Level 1: whatever constraint propagation already decided.
      let decided = false;
      for (let v = 0; v < nv; v++) {
        if (P.dom[v] === 1) { F.safe[v] = 1; decided = true; }
        else if ((P.dom[v] & 1) === 0 && marks[P.cell[v]] === 0) { F.mine[v] = 1; decided = true; }
      }

      // Level 2: the total mine count.
      if (!decided) {
        let closed = 0;
        for (let i = 0; i < g.n; i++) closed += g.open[i] === 0 && marks[i] === 0 ? 1 : 0;
        const placed = this.markCount();
        const left = g.mines > placed ? g.mines - placed : 0;
        if (closed > 0 && (left === 0 || left === closed)) {
          for (let i = 0; i < g.n; i++) {
            if (g.open[i] !== 0 || marks[i] !== 0) continue;
            (left === 0 ? extraOpens : extraMines).push(makeMove(left === 0 ? KIND_OPEN : KIND_FLAG, i));
          }
          decided = true;
        }
      }

      // Level 3: exhaustive enumeration, which also yields mine probabilities for guessing.
      if (!decided) {
        const { groups, groupOf } = components(P);
        const tooBig = [];
        groups.forEach((comp, ci) => {
          const e = new Enumeration(P, comp, COMPONENT_BUDGET);
          e.sym = true;
          e.dfs(0);
          if (e.aborted) {
            tooBig.push(ci);
          } else if (e.total > 0) {
            for (const v of comp) {
              F.add(v, e.cnt[v][0], e.total);
              F.prob[v] = f32(1 - f32(f32(e.cnt[v][0]) / f32(e.total)));
            }
          }
        });
        // Components too big: enumerate small windows around each constraint instead.
        if (!F.any && tooBig.length) {
          const consOfComp = groups.map(() => []);
          P.cons.forEach((c, ci) => consOfComp[groupOf[c.vars[0]]].push(ci));
          for (let radius = 1; radius <= MAX_WINDOW_RADIUS && !F.any; radius++) {
            for (const comp of tooBig) {
              for (const center of consOfComp[comp]) {
                const back = [];
                const Q = subProblem(P, windowOf(P, center, radius), back);
                const all = back.map((_, i) => i);
                const e = new Enumeration(Q, all, WINDOW_BUDGET);
                e.sym = true;
                e.dfs(0);
                if (!e.complete()) continue;
                for (let i = 0; i < back.length; i++) F.add(back[i], e.cnt[i][0], e.total);
              }
            }
          }
        }
      }

      if (F.any || decided) {
        const opens = extraOpens.slice(), mines = extraMines.slice();
        for (let v = 0; v < nv; v++) {
          if (F.safe[v] && !F.mine[v]) opens.push(makeMove(KIND_OPEN, P.cell[v]));
          else if (F.mine[v] && !F.safe[v] && marks[P.cell[v]] === 0) mines.push(makeMove(KIND_FLAG, P.cell[v]));
        }
        if (opens.length || mines.length) {
          this.queue = mines.slice().reverse().concat(opens.slice().reverse());
          return true;
        }
      }

      // Level 4: guess.
      let exactMines = 0, otherCount = 0, bestV = -1;
      for (let v = 0; v < nv; v++) {
        if (marks[P.cell[v]] !== 0) continue;
        if (F.prob[v] < 0) { otherCount++; continue; }
        exactMines += F.prob[v];
        if (bestV < 0 || F.prob[v] < F.prob[bestV]) bestV = v;
      }
      let outsideBest = g.n, outsideBestNbrs = 99;
      for (let i = 0; i < g.n; i++) {
        if (g.open[i] !== 0 || marks[i] !== 0 || P.var_of[i] >= 0) continue;
        otherCount++;
        const nb = g.nbrs(i).length;  // fewer neighbors: a corner or edge, likelier to open a blank area
        if (nb < outsideBestNbrs) { outsideBestNbrs = nb; outsideBest = i; }
      }
      const minesLeft = Math.max(0, g.mines - this.markCount() - exactMines);
      const density = otherCount ? Math.min(1, minesLeft / otherCount) : 2;

      const m = makeMove(KIND_OPEN, 0, REASON_GUESS);
      if (bestV >= 0 && F.prob[bestV] <= density) {
        m.cell = P.cell[bestV];
        m.risk = F.prob[bestV];
      } else if (outsideBest < g.n) {
        m.cell = outsideBest;
        m.risk = f32(density);
      } else {
        let any = -1;
        for (let v = 0; v < nv && any < 0; v++) if (marks[P.cell[v]] === 0 && F.prob[v] < 0) any = v;
        if (any < 0) return false;
        m.cell = P.cell[any];
        m.risk = f32(density);
      }
      this.queue = [m];
      return true;
    }

    // Advance one step and apply it to `g`. kind === KIND_NONE: nothing left to do.
    step(g) {
      if (g.over) return makeMove(KIND_NONE, 0);
      if (!g.started) {
        this.queue = [];
        this.marks.fill(0);
        const m = makeMove(KIND_OPEN, Math.floor(g.h / 2) * g.w + Math.floor(g.w / 2), REASON_FIRST_CLICK);
        g.startAt(m.cell);
        return m;
      }
      this.syncMarks(g);
      for (let attempt = 0; attempt < 2; attempt++) {
        while (this.queue.length) {
          const m = this.queue.pop();
          if (g.open[m.cell] !== 0 || this.marks[m.cell] !== 0) continue;  // already handled
          if (m.kind === KIND_OPEN) {
            if (g.flag[m.cell] !== 0) g.setFlag(m.cell, 0);  // a player's flag must not block the solver
            g.reveal(m.cell);
            if (!g.over) {
              m.retyped = this.repair(g, REPAIR_BUDGET);
              if (m.retyped !== 0) g.checkWin();
            }
          } else {
            m.type = this.chooseType(g, m.cell);
            g.setFlag(m.cell, m.type);
            this.marks[m.cell] = 1;
            m.retyped = this.repair(g, REPAIR_BUDGET);
            m.type = g.flag[m.cell];  // re-orienting may have changed this flag too
            g.checkWin();
          }
          return m;
        }
        if (!this.analyze(g)) break;
      }
      // Nothing left to play: if every mine is flagged but the flags disagree with the numbers,
      // re-orient them with a much larger budget.
      const m = makeMove(KIND_NONE, 0);
      m.retyped = this.repair(g, FINAL_REPAIR_BUDGET);
      if (m.retyped !== 0) {
        m.kind = KIND_RETYPE;
        g.checkWin();
        return m;
      }
      return makeMove(KIND_NONE, 0);
    }
  }

  // ---------------------------------------------------------------- Session
  // Game + solver + undo history. Before every action that changes the position the whole state
  // is saved; undo restores the latest snapshot.
  class Session {
    constructor(historyLimit = 2000) {
      this.game = new Game();
      this.solver = new Solver();
      this.history = [];
      this.limit = historyLimit;
      this.orientation = 0;
    }

    newGame(w, h, mines, mode, seed) {
      const g = this.game;
      g.w = w; g.h = h; g.mines = mines; g.mode = mode;
      g.newGame(seed >>> 0);
      this.solver = new Solver();
      this.solver.setOrientation(this.orientation);
      this.history = [];
    }
    setSolverOrientation(o) {
      this.orientation = o & 7;
      this.solver.setOrientation(this.orientation);
    }

    checkpoint() {
      this.history.push({ game: this.game.clone(), solver: this.solver.clone() });
      if (this.history.length > this.limit) this.history.shift();
    }
    static sameState(a, b) {
      if (a.n !== b.n || a.started !== b.started || a.over !== b.over || a.win !== b.win || a.moves !== b.moves) return false;
      for (let i = 0; i < a.n; i++) if (a.open[i] !== b.open[i] || a.flag[i] !== b.flag[i]) return false;
      return true;
    }
    // Runs `action`, keeping a history entry only if it changed the position.
    recorded(action) {
      this.checkpoint();
      action();
      if (Session.sameState(this.history[this.history.length - 1].game, this.game)) {
        this.history.pop();
        return false;
      }
      return true;
    }

    click(cell) {
      const g = this.game;
      if (g.over || !g.validCell(cell)) return false;
      return this.recorded(() => { if (!g.started) g.startAt(cell); else g.reveal(cell); });
    }
    cycleFlag(cell) {
      const g = this.game;
      if (!g.started || g.over || !g.validCell(cell)) return false;
      return this.recorded(() => g.cycleFlag(cell));
    }
    chord(cell) {
      const g = this.game;
      if (!g.started || g.over || !g.validCell(cell)) return false;
      return this.recorded(() => g.tryExpand(cell));
    }
    solverStep() {
      if (this.game.over) return makeMove(KIND_NONE, 0);
      this.checkpoint();
      const m = this.solver.step(this.game);
      if (m.kind === KIND_NONE) this.history.pop();
      return m;
    }
    undo() {
      if (!this.history.length) return false;
      const h = this.history.pop();
      this.game = h.game;
      this.solver = h.solver;
      return true;
    }
    get undoDepth() { return this.history.length; }
    markedCount() { let k = 0; for (let i = 0; i < this.game.n; i++) k += this.game.flag[i] !== 0; return k; }
    get state() {
      const g = this.game;
      if (!g.started) return READY;
      if (!g.over) return PLAYING;
      return g.win ? WON : LOST;
    }
    // What the player may see of one cell (the mine only after the game is over).
    cellView(i) {
      const g = this.game;
      const opened = g.open[i] !== 0;
      return {
        clue: opened ? g.clue[i] : -1,
        open: opened,
        flag: g.flag[i],
        mine: g.over ? g.mine[i] : 0,
        blank: opened && g.isBlank(i),
      };
    }
  }

  const CW = {
    MODE_COMPLEX, MODE_HYPER, READY, PLAYING, WON, LOST,
    KIND_NONE, KIND_OPEN, KIND_FLAG, KIND_RETYPE,
    REASON_FIRST_CLICK, REASON_CERTAIN, REASON_GUESS,
    LEVEL_NAMES, preset, Rng, Game, Solver, Session,
  };
  root.CW = CW;
  if (typeof module !== 'undefined' && module.exports) module.exports = CW;
})(typeof globalThis !== 'undefined' ? globalThis : this);
