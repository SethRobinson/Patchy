// Precompresses the staged wasm site's large assets so Apache can serve
// Brotli/gzip variants via the rewrite rules in packaging/web/.htaccess
// (patchy.wasm is ~tens of MB uncompressed; Brotli cuts the transfer to
// roughly a quarter). Runs on the emsdk-bundled node from
// scripts\release\build-wasm.bat; no dependencies beyond node's zlib.
// Usage: node precompress-site.mjs <site-dir>
import { readFile, writeFile, rm } from 'node:fs/promises';
import { join } from 'node:path';
import { brotliCompressSync, gzipSync, constants } from 'node:zlib';

// Only the large immutable assets. The html entry pages are tiny, marked
// no-cache, and stay identity-encoded.
const assets = ['patchy.wasm', 'patchy.js', 'patchy.data', 'qtloader.js'];

const siteDir = process.argv[2];
if (!siteDir) {
  console.error('usage: node precompress-site.mjs <site-dir>');
  process.exit(1);
}

const megabytes = (bytes) => (bytes / (1024 * 1024)).toFixed(1);

for (const name of assets) {
  const path = join(siteDir, name);
  let source;
  try {
    source = await readFile(path);
  } catch {
    console.error(`precompress-site: missing staged asset ${path}`);
    process.exit(1);
  }
  // Stale variants from an earlier staging must never outlive their source.
  await rm(path + '.br', { force: true });
  await rm(path + '.gz', { force: true });
  // Quality 10 compresses tens of MB in seconds with a ratio within ~1% of
  // the much slower maximum (11); SIZE_HINT and the maximum window help both.
  const brotli = brotliCompressSync(source, {
    params: {
      [constants.BROTLI_PARAM_QUALITY]: 10,
      [constants.BROTLI_PARAM_SIZE_HINT]: source.length,
      [constants.BROTLI_PARAM_LGWIN]: constants.BROTLI_MAX_WINDOW_BITS,
    },
  });
  const gzip = gzipSync(source, { level: constants.Z_BEST_COMPRESSION });
  await writeFile(path + '.br', brotli);
  await writeFile(path + '.gz', gzip);
  console.log(
    `${name}: ${megabytes(source.length)} MB -> ` +
    `${megabytes(brotli.length)} MB br, ${megabytes(gzip.length)} MB gz`);
}
