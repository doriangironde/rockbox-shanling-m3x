// Runtime notices only. Never read firmware, APKs, private directories or keys.
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('../', import.meta.url)), seen = new Set(), sections = [];
function collect(name, parent) {
  const require = createRequire(join(parent, 'package.json'));
  let path;
  try { path = dirname(require.resolve(name + '/package.json')); }
  catch {
    path = dirname(require.resolve(name));
    while (!existsSync(join(path, 'package.json'))) {
      const next = dirname(path); if (next === path) throw new Error('Cannot find package: ' + name); path = next;
    }
  }
  let pkg = JSON.parse(readFileSync(join(path, 'package.json')));
  // Some packages have an esm/package.json specifying type only.
  while (pkg.name !== name) {
    path = dirname(path); pkg = JSON.parse(readFileSync(join(path, 'package.json')));
  }
  const id = `${pkg.name}@${pkg.version}`; if (seen.has(id)) return; seen.add(id);
  const license = ['LICENSE', 'LICENSE.txt', 'LICENSE.md'].map(n => join(path, n)).find(existsSync);
  if (!license || pkg.license !== 'MIT') throw new Error('Review runtime license: ' + id);
  sections.push({ id, text: readFileSync(license, 'utf8') });
  for (const dependency of Object.keys(pkg.dependencies ?? {})) collect(dependency, path);
}
const pkg = JSON.parse(readFileSync(join(root, 'package.json')));
for (const name of Object.keys(pkg.dependencies)) collect(name, root);
const notice = 'Third-party runtime notices (lockfile-pinned installation)\n\n' +
  sections.sort((a, b) => a.id.localeCompare(b.id)).map(p => `${p.id}\n${'='.repeat(60)}\n${p.text}\n`).join('\n') +
  '\nSahara/Firehose reference: linux-msm/qdl (BSD-3-Clause)\n' + readFileSync(join(root, 'LICENSES/qdl.txt'), 'utf8');
writeFileSync(join(root, 'public/third-party-licenses.txt'), notice);
console.log(`Runtime notices generated: ${sections.length} MIT packages + qdl reference`);
