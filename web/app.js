/* Complexweeper web UI: the page logic of gui/app.py. Needs engine.js, sprites.js, atlas-slots.js. */
(function () {
  'use strict';
  const { Session, MODE_COMPLEX, MODE_HYPER, READY, PLAYING, WON, LOST } = CW;
  const { KIND_OPEN, KIND_RETYPE, REASON_FIRST_CLICK, REASON_GUESS } = CW;

  const MAX_CELL = 44;        // css pixels
  const MIN_CELL = 13;        // below this the board scrolls sideways instead of shrinking
  const LONG_PRESS_MS = 450;
  // Highlight colors for the cell the solver just acted on.
  const COLOR_FIRST = '#3b82f6', COLOR_OPEN = '#16a34a', COLOR_FLAG = '#f97316', COLOR_GUESS = '#eab308';

  const $ = (id) => document.getElementById(id);
  const el = {
    window: $('window'), mode: $('mode'), level: $('level'), seed: $('seed'), newBtn: $('new-btn'),
    orient: $('orient'), face: $('face'), faceCanvas: $('face-canvas'),
    mineLed: $('mine-led'), timeLed: $('time-led'), board: $('board'), wrap: $('board-wrap'),
    toolOpen: $('tool-open'), toolFlag: $('tool-flag'), undo: $('undo'), step: $('step'), auto: $('auto'),
    delay: $('delay'), delayOut: $('delay-out'), status: $('status'),
  };

  const ctx = el.board.getContext('2d');
  const sess = new Session();

  const st = {
    gameSeed: null,       // the seed the current game was generated from
    highlight: null,      // {cell, color} of the solver's latest step
    auto: false, autoTimer: null,
    tStart: null, elapsed: 0,
    prevState: READY, hinted: false,
    flagTool: false,
    cs: 24,               // css pixels per cell
    pressing: false,
    ledText: { mine: null, time: null },
  };

  // ------------------------------------------------------------------ helpers
  function randomSeed() {
    const a = new Uint32Array(1);
    do { crypto.getRandomValues(a); } while ((a[0] & 0x7fffffff) === 0);
    return a[0] & 0x7fffffff;  // 1 .. 2^31-1
  }

  function typedSeed() {
    const t = el.seed.value.trim();
    if (!/^-?\d+$/.test(t)) return null;
    const v = Number(t);
    return Number.isSafeInteger(v) ? v : null;
  }

  function seedKey(v) { return v === null ? null : v >>> 0; }  // the engine keeps 32 bits

  function typeName(t, mode) {
    const unit = mode === MODE_HYPER ? 'j' : 'i';
    return ['', '+1', '-1', '+' + unit, '-' + unit][t];
  }

  const say = (text) => { el.status.textContent = text; };

  // ------------------------------------------------------------------ game flow
  function currentMode() { return Number(el.mode.value) === MODE_HYPER ? MODE_HYPER : MODE_COMPLEX; }

  function updateNewButton() {
    const seed = typedSeed();
    el.newBtn.textContent = seed !== null && seedKey(seed) !== st.gameSeed ? 'Generate game' : 'New game';
  }

  // A game from the seed in the box (a random one if the box is empty or not a number).
  function newGame() {
    stopAuto();
    const mode = currentMode();
    const p = CW.preset(mode, Number(el.level.value));
    let seed = typedSeed();
    if (seed === null) seed = randomSeed();
    el.seed.value = String(seed);
    st.gameSeed = seedKey(seed);
    updateNewButton();
    // The orientation follows from the seed, so replaying a seed looks the same.
    const orientation = el.orient.checked ? (Math.imul(st.gameSeed, 2654435761) >>> 29) : 0;
    sess.setSolverOrientation(orientation);
    sess.newGame(p.w, p.h, p.mines, mode, st.gameSeed);
    st.tStart = null; st.elapsed = 0;
    st.prevState = READY; st.hinted = false; st.highlight = null;
    layoutBoard();
    say('New game: ' + p.mines + ' mines on a ' + p.w + 'x' + p.h +
        ' board. Click a cell, or press Step and let the solver play.');
    refresh();
  }

  // A game from a fresh random seed that does not depend on the previous one.
  function newRandomGame() {
    let seed = randomSeed();
    while (seed === st.gameSeed) seed = randomSeed();
    el.seed.value = String(seed);
    newGame();
  }

  function onNewButton() {
    if (el.newBtn.textContent === 'Generate game') newGame(); else newRandomGame();
  }

  const over = () => sess.state === WON || sess.state === LOST;

  function manual(changed) {
    if (!changed) return;
    stopAuto();
    st.highlight = null;
    say('');
    refresh();
  }

  // ------------------------------------------------------------------ board size
  const dpr = () => window.devicePixelRatio || 1;

  // Sizes a canvas to css pixels while keeping it sharp on high-density screens.
  function sizeCanvas(canvas, cssW, cssH) {
    canvas.style.width = cssW + 'px';
    canvas.style.height = cssH + 'px';
    canvas.width = Math.round(cssW * dpr());
    canvas.height = Math.round(cssH * dpr());
  }

  function layoutBoard() {
    const g = sess.game;
    const narrow = window.innerWidth <= 560;
    // The window's chrome (padding and borders) takes about 50px on the desktop, less on a phone.
    const availW = Math.max(160, document.documentElement.clientWidth - (narrow ? 34 : 50));
    const availH = window.innerHeight - 330;
    let cs = Math.min(availW / g.w, Math.max(availH / g.h, 24), MAX_CELL);
    cs = Math.max(Math.floor(cs), MIN_CELL);
    st.cs = cs;
    sizeCanvas(el.board, g.w * cs, g.h * cs);
    // The LED displays and the face have a fixed size.
    const ledH = narrow ? 34 : 44, ledW = Math.round(ledH * 13 / 23);
    sizeCanvas(el.mineLed, ledW * 3, ledH);
    sizeCanvas(el.timeLed, ledW * 3, ledH);
    sizeCanvas(el.faceCanvas, ledH, ledH);
    st.ledText.mine = st.ledText.time = null;
  }

  // ------------------------------------------------------------------ drawing
  function drawLed(canvas, key, value) {
    const text = value < 0 ? '-' + String(Math.min(-value, 99)).padStart(2, '0') : String(Math.min(value, 999)).padStart(3, '0');
    if (st.ledText[key] === text) return;
    st.ledText[key] = text;
    const c = canvas.getContext('2d');
    c.setTransform(dpr(), 0, 0, dpr(), 0, 0);
    const w = canvas.width / dpr() / 3, h = canvas.height / dpr();
    c.clearRect(0, 0, w * 3, h);
    for (let i = 0; i < 3; i++) Sprites.draw(c, text[i] === '-' ? 'led_minus' : 'led_' + text[i], i * w, 0, w, h);
  }

  function drawFace(name) {
    const c = el.faceCanvas.getContext('2d');
    c.setTransform(dpr(), 0, 0, dpr(), 0, 0);
    const s = el.faceCanvas.width / dpr();
    c.clearRect(0, 0, s, s);
    Sprites.draw(c, name, 0, 0, s, s);
  }

  function drawBoard(hyper, isOver) {
    const g = sess.game, cs = st.cs;
    ctx.setTransform(dpr(), 0, 0, dpr(), 0, 0);
    ctx.fillStyle = Sprites.boardColor;
    ctx.fillRect(0, 0, g.w * cs, g.h * cs);
    let closed = 0;
    for (let i = 0; i < g.n; i++) {
      const cell = sess.cellView(i);
      closed += !cell.open;
      Sprites.draw(ctx, Sprites.cellSprite(cell, i, hyper, isOver, g.boom), (i % g.w) * cs, Math.floor(i / g.w) * cs, cs, cs);
    }
    if (st.highlight) {
      const { cell, color } = st.highlight;
      ctx.strokeStyle = color;
      ctx.lineWidth = Math.max(2, cs / 10);
      ctx.beginPath();
      ctx.roundRect((cell % g.w) * cs + cs * 0.06, Math.floor(cell / g.w) * cs + cs * 0.06, cs * 0.88, cs * 0.88, cs * 0.16);
      ctx.stroke();
    }
    return closed;
  }

  function refresh() {
    const g = sess.game, state = sess.state;
    const isOver = state === WON || state === LOST;
    const closed = drawBoard(g.mode === MODE_HYPER, isOver);

    // Timer: starts with the first click, freezes at the end, resets with the game.
    if (state === READY) { st.tStart = null; st.elapsed = 0; }
    else if (state === PLAYING && st.tStart === null) st.tStart = performance.now() - st.elapsed * 1000;
    drawLed(el.mineLed, 'mine', g.mines - sess.markedCount());
    drawLed(el.timeLed, 'time', st.elapsed);

    drawFace(state === LOST ? 'face_dead' : state === WON ? 'face_win' : st.pressing ? 'face_down' : st.auto ? 'face_scan' : 'face_normal');

    if (state !== st.prevState) {
      if (state === WON) say('Every mine is flagged and every number agrees. You win!');
      else if (state === LOST) say((st.highlight ? el.status.textContent + ' ' : '') + 'Boom, that was a mine. The game is lost.');
      st.prevState = state;
    } else if (state === PLAYING && closed === g.mines && !st.hinted && !st.auto) {
      st.hinted = true;
      say('Only mines are left closed. Flag them all (right click or the Flag switch) to win.');
    }

    el.undo.disabled = sess.undoDepth === 0;
    el.step.disabled = isOver;
    el.auto.disabled = isOver && !st.auto;
    el.auto.textContent = st.auto ? 'Pause' : 'Auto play';
  }

  // ------------------------------------------------------------------ solver
  function describe(mv) {
    const g = sess.game;
    const where = '(' + (Math.floor(mv.cell / g.w) + 1) + ', ' + (mv.cell % g.w + 1) + ')';
    const step = 'Step ' + sess.undoDepth + ': ';
    const re = mv.retyped ? ' Re-oriented ' + mv.retyped + ' flag' + (mv.retyped === 1 ? '' : 's') + '.' : '';
    if (mv.kind === KIND_RETYPE) return step + 're-oriented ' + mv.retyped + ' flags so that every number agrees.';
    if (mv.reason === REASON_FIRST_CLICK) return step + 'first click in the middle ' + where + '. It and its neighbors are always safe.';
    if (mv.reason === REASON_GUESS) return step + 'guess ' + where + ', ' + Math.round(mv.risk * 100) + '% chance of a mine. Nothing is certain.' + re;
    if (mv.kind === KIND_OPEN) return step + 'open ' + where + ', certainly safe.' + re;
    return step + 'flag ' + where + ' as ' + typeName(mv.type, g.mode) + ', certainly a mine.' + re;
  }

  function step() {
    if (over()) return false;
    const mv = sess.solverStep();
    if (mv.kind === CW.KIND_NONE) {
      stopAuto();
      say('The solver has no further move.');
      refresh();
      return false;
    }
    if (mv.kind === KIND_RETYPE) st.highlight = null;
    else {
      const color = mv.reason === REASON_FIRST_CLICK ? COLOR_FIRST : mv.reason === REASON_GUESS ? COLOR_GUESS
        : mv.kind === KIND_OPEN ? COLOR_OPEN : COLOR_FLAG;
      st.highlight = { cell: mv.cell, color };
    }
    say(describe(mv));
    refresh();
    return true;
  }

  function undo() {
    if (!sess.undo()) return;
    stopAuto();
    st.highlight = null;
    st.hinted = false;
    say('Undid one step (' + sess.undoDepth + ' left to undo).');
    // The restored game may have a different state than the last one drawn.
    st.prevState = sess.state;
    refresh();
  }

  function toggleAuto() {
    if (st.auto) { stopAuto(); refresh(); }
    else if (!over()) {
      st.auto = true;
      refresh();
      autoTick();
    }
  }

  function stopAuto() {
    st.auto = false;
    if (st.autoTimer !== null) { clearTimeout(st.autoTimer); st.autoTimer = null; }
    el.auto.textContent = 'Auto play';
  }

  function autoTick() {
    st.autoTimer = null;
    if (!st.auto) return;
    if (!step() || over()) { stopAuto(); refresh(); return; }
    st.autoTimer = setTimeout(autoTick, Number(el.delay.value));
  }

  // ------------------------------------------------------------------ input
  function cellAt(ev) {
    const r = el.board.getBoundingClientRect();
    const g = sess.game;
    const col = Math.floor((ev.clientX - r.left) / st.cs);
    const row = Math.floor((ev.clientY - r.top) / st.cs);
    return col >= 0 && col < g.w && row >= 0 && row < g.h ? row * g.w + col : null;
  }

  let press = null;  // {cell, type, button, timer, long}

  function cancelPress() {
    if (press && press.timer) clearTimeout(press.timer);
    press = null;
    if (st.pressing) { st.pressing = false; refresh(); }
  }

  el.board.addEventListener('contextmenu', (e) => e.preventDefault());

  el.board.addEventListener('pointerdown', (e) => {
    const cell = cellAt(e);
    if (cell === null) return;
    if (e.pointerType === 'mouse') {
      if (e.button === 2) { manual(sess.cycleFlag(cell)); e.preventDefault(); return; }
      if (e.button === 1) { manual(sess.chord(cell)); e.preventDefault(); return; }
      if (e.button !== 0) return;
    }
    press = { cell, type: e.pointerType, shift: e.shiftKey, long: false, timer: null };
    if (!over()) { st.pressing = true; refresh(); }
    if (e.pointerType !== 'mouse') {
      press.timer = setTimeout(() => {
        if (!press) return;
        press.long = true;
        manual(sess.cycleFlag(press.cell));
        if (navigator.vibrate) navigator.vibrate(20);
        st.pressing = false;
        refresh();
      }, LONG_PRESS_MS);
    }
  });

  el.board.addEventListener('pointerup', (e) => {
    if (!press) return;
    const p = press;
    cancelPress();
    if (p.long || over()) return;
    const cell = cellAt(e);
    if (cell === null) return;
    const open = sess.game.open[cell] !== 0;
    if (p.shift) manual(sess.chord(cell));
    else if (p.type === 'mouse') manual(sess.click(cell));
    else if (st.flagTool) manual(open ? sess.chord(cell) : sess.cycleFlag(cell));
    else manual(open ? sess.chord(cell) : sess.click(cell));
  });

  el.board.addEventListener('pointercancel', cancelPress);
  el.board.addEventListener('pointerleave', (e) => { if (e.pointerType === 'mouse') cancelPress(); });
  // Scrolling the page or the board with a finger must not count as a tap.
  el.board.addEventListener('pointermove', (e) => {
    if (press && press.type !== 'mouse' && press.timer && cellAt(e) !== press.cell) cancelPress();
  });

  function setTool(flag) {
    st.flagTool = flag;
    el.toolFlag.classList.toggle('on', flag);
    el.toolOpen.classList.toggle('on', !flag);
  }

  el.toolOpen.addEventListener('click', () => setTool(false));
  el.toolFlag.addEventListener('click', () => setTool(true));
  el.undo.addEventListener('click', undo);
  el.step.addEventListener('click', step);
  el.auto.addEventListener('click', toggleAuto);
  el.face.addEventListener('click', newRandomGame);
  el.newBtn.addEventListener('click', onNewButton);
  el.mode.addEventListener('change', newGame);
  el.level.addEventListener('change', newGame);
  el.orient.addEventListener('change', newGame);
  el.seed.addEventListener('input', updateNewButton);
  el.seed.addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); onNewButton(); } });
  el.delay.addEventListener('input', () => { el.delayOut.textContent = el.delay.value; });

  document.addEventListener('keydown', (e) => {
    if (e.key === 'F2') { e.preventDefault(); newRandomGame(); return; }
    const tag = document.activeElement && document.activeElement.tagName;
    if (tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA' || tag === 'BUTTON' || tag === 'SUMMARY') return;
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    if (e.key === 'ArrowRight') { e.preventDefault(); step(); }
    else if (e.key === 'ArrowLeft') { e.preventDefault(); undo(); }
    else if (e.key === ' ') { e.preventDefault(); toggleAuto(); }
  });

  window.addEventListener('pointerup', () => { if (st.pressing && !press) { st.pressing = false; refresh(); } });

  let resizeTimer = null;
  window.addEventListener('resize', () => {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(() => { layoutBoard(); refresh(); }, 100);
  });

  setInterval(() => {
    if (sess.state === PLAYING && st.tStart !== null) {
      st.elapsed = Math.floor((performance.now() - st.tStart) / 1000);
      drawLed(el.timeLed, 'time', st.elapsed);
    }
  }, 200);

  // ------------------------------------------------------------------ start
  newGame();

  // For the tests in the browser.
  window.cwApp = { sess, st, newGame, newRandomGame, step, undo, onNewButton, cellAt, refresh, layoutBoard };
})();
