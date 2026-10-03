/* Local IR profile parsers. Keep imports independent of the remote UI. */
(() => {
  function safeImportName(s) {
    s = String(s || "Command")
      .replace(/[|"\\\r\n]/g, " ")
      .replace(/\s+/g, " ")
      .trim()
      .slice(0, 96);
    return s || "Command";
  }
  function rev8(v) {
    v = ((v & 240) >> 4) | ((v & 15) << 4);
    v = ((v & 204) >> 2) | ((v & 51) << 2);
    v = ((v & 170) >> 1) | ((v & 85) << 1);
    return v & 255;
  }
  function keyFromParts(proto, d, s, f) {
    if (!/^(NEC|Samsung32|Pioneer)/i.test(proto)) return "";
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? d ^ 255 : Number(s);
    f = Number(f);
    if ([d, s, f].some((x) => !Number.isFinite(x) || x < 0 || x > 255))
      return "";
    if (/^Samsung32/i.test(proto)) s = d;
    const val =
      ((rev8(d) << 24) | (rev8(s) << 16) | (rev8(f) << 8) | rev8(~f & 255)) >>>
      0;
    return (
      "G:Toshiba 32 Bit:(0x" +
      val.toString(16).toUpperCase().padStart(8, "0") +
      ")(Repeat)():3"
    );
  }
  function csvCells(line) {
    const out = [];
    let cur = "",
      q = false;
    for (let i = 0; i < line.length; i++) {
      const ch = line[i];
      if (ch === '"') {
        if (q && line[i + 1] === '"') {
          cur += '"';
          i++;
        } else q = !q;
      } else if (ch === "," && !q) {
        out.push(cur);
        cur = "";
      } else cur += ch;
    }
    out.push(cur);
    return out.map((x) => x.trim());
  }
  function csvEntries(t) {
    return t
      .replace(/\r/g, "")
      .split("\n")
      .map((x) => x.trim())
      .filter(Boolean)
      .map((line) => {
        const p = csvCells(line),
          proto = p[1] || "",
          key =
            p[0] === "functionname"
              ? ""
              : keyFromParts(proto, p[2] || "", p[3] || "", p[4] || ""),
          raw =
            p[0] === "functionname"
              ? ""
              : csvProtocolRaw(
                  proto,
                  p[2] || "",
                  p[3] || "",
                  p[4] || "",
                  p[0] || "",
                );
        return {
          name: p[0] || "",
          meta:
            proto +
            " " +
            (p[2] || "") +
            "," +
            (p[3] || "") +
            "," +
            (p[4] || ""),
          protocol: proto,
          keycode: key,
          raw: raw,
        };
      })
      .filter((r) => r.name && r.name !== "functionname");
  }
  function hexBytes(v) {
    return String(v || "")
      .trim()
      .split(/\s+/)
      .filter(Boolean)
      .map((x) => parseInt(x, 16) || 0);
  }
  function hexValue(v) {
    return (
      hexBytes(v)
        .slice(0, 4)
        .reduce((a, b, i) => a | ((b & 255) << (8 * i)), 0) >>> 0
    );
  }
  function harmonyRawFromTimings(freq, vals) {
    freq = Math.max(10000, Math.min(60000, Math.round(Number(freq) || 38000)));
    vals = (vals || [])
      .map((v) =>
        Math.max(1, Math.min(0xfffff, Math.round(Math.abs(Number(v) || 0)))),
      )
      .filter(Boolean);
    if (vals.length === 3) vals.push(100000);
    if (vals.length < 4) return "";
    let raw = "F" + freq.toString(16).toUpperCase();
    vals.forEach((v, i) => {
      raw += (i % 2 ? "S" : "P") + v.toString(16).toUpperCase();
    });
    return raw.length <= 4096 ? raw : "";
  }
  function pulse(seq, level, dur) {
    dur = Math.round(dur);
    if (dur <= 0) return;
    const last = seq[seq.length - 1];
    if (last && last.level === level) last.dur += dur;
    else seq.push({ level: level, dur: dur });
  }
  function seqRaw(freq, seq) {
    if (!seq.length) return "";
    if (seq[0].level === 0) seq.unshift({ level: 1, dur: 1 });
    return harmonyRawFromTimings(
      freq,
      seq.map((x) => x.dur),
    );
  }
  function manchester(seq, bits, half, doubleIndex) {
    bits.forEach((bit, i) => {
      const h = i === doubleIndex ? half * 2 : half;
      if (bit) {
        pulse(seq, 1, h);
        pulse(seq, 0, h);
      } else {
        pulse(seq, 0, h);
        pulse(seq, 1, h);
      }
    });
  }
  function msbBits(v, n) {
    const a = [];
    for (let i = n - 1; i >= 0; i--) a.push((v >> i) & 1);
    return a;
  }
  function lsbBits(v, n) {
    const a = [];
    for (let i = 0; i < n; i++) a.push((v >> i) & 1);
    return a;
  }
  function pop8(v) {
    v &= 255;
    let n = 0;
    while (v) {
      n += v & 1;
      v >>= 1;
    }
    return n;
  }
  function rc5Raw(cur) {
    const addr = hexValue(cur.address) & 31,
      cmd = hexValue(cur.command) & 127,
      tog = hexValue(cur.toggle) & 1,
      seq = [];
    const bits = [1, cmd < 64 ? 1 : 0, tog].concat(
      msbBits(addr, 5),
      msbBits(cmd & 63, 6),
    );
    manchester(seq, bits, 889, -1);
    return seqRaw(36000, seq);
  }
  function rc6Raw(cur) {
    const addr = hexValue(cur.address) & 255,
      cmd = hexValue(cur.command) & 255,
      tog = hexValue(cur.toggle) & 1,
      seq = [];
    pulse(seq, 1, 2666);
    pulse(seq, 0, 889);
    const bits = [1, 0, 0, 0, tog].concat(msbBits(addr, 8), msbBits(cmd, 8));
    manchester(seq, bits, 444, 4);
    return seqRaw(36000, seq);
  }
  function mceRaw(d, s, f) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 15 : Number(s);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(s) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 127 ||
      s < 0 ||
      s > 255 ||
      f < 0 ||
      f > 255
    )
      return "";
    const seq = [],
      bits = [1, 1, 1, 0, 0].concat(
        msbBits(128, 8),
        msbBits(s, 8),
        [0],
        msbBits(d, 7),
        msbBits(f, 8),
      );
    pulse(seq, 1, 2664);
    pulse(seq, 0, 888);
    manchester(seq, bits, 444, 4);
    pulse(seq, 0, 100000);
    return seqRaw(36000, seq);
  }
  function recs80Raw(d, s, f, name = "") {
    d = Number(d);
    f = Number(f);
    const t = /\bT1\b/i.test(String(name || "")) ? 1 : 0;
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 7 ||
      f < 0 ||
      f > 63
    )
      return "";
    const seq = [];
    pulse(seq, 1, 158);
    pulse(seq, 0, 7432);
    [t].concat(msbBits(d, 3), msbBits(f, 6)).forEach((b) => {
      pulse(seq, 1, 158);
      pulse(seq, 0, b ? 7432 : 4902);
    });
    pulse(seq, 1, 158);
    pulse(seq, 0, 45000);
    return seqRaw(38000, seq);
  }
  function akaiRaw(d, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 7 ||
      f < 0 ||
      f > 127
    )
      return "";
    const seq = [];
    lsbBits(d, 3)
      .concat(lsbBits(f, 7), [1])
      .forEach((b) => {
        pulse(seq, 1, 289);
        pulse(seq, 0, Math.round((b ? 6.3 : 2.6) * 289));
      });
    pulse(seq, 0, 25300);
    return seqRaw(38000, seq);
  }
  function denonRaw(d, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 31 ||
      f < 0 ||
      f > 255
    )
      return "";
    const seq = [],
      u = 264;
    function bit(b) {
      pulse(seq, 1, u);
      pulse(seq, 0, b ? u * 7 : u * 3);
    }
    function half(func, suffix) {
      lsbBits(d, 5).concat(lsbBits(func, 8), lsbBits(suffix, 2)).forEach(bit);
      pulse(seq, 1, u);
      pulse(seq, 0, u * 165);
    }
    half(f, 0);
    half(~f & 255, 3);
    return seqRaw(38000, seq);
  }
  function denonKRaw(d, s, f) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 0 : Number(s);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(s) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 15 ||
      s < 0 ||
      s > 15 ||
      f < 0 ||
      f > 4095
    )
      return "";
    const seq = [],
      u = 432,
      c = ((d << 4) ^ s ^ ((f << 4) & 255) ^ ((f >> 4) & 255)) & 255;
    function bit(b) {
      pulse(seq, 1, u);
      pulse(seq, 0, b ? u * 3 : u);
    }
    pulse(seq, 1, u * 8);
    pulse(seq, 0, u * 4);
    lsbBits(84, 8)
      .concat(
        lsbBits(50, 8),
        lsbBits(0, 4),
        lsbBits(d, 4),
        lsbBits(s, 4),
        lsbBits(f, 12),
        lsbBits(c, 8),
      )
      .forEach(bit);
    pulse(seq, 1, u);
    pulse(seq, 0, u * 173);
    return seqRaw(37000, seq);
  }
  function mitsubishiRaw(d, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 255 ||
      f < 0 ||
      f > 255
    )
      return "";
    const seq = [],
      u = 300;
    lsbBits(d, 8)
      .concat(lsbBits(f, 8))
      .forEach((b) => {
        pulse(seq, 1, u);
        pulse(seq, 0, b ? u * 7 : u * 3);
      });
    pulse(seq, 1, u);
    pulse(seq, 0, u * 80);
    return seqRaw(32600, seq);
  }
  function vellemanRaw(d, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 7 ||
      f < 0 ||
      f > 63
    )
      return "";
    const seq = [];
    [1, 0].concat(msbBits(d, 3), msbBits(f, 6), [1]).forEach((b) => {
      pulse(seq, 1, 700);
      pulse(seq, 0, b ? 7590 : 5060);
    });
    pulse(seq, 0, 55000);
    return seqRaw(38000, seq);
  }
  function fujitsuRaw(d, s, f) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 0 : Number(s);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(s) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 255 ||
      s < 0 ||
      s > 255 ||
      f < 0 ||
      f > 255
    )
      return "";
    const seq = [],
      u = 432;
    function bit(b) {
      pulse(seq, 1, u);
      pulse(seq, 0, b ? u * 3 : u);
    }
    pulse(seq, 1, u * 8);
    pulse(seq, 0, u * 4);
    lsbBits(20, 8)
      .concat(
        lsbBits(99, 8),
        lsbBits(0, 4),
        lsbBits(0, 4),
        lsbBits(d, 8),
        lsbBits(s, 8),
        lsbBits(f, 8),
      )
      .forEach(bit);
    pulse(seq, 1, u);
    pulse(seq, 0, u * 110);
    return seqRaw(37000, seq);
  }
  function sharpRaw(d, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 31 ||
      f < 0 ||
      f > 255
    )
      return "";
    const seq = [],
      u = 264;
    function bit(b) {
      pulse(seq, 1, u);
      pulse(seq, 0, b ? u * 7 : u * 3);
    }
    function half(func, suffix) {
      lsbBits(d, 5).concat(lsbBits(func, 8), lsbBits(suffix, 2)).forEach(bit);
      pulse(seq, 1, u);
      pulse(seq, 0, u * 165);
    }
    half(f, 1);
    half(~f & 255, 2);
    return seqRaw(38000, seq);
  }
  function directvRaw(d, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 15 ||
      f < 0 ||
      f > 255
    )
      return "";
    const c =
        (7 * ((f >> 6) & 3) +
          5 * ((f >> 4) & 3) +
          3 * ((f >> 2) & 3) +
          (f & 3)) &
        15,
      seq = [],
      u = 600,
      bits = msbBits(d, 4).concat(msbBits(f, 8), msbBits(c, 4));
    pulse(seq, 1, u * 10);
    pulse(seq, 0, u * 2);
    for (let i = 0; i < bits.length; i += 2) {
      const v = (bits[i] << 1) | bits[i + 1];
      pulse(seq, 1, v & 2 ? u * 2 : u);
      pulse(seq, 0, v & 1 ? u * 2 : u);
    }
    pulse(seq, 1, u);
    pulse(seq, 0, u * 50);
    return seqRaw(38000, seq);
  }
  function gxbRaw(d, s, f) {
    d = Number(d);
    f = Number(f);
    const ss = String(s === undefined ? "" : s).trim();
    const p = ss && ss !== "-1" ? Number(ss) & 1 : 0;
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 15 ||
      f < 0 ||
      f > 255
    )
      return "";
    const seq = [],
      u = 520;
    pulse(seq, 1, u);
    pulse(seq, 0, u);
    msbBits(d, 4)
      .concat(msbBits(f, 8), [p])
      .forEach((b) => {
        pulse(seq, 1, b ? u * 3 : u);
        pulse(seq, 0, b ? u : u * 3);
      });
    pulse(seq, 1, u);
    pulse(seq, 0, 60000);
    return seqRaw(38300, seq);
  }
  function giCableRaw(d, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 15 ||
      f < 0 ||
      f > 255
    )
      return "";
    const seq = [],
      u = 490,
      c = -(d + (f & 15) + ((f >> 4) & 15)) & 15;
    function bit(b) {
      pulse(seq, 1, u);
      pulse(seq, 0, b ? u * 9 : u * 4.5);
    }
    pulse(seq, 1, u * 18);
    pulse(seq, 0, u * 9);
    lsbBits(f, 8).concat(lsbBits(d, 4), lsbBits(c, 4)).forEach(bit);
    pulse(seq, 1, u);
    pulse(seq, 0, u * 84);
    pulse(seq, 1, u * 18);
    pulse(seq, 0, u * 4.5);
    pulse(seq, 1, u);
    pulse(seq, 0, u * 178);
    return seqRaw(38700, seq);
  }
  function protonRaw(d, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 255 ||
      f < 0 ||
      f > 255
    )
      return "";
    const seq = [],
      u = 500;
    function bit(b) {
      pulse(seq, 1, u);
      pulse(seq, 0, b ? u * 3 : u);
    }
    pulse(seq, 1, u * 16);
    pulse(seq, 0, u * 8);
    lsbBits(d, 8).forEach(bit);
    pulse(seq, 1, u);
    pulse(seq, 0, u * 8);
    lsbBits(f, 8).forEach(bit);
    pulse(seq, 1, u);
    pulse(seq, 0, 63000);
    return seqRaw(38000, seq);
  }
  function f12Raw(d, s, f) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 0 : Number(s);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(s) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 7 ||
      s < 0 ||
      s > 1 ||
      f < 0 ||
      f > 255
    )
      return "";
    const seq = [],
      u = 422,
      bits = lsbBits(d, 3).concat([s & 1], lsbBits(f, 8));
    function bit(b) {
      pulse(seq, 1, b ? u * 3 : u);
      pulse(seq, 0, b ? u : u * 3);
    }
    function part() {
      bits.forEach(bit);
      pulse(seq, 0, u * 34);
      bits.forEach(bit);
    }
    part();
    if (s & 1) {
      pulse(seq, 0, u * 88);
      part();
    }
    return seqRaw(37900, seq);
  }
  function nokiaRaw(d, s, f) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 0 : Number(s);
    f = Number(f);
    if (![d, s, f].every((x) => Number.isFinite(x) && x >= 0 && x <= 255))
      return "";
    const seq = [],
      bits = msbBits(d, 8).concat(msbBits(s, 8), msbBits(f, 8));
    pulse(seq, 1, 412);
    pulse(seq, 0, 276);
    for (let i = 0; i < bits.length; i += 2) {
      const v = (bits[i] << 1) | bits[i + 1];
      pulse(seq, 1, 164);
      pulse(seq, 0, [276, 445, 614, 783][v]);
    }
    pulse(seq, 1, 164);
    pulse(seq, 0, 100000);
    return seqRaw(36000, seq);
  }
  function nokia12Raw(d, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 15 ||
      f < 0 ||
      f > 255
    )
      return "";
    const seq = [],
      bits = msbBits(d, 4).concat(msbBits(f, 8));
    pulse(seq, 1, 412);
    pulse(seq, 0, 276);
    for (let i = 0; i < bits.length; i += 2) {
      const v = (bits[i] << 1) | bits[i + 1];
      pulse(seq, 1, 164);
      pulse(seq, 0, [276, 445, 614, 783][v]);
    }
    pulse(seq, 1, 164);
    pulse(seq, 0, 60000);
    return seqRaw(36000, seq);
  }
  function nrc17Raw(d, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 255 ||
      f < 0 ||
      f > 255
    )
      return "";
    const seq = [],
      u = 500;
    function frame(cmd, addr, gap) {
      pulse(seq, 1, u);
      pulse(seq, 0, u * 5);
      manchester(seq, [1].concat(lsbBits(cmd, 8), lsbBits(addr, 8)), u, -1);
      pulse(seq, 0, gap);
    }
    frame(254, 255, u * 28);
    frame(f, d, u * 220);
    frame(254, 255, u * 200);
    return seqRaw(38000, seq);
  }
  function streamZapRaw(d, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 63 ||
      f < 0 ||
      f > 127
    )
      return "";
    const seq = [],
      bits = [1, (~f >> 6) & 1, 0].concat(msbBits(d, 6), msbBits(f & 63, 6));
    manchester(seq, bits, 889, -1);
    pulse(seq, 0, 114000);
    return seqRaw(36000, seq);
  }
  function logitechRaw(d, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 15 ||
      f < 0 ||
      f > 255
    )
      return "";
    const seq = [],
      u = 127;
    function bit(b) {
      pulse(seq, 1, u * 3);
      pulse(seq, 0, b ? u * 8 : u * 4);
    }
    pulse(seq, 1, u * 31);
    pulse(seq, 0, u * 36);
    lsbBits(d, 4)
      .concat(lsbBits(~d & 15, 4), lsbBits(f, 8), lsbBits(~f & 255, 8))
      .forEach(bit);
    pulse(seq, 1, u * 3);
    pulse(seq, 0, 50000);
    return seqRaw(38000, seq);
  }
  function sircRaw(cur, proto) {
    const cmd = hexValue(cur.command) & 127,
      addr = hexValue(cur.address),
      bits = /20/.test(proto) ? 20 : /15/.test(proto) ? 15 : 12,
      addrBits = bits - 7,
      seq = [];
    pulse(seq, 1, 2400);
    pulse(seq, 0, 600);
    lsbBits(cmd, 7)
      .concat(lsbBits(addr, addrBits))
      .forEach((b) => {
        pulse(seq, 1, b ? 1200 : 600);
        pulse(seq, 0, 600);
      });
    return seqRaw(40000, seq);
  }
  function jvcRaw(d, f) {
    d = Number(d);
    f = Number(f);
    if (![d, f].every((x) => Number.isFinite(x) && x >= 0 && x <= 255))
      return "";
    const seq = [];
    pulse(seq, 1, 8400);
    pulse(seq, 0, 4200);
    lsbBits(d, 8)
      .concat(lsbBits(f, 8))
      .forEach((b) => {
        pulse(seq, 1, 525);
        pulse(seq, 0, b ? 1575 : 525);
      });
    pulse(seq, 1, 525);
    pulse(seq, 0, 23625);
    return seqRaw(38000, seq);
  }
  function jvc48Raw(d, s, f) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 0 : Number(s);
    f = Number(f);
    if (![d, s, f].every((x) => Number.isFinite(x) && x >= 0 && x <= 255))
      return "";
    const seq = [],
      u = 432;
    function bit(b) {
      pulse(seq, 1, u);
      pulse(seq, 0, b ? u * 3 : u);
    }
    pulse(seq, 1, u * 8);
    pulse(seq, 0, u * 4);
    lsbBits(3, 8)
      .concat(
        lsbBits(1, 8),
        lsbBits(d, 8),
        lsbBits(s, 8),
        lsbBits(f, 8),
        lsbBits((d ^ s ^ f) & 255, 8),
      )
      .forEach(bit);
    pulse(seq, 1, u);
    pulse(seq, 0, u * 173);
    return seqRaw(37000, seq);
  }
  function konkaRaw(d, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 255 ||
      f < 0 ||
      f > 255
    )
      return "";
    const seq = [],
      u = 500;
    function bit(b) {
      pulse(seq, 1, u);
      pulse(seq, 0, b ? u * 5 : u * 3);
    }
    pulse(seq, 1, u * 6);
    pulse(seq, 0, u * 6);
    msbBits(d, 8).concat(msbBits(f, 8)).forEach(bit);
    pulse(seq, 1, u);
    pulse(seq, 0, u * 8);
    pulse(seq, 1, u);
    pulse(seq, 0, u * 46);
    return seqRaw(38000, seq);
  }
  function tivoRaw(proto, d, s, f) {
    f = Number(f);
    const m = String(proto || "").match(/unit\s*=\s*(\d+)/i),
      u = m ? Number(m[1]) : 0;
    if (
      !Number.isFinite(f) ||
      !Number.isFinite(u) ||
      f < 0 ||
      f > 255 ||
      u < 0 ||
      u > 15
    )
      return "";
    const seq = [],
      unit = 564;
    function bit(b) {
      pulse(seq, 1, unit);
      pulse(seq, 0, b ? unit * 3 : unit);
    }
    pulse(seq, 1, unit * 16);
    pulse(seq, 0, unit * 8);
    lsbBits(133, 8)
      .concat(
        lsbBits(48, 8),
        lsbBits(f, 8),
        lsbBits(u, 4),
        lsbBits((~f >> 4) & 15, 4),
      )
      .forEach(bit);
    pulse(seq, 1, unit);
    pulse(seq, 0, unit * 78);
    pulse(seq, 1, unit * 16);
    pulse(seq, 0, unit * 4);
    pulse(seq, 1, unit);
    pulse(seq, 0, unit * 173);
    return seqRaw(38400, seq);
  }
  function xmpRaw(proto, d, s, f) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 0 : Number(s);
    f = Number(f);
    if (
      ![d, s, f].every((x) => Number.isFinite(x)) ||
      d < 0 ||
      d > 255 ||
      s < 0 ||
      s > 255 ||
      f < 0 ||
      f > 65535
    )
      return "";
    const p = String(proto || ""),
      oem = 68,
      full = /XMP-1/i.test(p)
        ? (f & 255) << 8
        : /XMP-2/i.test(p)
          ? f & 255
          : f & 65535,
      seq = [],
      u = 136,
      sh = (s >> 4) & 15,
      sl = s & 15,
      oh = (oem >> 4) & 15,
      ol = oem & 15,
      dh = (d >> 4) & 15,
      dl = d & 15,
      fn = [(full >> 12) & 15, (full >> 8) & 15, (full >> 4) & 15, full & 15],
      c1 = -(sh + sl + 15 + oh + ol + dh + dl) & 15;
    function nib(n) {
      pulse(seq, 1, 210);
      pulse(seq, 0, 760 + (n & 15) * u);
    }
    function frame(t) {
      const c2 = -(sh + t + sl + fn[0] + fn[1] + fn[2] + fn[3]) & 15;
      [sh, c1, sl, 15, oh, ol, dh, dl].forEach(nib);
      pulse(seq, 1, 210);
      pulse(seq, 0, 13800);
      [sh, c2, t, sl].concat(fn).forEach(nib);
      pulse(seq, 1, 210);
      pulse(seq, 0, 80400);
    }
    frame(0);
    frame(8);
    return seqRaw(38000, seq);
  }
  function sharpDvdRaw(d, s, f) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 0 : Number(s);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(s) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 15 ||
      s < 0 ||
      s > 255 ||
      f < 0 ||
      f > 255
    )
      return "";
    const e = 1,
      c =
        (d ^ (s & 15) ^ ((s >> 4) & 15) ^ (f & 15) ^ ((f >> 4) & 15) ^ e) & 15,
      seq = [],
      u = 400;
    function bit(b) {
      pulse(seq, 1, u);
      pulse(seq, 0, b ? u * 3 : u);
    }
    pulse(seq, 1, u * 8);
    pulse(seq, 0, u * 4);
    lsbBits(170, 8)
      .concat(
        lsbBits(90, 8),
        lsbBits(15, 4),
        lsbBits(d, 4),
        lsbBits(s, 8),
        lsbBits(f, 8),
        lsbBits(e, 4),
        lsbBits(c, 4),
      )
      .forEach(bit);
    pulse(seq, 1, u);
    pulse(seq, 0, u * 48);
    return seqRaw(38000, seq);
  }
  function rcaRaw(proto, d, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 15 ||
      f < 0 ||
      f > 255
    )
      return "";
    const old = /old/i.test(proto || ""),
      freq = /38/.test(proto || "") ? 38700 : 58000,
      seq = [],
      u = 460;
    if (old) pulse(seq, 1, u * 32);
    pulse(seq, 1, u * 8);
    pulse(seq, 0, u * 8);
    msbBits(d, 4)
      .concat(msbBits(f, 8), msbBits(~d & 15, 4), msbBits(~f & 255, 8))
      .forEach((b) => {
        pulse(seq, 1, u);
        pulse(seq, 0, b ? u * 4 : u * 2);
      });
    pulse(seq, 1, u * (old ? 2 : 1));
    pulse(seq, 0, u * 16);
    return seqRaw(freq, seq);
  }
  function panasonicRaw(d, s, f) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 0 : Number(s);
    f = Number(f);
    if (![d, s, f].every((x) => Number.isFinite(x) && x >= 0 && x <= 255))
      return "";
    const seq = [],
      bytes = [2, 32, d & 255, s & 255, f & 255, (d ^ s ^ f) & 255];
    pulse(seq, 1, 3456);
    pulse(seq, 0, 1728);
    bytes
      .flatMap((b) => lsbBits(b, 8))
      .forEach((b) => {
        pulse(seq, 1, 432);
        pulse(seq, 0, b ? 1296 : 432);
      });
    pulse(seq, 1, 432);
    pulse(seq, 0, 74400);
    return seqRaw(37000, seq);
  }
  function panasonic2Raw(d, s, f, x = 0) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 0 : Number(s);
    f = Number(f);
    x = Number(x);
    if (![d, s, f, x].every((v) => Number.isFinite(v) && v >= 0 && v <= 255))
      return "";
    const seq = [],
      bytes = [
        2,
        32,
        d & 255,
        s & 255,
        x & 255,
        f & 255,
        (d ^ s ^ x ^ f) & 255,
      ];
    pulse(seq, 1, 3456);
    pulse(seq, 0, 1728);
    bytes
      .flatMap((b) => lsbBits(b, 8))
      .forEach((b) => {
        pulse(seq, 1, 432);
        pulse(seq, 0, b ? 1296 : 432);
      });
    pulse(seq, 1, 432);
    pulse(seq, 0, 74400);
    return seqRaw(37000, seq);
  }
  function aiwaRaw(d, s, f) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 0 : Number(s);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(s) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 255 ||
      s < 0 ||
      s > 31 ||
      f < 0 ||
      f > 255
    )
      return "";
    const seq = [];
    pulse(seq, 1, 8800);
    pulse(seq, 0, 4400);
    lsbBits(d, 8)
      .concat(
        lsbBits(s, 5),
        lsbBits(~d & 255, 8),
        lsbBits(~s & 31, 5),
        lsbBits(f, 8),
        lsbBits(~f & 255, 8),
      )
      .forEach((b) => {
        pulse(seq, 1, 550);
        pulse(seq, 0, b ? 1650 : 550);
      });
    pulse(seq, 1, 550);
    pulse(seq, 0, 23100);
    pulse(seq, 1, 8800);
    pulse(seq, 0, 4400);
    pulse(seq, 1, 550);
    pulse(seq, 0, 90750);
    return seqRaw(38000, seq);
  }
  function panasonicOldRaw(d, s, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 31 ||
      f < 0 ||
      f > 63
    )
      return "";
    const seq = [];
    pulse(seq, 1, 3332);
    pulse(seq, 0, 3332);
    lsbBits(d, 5)
      .concat(lsbBits(f, 6), lsbBits(~d & 31, 5), lsbBits(~f & 63, 6))
      .forEach((b) => {
        pulse(seq, 1, 833);
        pulse(seq, 0, b ? 2499 : 833);
      });
    pulse(seq, 1, 833);
    pulse(seq, 0, 100000);
    return seqRaw(57600, seq);
  }
  function nec48Raw(d, s, f, e = 0) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? d ^ 255 : Number(s);
    f = Number(f);
    e = Number(e);
    if (![d, s, f, e].every((x) => Number.isFinite(x) && x >= 0 && x <= 255))
      return "";
    const seq = [];
    pulse(seq, 1, 9024);
    pulse(seq, 0, 4512);
    lsbBits(d, 8)
      .concat(
        lsbBits(s, 8),
        lsbBits(f, 8),
        lsbBits(~f & 255, 8),
        lsbBits(e, 8),
        lsbBits(~e & 255, 8),
      )
      .forEach((b) => {
        pulse(seq, 1, 564);
        pulse(seq, 0, b ? 1692 : 564);
      });
    pulse(seq, 1, 564);
    pulse(seq, 0, 108000);
    pulse(seq, 1, 9024);
    pulse(seq, 0, 2256);
    pulse(seq, 1, 564);
    pulse(seq, 0, 108000);
    return seqRaw(38000, seq);
  }
  function blaupunktRaw(d, s, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 7 ||
      f < 0 ||
      f > 63
    )
      return "";
    const seq = [];
    pulse(seq, 1, 528);
    pulse(seq, 0, 2640);
    manchester(seq, Array(10).fill(1), 528, -1);
    pulse(seq, 0, 20592);
    pulse(seq, 1, 528);
    pulse(seq, 0, 2640);
    manchester(seq, [1].concat(lsbBits(f, 6), lsbBits(d, 3)), 528, -1);
    pulse(seq, 0, 121440);
    return seqRaw(30300, seq);
  }
  function dishRaw(d, s, f) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 0 : Number(s);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(s) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 31 ||
      s < 0 ||
      s > 31 ||
      f < 0 ||
      f > 63
    )
      return "";
    const bits = msbBits(f, 6).concat(msbBits(s, 5), msbBits(d, 5)),
      seq = [];
    pulse(seq, 1, 400);
    pulse(seq, 0, 6100);
    for (let r = 0; r < 4; r++) {
      bits.forEach((b) => {
        pulse(seq, 1, 400);
        pulse(seq, 0, b ? 1700 : 2800);
      });
      pulse(seq, 1, 400);
      pulse(seq, 0, 6100);
    }
    return seqRaw(57600, seq);
  }
  function barcoRaw(d, s, f) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 0 : Number(s);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(s) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 127 ||
      s < 0 ||
      s > 63 ||
      f < 0 ||
      f > 127
    )
      return "";
    const seq = [],
      u = 250;
    function bit(b) {
      if (b) {
        pulse(seq, 1, u);
        pulse(seq, 0, u);
      } else {
        pulse(seq, 0, u);
        pulse(seq, 1, u);
      }
    }
    pulse(seq, 1, u);
    pulse(seq, 0, u);
    msbBits(d, 7).concat(msbBits(s, 6), [0, 0], msbBits(f, 7)).forEach(bit);
    pulse(seq, 0, 89000);
    return seqRaw(55500, seq);
  }
  function thomsonRaw(proto, d, f) {
    d = Number(d);
    f = Number(f);
    const seven = /7$/i.test(proto);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > (seven ? 15 : 31) ||
      f < 0 ||
      f > (seven ? 127 : 63)
    )
      return "";
    const seq = [],
      u = 500;
    function bit(b) {
      pulse(seq, 1, u);
      pulse(seq, 0, b ? u * 9 : u * 4);
    }
    const bits = seven
      ? lsbBits(d, 4).concat([0], lsbBits(f, 7))
      : lsbBits(d, 4).concat([0], lsbBits(d >> 4, 1), lsbBits(f, 6));
    bits.concat([1]).forEach(bit);
    pulse(seq, 0, 80000);
    return seqRaw(33000, seq);
  }
  function emersonLikeRaw(proto, d, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 63 ||
      f < 0 ||
      f > 63
    )
      return "";
    const sc = /^ScAtl-6$/i.test(proto),
      u = sc ? 846 : 872,
      seq = [];
    function bit(b) {
      pulse(seq, 1, u);
      pulse(seq, 0, b ? u * 3 : u);
    }
    pulse(seq, 1, u * 4);
    pulse(seq, 0, u * 4);
    lsbBits(d, 6)
      .concat(lsbBits(f, 6), lsbBits(~d & 63, 6), lsbBits(~f & 63, 6))
      .forEach(bit);
    pulse(seq, 1, u);
    pulse(seq, 0, u * (sc ? 40 : 39));
    return seqRaw(sc ? 57600 : 36700, seq);
  }
  function appleRaw(d, s, f) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 135 : Number(s);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(s) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 255 ||
      s < 0 ||
      s > 255 ||
      f < 0 ||
      f > 127
    )
      return "";
    const i = 0,
      c = (pop8(f & 127) + pop8(i)) % 2 === 0 ? 1 : 0,
      seq = [],
      u = 564;
    function bit(b) {
      pulse(seq, 1, u);
      pulse(seq, 0, b ? u * 3 : u);
    }
    pulse(seq, 1, u * 16);
    pulse(seq, 0, u * 8);
    lsbBits(d, 8)
      .concat(lsbBits(s, 8), [c], lsbBits(f, 7), lsbBits(i, 8))
      .forEach(bit);
    pulse(seq, 1, u);
    pulse(seq, 0, u * 78);
    pulse(seq, 1, u * 16);
    pulse(seq, 0, u * 4);
    pulse(seq, 1, u);
    pulse(seq, 0, u * 173);
    return seqRaw(38400, seq);
  }
  function nokia32Raw(d, s, f) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 0 : Number(s);
    f = Number(f);
    if (![d, s, f].every((x) => Number.isFinite(x) && x >= 0 && x <= 255))
      return "";
    const seq = [],
      bits = msbBits(d, 8).concat(msbBits(s, 8), msbBits(0, 8), msbBits(f, 8));
    pulse(seq, 1, 412);
    pulse(seq, 0, 276);
    for (let i = 0; i < bits.length; i += 2) {
      const v = (bits[i] << 1) | bits[i + 1];
      pulse(seq, 1, 164);
      pulse(seq, 0, [276, 445, 614, 783][v]);
    }
    pulse(seq, 1, 164);
    pulse(seq, 0, 100000);
    return seqRaw(36000, seq);
  }
  function paceMssRaw(d, f) {
    d = Number(d);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 1 ||
      f < 0 ||
      f > 255
    )
      return "";
    const seq = [],
      u = 630;
    function bit(b) {
      pulse(seq, 1, u);
      pulse(seq, 0, b ? u * 11 : u * 7);
    }
    pulse(seq, 1, u);
    pulse(seq, 0, u * 5);
    pulse(seq, 1, u);
    pulse(seq, 0, u * 5);
    [0, d & 1].concat(msbBits(f, 8)).forEach(bit);
    pulse(seq, 1, u);
    pulse(seq, 0, 120000);
    return seqRaw(38000, seq);
  }
  function sejinRaw(proto, d, s, f) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 0 : Number(s);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(s) ||
      !Number.isFinite(f) ||
      d < 0 ||
      d > 255 ||
      s < 0 ||
      s > 255 ||
      f < 0 ||
      f > 255
    )
      return "";
    const freq = /-38$/i.test(proto) ? 38800 : 56300,
      u = 310,
      dx = d || 1,
      fx = s,
      fy = f,
      e = 0,
      c =
        ((dx & 15) +
          ((dx >> 4) & 15) +
          (fx & 15) +
          ((fx >> 4) & 15) +
          (fy & 15) +
          ((fy >> 4) & 15) +
          e) &
        15,
      seq = [],
      bits = msbBits(3, 2).concat(
        msbBits(dx, 8),
        msbBits(fx, 8),
        msbBits(fy, 8),
        msbBits(e, 4),
        msbBits(c, 4),
      );
    function slot(b) {
      pulse(seq, b ? 1 : 0, u);
    }
    function pair(v) {
      msbBits([8, 4, 2, 1][v & 3], 4).forEach(slot);
    }
    pulse(seq, 1, u * 3);
    for (let i = 0; i < bits.length; i += 2) pair((bits[i] << 1) | bits[i + 1]);
    pulse(seq, 0, 76000);
    return seqRaw(freq, seq);
  }
  function zenithRaw(d, s, f) {
    d = Number(d);
    const ss = String(s === undefined ? "" : s).trim();
    s = ss === "" || ss === "-1" ? 0 : Number(s);
    f = Number(f);
    if (
      !Number.isFinite(d) ||
      !Number.isFinite(s) ||
      !Number.isFinite(f) ||
      d < 1 ||
      d > 12 ||
      s < 0 ||
      s > 1 ||
      f < 0 ||
      f >= Math.pow(2, d)
    )
      return "";
    const seq = [],
      u = 520;
    function zbit(b) {
      if (b) {
        pulse(seq, 1, u);
        pulse(seq, 0, u);
        pulse(seq, 1, u);
        pulse(seq, 0, u * 8);
      } else {
        pulse(seq, 1, u);
        pulse(seq, 0, u * 10);
      }
    }
    zbit(s & 1);
    for (let i = d - 1; i >= 0; i--) {
      const b = Math.floor(f / Math.pow(2, i)) & 1;
      if (b) {
        zbit(1);
        zbit(0);
      } else {
        zbit(0);
        zbit(1);
      }
    }
    pulse(seq, 0, 90000);
    return seqRaw(40000, seq);
  }
  function csvProtocolRaw(proto, d, s, f, name = "") {
    proto = String(proto || "");
    const D = Number(d),
      F = Number(f),
      S = String(s === undefined ? "" : s).trim();
    if (!Number.isFinite(D) || !Number.isFinite(F) || D < 0 || F < 0) return "";
    if (/^RC5X?/i.test(proto))
      return rc5Raw({
        address: D.toString(16),
        command: F.toString(16),
        toggle: "0",
      });
    if (/^RC6/i.test(proto))
      return rc6Raw({
        address: D.toString(16),
        command: F.toString(16),
        toggle: "0",
      });
    if (/^MCE$/i.test(proto)) return mceRaw(D, S, F);
    if (/^RECS80$/i.test(proto)) return recs80Raw(D, S, F, name);
    if (/^Akai$/i.test(proto)) return akaiRaw(D, F);
    if (/^Denon-K$/i.test(proto)) return denonKRaw(D, S, F);
    if (/^Denon(?:\{[12]\})?$/i.test(proto)) return denonRaw(D, F);
    if (/^Mitsubishi$/i.test(proto)) return mitsubishiRaw(D, F);
    if (/^Velleman$/i.test(proto)) return vellemanRaw(D, F);
    if (/^Fujitsu$/i.test(proto)) return fujitsuRaw(D, S, F);
    if (/^SharpDVD$/i.test(proto)) return sharpDvdRaw(D, S, F);
    if (/^Sharp(?:\{[12]\})?$/i.test(proto)) return sharpRaw(D, F);
    if (/^DirecTV$/i.test(proto)) return directvRaw(D, F);
    if (/^GXB$/i.test(proto)) return gxbRaw(D, S, F);
    if (/^G\.I\.Cable(?:\{[12]\})?$/i.test(proto)) return giCableRaw(D, F);
    if (/^Proton$/i.test(proto)) return protonRaw(D, F);
    if (/^F12$/i.test(proto)) return f12Raw(D, S, F);
    if (/^NRC17$/i.test(proto)) return nrc17Raw(D, F);
    if (/^Nokia32$/i.test(proto)) return nokia32Raw(D, S, F);
    if (/^Nokia$/i.test(proto)) return nokiaRaw(D, S, F);
    if (/^Nokia12$/i.test(proto)) return nokia12Raw(D, F);
    if (/^StreamZap$/i.test(proto)) return streamZapRaw(D, F);
    if (/^Logitech$/i.test(proto)) return logitechRaw(D, F);
    if (/^JVC-48$/i.test(proto)) return jvc48Raw(D, S, F);
    if (/^JVC(?:\{[12]\})?$/i.test(proto)) return jvcRaw(D, F);
    if (/^Konka$/i.test(proto)) return konkaRaw(D, F);
    if (/^Tivo\s+unit\s*=\s*\d+$/i.test(proto)) return tivoRaw(proto, D, S, F);
    if (/^XMP(?:-[12])?$/i.test(proto)) return xmpRaw(proto, D, S, F);
    if (/^Apple$/i.test(proto)) return appleRaw(D, S, F);
    if (/^(?:Emerson|ScAtl-6)$/i.test(proto))
      return emersonLikeRaw(proto, D, F);
    if (/^PaceMSS$/i.test(proto)) return paceMssRaw(D, F);
    if (/^Sejin-1-(?:38|56)$/i.test(proto)) return sejinRaw(proto, D, S, F);
    if (/^Zenith$/i.test(proto)) return zenithRaw(D, S, F);
    if (/^Barco$/i.test(proto)) return barcoRaw(D, S, F);
    if (/^Thomson7?$/i.test(proto)) return thomsonRaw(proto, D, F);
    if (/^Panasonic2$/i.test(proto)) return panasonic2Raw(D, S, F);
    if (/^Panasonic$/i.test(proto)) return panasonicRaw(D, S, F);
    if (/^Aiwa$/i.test(proto)) return aiwaRaw(D, S, F);
    if (/^Panasonic_Old$/i.test(proto)) return panasonicOldRaw(D, S, F);
    if (/^Dish_Network$/i.test(proto)) return dishRaw(D, S, F);
    if (/^48-NEC1$/i.test(proto)) return nec48Raw(D, S, F, 0);
    if (/^Blaupunkt$/i.test(proto)) return blaupunktRaw(D, S, F);
    if (/^RCA(?:-38)?(?:\(Old\))?$/i.test(proto)) return rcaRaw(proto, D, F);
    if (/^Sony(12|15|20)?/i.test(proto)) {
      const bits = (proto.match(/Sony(\d+)/i) || [])[1] || "12";
      let addr = D;
      if (bits === "20" && S && S !== "-1") {
        const sub = Number(S);
        if (Number.isFinite(sub) && sub >= 0)
          addr = (D & 31) | ((sub & 255) << 5);
      }
      return sircRaw(
        { address: addr.toString(16), command: F.toString(16) },
        "SIRC" + bits,
      );
    }
    return "";
  }
  function prontoToHarmonyRaw(hex) {
    const words = String(hex || "")
      .trim()
      .split(/\s+/)
      .filter(Boolean)
      .map((x) => parseInt(x, 16));
    if (words.length < 8 || words.some((x) => !Number.isFinite(x))) return "";
    if (words[0] !== 0) return "";
    const unit = (words[1] || 1) * 0.241246,
      freq = Math.round(1000000 / unit),
      pairs = (words[2] || 0) + (words[3] || 0),
      vals = words.slice(4, 4 + pairs * 2).map((w) => Math.round(w * unit));
    return harmonyRawFromTimings(freq, vals);
  }
  function kaseikyoRaw(cur) {
    const a = hexBytes(cur.address),
      c = hexBytes(cur.command);
    if (a.length < 4 || c.length < 1) return "";
    const bytes = [a[1], a[2], a[0], a[3], c[0], (a[0] ^ a[3] ^ c[0]) & 255],
      seq = [];
    pulse(seq, 1, 3456);
    pulse(seq, 0, 1728);
    bytes
      .flatMap((b) => lsbBits(b, 8))
      .forEach((b) => {
        pulse(seq, 1, 432);
        pulse(seq, 0, b ? 1296 : 432);
      });
    pulse(seq, 1, 432);
    pulse(seq, 0, 74736);
    return seqRaw(38000, seq);
  }
  function flipperEntries(t, path = "") {
    const out = [],
      src = String(path || "");
    let cur = {};
    function push() {
      if (!cur.name) {
        cur = {};
        return;
      }
      let key = "",
        raw = "",
        meta = cur.protocol || cur.type || "raw";
      if (String(cur.type || "").toLowerCase() === "parsed") {
        const a = hexBytes(cur.address),
          c = hexBytes(cur.command),
          p = cur.protocol || "";
        if (/^Samsung32/i.test(p)) key = keyFromParts(p, a[0], a[0], c[0]);
        else if (/^NECext/i.test(p)) {
          key = keyFromParts(p, a[0], a[1], c[0]);
          if (!key && /_Converted_\/CSV\/D\/Denon\//i.test(src)) {
            raw = denonKRaw(a[0], a[1], c[0]);
            meta = raw ? "Denon-K converted timing" : "NECext unsupported";
          }
        } else if (/^NEC/i.test(p))
          key = keyFromParts(p, a[0], a[0] ^ 255, c[0]);
        else if (/^Pioneer/i.test(p))
          key = keyFromParts(p, a[0], a[0] ^ 255, c[0]);
        else if (/^RCA/i.test(p)) {
          raw = rcaRaw(p, a[0], c[0]);
          meta = raw ? p + " converted timing" : p + " unsupported";
        } else if (/^RC5/i.test(p)) {
          raw = rc5Raw(cur);
          meta = raw ? "RC5 converted timing" : "RC5 unsupported";
        } else if (/^RC6/i.test(p)) {
          raw = rc6Raw(cur);
          meta = raw ? "RC6 converted timing" : "RC6 unsupported";
        } else if (/^SIRC/i.test(p)) {
          raw = sircRaw(cur, p);
          meta = raw ? p + " converted timing" : p + " unsupported";
        } else if (/^Kaseikyo/i.test(p)) {
          raw = kaseikyoRaw(cur);
          meta = raw ? "Kaseikyo converted timing" : "Kaseikyo unsupported";
        }
      } else if (String(cur.type || "").toLowerCase() === "raw") {
        const vals = String(cur.data || "")
          .trim()
          .split(/\s+/)
          .filter(Boolean)
          .map(Number);
        raw = harmonyRawFromTimings(cur.frequency || 38000, vals);
        meta =
          "raw timings " +
          (cur.frequency || 38000) +
          " Hz (" +
          vals.length +
          " durations)";
      }
      out.push({ name: cur.name, meta: meta, keycode: key, raw: raw });
      cur = {};
    }
    t.replace(/\r/g, "")
      .split("\n")
      .forEach((line) => {
        line = line.trim();
        if (!line) return;
        if (line[0] === "#") {
          push();
          return;
        }
        const i = line.indexOf(":");
        if (i < 0) return;
        const k = line.slice(0, i).trim().toLowerCase(),
          v = line.slice(i + 1).trim();
        if (k === "name" && cur.name) push();
        cur[k] = cur[k] && k === "data" ? cur[k] + " " + v : v;
      });
    push();
    return out;
  }
  function base64Bytes(s) {
    try {
      const bin = atob(String(s || "").replace(/\s+/g, ""));
      return Array.from(bin, (c) => c.charCodeAt(0));
    } catch (e) {
      return [];
    }
  }
  function broadlinkKind(s) {
    const b = base64Bytes(s);
    if (b.length < 8) return "";
    if (b[0] === 0x26) return "ir";
    if (b[0] === 0xb2 || b[0] === 0xd7) return "rf";
    return "";
  }
  function broadlinkRaw(s) {
    const b = base64Bytes(s);
    if (b.length < 8 || b[0] !== 0x26) return "";
    let len = b[2] | (b[3] << 8),
      end = Math.min(b.length, 4 + len),
      vals = [];
    for (let i = 4; i < end; ) {
      let v = b[i++];
      if (v === 0) {
        if (i + 1 >= end) break;
        v = (b[i] << 8) | b[i + 1];
        i += 2;
      }
      vals.push(Math.round((v * 269000) / 8192));
    }
    return harmonyRawFromTimings(38000, vals);
  }
  function miioRaw(s) {
    const b = base64Bytes(s);
    if (b.length < 8) return "";
    let vals = [];
    if (b[0] === 0x67 && b[1] === 0xa5 && b.length >= 72) {
      const edge = b[2] | (b[3] << 8),
        pairs = Math.floor((edge + 1) / 2),
        times = [];
      for (let i = 0; i < 16; i++) {
        const o = 4 + i * 4;
        times.push(
          (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0,
        );
      }
      for (let i = 68; i < Math.min(b.length, 68 + pairs); i++) {
        const p = times[b[i] & 15] || 0,
          g = times[(b[i] >> 4) & 15] || 0;
        vals.push(p, g);
      }
    } else if (b[0] === 0xa5 && b[1] === 0x67) {
      const pairs = Math.floor(((b[2] << 8) + b[3] + 1) / 2),
        dataStart = Math.max(4, b.length - pairs),
        times = [];
      for (let i = 4; i + 1 < dataStart; i += 2)
        times.push((b[i] << 8) + b[i + 1]);
      for (let i = dataStart; i < b.length; i++)
        vals.push(times[b[i] & 15] || 0, times[(b[i] >> 4) & 15] || 0);
    }
    vals = vals.filter(Boolean);
    return harmonyRawFromTimings(38000, vals);
  }
  function sendirRaw(s) {
    const m = String(s || "").match(
      /sendir\s*,\s*[^,]+\s*,\s*\d+\s*,\s*(\d+)\s*,\s*\d+\s*,\s*\d+\s*,\s*([0-9,\s]+)/i,
    );
    if (!m) return "";
    const freq = Math.max(10000, Math.min(60000, parseInt(m[1], 10) || 38000)),
      vals = m[2]
        .split(",")
        .map((x) => Math.round(((parseInt(x, 10) || 0) * 1000000) / freq))
        .filter(Boolean);
    return harmonyRawFromTimings(freq, vals);
  }
  function rawTimingRows(t, label) {
    const rows = [];
    String(t || "")
      .replace(/\r/g, "")
      .split("\n")
      .forEach((line, i) => {
        let m = line.match(
            /^\s*([^:=]+?)\s*[:=]\s*(\[?\s*(?:[-+]?\d+[\s,]+){3,}[-+]?\d+\s*\]?)\s*$/,
          ),
          body = m ? m[2] : String(line || "").trim();
        if (!m) {
          const bm = body.match(
            /^\[?\s*((?:[-+]?\d+[\s,]+){3,}[-+]?\d+)\s*\]?\s*$/,
          );
          if (!bm) return;
          body = bm[1];
        }
        body = String(body)
          .replace(/^\s*\[/, "")
          .replace(/\]\s*$/, "");
        const vals = body
            .split(/[\s,]+/)
            .map(Number)
            .filter(Number.isFinite),
          raw = harmonyRawFromTimings(38000, vals);
        if (raw)
          rows.push({
            name: m
              ? safeImportName(m[1])
              : (label || "Raw command") + " " + (i + 1),
            meta: "raw timings 38000 Hz (" + vals.length + " durations)",
            raw: raw,
          });
      });
    return rows;
  }
  function prontoEntries(t) {
    const rows = [],
      lines = String(t || "")
        .replace(/\r/g, "")
        .split("\n"),
      re = /((?:0000|0100|5000|6000|7000)(?:\s+[0-9a-fA-F]{4}){6,})/g;
    lines.forEach((line, i) => {
      let m;
      while ((m = re.exec(line))) {
        const hex = m[1].replace(/\s+/g, " ").trim(),
          raw = prontoToHarmonyRaw(hex),
          name = safeImportName(
            line
              .slice(0, m.index)
              .trim()
              .replace(/[:=,]+$/, "")
              .trim() || "Pronto " + (i + 1),
          );
        rows.push({
          name: name,
          meta: raw ? "Pronto Hex converted timing" : "Pronto Hex unsupported",
          raw: raw,
        });
      }
    });
    return rows;
  }
  function codeStringRow(name, s, meta) {
    s = String(s || "").trim();
    if (!s) return null;
    if (/^G:[^\r\n]+?:\d+$/i.test(s))
      return { name: name, meta: meta || "Harmony compact code", keycode: s };
    if (/^F[0-9A-F]+(?:[PS][0-9A-F]+)+$/i.test(s))
      return { name: name, meta: meta || "Harmony raw timing", raw: s };
    let raw = sendirRaw(s);
    if (raw)
      return {
        name: name,
        meta: "Global Cache sendir converted timing",
        raw: raw,
      };
    raw = prontoToHarmonyRaw(
      (s.match(/(?:0000|0100|5000|6000|7000)(?:\s+[0-9a-fA-F]{4}){6,}/) ||
        [])[0] || "",
    );
    if (raw)
      return { name: name, meta: "Pronto Hex converted timing", raw: raw };
    const bk = broadlinkKind(s);
    raw = broadlinkRaw(s);
    if (raw)
      return {
        name: name,
        meta: "BroadLink IR base64 converted timing",
        raw: raw,
      };
    if (bk === "rf")
      return {
        name: name,
        meta: "BroadLink RF packet - not an IR command",
        raw: "",
      };
    raw = miioRaw(s);
    if (raw)
      return { name: name, meta: "Xiaomi Miio raw converted timing", raw: raw };
    const rr = rawTimingRows(s, name);
    if (rr[0]) rr[0].name = name;
    return rr[0] || null;
  }
  function lircProp(block, key) {
    const m = String(block || "").match(
      new RegExp("^\\s*" + key + "\\s+([^\\n]+)", "im"),
    );
    return m ? m[1].trim() : "";
  }
  function lircNums(block, key) {
    return lircProp(block, key)
      .split(/\s+/)
      .map(Number)
      .filter(Number.isFinite);
  }
  function lircCodeBits(hex, n, rev) {
    hex = String(hex || "").replace(/^0x/i, "") || "0";
    if (typeof BigInt === "function") {
      try {
        const v = BigInt("0x" + hex),
          one = BigInt(1),
          a = [];
        if (rev) {
          for (let i = 0; i < n; i++) a.push(Number((v >> BigInt(i)) & one));
        } else {
          for (let i = n - 1; i >= 0; i--)
            a.push(Number((v >> BigInt(i)) & one));
        }
        return a;
      } catch (e) {}
    }
    const v = parseInt(hex, 16) || 0;
    return rev ? lsbBits(v, n) : msbBits(v, n);
  }
  function lircCodeRaw(block, hex) {
    const bits = parseInt(lircProp(block, "bits"), 10) || 32,
      preBits = parseInt(lircProp(block, "pre_data_bits"), 10) || 0,
      one = lircNums(block, "one"),
      zero = lircNums(block, "zero"),
      head = lircNums(block, "header"),
      ptrail = parseInt(lircProp(block, "ptrail"), 10) || 0,
      gap = parseInt(lircProp(block, "gap"), 10) || 45000,
      freq = parseInt(lircProp(block, "frequency"), 10) || 38000,
      flags = lircProp(block, "flags"),
      rev = /REVERSE/i.test(flags);
    if (one.length < 2 || zero.length < 2) return "";
    let arr = [];
    if (preBits)
      arr = arr.concat(lircCodeBits(lircProp(block, "pre_data"), preBits, rev));
    arr = arr.concat(lircCodeBits(hex, bits, rev));
    const seq = [];
    if (head.length >= 2) {
      pulse(seq, 1, head[0]);
      pulse(seq, 0, head[1]);
    }
    if (/SHIFT_ENC/i.test(flags)) {
      const half = Math.max(
        250,
        Math.round(((one[0] || 889) + (one[1] || 889)) / 2),
      );
      manchester(seq, arr, half, -1);
    } else
      arr.forEach((b) => {
        pulse(seq, 1, b ? one[0] : zero[0]);
        pulse(seq, 0, b ? one[1] : zero[1]);
      });
    if (ptrail) pulse(seq, 1, ptrail);
    pulse(seq, 0, gap);
    return seqRaw(freq, seq);
  }
  function lircUnsupportedReason(block) {
    const driver = lircProp(block, "driver"),
      one = lircNums(block, "one"),
      zero = lircNums(block, "zero");
    if (
      /irman/i.test(driver) ||
      (one[0] === 0 && one[1] === 0 && zero[0] === 0 && zero[1] === 0)
    )
      return "LIRC IRMan decoded code - no replayable timing data";
    if (driver && one.length < 2 && zero.length < 2)
      return "LIRC driver decoded code - no replayable timing data";
    return "LIRC code unsupported";
  }
  function lircEntries(t) {
    const out = [];
    String(t || "")
      .replace(/\r/g, "")
      .split(/begin\s+remote/i)
      .slice(1)
      .forEach((part, bi) => {
        const block = part.split(/end\s+remote/i)[0] || "",
          remote = safeImportName(
            lircProp(block, "name") || "LIRC remote " + (bi + 1),
          ),
          freq = parseInt(lircProp(block, "frequency"), 10) || 38000,
          rawSec = (block.match(
            /begin\s+raw_codes([\s\S]*?)end\s+raw_codes/i,
          ) || [])[1];
        if (rawSec) {
          let name = "",
            vals = [];
          function push() {
            const raw = harmonyRawFromTimings(freq, vals);
            if (name && raw)
              out.push({
                name: name,
                meta:
                  "LIRC raw timings " +
                  freq +
                  " Hz (" +
                  vals.length +
                  " durations)",
                raw: raw,
              });
            vals = [];
          }
          rawSec.split("\n").forEach((line) => {
            line = line.trim();
            if (!line) return;
            const m = line.match(/^name\s+(.+)/i);
            if (m) {
              push();
              name = safeImportName(m[1]);
            } else
              vals = vals.concat(
                line.split(/\s+/).map(Number).filter(Number.isFinite),
              );
          });
          push();
        }
        const codes = (block.match(/begin\s+codes([\s\S]*?)end\s+codes/i) ||
          [])[1];
        if (codes) {
          codes.split("\n").forEach((line) => {
            line = line.trim();
            const m = line.match(/^(.+?)\s+(0x[0-9a-fA-F]+|[0-9a-fA-F]+)\b/);
            if (!m) return;
            const raw = lircCodeRaw(block, m[2]);
            out.push({
              name: safeImportName(m[1]),
              meta: raw
                ? "LIRC " + remote + " rendered timing"
                : lircUnsupportedReason(block),
              raw: raw,
            });
          });
        }
      });
    return out;
  }
  function girrRegexEntries(t) {
    const out = [],
      cmdRe =
        /<(?:[^:>\s]+:)?command\b([^>]*)>([\s\S]*?)<\/(?:[^:>\s]+:)?command>/gi;
    let m,
      i = 0;
    while ((m = cmdRe.exec(String(t || "")))) {
      const attrs = m[1] || "",
        body = m[2] || "",
        am = attrs.match(/\b(?:name|id)=["']([^"']+)["']/i),
        name = safeImportName((am && am[1]) || "GIRR " + ++i);
      ["raw", "pronto", "ccf", "sendir"].forEach((tag) => {
        const re = new RegExp(
          "<(?:[^:>\\s]+:)?" +
            tag +
            "\\b[^>]*>([\\s\\S]*?)<\\/(?:[^:>\\s]+:)?" +
            tag +
            ">",
          "gi",
        );
        let tm;
        while ((tm = re.exec(body))) {
          const txt = decodeHtml((tm[1] || "").replace(/<[^>]+>/g, " ")).trim();
          let row = null;
          if (tag === "raw") {
            row = rawTimingRows(txt, name)[0];
            if (row) row.name = name;
          } else
            row = codeStringRow(
              name,
              txt,
              tag === "sendir"
                ? "Global Cache sendir converted timing"
                : "GIRR " + tag + " converted timing",
            );
          if (row) out.push(row);
        }
      });
    }
    return out;
  }
  function girrEntries(t) {
    const out = [];
    try {
      const doc = new DOMParser().parseFromString(
        String(t || ""),
        "application/xml",
      );
      if (doc.querySelector("parsererror")) return girrRegexEntries(t);
      doc.querySelectorAll("command").forEach((cmd, i) => {
        const name = safeImportName(
          cmd.getAttribute("name") ||
            cmd.getAttribute("id") ||
            "GIRR " + (i + 1),
        );
        ["raw", "pronto", "ccf", "sendir"].forEach((tag) => {
          cmd.querySelectorAll(tag).forEach((el) => {
            const txt = (el.textContent || "").trim();
            let row = null;
            if (tag === "raw") {
              row = rawTimingRows(txt, name)[0];
              if (row) row.name = name;
            } else
              row = codeStringRow(
                name,
                txt,
                tag === "sendir"
                  ? "Global Cache sendir converted timing"
                  : "GIRR " + tag + " converted timing",
              );
            if (row) out.push(row);
          });
        });
      });
    } catch (e) {}
    return out.length ? out : girrRegexEntries(t);
  }
  function jsonEntries(t) {
    let obj;
    try {
      obj = JSON.parse(String(t || ""));
    } catch (e) {
      return [];
    }
    const out = [];
    function walk(v, path) {
      const name = safeImportName(
        path.filter(Boolean).slice(-4).join(" ") || "JSON command",
      );
      if (typeof v === "string") {
        const row = codeStringRow(name, v, "JSON code");
        if (row) out.push(row);
        return;
      }
      if (Array.isArray(v)) {
        if (v.length >= 4 && v.every((x) => Number.isFinite(Number(x)))) {
          const raw = harmonyRawFromTimings(38000, v.map(Number));
          if (raw)
            out.push({ name: name, meta: "JSON raw timing array", raw: raw });
        } else v.forEach((x, i) => walk(x, path.concat(String(i + 1))));
        return;
      }
      if (v && typeof v === "object") {
        const label = v.name || v.command || v.button || v.key || v.label;
        [
          "code",
          "data",
          "raw",
          "pronto",
          "prontoHex",
          "sendir",
          "broadlink",
          "base64",
          "command",
        ].forEach((k) => {
          if (typeof v[k] === "string") {
            const row = codeStringRow(
              safeImportName(label || name),
              v[k],
              "JSON " + k,
            );
            if (row) out.push(row);
          }
        });
        Object.keys(v).forEach((k) => walk(v[k], path.concat(k)));
      }
    }
    walk(obj, []);
    return out;
  }
  function genericCsvEntries(t) {
    const lines = String(t || "")
      .replace(/\r/g, "")
      .split("\n")
      .filter((x) => x.trim());
    if (!lines.length) return [];
    const head = csvCells(lines[0]).map((x) => x.toLowerCase());
    if (head[0] === "functionname") return csvEntries(t);
    const nameIdx = head.findIndex((x) =>
        /^(name|button|command|function|key)$/.test(x),
      ),
      codeIdx = head.findIndex((x) =>
        /(code|raw|pronto|sendir|broadlink|base64|data)/.test(x),
      );
    if (nameIdx < 0 || codeIdx < 0) return csvEntries(t);
    return lines
      .slice(1)
      .map((line, i) => {
        const c = csvCells(line);
        return codeStringRow(
          safeImportName(c[nameIdx] || "CSV " + (i + 1)),
          c[codeIdx],
          "CSV code",
        );
      })
      .filter(Boolean);
  }
  function ircEntries(t) {
    const rows = [];
    String(t || "")
      .replace(/\r/g, "")
      .split("\n")
      .forEach((line, i) => {
        const m = line.match(/^\s*([^:=,]+?)\s*[:=,]\s*(.+)$/);
        const row = codeStringRow(
          safeImportName(m ? m[1] : "IRC " + (i + 1)),
          m ? m[2] : line,
          "IRC code",
        );
        if (row) rows.push(row);
      });
    return rows;
  }
  function dedupeCommandRows(rows) {
    const seen = new Set(),
      names = new Set(),
      out = [];
    (rows || []).forEach((r) => {
      if (!(r && r.name)) return;
      const supported = !!(r.raw || r.keycode),
        fp = (
          supported
            ? r.raw
              ? "raw:" + r.raw
              : "key:" + r.keycode
            : "unsupported:" + (r.meta || "") + ":" + r.name
        )
          .replace(/\s+/g, "")
          .toLowerCase();
      if (seen.has(fp)) return;
      seen.add(fp);
      let base = safeImportName(r.name),
        name = base,
        n = 2;
      while (names.has(name.toLowerCase()))
        name = (base + " " + n++).slice(0, 96);
      names.add(name.toLowerCase());
      out.push({ ...r, name: name });
    });
    return out;
  }
  function parseIrText(t, source, path) {
    t = String(t || "");
    let rows = [];
    const low = String(path || "").toLowerCase(),
      hint = String(source || "").toLowerCase();
    if (hint === "flipper" || /\.ir$/.test(low) || /^Filetype:\s*IR/i.test(t))
      rows = rows.concat(flipperEntries(t, path));
    if (hint === "irdb" || /\.csv$/.test(low))
      rows = rows.concat(genericCsvEntries(t));
    if (
      hint === "lirc" ||
      /begin\s+remote/i.test(t) ||
      /\.lirc|\.lircd|\.conf/.test(low)
    )
      rows = rows.concat(lircEntries(t));
    if (hint === "smartir" || /\.json$/.test(low) || /^\s*[\[{]/.test(t))
      rows = rows.concat(jsonEntries(t));
    if (
      hint === "remotecentral" ||
      /<html|Copy to Clipboard|Infrared Hex/i.test(t)
    )
      rows = rows.concat(remoteCentralCommandEntries(t));
    if (/<\?xml|<girr|<command/i.test(t)) rows = rows.concat(girrEntries(t));
    const structured =
      /^(flipper|irdb|lirc|smartir|remotecentral)$/.test(hint) ||
      /^Filetype:\s*IR/i.test(t) ||
      /begin\s+remote/i.test(t) ||
      /^\s*[\[{]/.test(t);
    if (hint === "custom" || !structured || !rows.length)
      rows = rows.concat(
        prontoEntries(t),
        ircEntries(t),
        rawTimingRows(t, "Raw command"),
      );
    return dedupeCommandRows(rows);
  }
  function decodeHtml(s) {
    const e = document.createElement("textarea");
    e.innerHTML = String(s || "");
    return e.value;
  }
  function rcCleanCommandName(s) {
    s = decodeHtml(String(s || ""))
      .replace(/\(\s*Copy\s+to\s+Clipboard\s*\)/gi, "")
      .replace(/[|"\\]/g, "")
      .replace(/\s+/g, " ")
      .trim();
    if (
      !s ||
      /^(Image|Return|Remote Model|Infrared Hex|This model|Features|Hex Codes|Page:|Copyright|Home|News|Reviews|Files|Forums)$/i.test(
        s,
      )
    )
      return "";
    return s.slice(0, 96);
  }
  function rcCommandName(lines, i, prefix) {
    let n = rcCleanCommandName(prefix);
    if (n) return n;
    for (let j = i - 1; j >= 0 && j >= i - 8; j--) {
      n = rcCleanCommandName(lines[j]);
      if (n && !/^[0-9a-f]{4}\s/i.test(n)) return n;
    }
    return "Command " + (i + 1);
  }
  function remoteCentralCommandEntries(html) {
    let text = String(html || "")
      .replace(/<script[\s\S]*?<\/script>/gi, "")
      .replace(/<style[\s\S]*?<\/style>/gi, "")
      .replace(/<br\s*\/?\s*>/gi, "\n")
      .replace(/<\/(td|tr|p|div|li)>/gi, "\n")
      .replace(/<[^>]+>/g, " ");
    text = decodeHtml(text).replace(
      /\(Copy to Clipboard\)/gi,
      "\n(Copy to Clipboard)\n",
    );
    const lines = text
        .replace(/\r/g, "")
        .split("\n")
        .map((x) => x.replace(/\s+/g, " ").trim())
        .filter(Boolean),
      rows = [];
    const pronto = /((?:0000|0100|5000|6000|7000)(?:\s+[0-9a-fA-F]{4}){10,})/g;
    lines.forEach((line, i) => {
      let m;
      while ((m = pronto.exec(line))) {
        const hex = m[1].replace(/\s+/g, " ").trim(),
          raw = prontoToHarmonyRaw(hex);
        rows.push({
          name: rcCommandName(lines, i, line.slice(0, m.index)),
          meta: raw
            ? "Pronto converted raw (" + hex.split(" ").length + " words)"
            : "Pronto unsupported format",
          raw: raw,
        });
      }
    });
    return rows;
  }
  window.harmonyParseProfile = parseIrText;
})();
