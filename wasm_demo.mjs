// Node demo for the WebAssembly build of iec2json (see build_wasm.sh).
//
//   node wasm_demo.mjs file.st [more.st ...]
//
// For each file: writes its source into MEMFS as /work/<name>, runs
// main(['-I', '/lib', '<name>']) with cwd /work and prints the JSON that
// iec2json wrote to stdout. Exit code is non-zero if any file failed.
//
// Each run uses a fresh module instance (the compiler keeps global state and
// calls exit() on errors), but the compiled WebAssembly.Module is reused, so
// only instantiation is paid per run.
import { readFileSync } from 'node:fs';
import { basename, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const wasmDir = process.env.IEC2JSON_WASM_DIR || join(here, 'wasm');
const { default: createIec2json } = await import(join(wasmDir, 'iec2json.mjs'));
const wasmModule = await WebAssembly.compile(readFileSync(join(wasmDir, 'iec2json.wasm')));

// Run iec2json on one source text. Returns { code, stdout, stderr } where
// stdout/stderr are exact byte-for-byte strings (UTF-8 decoded).
export async function iec2json(source, name = 'input.st', extraArgs = []) {
  const out = [], err = [];
  const mod = await createIec2json({
    instantiateWasm(imports, done) {
      WebAssembly.instantiate(wasmModule, imports).then((inst) => done(inst, wasmModule));
      return {};
    },
    preRun: [(m) => {
      // Byte-level stdout/stderr: no line splitting, so the output is exact.
      m.FS.init(null, (c) => out.push(c), (c) => err.push(c));
      m.FS.mkdir('/work');
      m.FS.writeFile('/work/' + name, source);
      m.FS.chdir('/work');
    }],
    print() {}, printErr() {},
  });
  let code;
  try {
    code = mod.callMain([...extraArgs, '-I', '/lib', name]);
  } catch (e) {
    if (e && e.name === 'ExitStatus') code = e.status; else throw e;
  }
  const dec = (a) => new TextDecoder().decode(Uint8Array.from(a, (c) => c & 0xff));
  return { code, stdout: dec(out), stderr: dec(err) };
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  let failed = 0;
  for (const file of process.argv.slice(2)) {
    const t0 = performance.now();
    const r = await iec2json(readFileSync(file), basename(file));
    const ms = (performance.now() - t0).toFixed(1);
    process.stdout.write(r.stdout);
    process.stderr.write(r.stderr);
    process.stderr.write(`[${file}: exit ${r.code}, ${ms} ms]\n`);
    if (r.code !== 0) failed++;
  }
  process.exit(failed ? 1 : 0);
}
