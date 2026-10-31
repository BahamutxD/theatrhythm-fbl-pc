// Trace guest functions of a running theatrhythm-fbl with Frida (entry arguments, return value).
//
// usage: node tools/frida_guest_trace.js BOOT.log HOOKS.tsv [MAX_CALLS_PER_HOOK]
//   BOOT.log   stdout of `python run.py ...` (the driver waits for its "guest image at" line)
//   HOOKS.tsv  "guest offset<TAB>name[<TAB>enter]" lines: module offsets from tools/il2cpp_symbols.py
//              plus the module base from out/game/link.json; "enter" logs the call only
// Limits (seen on this runtime): a guest C++ throw (IL2CPP raises managed exceptions that way) through
// a hooked function aborts the game ("guest raised debug exception"), worst with onLeave, so mark
// functions that may be on the stack during a throw "enter". Hooking __cxa_throw itself and
// Thread.backtrace(FUZZY) crash it too: use THEATRHYTHM_THROW_TRACE=1 for exceptions and
// THEATRHYTHM_FILE_TRACE=1 (guest backtraces at opens) instead. Check that a hooked boot behaves like
// an unhooked one.
// Guest code uses the SysV ABI: arguments in rdi, rsi, rdx, rcx, r8, r9; the result in rax.
// The guest TCB lives in a TEB TLS slot (GS stays the TEB), so Frida's hooks are safe on guest threads.
// node and frida: `npm install frida`, or FRIDA_NODE_MODULE naming an installed frida module.
const fs = require('fs');
const frida = require(process.env.FRIDA_NODE_MODULE || 'frida');

const [logPath, hooksPath, maxText] = process.argv.slice(2);
if (!logPath || !hooksPath) { console.error('usage: node frida_guest_trace.js BOOT.log HOOKS.tsv [MAX]'); process.exit(2); }
const hooks = fs.readFileSync(hooksPath, 'utf8').split(/\r?\n/).filter(Boolean)
    .map(line => { const [offset, name, mode] = line.split('\t');
        return { offset, name, enter: mode === 'enter' }; });

const agent = `
const base = ptr(BASE), max = MAX;
// An argument that points at an IL2CPP System.String (klass, monitor, int32 length, UTF-16) prints as text.
function show(p) {
    try {
        const n = p.add(0x10).readS32();
        if (n > 0 && n < 400) { const s = p.add(0x14).readUtf16String(n); if (/^[\\x20-\\x7e]+$/.test(s)) return JSON.stringify(s); }
    } catch (e) {}
    return p.toString();
}
let hooked = 0;
for (const h of HOOKS) {
    let calls = 0;
    const callbacks = {
        onEnter(args) {
            this.n = ++calls;
            if (this.n > max) return;
            const c = this.context;
            send(h.name + '(' + [c.rdi, c.rsi, c.rdx, c.rcx].map(show).join(', ') + ') tid ' + Process.getCurrentThreadId());
        },
    };
    if (!h.enter) callbacks.onLeave = function (result) { if (this.n <= max) send('  ' + h.name + ' -> ' + result); };
    try { Interceptor.attach(base.add(ptr(h.offset)), callbacks); hooked++; }
    catch (e) { send('cannot hook ' + h.name + ': ' + e.message); }
}
send('hooked ' + hooked + ' of ' + HOOKS.length + ' functions at image ' + base);
`;

function imageBase() {
    const m = /guest image at (0x[0-9a-fA-F]+|[0-9a-fA-F]{8,})/.exec(fs.existsSync(logPath) ? fs.readFileSync(logPath, 'utf8') : '');
    return m ? (m[1].startsWith('0x') ? m[1] : '0x' + m[1]) : null;
}

(async () => {
    let base;
    while (!(base = imageBase())) await new Promise(r => setTimeout(r, 200));
    const device = await frida.getLocalDevice();
    let session;
    for (;;) {
        try { session = await device.attach('theatrhythm-fbl.exe'); break; }
        catch { await new Promise(r => setTimeout(r, 200)); }
    }
    const source = agent.replace('BASE', JSON.stringify(base)).replace('MAX', String(Number(maxText || 20)))
        .split('HOOKS').join(JSON.stringify(hooks));
    const script = await session.createScript(source);
    script.message.connect(m => console.log(m.type === 'send' ? m.payload : 'error: ' + (m.stack || m.description)));
    await script.load();
    session.detached.connect(reason => { console.log('detached: ' + reason); process.exit(0); });
})().catch(e => { console.error(e); process.exit(1); });
