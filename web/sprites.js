/* Draws the vector sprites (data from sprites-data.js, made by tools/make_sprites.py) and picks the
 * sprite for each cell (a port of gui/sprites.py). */
(function (root) {
  'use strict';

  const SPRITES = SPRITE_DATA.sprites;

  function typed(prefix, kind, hyper) {
    // Types 3 and 4 are the imaginary units; the Minkowski mode (j) has its own labels for them.
    return (hyper && kind >= 3 ? 'h' : '') + prefix + '_' + kind;
  }

  // The displayed value is the square of the modulus; negative values only occur in Minkowski mode.
  function numberName(value) {
    return value < 0 ? 'num_m' + (-value) : 'num_' + value;
  }

  // `cell` is a Session.cellView(); `boom` is the stepped-on cell or -1. While the game runs
  // cell.mine is always 0, so nothing here can leak mines.
  function cellSprite(cell, index, hyper, over, boom) {
    if (!over) {
      if (cell.open) return cell.blank ? 'blank' : numberName(cell.clue);
      if (cell.flag) return typed('flag', cell.flag, hyper);
      return 'closed';
    }
    if (cell.mine) {
      if (index === boom) return typed('boom', cell.mine, hyper);
      if (cell.flag) return typed('rightflag', cell.flag, hyper);
      return typed('mine', cell.mine, hyper);
    }
    if (cell.open) return cell.blank ? 'blank' : numberName(cell.clue);
    if (cell.flag) return typed('wrong', cell.flag, hyper);
    return 'closed';
  }

  // Draws a sprite into the box (x, y, w, h) of a canvas context, in the context's own units.
  function draw(ctx, name, x, y, w, h) {
    const s = SPRITES[name];
    ctx.save();
    ctx.translate(x, y);
    ctx.scale(w / s.vw, h / s.vh);
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';
    for (const it of s.items) {
      switch (it[0]) {
        case 'rect': {
          ctx.fillStyle = it[6];
          ctx.beginPath();
          ctx.roundRect(it[1], it[2], it[3], it[4], it[5]);
          ctx.fill();
          break;
        }
        case 'circle':
          ctx.fillStyle = it[4];
          ctx.beginPath();
          ctx.arc(it[1], it[2], it[3], 0, 2 * Math.PI);
          ctx.fill();
          break;
        case 'poly': {
          const p = it[1];
          ctx.fillStyle = it[2];
          ctx.beginPath();
          ctx.moveTo(p[0], p[1]);
          for (let i = 2; i < p.length; i += 2) ctx.lineTo(p[i], p[i + 1]);
          ctx.closePath();
          ctx.fill();
          break;
        }
        case 'ring':
          ctx.strokeStyle = it[5];
          ctx.lineWidth = it[4];
          ctx.beginPath();
          ctx.arc(it[1], it[2], it[3], 0, 2 * Math.PI);
          ctx.stroke();
          break;
        case 'path': {
          const p = it[1];
          ctx.strokeStyle = it[3];
          ctx.lineWidth = it[2];
          ctx.beginPath();
          ctx.moveTo(p[0], p[1]);
          for (let i = 2; i < p.length; i += 2) ctx.lineTo(p[i], p[i + 1]);
          if (it[4]) ctx.closePath();
          // A path of two equal points is a dot: draw it as a round cap.
          ctx.stroke();
          break;
        }
      }
    }
    ctx.restore();
  }

  root.Sprites = { cellSprite, numberName, draw, boardColor: SPRITE_DATA.board, size: (n) => [SPRITES[n].w, SPRITES[n].h] };
})(globalThis);
